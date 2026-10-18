#pragma once
#include "DspCore.h"
#include <cstdint>
#include <stdexcept>

namespace hem {
// Mandatory final device stage. No bypass, makeup, or gain may follow this.
// The ceiling bounds the defined 8x/32-tap reconstruction, not unknown DACs.
// prepare() is a lifecycle operation; process() allocates nothing and locks nothing.
class MasterLimiter final {
public:
    void prepare(double sampleRate) {
        if (!std::isfinite(sampleRate) || sampleRate < 8000 || sampleRate > 384000)
            throw std::invalid_argument("Unsupported master limiter sample rate");
        core.prepare(sampleRate);
        faults.store(0); ready = true;
    }
    void process(float* left, float* right, int count) noexcept {
        if (!ready) { std::fill_n(left,count,0.f); std::fill_n(right,count,0.f); return; }
        // Broken upstream DSP must never poison the limiter history. A very
        // large finite overload is contained too, before float arithmetic.
        std::uint32_t bad = 0;
        for (int i=0;i<count;++i) for (auto* channel : {left,right}) {
            float& x=channel[i];
            if (!std::isfinite(x)) { x=0; ++bad; }
            else x=std::max(-1000000.f,std::min(1000000.f,x));
        }
        if (bad) faults.fetch_add(bad,std::memory_order_relaxed);
        LimiterParameters p; p.input=0; p.ceiling=-1.f; p.release=100.f;
        core.process(left,right,count,p);
    }
    int latencySamples() const noexcept { return core.latency(); }
    float truePeakDb() const noexcept { return decibels(core.truePeak.load()); }
    float reductionDb() const noexcept { return core.reduction.load(); }
    std::uint32_t invalidSampleCount() const noexcept { return faults.load(); }
private:
    LimiterCore core;
    std::atomic<std::uint32_t> faults{0};
    bool ready=false;
};
}
