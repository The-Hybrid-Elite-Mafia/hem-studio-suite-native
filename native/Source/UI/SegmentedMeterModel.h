#pragma once
// NEW implementation, 2026-10-07. See PROVENANCE.md; not recovered source.
#include <algorithm>
#include <cmath>
#include <limits>

namespace hem::ui {
inline constexpr float meterFloorDb = -60.0f;
inline constexpr float meterCeilingDb = 0.0f;
inline constexpr int defaultMeterSegments = 24;
inline float meterDbPosition(float db) noexcept {
    if (!std::isfinite(db)) return 0.0f;
    return std::clamp((db - meterFloorDb) / (meterCeilingDb - meterFloorDb), 0.0f, 1.0f);
}
inline float meterAmplitudePosition(float amplitude) noexcept {
    return std::isfinite(amplitude) && amplitude > 0.0f
        ? meterDbPosition(20.0f * std::log10(amplitude)) : 0.0f;
}
struct MeterRect { float x{}, y{}, width{}, height{}; };
inline bool usableMeterRect(MeterRect r) noexcept {
    return std::isfinite(r.x) && std::isfinite(r.y) && std::isfinite(r.width)
        && std::isfinite(r.height) && r.width > 0 && r.height > 0
        && std::isfinite(r.x + r.width) && std::isfinite(r.y + r.height);
}
struct MeterSegments {
    MeterRect lane{};
    int count{};
    float step{}, gap{}, position{};
    bool valid{}, reduction{};
    MeterRect segment(int index) const noexcept {
        if (index < 0 || index >= count) return {};
        // Level grows bottom-up; gain reduction grows top-down.
        const int row = reduction ? index : count - 1 - index;
        return {lane.x, lane.y + row * step + gap * 0.5f, lane.width, step - gap};
    }
    bool lit(int index) const noexcept {
        // Never invent a first LED at zero or on missing/nonfinite data.
        return valid && index >= 0 && index < count && position > 0
            && position >= static_cast<float>(index + 1) / static_cast<float>(count);
    }
};
inline MeterSegments meterSegments(MeterRect lane, float normalized, bool valid,
                                   bool reduction = false, int requested = defaultMeterSegments) noexcept {
    MeterSegments result;
    if (!usableMeterRect(lane)) return result;
    result.lane = lane;
    // A minimum two-unit pitch keeps compact lanes well-formed. Subpixel lanes
    // remain a single segment. Cap user input before float-to-int conversion.
    const int capacity = static_cast<int>(std::clamp(std::floor(lane.height / 2.0f), 1.0f, 256.0f));
    result.count = std::clamp(requested, 1, capacity);
    result.step = lane.height / static_cast<float>(result.count);
    result.gap = std::min(1.0f, result.step * 0.2f);
    result.valid = valid && std::isfinite(normalized);
    result.position = result.valid ? std::clamp(normalized, 0.0f, 1.0f) : 0.0f;
    result.reduction = reduction;
    return result;
}
enum class MeterMarkRole { rms, peakHold };
struct MeterMarks { MeterRect first{}, second{}, highlight{}; bool visible{}; };
inline MeterMarks meterMarks(MeterRect lane, float normalized, bool valid,
                             MeterMarkRole role, float thickness = 1.5f) noexcept {
    MeterMarks marks;
    if (!valid || !usableMeterRect(lane) || !std::isfinite(normalized)
        || normalized <= 0 || !std::isfinite(thickness) || thickness <= 0) return marks;
    const float h = std::min(thickness, lane.height);
    const float position = std::clamp(normalized, 0.0f, 1.0f);
    const float y = std::clamp(lane.y + lane.height * (1.0f-position) - h*0.5f,
                             lane.y, lane.y + lane.height - h);
    // Disjoint horizontal regions preserve both signals even at identical dB.
    if (role == MeterMarkRole::rms) {
        marks.first = {lane.x + lane.width*0.3f, y, lane.width*0.4f, h};
        marks.highlight = {lane.x + lane.width*0.45f, y, lane.width*0.1f, h};
    } else {
        marks.first = {lane.x, y, lane.width*0.2f, h};
        marks.second = {lane.x + lane.width*0.8f, y, lane.width*0.2f, h};
    }
    marks.visible = true;
    return marks;
}
} // namespace hem::ui
