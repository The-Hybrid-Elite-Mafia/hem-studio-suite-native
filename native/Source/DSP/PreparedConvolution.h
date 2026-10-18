#pragma once
#include <algorithm>
#include <array>
#include <cassert>
#include <complex>
#include <cmath>
#include <cstddef>
#include <memory>
#include <vector>

namespace hem::reverb {

// Tables and storage are prepared off-thread. Both transform paths are owned,
// serialized, allocation-free, and independent of any global FFT backend.
class PreparedFFT {
public:
    struct Job {
        std::size_t index = 0, length = 2;
        bool reversing = true, inverse = false, finished = false;
    };

    explicit PreparedFFT(std::size_t n) : size(n), reverse(n), twiddle(n/2) {
        assert(n >= 2 && (n & (n-1)) == 0);
        for (std::size_t v=n; v>1; v>>=1) ++bits;
        for (std::size_t i=0; i<n; ++i) {
            std::size_t v=i, r=0;
            for (unsigned b=0; b<bits; ++b) { r=(r<<1)|(v&1); v>>=1; }
            reverse[i]=r;
        }
        for (std::size_t i=0; i<n/2; ++i) {
            const double phase=-6.283185307179586476925286766559*double(i)/double(n);
            twiddle[i]={float(std::cos(phase)),float(std::sin(phase))};
        }
    }

    // Explicit finite-signal arithmetic avoids std::complex's exceptional-value
    // fallback and permits vectorization of the dominant spectral products.
    static std::complex<float> product(std::complex<float> a, std::complex<float> b) noexcept {
        return {a.real()*b.real()-a.imag()*b.imag(), a.real()*b.imag()+a.imag()*b.real()};
    }

    void transform(std::complex<float>* x, bool inverse) const noexcept {
        for (std::size_t i=0; i<size; ++i)
            if (reverse[i]>i) std::swap(x[i],x[reverse[i]]);
        for (std::size_t length=2; length<=size; length*=2) {
            const std::size_t half=length/2, stride=size/length;
            for (std::size_t first=0; first<size; first+=length)
                for (std::size_t k=0; k<half; ++k) {
                    const auto w=inverse?std::conj(twiddle[k*stride]):twiddle[k*stride];
                    const auto odd=product(w,x[first+k+half]), even=x[first+k];
                    x[first+k]=even+odd;
                    x[first+k+half]=even-odd;
                }
        }
        if (inverse) {
            const float scale=1.f/float(size);
            for (std::size_t i=0; i<size; ++i) x[i]*=scale;
        }
    }

    // One work unit is one bit-reversal visit or one complex butterfly.
    // Incremental inverse transforms are deliberately unnormalized; their
    // caller incorporates 1/N into its separately scheduled overlap assembly.
    std::size_t workUnits() const noexcept { return size+(size/2)*bits; }
    static Job begin(bool inverse) noexcept { Job job; job.inverse=inverse; return job; }
    std::size_t advance(std::complex<float>* x, Job& job, std::size_t budget) const noexcept {
        const std::size_t original=budget;
        while (budget>0 && !job.finished) {
            if (job.reversing) {
                const auto amount=std::min(budget,size-job.index);
                const auto end=job.index+amount;
                for (auto i=job.index; i<end; ++i)
                    if (reverse[i]>i) std::swap(x[i],x[reverse[i]]);
                job.index=end; budget-=amount;
                if (job.index==size) { job.reversing=false; job.index=0; }
            } else {
                const std::size_t half=job.length/2, mask=half-1, stride=size/job.length;
                const auto amount=std::min(budget,size/2-job.index);
                const auto end=job.index+amount;
                for (auto i=job.index; i<end; ++i) {
                    const std::size_t local=i&mask, first=i+(i&~mask);
                    const auto w=job.inverse?std::conj(twiddle[local*stride]):twiddle[local*stride];
                    const auto odd=product(w,x[first+half]), even=x[first];
                    x[first]=even+odd;
                    x[first+half]=even-odd;
                }
                job.index=end; budget-=amount;
                if (job.index==size/2) {
                    job.index=0; job.length*=2;
                    if (job.length>size) job.finished=true;
                }
            }
        }
        return original-budget;
    }

private:
    std::size_t size;
    unsigned bits=0;
    std::vector<std::size_t> reverse;
    std::vector<std::complex<float>> twiddle;
};

// Hybrid zero-latency FIR: a direct 64-tap head, short synchronous FFT tiers,
// and fully amortized 1024/4096-frame tiers. Each scheduled tier has a lookahead
// interval; its coefficients begin at 2P, so collecting P input frames and
// doing P frames of incremental work still meets the exact acoustic deadline.
class PreparedConvolution {
    static constexpr std::size_t headLength=64;

