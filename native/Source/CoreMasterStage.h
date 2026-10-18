#pragma once
#include "CoreBaseline/Engine.h"

namespace hem {
// Host adapter for the active 4x limiter and final dual-kernel reconstruction guard.
// Preparation runs with the callback stopped. No processing buffers are resized
// or allocated by process(). Metering reports sample peak, not reconstructed TP.
class CoreMasterStage final {
public:
    void prepare(double actualRate, int maximumBlock) {
        if (!std::isfinite(actualRate) || actualRate <= 0 || maximumBlock <= 0)
            throw std::invalid_argument("Invalid native master preparation");
        maximum = maximumBlock;
        buffer.setSize(2, maximum);
        buffer.clear();
        limiter.prepare(actualRate, maximum, 2);
        reportedLatency.store(limiter.latency(), std::memory_order_release);
        samplePeak.store(0, std::memory_order_relaxed);
    }

    void process(float* left, float* right, int count) noexcept {
        if (!left || !right || count <= 0) return;
        if (maximum <= 0 || count > maximum) {
            std::fill_n(left, count, 0.f);
            std::fill_n(right, count, 0.f);
            samplePeak.store(0, std::memory_order_relaxed);
            return;
        }
        buffer.copyFrom(0, 0, left, count);
        buffer.copyFrom(1, 0, right, count);
        limiter.process(buffer, count);
        std::copy_n(buffer.getReadPointer(0), count, left);
        std::copy_n(buffer.getReadPointer(1), count, right);
        samplePeak.store(std::max(buffer.getMagnitude(0, 0, count),
                                  buffer.getMagnitude(1, 0, count)),
                         std::memory_order_relaxed);
    }

    int latencySamples() const noexcept { return reportedLatency.load(std::memory_order_acquire); }
    float samplePeakDb() const noexcept {
        return juce::Decibels::gainToDecibels(samplePeak.load(std::memory_order_relaxed), -120.f);
    }
private:
    MasterTruePeakLimiter limiter;
    juce::AudioBuffer<float> buffer;
    std::atomic<float> samplePeak{0};
    std::atomic<int> reportedLatency{0};
    int maximum = 0;
};
}
