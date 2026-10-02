#include "../Source/Systems/AudioMeters.h"
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <new>
#include <stdexcept>
#include <vector>

namespace {
thread_local bool checkAllocations = false;
std::uint64_t allocations = 0;
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
void near(float actual, double expected, double tolerance, const char* message) {
    require(std::isfinite(actual) && std::abs(actual - expected) <= tolerance, message);
}
void push(hem::AudioMeters& meter, const float* left, const float* right, int count) {
    checkAllocations = true;
    meter.push(left, right, count);
    checkAllocations = false;
}
void sameChannel(const hem::AudioMeters::ChannelSnapshot& a, const hem::AudioMeters::ChannelSnapshot& b) {
    require(a.peakDb == b.peakDb && a.rmsDb == b.rmsDb && a.heldPeakDb == b.heldPeakDb
            && a.clipped == b.clipped, "Meter result depends on callback partitioning");
}
}
void* operator new(std::size_t count) {
    if (checkAllocations) ++allocations;
    if (auto* ptr = std::malloc(count ? count : 1)) return ptr;
    throw std::bad_alloc();
}
void* operator new[](std::size_t count) { return ::operator new(count); }
void operator delete(void* ptr) noexcept { std::free(ptr); }
void operator delete[](void* ptr) noexcept { std::free(ptr); }
void operator delete(void* ptr, std::size_t) noexcept { std::free(ptr); }
void operator delete[](void* ptr, std::size_t) noexcept { std::free(ptr); }