    struct Tier {
        const std::size_t partition, count, bins;
        PreparedFFT fft;
        std::array<std::vector<std::complex<float>>,2> coefficients, history;
        std::array<std::vector<float>,2> input, output, overlap;
        std::vector<std::complex<float>> work, accumulator;
        std::size_t position=0, historyCursor=0;

        Tier(std::size_t p, std::size_t first, std::size_t length, const float* l, const float* r)
            : partition(p), count((length+p-1)/p), bins(p+1), fft(p*2), work(p*2), accumulator(p*2) {
            for (std::size_t c=0; c<2; ++c) {
                coefficients[c].resize(count*bins); history[c].resize(count*bins);
                input[c].resize(p); output[c].resize(p); overlap[c].resize(p);
                const float* ir=c?r:l;
                for (std::size_t k=0; k<count; ++k) {
                    std::fill(work.begin(),work.end(),std::complex<float>{});
                    for (std::size_t j=0; j<p && k*p+j<length; ++j) work[j]={ir[first+k*p+j],0};
                    fft.transform(work.data(),false);
                    std::copy_n(work.begin(),bins,coefficients[c].begin()+std::ptrdiff_t(k*bins));
                }
            }
        }
        void reset() noexcept {
            for (std::size_t c=0; c<2; ++c) {
                std::fill(history[c].begin(),history[c].end(),std::complex<float>{});
                std::fill(input[c].begin(),input[c].end(),0);
                std::fill(output[c].begin(),output[c].end(),0);
                std::fill(overlap[c].begin(),overlap[c].end(),0);
            }
            position=historyCursor=0;
        }
        std::array<float,2> push(float l, float r) noexcept {
            const std::array<float,2> result{output[0][position],output[1][position]};
            input[0][position]=l; input[1][position]=r;
            if (++position==partition) { calculate(); position=0; }
            return result;
        }
        void calculate() noexcept {
            for (std::size_t c=0; c<2; ++c) {
                for (std::size_t i=0; i<partition; ++i) work[i]={input[c][i],0};
                std::fill(work.begin()+std::ptrdiff_t(partition),work.end(),std::complex<float>{});
                fft.transform(work.data(),false);
                std::copy_n(work.begin(),bins,history[c].begin()+std::ptrdiff_t(historyCursor*bins));
                std::fill(accumulator.begin(),accumulator.end(),std::complex<float>{});
                for (std::size_t k=0; k<count; ++k) {
                    const std::size_t index=(historyCursor+count-k)%count;
                    const auto* h=history[c].data()+index*bins;
                    const auto* f=coefficients[c].data()+k*bins;
                    for (std::size_t bin=0; bin<bins; ++bin) accumulator[bin]+=PreparedFFT::product(h[bin],f[bin]);
                }
                for (std::size_t k=1; k<partition; ++k) accumulator[2*partition-k]=std::conj(accumulator[k]);
                fft.transform(accumulator.data(),true);
                for (std::size_t i=0; i<partition; ++i) {
                    output[c][i]=accumulator[i].real()+overlap[c][i];
                    overlap[c][i]=accumulator[i+partition].real();
                }
            }
            historyCursor=(historyCursor+1)%count;
        }
    };

    struct ScheduledTier {
        enum class Phase { copyInput, forwardFFT, copyHistory, multiply, mirror, inverseFFT, overlap };
        const std::size_t partition, count, bins, quota;
        PreparedFFT fft;
        std::array<std::vector<std::complex<float>>,2> coefficients, history;
        std::array<std::array<std::vector<float>,2>,2> input, output;
        std::array<std::vector<float>,2> overlap;
        std::vector<std::complex<float>> work, accumulator;
        std::size_t position=0, historyCursor=0, inputBuffer=0, outputBuffer=0;
        std::size_t jobInput=0, jobOutput=0, channel=0, index=0, productPartition=0;
        Phase phase=Phase::copyInput;
        PreparedFFT::Job fftJob;
        bool running=false;

