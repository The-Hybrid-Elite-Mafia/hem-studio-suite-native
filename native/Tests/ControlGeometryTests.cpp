#include "../Source/UI/ControlGeometry.h"
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
void require(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
bool near(float a, float b) { return std::abs(a - b) < 0.0001f; }
}
int main() {
    try {
        constexpr float pi = 3.14159265358979323846f;
        const float start = pi * 1.2f, end = pi * 2.8f;
        for (const float scale : {0.50f, 0.75f, 1.0f, 1.25f, 2.0f}) {
            const float x = 23 * scale, y = 41 * scale, w = 94 * scale, h = 98 * scale;
            const auto first = hem::ui::dialGeometry(x, y, w, h, 0, start, end);
            const auto middle = hem::ui::dialGeometry(x, y, w, h, 0.5f, start, end);
            const auto last = hem::ui::dialGeometry(x, y, w, h, 1, start, end);
            require(first.drawable && middle.drawable && last.drawable, "Valid dial disappeared");
            require(near(middle.centreX, x + w * 0.5f) && near(middle.centreY, y + h * 0.5f), "Dial ignores local coordinate offsets");
            require(near(first.angle, start) && near(last.angle, end), "Dial endpoint mapping incorrect");
            require(middle.pointerEndY < middle.centreY, "Midpoint indicator does not point up");
            require(first.pointerEndX < first.centreX && last.pointerEndX > last.centreX, "Dial pointer does not rotate left to right");
            require(first.centreX - first.ringRadius >= x && first.centreX + first.ringRadius <= x + w
                    && first.centreY - first.ringRadius >= y && first.centreY + first.ringRadius <= y + h,
                    "Dial ring escapes its mapped hitbox");
            const auto scaled = hem::ui::dialGeometry(0, 0, 94, 98, 0.5f, start, end);
            require(near(middle.pointerEndX, x + scaled.pointerEndX * scale)
                    && near(middle.pointerEndY, y + scaled.pointerEndY * scale), "Scaled pointer drifts from artwork");
        }
        require(!hem::ui::dialGeometry(0, 0, 0, 10, 0, start, end).drawable, "Zero-width dial is drawable");
        require(!hem::ui::dialGeometry(0, 0, 10, -1, 0, start, end).drawable, "Negative-height dial is drawable");
        require(near(hem::ui::dialGeometry(0, 0, 50, 50, -4, start, end).angle, start), "Negative position is not clamped");
        require(near(hem::ui::dialGeometry(0, 0, 50, 50, 4, start, end).angle, end), "Oversized position is not clamped");
        require(near(hem::ui::dialGeometry(0, 0, 50, 50, std::numeric_limits<float>::quiet_NaN(), start, end).angle, start), "NaN position produces invalid pointer geometry");
        require(near(hem::ui::boundedSliderPosition(50, 20, 100), 50), "Fader does not retain its actual thumb position");
        require(near(hem::ui::boundedSliderPosition(-500, 20, 100), 20), "Fader minimum bounds incorrect");
        require(near(hem::ui::boundedSliderPosition(500, 20, 100), 120), "Fader maximum bounds incorrect");
        std::cout << "PASS: local-coordinate dial rotation, bounds, scale/offset preservation, finite input and fader positions\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