int main() {
    try {
        hem::AudioMeters meter;
        require(!meter.snapshot().ready, "Unprepared meter is marked ready");
        require(!meter.prepare(0) && !meter.prepare(-48000)
                && !meter.prepare(std::numeric_limits<double>::infinity()), "Invalid rate was accepted");
        push(meter, nullptr, nullptr, 16);
        require(!meter.snapshot().ready, "Invalid rate does not keep meter unavailable");

        for (const int rate : {44100, 48000, 96000}) {
            require(meter.prepare(rate), "Valid sample rate rejected");
            std::vector<float> left(std::size_t(rate) * 3, 0.5f), right(left.size(), 0.25f);
            push(meter, left.data(), right.data(), int(left.size()));
            auto s = meter.snapshot();
            require(s.ready && !s.left.clipped && !s.right.clipped, "Steady DC incorrectly clips");
            near(s.left.peakDb, -6.0205999, 0.00001, "Left DC sample peak calibration incorrect");
            near(s.right.peakDb, -12.0411998, 0.00001, "Right DC sample peak calibration incorrect");
            near(s.left.rmsDb, -6.0205999, 0.001, "Left steady DC RMS calibration incorrect");
            near(s.right.rmsDb, -12.0411998, 0.001, "Independent right RMS calibration incorrect");

            meter.prepare(rate);
            for (std::size_t i = 0; i < left.size(); ++i) {
                const double wave = std::sin(6.28318530717958647692 * 1000.0 * double(i) / rate);
                left[i] = float(0.5 * wave);
                right[i] = float(0.125 * wave);
            }
            push(meter, left.data(), right.data(), int(left.size()));
            s = meter.snapshot();
            near(s.left.peakDb, -6.0205999, 0.03, "Sine sample peak calibration incorrect");
            near(s.left.rmsDb, -9.0308999, 0.01, "Sine RMS is not amplitude divided by sqrt(2)");
            near(s.right.rmsDb, -21.0720997, 0.01, "Independent right sine RMS incorrect");
            near(s.left.heldPeakDb, -6.0205999, 0.001, "Sine held peak incorrect");

            meter.prepare(rate);
            push(meter, nullptr, nullptr, rate);
            s = meter.snapshot();
            require(s.left.peakDb == hem::AudioMeters::silenceFloorDb
                    && s.left.rmsDb == hem::AudioMeters::silenceFloorDb
                    && s.right.heldPeakDb == hem::AudioMeters::silenceFloorDb,
                    "Silence does not produce the documented meter floor");
        }

        // Exactly the same samples through different callback sizes must produce
        // exactly the same detectors, hold timing, counters and clip latches.
        std::vector<float> left(20000), right(left.size());
        for (std::size_t i = 0; i < left.size(); ++i) {
            left[i] = float(0.8 * std::sin(double(i) * 0.013));
            right[i] = float(1.05 * std::cos(double(i) * 0.027));
        }
        hem::AudioMeters whole, partitioned;
        whole.prepare(48000); partitioned.prepare(48000);
        push(whole, left.data(), right.data(), int(left.size()));
        const int sizes[] {1, 17, 256, 1024, 3, 819};
        int offset = 0, part = 0;
        while (offset < int(left.size())) {
            const int n = std::min(sizes[part++ % 6], int(left.size()) - offset);
            push(partitioned, left.data() + offset, right.data() + offset, n);
            offset += n;
        }
        sameChannel(whole.snapshot().left, partitioned.snapshot().left);
        sameChannel(whole.snapshot().right, partitioned.snapshot().right);

        // The 1 kHz test rate makes exact hold durations easy to inspect.
        meter.prepare(1000);
        const float fullScale = 1.0f, belowFullScale = 0.999f;
        push(meter, &fullScale, &belowFullScale, 1);
        require(meter.snapshot().left.clipped && !meter.snapshot().right.clipped,
                "Clip threshold or stereo independence incorrect");
        push(meter, nullptr, nullptr, 1000);
        auto s = meter.snapshot();
        near(s.left.peakDb, -24.0, 0.0001, "Sample peak release is not 24 dB per second");
        near(s.left.heldPeakDb, 0.0, 0.0001, "Held peak decays before one full second");
        require(s.left.clipped, "Clip latch vanished during silence");
        push(meter, nullptr, nullptr, 1000);
        near(meter.snapshot().left.heldPeakDb, -18.0, 0.0001, "Hold decay is not 18 dB per second");
        meter.resetClip();
        push(meter, nullptr, nullptr, 0);
        require(!meter.snapshot().left.clipped, "Audio-thread reset did not clear clip latch");
        meter.resetClip();
        push(meter, &fullScale, nullptr, 1);
        require(meter.snapshot().left.clipped, "Fresh clipping after reset did not re-latch");

        meter.prepare(48000);
        const float invalidL[] {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(), 0.5f};
        const float invalidR[] {0.0f, -std::numeric_limits<float>::infinity(), 0.25f};
        push(meter, invalidL, invalidR, 3);
        s = meter.snapshot();
        require(s.nonFiniteSamples == 3, "Non-finite samples were not counted independently");
        require(std::isfinite(s.left.rmsDb) && std::isfinite(s.right.rmsDb), "Non-finite input poisoned RMS");
        require(!s.left.clipped && !s.right.clipped, "Non-finite input was mislabeled as finite clipping");
        meter.resetClip();
        push(meter, nullptr, nullptr, 0);
        require(meter.snapshot().nonFiniteSamples == 0, "Reset did not clear invalid-sample counter");
        const float large = std::numeric_limits<float>::max();
        push(meter, &large, &large, 1);
        s = meter.snapshot();
        require(std::isfinite(s.left.rmsDb) && std::isfinite(s.left.peakDb) && s.left.clipped,
                "Largest finite float overflowed meter math");
        meter.prepare(96000);
        s = meter.snapshot();
        require(s.left.peakDb == hem::AudioMeters::silenceFloorDb && !s.left.clipped && s.nonFiniteSamples == 0,
                "Device-rate prepare did not reset old measurements");
        require(allocations == 0, "Meter allocated on the audio callback");
        std::cout << "PASS: AudioMeters DC/sine calibration at 44.1/48/96 kHz, independent stereo, silence, "
                     "partition invariance, peak release/hold, clip/reset, invalid samples, finite bounds, "
                     "rate reset and zero callback allocations\n";
    } catch (const std::exception& error) {
        checkAllocations = false;
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