        static std::size_t workPerBlock(std::size_t p, std::size_t partitions) noexcept {
            unsigned bits=0;
            for (auto n=2*p; n>1; n>>=1) ++bits;
            const auto fftWork=2*p+p*bits;
            // Both channels: input/zero copy, two complete FFTs, history copy,
            // every partition/bin product, Hermitian mirror, overlap assembly.
            return 2*(2*p+2*fftWork+(p+1)+partitions*(p+1)+(p-1)+p);
        }
        ScheduledTier(std::size_t p, std::size_t first, std::size_t length, const float* l, const float* r)
            : partition(p), count((length+p-1)/p), bins(p+1),
              quota((workPerBlock(p,count)+p-1)/p), fft(p*2), work(p*2), accumulator(p*2) {
            assert(first==2*p);
            for (std::size_t c=0; c<2; ++c) {
                coefficients[c].resize(count*bins); history[c].resize(count*bins);
                for (auto& block:input[c]) block.resize(p);
                for (auto& block:output[c]) block.resize(p);
                overlap[c].resize(p);
                const float* ir=c?r:l;
                for (std::size_t k=0; k<count; ++k) {
                    std::fill(work.begin(),work.end(),std::complex<float>{});
                    for (std::size_t j=0; j<p && k*p+j<length; ++j) work[j]={ir[first+k*p+j],0};
                    fft.transform(work.data(),false);
                    std::copy_n(work.begin(),bins,coefficients[c].begin()+std::ptrdiff_t(k*bins));
                }
            }
        }
        void reset() noexcept {
            for (std::size_t c=0; c<2; ++c) {
                std::fill(history[c].begin(),history[c].end(),std::complex<float>{});
                for (auto& block:input[c]) std::fill(block.begin(),block.end(),0);
                for (auto& block:output[c]) std::fill(block.begin(),block.end(),0);
                std::fill(overlap[c].begin(),overlap[c].end(),0);
            }
            position=historyCursor=inputBuffer=outputBuffer=0;
            jobInput=jobOutput=channel=index=productPartition=0;
            running=false; phase=Phase::copyInput; fftJob={};
        }
        std::array<float,2> push(float l, float r) noexcept {
            const std::array<float,2> result{output[0][outputBuffer][position],output[1][outputBuffer][position]};
            input[0][inputBuffer][position]=l; input[1][inputBuffer][position]=r;
            if (running) advance(quota);
            if (++position==partition) {
                // The quota counts every scheduled operation, so this is an
                // invariant, not an audio-thread emergency catch-up path.
                assert(!running);
                position=0;
                outputBuffer^=1u;
                jobInput=inputBuffer;
                inputBuffer^=1u;
                jobOutput=outputBuffer^1u;
                channel=index=productPartition=0;
                phase=Phase::copyInput;
                running=true;
            }
            return result;
        }
        void nextPhase(Phase next) noexcept { phase=next; index=0; }
        void advance(std::size_t budget) noexcept {
            while (budget>0 && running) {
                switch (phase) {
                case Phase::copyInput: {
                    const auto end=index+std::min(budget,2*partition-index);
                    for (auto i=index; i<end; ++i) work[i]={i<partition?input[channel][jobInput][i]:0.f,0.f};
                    budget-=end-index; index=end;
                    if (index==2*partition) { fftJob=PreparedFFT::begin(false); nextPhase(Phase::forwardFFT); }
                    break;
                }
                case Phase::forwardFFT:
                    budget-=fft.advance(work.data(),fftJob,budget);
                    if (fftJob.finished) nextPhase(Phase::copyHistory);
                    break;
                case Phase::copyHistory: {
                    const auto amount=std::min(budget,bins-index);
                    std::copy_n(work.data()+index,amount,history[channel].data()+historyCursor*bins+index);
                    budget-=amount; index+=amount;
                    if (index==bins) { productPartition=0; nextPhase(Phase::multiply); }
                    break;
                }
                case Phase::multiply: {
                    const auto amount=std::min(budget,bins-index);
                    const auto historyIndex=(historyCursor+count-productPartition)%count;
                    const auto* h=history[channel].data()+historyIndex*bins+index;
                    const auto* f=coefficients[channel].data()+productPartition*bins+index;
                    auto* destination=accumulator.data()+index;
                    if (productPartition==0)
                        for (std::size_t i=0; i<amount; ++i) destination[i]=PreparedFFT::product(h[i],f[i]);
                    else
                        for (std::size_t i=0; i<amount; ++i) destination[i]+=PreparedFFT::product(h[i],f[i]);
                    budget-=amount; index+=amount;
                    if (index==bins) {
                        index=0;
                        if (++productPartition==count) nextPhase(Phase::mirror);
                    }
                    break;
                }
                case Phase::mirror: {
                    const auto end=index+std::min(budget,partition-1-index);
                    for (auto i=index; i<end; ++i) accumulator[2*partition-(i+1)]=std::conj(accumulator[i+1]);
                    budget-=end-index; index=end;
                    if (index==partition-1) { fftJob=PreparedFFT::begin(true); nextPhase(Phase::inverseFFT); }
                    break;
                }
                case Phase::inverseFFT:
                    budget-=fft.advance(accumulator.data(),fftJob,budget);
                    if (fftJob.finished) nextPhase(Phase::overlap);
                    break;
                case Phase::overlap: {
                    const auto end=index+std::min(budget,partition-index);
                    const float scale=1.f/float(2*partition);
                    for (auto i=index; i<end; ++i) {
                        output[channel][jobOutput][i]=accumulator[i].real()*scale+overlap[channel][i];
                        overlap[channel][i]=accumulator[i+partition].real()*scale;
                    }
                    budget-=end-index; index=end;
                    if (index==partition) {
                        if (++channel==2) { historyCursor=(historyCursor+1)%count; running=false; }
                        else nextPhase(Phase::copyInput);
                    }
                    break;
                }
                }
            }
        }
    };

public:
    void prepare(const float* left, const float* right, std::size_t length) {
        tiers.clear(); middleTail.reset(); tail.reset(); head={};
        headCount=std::min(headLength,length);
        for (std::size_t c=0; c<2; ++c) std::copy_n(c?right:left,headCount,head[c].begin());
        for (const auto spec:std::array<std::array<std::size_t,2>,2>{{{64,256},{256,2048}}}) {
            const auto partition=spec[0];
            if (length>partition) {
                const auto end=std::min(length,spec[1]);
                tiers.push_back(std::make_unique<Tier>(partition,partition,end-partition,left,right));
            }
        }
        if (length>2048) middleTail=std::make_unique<ScheduledTier>(1024,2048,std::min(length,std::size_t(8192))-2048,left,right);
        if (length>8192) tail=std::make_unique<ScheduledTier>(4096,8192,length-8192,left,right);
        reset();
    }
    void reset() noexcept {
        history={}; cursor=0;
        for (auto& t:tiers) t->reset();
        if (middleTail) middleTail->reset();
        if (tail) tail->reset();
    }
    void process(float* left, float* right, int samples) noexcept {
        for (int n=0; n<samples; ++n) {
            const float l=left[n],r=right[n];
            history[0][cursor]=l; history[1][cursor]=r;
            std::array<float,2> out{};
            for (std::size_t c=0; c<2; ++c) {
                double sum=0;
                for (std::size_t k=0; k<headCount; ++k) sum+=double(head[c][k])*history[c][(cursor+headLength-k)%headLength];
                out[c]=float(sum);
            }
            for (auto& t:tiers) {
                const auto contribution=t->push(l,r);
                out[0]+=contribution[0]; out[1]+=contribution[1];
            }
            if (middleTail) {
                const auto contribution=middleTail->push(l,r);
                out[0]+=contribution[0]; out[1]+=contribution[1];
            }
            if (tail) {
                const auto contribution=tail->push(l,r);
                out[0]+=contribution[0]; out[1]+=contribution[1];
            }
            left[n]=out[0]; right[n]=out[1]; cursor=(cursor+1)%headLength;
        }
    }

private:
    std::array<std::array<float,headLength>,2> head{},history{};
    std::size_t cursor=0,headCount=0;
    std::vector<std::unique_ptr<Tier>> tiers;
    std::unique_ptr<ScheduledTier> middleTail, tail;
};

} // namespace hem::reverb
