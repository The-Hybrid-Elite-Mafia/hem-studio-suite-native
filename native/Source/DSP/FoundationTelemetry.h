#pragma once
#include <array>
#include <atomic>
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace hem {
// Fixed storage, single audio writer, any number of bounded readers. Every
// payload member is atomic: sequence validation never permits a C++ data race.
struct FoundationDisplayFrame {
    enum Field { inputPeak, outputPeak, inputDcL, inputDcR, outputDcL, outputDcR,
                 outputMinL, outputMaxL, outputMinR, outputMaxR, gainDb, active, dcReady, processedFrequency, fields };
    std::array<float,fields> values{};
    uint32_t generation=0, serial=0, samples=0;uint64_t endSample=0;
};
class FoundationTelemetry {
public:
    static constexpr size_t capacity=512;
    void prepare(double rate) noexcept {
        enabled=std::isfinite(rate)&&rate>=8000&&rate<=384000;
        if(!enabled){generation.fetch_add(1,std::memory_order_seq_cst);return;}
        sampleRate=rate; binSamples=std::max(1u,uint32_t(std::lround(rate/120.0)));
        dcSamples=std::max(1u,uint32_t(std::lround(rate*.5)));
        binCount=dcCount=0;totalSamples=0;dcValid=false; accumulated={}; dcSums={}; heldDc={};
        generation.fetch_add(1,std::memory_order_seq_cst);
    }
    void observe(const std::array<double,2>& input,const std::array<float,2>& output,
                 float gain,bool activity,float frequency=0) noexcept {
        if(!enabled)return;
        ++totalSamples;
        for(size_t c=0;c<2;++c){
            const float in=std::isfinite(input[c])?float(input[c]):0;
            const float out=std::isfinite(output[c])?output[c]:0;
            accumulated[0]=std::max(accumulated[0],std::abs(in));
            accumulated[1]=std::max(accumulated[1],std::abs(out));
            dcSums[c]+=in;dcSums[c+2]+=out;
            const size_t k=6+c*2;
            if(binCount==0)accumulated[k]=accumulated[k+1]=out;
            else {accumulated[k]=std::min(accumulated[k],out);accumulated[k+1]=std::max(accumulated[k+1],out);}
        }
        if(++dcCount==dcSamples){
            for(size_t i=0;i<4;++i)heldDc[i]=float(dcSums[i]/dcSamples);
            dcSums={};dcCount=0;dcValid=true;
        }
        accumulated[10]=std::isfinite(gain)?gain:0;
        accumulated[11]=activity?1.f:0.f;
        accumulated[12]=dcValid?1.f:0.f;
        accumulated[13]=std::isfinite(frequency)?frequency:0;
        if(++binCount==binSamples){
            for(size_t i=0;i<4;++i)accumulated[i+2]=heldDc[i];
            const auto serial=published.load(std::memory_order_relaxed)+1;
            auto& slot=slots[serial%capacity];
            slot.sequence.fetch_add(1,std::memory_order_seq_cst);
            for(size_t i=0;i<accumulated.size();++i)slot.values[i].store(accumulated[i],std::memory_order_seq_cst);
            slot.generation.store(generation.load(std::memory_order_seq_cst),std::memory_order_seq_cst);
            slot.serial.store(serial,std::memory_order_seq_cst);slot.samples.store(binSamples,std::memory_order_seq_cst);slot.endSample.store(totalSamples,std::memory_order_seq_cst);
            slot.sequence.fetch_add(1,std::memory_order_seq_cst);
            published.store(serial,std::memory_order_release);
            binCount=0;accumulated={};
        }
    }
    uint32_t latestSerial() const noexcept {return published.load(std::memory_order_acquire);}
    uint32_t currentGeneration() const noexcept {return generation.load(std::memory_order_seq_cst);}
    bool read(uint32_t serial,FoundationDisplayFrame& result) const noexcept {
        const auto& slot=slots[serial%capacity];
        for(int attempt=0;attempt<3;++attempt){
            const auto before=slot.sequence.load(std::memory_order_seq_cst);
            if(before&1)continue;
            FoundationDisplayFrame candidate;
            for(size_t i=0;i<candidate.values.size();++i)candidate.values[i]=slot.values[i].load(std::memory_order_seq_cst);
            candidate.generation=slot.generation.load(std::memory_order_seq_cst);
            candidate.serial=slot.serial.load(std::memory_order_seq_cst);candidate.samples=slot.samples.load(std::memory_order_seq_cst);candidate.endSample=slot.endSample.load(std::memory_order_seq_cst);
            if(before==slot.sequence.load(std::memory_order_seq_cst)&&candidate.serial==serial
               &&candidate.generation==currentGeneration()&&candidate.samples!=0){result=candidate;return true;}
        }
        return false;
    }
private:
    struct Slot {
        std::atomic<uint32_t> sequence{0},generation{0},serial{0},samples{0};std::atomic<uint64_t> endSample{0};
        std::array<std::atomic<float>,FoundationDisplayFrame::fields> values{};
    };
    std::array<Slot,capacity> slots{};
    std::atomic<uint32_t> published{0},generation{0};
    double sampleRate=0;uint64_t totalSamples=0;bool dcValid=false,enabled=false;
    uint32_t binSamples=1,dcSamples=1,binCount=0,dcCount=0;
    std::array<float,FoundationDisplayFrame::fields> accumulated{};
    std::array<double,4> dcSums{};
    std::array<float,4> heldDc{};
    static_assert(std::atomic<float>::is_always_lock_free&&std::atomic<uint32_t>::is_always_lock_free&&std::atomic<uint64_t>::is_always_lock_free);
};
class FoundationDisplaySource {
public:
    virtual ~FoundationDisplaySource()=default;
    virtual const FoundationTelemetry& displayTelemetry() const noexcept=0;
};
}
