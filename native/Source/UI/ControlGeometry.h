#pragma once
#include <algorithm>
#include <cmath>

namespace hem::ui {
// Pure local-coordinate geometry shared by the renderer and dependency-free tests.
// Host letterboxing belongs to the component layout, never to the look-and-feel.
struct DialGeometry {
    float centreX{}, centreY{}, radius{}, ringRadius{}, angle{};
    float pointerStartX{}, pointerStartY{}, pointerEndX{}, pointerEndY{};
    bool drawable{};
};

inline DialGeometry dialGeometry(float x, float y, float width, float height,
                                 float position, float start, float end) noexcept {
    DialGeometry d;
    if (!(width > 0.0f && height > 0.0f) || !std::isfinite(width)
        || !std::isfinite(height) || !std::isfinite(start) || !std::isfinite(end))
        return d;
    const float size = std::min(width, height);
    const float fraction = std::isfinite(position) ? std::clamp(position, 0.0f, 1.0f) : 0.0f;
    d.centreX = x + width * 0.5f;
    d.centreY = y + height * 0.5f;
    d.radius = size * 0.36f;
    d.ringRadius = size * 0.43f;
    d.angle = start + fraction * (end - start);
    const auto pointX = [&](float radius) { return d.centreX + std::sin(d.angle) * radius; };
    const auto pointY = [&](float radius) { return d.centreY - std::cos(d.angle) * radius; };
    d.pointerStartX = pointX(d.radius * 0.34f);
    d.pointerStartY = pointY(d.radius * 0.34f);
    d.pointerEndX = pointX(d.radius * 0.84f);
    d.pointerEndY = pointY(d.radius * 0.84f);
    d.drawable = true;
    return d;
}

inline float boundedSliderPosition(float position, float origin, float length) noexcept {
    if (!std::isfinite(position)) return origin;
    return std::clamp(position, origin, origin + std::max(0.0f, length));
}
} // namespace hem::ui
