#pragma once
#include "DelayPrimitives.h"

namespace hem::delay {
// Stable serialized enum values: never serialize a display/menu position instead.
enum class BeatDivision : int {
    n64 = 0, n64Dotted = 1, n64Triplet = 2,
    n32 = 3, n32Dotted = 4, n32Triplet = 5,
    n16 = 6, n16Dotted = 7, n16Triplet = 8,
    n8 = 9, n8Dotted = 10, n8Triplet = 11,
    quarter = 12, quarterDotted = 13, quarterTriplet = 14,
    half = 15, halfDotted = 16, halfTriplet = 17,
    whole = 18, wholeDotted = 19, wholeTriplet = 20
};
inline double beats(BeatDivision division) noexcept {
    constexpr std::array<double, 21> values{
        1.0/16, 3.0/32, 1.0/24, 1.0/8, 3.0/16, 1.0/12,
        1.0/4, 3.0/8, 1.0/6, 1.0/2, 3.0/4, 1.0/3,
        1, 1.5, 2.0/3, 2, 3, 4.0/3, 4, 6, 8.0/3
    };
    const auto index = static_cast<int>(division);
    return index >= 0 && index < static_cast<int>(values.size()) ? values[static_cast<std::size_t>(index)] : 0;
}
struct DelayTimeSetting {
    // Distinct state fields/units; toggling sync never mutates either field.
    double manualMilliseconds = 250;
    BeatDivision division = BeatDivision::quarter;
    bool sync = false;
};
struct TempoSnapshot { double bpm = 120; }; // legacy default is explicit
enum class TimeStatus { manual, synced, invalidState, invalidTempo, unavailableDivision };
struct ResolvedTime {
    TimeStatus status = TimeStatus::invalidState;
    double requestedMilliseconds = 0;
    double processingMilliseconds = 0;
    bool resolved() const noexcept { return status == TimeStatus::manual || status == TimeStatus::synced; }
};
inline ResolvedTime resolveTime(const DelayTimeSetting& setting, TempoSnapshot tempo) noexcept {
    if (!std::isfinite(setting.manualMilliseconds) || setting.manualMilliseconds < 1
        || setting.manualMilliseconds > 4000 || beats(setting.division) == 0) return {};
    if (!setting.sync) return {TimeStatus::manual, setting.manualMilliseconds, setting.manualMilliseconds};
    if (!std::isfinite(tempo.bpm) || tempo.bpm < 20 || tempo.bpm > 400)
        return {TimeStatus::invalidTempo, 0, setting.manualMilliseconds};
    const double milliseconds = 60000.0 * beats(setting.division) / tempo.bpm;
    if (milliseconds < 1 || milliseconds > 4000)
        return {TimeStatus::unavailableDivision, milliseconds, setting.manualMilliseconds};
    return {TimeStatus::synced, milliseconds, milliseconds};
}

struct TapActivity { bool on = false, solo = false, mute = false; };
inline std::array<bool, 8> audibleTaps(const std::array<TapActivity, 8>& taps,
                                     bool nodeCanAffectRender) noexcept {
    std::array<bool, 8> result{};
    if (!nodeCanAffectRender) return result;
    bool anySolo = false;
    for (const auto& tap : taps) anySolo = anySolo || (tap.on && !tap.mute && tap.solo);
    for (std::size_t i = 0; i < taps.size(); ++i)
        result[i] = taps[i].on && !taps[i].mute && (!anySolo || taps[i].solo);
    return result;
}
// Caller derives canAffect from captured track audibility, chain/module bypass,
// frozen mix>0 and actual tap activity. Inactive unresolved sync is not an error.
inline bool syncBlocksExport(const ResolvedTime& time, bool canAffectRender) noexcept {
    return canAffectRender && !time.resolved();
}

inline double tapeReadSamples(double baseMilliseconds, double wowPercent, double flutterPercent,
                              double wowPhase, double flutterPhase, double sampleRate) noexcept {
    if (!std::isfinite(sampleRate) || sampleRate < minimumRate || sampleRate > maximumRate) return 0;
    auto bounded = [](double value, double low, double high, double fallback) noexcept {
        return std::isfinite(value) ? std::clamp(value, low, high) : fallback;
    };
    const double base = bounded(baseMilliseconds, 1, 4000, 250);
    const double wow = bounded(wowPercent, 0, 100, 0) * 0.05;
    const double flutter = bounded(flutterPercent, 0, 100, 0) * 0.005;
    const double modulation = wow * (std::isfinite(wowPhase) ? std::sin(wowPhase) : 0)
                            + flutter * (std::isfinite(flutterPhase) ? std::sin(flutterPhase) : 0);
    // Keep minimum effective delay at 1 ms, even at maximum negative modulation.
    return std::clamp(base + modulation, 1.0, 4005.5) * sampleRate * 0.001;
}

// Per-tap bounds are combined in parallel, never summed in time. Each gain is
// multiplied by active count to give each tap epsilon/N residual allowance.
// Separate metadata type prevents passing the aggregate to a per-tap scheduler.
struct ParallelTailBound { bool valid = false; std::uint64_t samples = 0; double seconds = 0; };
inline ParallelTailBound parallelFrozenTail(const std::array<FrozenTailRequest, 8>& requests,
                                         const std::array<bool, 8>& audible, double sampleRate) noexcept {
    ParallelTailBound result;
    if (!std::isfinite(sampleRate) || sampleRate < minimumRate || sampleRate > maximumRate) return result;
    result.valid = true;
    unsigned activeCount = 0;
    for (bool active : audible) if (active) ++activeCount;
    for (std::size_t i = 0; i < requests.size(); ++i) {
        if (!audible[i]) continue;
        auto adjusted = requests[i];
        adjusted.audibleWet = true;
        adjusted.postHistoryPeakGain *= activeCount;
        const auto bound = frozenTail(adjusted, sampleRate);
        if (!bound.valid) { result.valid = false; return result; }
        result.samples = std::max(result.samples, bound.samples);
    }
    result.seconds = static_cast<double>(result.samples) / sampleRate;
    return result;
}
} // namespace hem::delay

