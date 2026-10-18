// NOT EXECUTED. Compile/run approval status is maintained outside this source.
#include "../Source/DSP/DelayKernelScaffold.h"
#include "../Source/DSP/DelaySettingsScaffold.h"
#include "DelayScaffoldGenerators.h"
#include <cstdlib>
#include <iostream>
#include <limits>
#include <new>
#include <stdexcept>

static bool watchAllocations = false;
static std::size_t allocationCount = 0;
void* operator new(std::size_t size) {
    if (watchAllocations) ++allocationCount;
    if (auto* memory = std::malloc(size == 0 ? 1 : size)) return memory;
    throw std::bad_alloc();
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* memory) noexcept { std::free(memory); }
void operator delete[](void* memory) noexcept { std::free(memory); }
void operator delete(void* memory, std::size_t) noexcept { std::free(memory); }
void operator delete[](void* memory, std::size_t) noexcept { std::free(memory); }
static void require(bool condition, const char* reason) { if (!condition) throw std::runtime_error(reason); }

int main() {
    using namespace hem::delayScaffold;
    using namespace hem::delayScaffoldTests;
    try {
        for (double rate : {8000.0, 44100.0, 48000.0, 96000.0, 384000.0}) {
            StereoHistory storage;
            storage.prepare(rate);
            require(storage.capacity() == static_cast<std::size_t>(std::ceil(rate * 4.006)) + 2,
                    "Delay ring did not allocate at native sample rate with guards");
            constexpr double halfPi = 1.57079632679489661923;
            const double longestRead = tapeReadSamples(4000, 100, 100, halfPi, halfPi, rate);
            const double shortestRead = tapeReadSamples(1, 100, 100, -halfPi, -halfPi, rate);
            require(std::abs(longestRead - 4.0055 * rate) < 1e-8
                    && std::ceil(longestRead) + 1 < storage.capacity(),
                    "Combined wow/flutter excursion lacks interpolation guard");
            require(std::abs(shortestRead - 0.001 * rate) < 1e-10,
                    "Negative wow/flutter escaped one-millisecond minimum");
            FixedEchoPrimitive echo;
            echo.prepare(rate);
            constexpr std::size_t delay = 17;
            watchAllocations = true;
            for (std::size_t n = 0; n < 8192; ++n) {
                const auto wet = echo.tick({n == 0 ? 1.0f : 0.0f, 0}, delay, 0.5);
                require(std::abs(wet.left - integerImpulseAt(n, delay, 0.5)) < 1e-7,
                        "Integer impulse position/recurrence is wrong");
                require(wet.right == 0, "Stereo history leaked channels");
            }
            watchAllocations = false;
            echo.reset();
            for (int n = 0; n < 30; ++n) {
                const auto wet = echo.tick({n == 0 ? 1.0f : 0.0f, 0}, 8.5, 0);
                require(wet.left == ((n == 8 || n == 9) ? 0.5f : 0),
                        "Fractional impulse did not split at adjacent expected samples");
            }
        }

        // Full-scale sustained input: theoretical steady state is 20, not 16.
        FixedEchoPrimitive sustained;
        sustained.prepare(8000);
        StereoSample last;
        watchAllocations = true;
        for (int n = 0; n < 200000; ++n) last = sustained.tick({1, -1}, 8, 0.95);
        watchAllocations = false;
        require(std::abs(last.left - 20) < 1e-3 && std::abs(last.right + 20) < 1e-3,
                "Supported full-scale domain was unexpectedly clipped");
        require(sustained.protectionEvents() == 0, "Protection activated for full-scale input");
        sustained.tick({std::numeric_limits<float>::max(), -std::numeric_limits<float>::max()}, 8, 0.95);
        require(sustained.protectionEvents() != 0, "Extreme-input protection was not exposed");
        for (int n = 0; n < 10000; ++n) {
            auto wet = sustained.tick({std::numeric_limits<float>::quiet_NaN(),
                                       std::numeric_limits<float>::infinity()}, 8, 0.5);
            require(std::isfinite(wet.left) && std::isfinite(wet.right), "Nonfinite history escaped");
        }
        sustained.reset();
        for (int n = 0; n < 40000; ++n)
            require(sustained.tick({}, 8.5, 0.95).left == 0, "Reset exposed old history");

        // Wrap multiple times; regroup exactly the same sample stream.
        const auto input = noise(180000);
        const auto blocks = partitions(input.size());
        FixedEchoPrimitive contiguous, partitioned, fresh;
        contiguous.prepare(8000); partitioned.prepare(8000); fresh.prepare(8000);
        std::vector<float> reference(input.size());
        watchAllocations = true;
        for (std::size_t i = 0; i < input.size(); ++i)
            reference[i] = contiguous.tick({input[i], -input[i]}, 199.25, 0.73).left;
        std::size_t cursor = 0;
        for (int block : blocks) for (int n = 0; n < block; ++n, ++cursor)
            require(partitioned.tick({input[cursor], -input[cursor]}, 199.25, 0.73).left == reference[cursor],
                    "Block partition changed fixed echo output");
        watchAllocations = false;
        partitioned.prepare(8000);
        for (std::size_t i = 0; i < 4096; ++i)
            require(partitioned.tick({input[i], 0}, 113.75, 0.8).left
                    == fresh.tick({input[i], 0}, 113.75, 0.8).left,
                    "Reprepare differed from fresh history");

        FrozenTailRequest request;
        const auto zeroFeedback = frozenTail(request, 48000);
        require(zeroFeedback.valid && zeroFeedback.samples == 12000
                && !zeroFeedback.needsResidualTermination, "Zero-feedback tail is not exactly actual delay");
        FrozenSilenceSchedule oneShotSchedule;
        require(oneShotSchedule.configure(zeroFeedback), "One-shot schedule rejected valid bound");
        oneShotSchedule.advance(true);
        for (std::uint64_t n = 1; n <= zeroFeedback.samples; ++n) {
            const auto decision = oneShotSchedule.advance(false);
            require(decision.wetGain == 1 && !decision.discardHistory,
                    "One-shot echo was faded or invalidated before naturally draining");
        }
        require(oneShotSchedule.advance(false).discardHistory, "Drained history was not invalidated");
        request.feedback = 0.5;
        const auto halfFeedback = frozenTail(request, 48000);
        require(halfFeedback.valid && halfFeedback.seconds > 0.25 && halfFeedback.seconds < 10,
                "Captured moderate feedback incorrectly requires safety-cap export");
        FrozenSilenceSchedule residualSchedule;
        residualSchedule.configure(halfFeedback);
        residualSchedule.advance(true);
        for (std::uint64_t n = 1; n <= halfFeedback.residualBoundSamples; ++n)
            require(residualSchedule.advance(false).wetGain == 1,
                    "Residual fade began before the proven bound");
        require(residualSchedule.advance(false).wetGain < 1
                && halfFeedback.samples == halfFeedback.residualBoundSamples + 960,
                "20 ms residual fade was not added after the bound");
        FrozenTailRequest filtered;
        filtered.filtersEnabled = true;
        filtered.enabledFilterStages = 1;
        const auto slowFilter = frozenTail(filtered, 48000);
        filtered.slowestEnabledFilterHz = 2000;
        const auto fastFilter = frozenTail(filtered, 48000);
        require(slowFilter.valid && fastFilter.valid && fastFilter.samples < slowFilter.samples,
                "Frozen filter bound ignored captured frequency");
        request.effectiveDelaySeconds = maximumHistorySeconds;
        request.feedback = maximumFeedback;
        request.postHistoryPeakGain = 256;
        request.filtersEnabled = true;
        request.finiteOutputSupportSeconds = 0.01;
        const auto maximum = frozenTail(request, 8000);
        require(maximum.valid && maximum.seconds < liveSafetyTailSeconds,
                "Worst supported frozen-tail bound exceeds disclosed safety cap");
        request.audibleWet = false;
        require(frozenTail(request, 48000).samples == 0, "Proven frozen dry path has an echo tail");
        request.feedback = std::numeric_limits<double>::quiet_NaN();
        require(!frozenTail(request, 48000).valid, "Invalid tail settings accepted");

        DelayTimeSetting time;
        time.manualMilliseconds = 271.25;
        time.division = BeatDivision::quarter;
        time.sync = true;
        require(resolveTime(time, {120}).processingMilliseconds == 500,
                "Quarter-note sync did not derive from real BPM");
        require(resolveTime(time, {60}).processingMilliseconds == 1000,
                "Tempo change did not change synced duration");
        time.sync = false;
        require(resolveTime(time, {60}).processingMilliseconds == 271.25,
                "Sync toggle reinterpreted or destroyed stored manual milliseconds");
        time.sync = true; time.division = BeatDivision::wholeDotted;
        const auto unresolved = resolveTime(time, {20});
        require(unresolved.status == TimeStatus::unavailableDivision
                && unresolved.requestedMilliseconds == 18000
                && unresolved.processingMilliseconds == 271.25,
                "Unavailable division silently clamped or falsely reported sync");
        require(syncBlocksExport(unresolved, true) && !syncBlocksExport(unresolved, false),
                "Sync preflight did not respect actual frozen audibility");
        std::array<TapActivity, 8> activity{};
        activity[0] = {true, false, false};
        activity[1] = {true, true, true}; // muted solo must not suppress tap 1
        activity[2] = {false, true, false}; // off solo must not suppress tap 1
        require(audibleTaps(activity, true)[0], "Muted/off solo suppressed an audible tap");
        activity[3] = {true, true, false};
        const auto soloMask = audibleTaps(activity, true);
        require(!soloMask[0] && soloMask[3] && !soloMask[1] && !soloMask[2],
                "On/solo/mute precedence differs from contract");
        for (bool active : audibleTaps(activity, false)) require(!active, "Inactive node leaked audible taps");
        std::array<FrozenTailRequest, 8> tapRequests{};
        std::array<bool, 8> active{}; active[0] = true; active[1] = true;
        tapRequests[0].effectiveDelaySeconds = 0.25;
        tapRequests[1].effectiveDelaySeconds = 0.5;
        const auto parallel = parallelFrozenTail(tapRequests, active, 48000);
        require(parallel.valid && parallel.samples == 24000,
                "Parallel one-shot tails were summed or shortened");
        active[1] = false;
        tapRequests[1].feedback = std::numeric_limits<double>::quiet_NaN();
        require(parallelFrozenTail(tapRequests, active, 48000).samples == 12000,
                "Inactive tap influenced captured tail");
        require(allocationCount == 0, "Primitive allocated while processing");
        std::cout << "PASS: isolated primitive tests only; no complete module/GUI/host certification.\n";
        return 0;
    } catch (const std::exception& error) {
        watchAllocations = false;
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
