#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace hem::delayScaffoldTests {
// Deterministic test inputs only, never included in a product callback.
inline std::vector<float> impulse(std::size_t size, std::size_t index = 0, float amplitude = 1) {
    std::vector<float> result(size, 0);
    if (index < size) result[index] = amplitude;
    return result;
}
inline std::vector<float> noise(std::size_t size, std::uint32_t seed = 0x783ab4u) {
    std::vector<float> result(size);
    for (auto& value : result) {
        seed = 1664525u * seed + 1013904223u;
        value = static_cast<float>((static_cast<double>(seed) / 4294967295.0) * 2 - 1);
    }
    return result;
}
inline std::vector<int> partitions(std::size_t count) {
    constexpr std::array<int, 8> pattern{1, 17, 255, 3, 1024, 64, 511, 7};
    std::vector<int> result;
    for (std::size_t i = 0; count != 0; ++i) {
        const auto size = std::min(count, static_cast<std::size_t>(pattern[i % pattern.size()]));
        result.push_back(static_cast<int>(size));
        count -= size;
    }
    return result;
}
// Independent analytic oracle for integer-delay, unclipped feedback impulses.
inline double integerImpulseAt(std::size_t sample, std::size_t delay, double feedback) {
    if (delay == 0 || sample < delay || sample % delay != 0) return 0;
    return std::pow(feedback, static_cast<double>(sample / delay - 1));
}
} // namespace hem::delayScaffoldTests
