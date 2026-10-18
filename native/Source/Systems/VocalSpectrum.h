#pragma once
#include <juce_dsp/juce_dsp.h>
#include <array>

namespace hem {
// One producer (prepare/release serialized with processBlock), one consumer.
// Only the analysis worker and message thread use snapshotLock. Audio only
// copies into preallocated slots and publishes lock-free 32-bit indices.
class VocalSpectrum final : private juce::Thread {
public:
    static constexpr int order=13, size=1<<order, bins=size/2+1;
    static constexpr unsigned packetSize=256, packetCount=128;
    static_assert(std::atomic<unsigned>::is_always_lock_free, "Audio indices must be lock-free");
    struct Snapshot {
        std::array<float,bins> db{};
        double sampleRate=0, completedAt=0;
        std::uint64_t sequence=0, frameEnd=0;
        unsigned epoch=0, droppedFrames=0;
        bool silent=true;
    };
    VocalSpectrum() : Thread("Vocal post-FX spectrum") {
        for(int i=0;i<size;++i) { window[i]=float(.5-.5*std::cos(juce::MathConstants<double>::twoPi*i/(size-1))); windowSum+=window[i]; }
        latest.db.fill(-160); startThread();
    }
    ~VocalSpectrum() override { signalThreadShouldExit(); notify(); waitForThreadToExit(-1); }
    void prepare(double rate) noexcept {
        producerRate=rate; producerFrame=0; partial=0; drops.store(0,std::memory_order_relaxed);
        generation.fetch_add(1,std::memory_order_release);
    }
    void suspend() noexcept { producerRate=0; partial=0; generation.fetch_add(1,std::memory_order_release); }
    unsigned epoch() const noexcept { return generation.load(std::memory_order_acquire); }
    bool operational() const { return isThreadRunning(); }
    void push(const float* l,const float* r,int count) noexcept {
        if(producerRate<=0) return;
        for(int offset=0;offset<count;) {
            const unsigned w=writeIndex.load(std::memory_order_relaxed);
            if(w-readIndex.load(std::memory_order_acquire)>=packetCount) {
                const unsigned lost=unsigned(count-offset);
                drops.fetch_add(lost,std::memory_order_relaxed); producerFrame+=lost; return;
            }
            auto& p=packets[w%packetCount];
            if(partial==0) {p.rate=producerRate;p.epoch=epoch();p.first=producerFrame;}
            const unsigned n=std::min(packetSize-partial,unsigned(count-offset));
            std::copy_n(l+offset,n,p.l.data()+partial);std::copy_n(r+offset,n,p.r.data()+partial);
            partial+=n;producerFrame+=n;offset+=int(n);
            if(partial==packetSize) {p.n=partial;partial=0;writeIndex.store(w+1,std::memory_order_release);}
        }
    }

    bool copyLatest(Snapshot& out) const {
        const juce::ScopedTryLock lock(snapshotLock);
        if(!lock.isLocked()||latest.epoch!=epoch()||latest.sequence==0) return false;
        out=latest;return out.epoch==epoch();
    }
private:
    struct Packet { std::array<float,packetSize> l{},r{}; unsigned n=0,epoch=0;double rate=0;std::uint64_t first=0; };
    void run() override {
        while(!threadShouldExit()) {
            const unsigned r=readIndex.load(std::memory_order_relaxed);
            if(r==writeIndex.load(std::memory_order_acquire)) { wait(3);continue; }
            const auto& p=packets[r%packetCount];
            if(p.epoch==epoch()) consume(p);
            readIndex.store(r+1,std::memory_order_release);
        }
    }
    void consume(const Packet& p) {
        if(p.epoch!=workerEpoch||p.first!=expected) { fill=cursor=hop=0;workerEpoch=p.epoch; }
        for(unsigned i=0;i<p.n;++i) {
            ringL[cursor]=std::isfinite(p.l[i])?p.l[i]:0;
            ringR[cursor]=std::isfinite(p.r[i])?p.r[i]:0;
            cursor=(cursor+1)%size;fill=std::min(size,fill+1);++hop;
            if(fill==size&&hop>=size/4) { hop=0;publish(p.rate,p.first+i+1); }
        }
        expected=p.first+p.n;
    }
    void publish(double rate,std::uint64_t end) {
        fftL.fill(0);fftR.fill(0);float peak=0;
        for(int i=0;i<size;++i) {
            const auto ix=(cursor+i)%size;
            peak=std::max(peak,std::max(std::abs(ringL[ix]),std::abs(ringR[ix])));
            fftL[i]=ringL[ix]*window[i];fftR[i]=ringR[ix]*window[i];
        }
        fft.performFrequencyOnlyForwardTransform(fftL.data(),true);
        fft.performFrequencyOnlyForwardTransform(fftR.data(),true);
        for(int i=0;i<bins;++i) {
            const double scale=(i==0||i==bins-1?1.:2.)/windowSum;
            work.db[i]=float(20*std::log10(std::max(1.e-8,double(std::max(fftL[i],fftR[i]))*scale)));
        }
        work.completedAt=juce::Time::getMillisecondCounterHiRes();work.sampleRate=rate;work.epoch=workerEpoch;work.frameEnd=end;work.sequence=++sequence;
        work.droppedFrames=drops.load(std::memory_order_relaxed);work.silent=peak<1.e-8f;
        const juce::ScopedLock lock(snapshotLock);latest=work;
    }
    std::array<Packet,packetCount> packets;
    std::atomic<unsigned> writeIndex{0},readIndex{0},generation{0},drops{0};
    double producerRate=0,windowSum=0;
    std::uint64_t producerFrame=0,expected=0,sequence=0;
    unsigned partial=0,workerEpoch=0;int cursor=0,fill=0,hop=0;
    juce::dsp::FFT fft{order};
    std::array<float,size> ringL{},ringR{},window{};
    std::array<float,2*size> fftL{},fftR{};
    mutable juce::CriticalSection snapshotLock;
    Snapshot latest,work;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VocalSpectrum)
};
}
