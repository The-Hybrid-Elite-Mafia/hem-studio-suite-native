#pragma once
#include <array>
#include <atomic>
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace hem {
// Single serialized producer; bounded, nonblocking readers. Group complete
// measured blocks until at least 10ms have elapsed. Never invent sub-block
// measurements. 512 slots retain at least 5.11s before the current bucket.
class ReductionHistory {
public:
    static constexpr unsigned capacity=512;
    struct Point { float value=0,auxiliary=0;std::uint64_t startSample=0,endSample=0;unsigned epoch=0; };
    void prepare(double rate,unsigned generation) noexcept {
        bucketSamples=std::isfinite(rate)&&rate>=8000&&rate<=384000?std::uint64_t(std::ceil(rate*.01)):0;
        serial=0;current={};current.epoch=generation;
    }
    std::uint64_t push(unsigned samples,float value,float auxiliary) noexcept {
        if(!samples||!bucketSamples)return serial;
        if(!serial||current.endSample-current.startSample>=bucketSamples){
            ++serial;current.startSample=current.endSample;current.value=value;current.auxiliary=auxiliary;
        }else{
            // Invalid measurements stay invalid for this bucket, not hidden by max.
            current.value=std::isfinite(current.value)&&std::isfinite(value)?std::max(current.value,value):NAN;
            current.auxiliary=std::isfinite(current.auxiliary)&&std::isfinite(auxiliary)?std::max(current.auxiliary,auxiliary):NAN;
        }
        current.endSample+=samples;
        auto& slot=history[serial%capacity];
        // Revision changes even when publishing the SAME unfinished bucket.
        slot.revision.fetch_add(1);
        slot.serial.store(serial);slot.start.store(current.startSample);slot.end.store(current.endSample);
        slot.value.store(current.value);slot.auxiliary.store(current.auxiliary);slot.epoch.store(current.epoch);
        slot.revision.fetch_add(1);
        return serial;
    }
    bool read(std::uint64_t wanted,unsigned generation,Point& result) const noexcept {
        if(!wanted)return false;const auto& slot=history[wanted%capacity];
        for(int attempt=0;attempt<3;++attempt){
            const auto before=slot.revision.load();if(before&1)continue;
            const auto id=slot.serial.load();Point p{slot.value.load(),slot.auxiliary.load(),slot.start.load(),slot.end.load(),slot.epoch.load()};
            if(before==slot.revision.load()&&id==wanted&&p.epoch==generation){result=p;return true;}
        }return false;
    }
    // Shared renderer mapping: clip actual measured intervals to a fixed 4s
    // window. No startup padding and no stretching the available history.
    static bool interval(const Point& p,std::uint64_t newest,double rate,float& left,float& right) noexcept {
        if(rate<=0||p.startSample>=p.endSample||p.endSample>newest)return false;
        const double endAge=double(newest-p.endSample)/rate;
        if(endAge>=4)return false;
        left=float(std::max(0.,1.-double(newest-p.startSample)/(4.*rate)));
        right=float(1.-endAge/4.);return left<right;
    }
private:
    struct Slot {
        std::atomic<std::uint64_t> revision{0},serial{0},start{0},end{0};
        std::atomic<float> value{0},auxiliary{0};std::atomic<unsigned> epoch{0};
    };
    std::array<Slot,capacity> history{};
    std::uint64_t serial=0,bucketSamples=0;Point current;
    static_assert(std::atomic<std::uint64_t>::is_always_lock_free&&std::atomic<float>::is_always_lock_free&&std::atomic<unsigned>::is_always_lock_free);
};
}
