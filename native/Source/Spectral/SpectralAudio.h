#pragma once
// SPDX-License-Identifier: AGPL-3.0-or-later
// Minimal PCM/validation extraction from HEM Stem Workbench AudioCore.h.
// See ATTRIBUTION.md. Offline worker use only; no session or playback engine.
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

namespace hem::spectral {
inline constexpr std::size_t maxAudioBytes = 512ULL * 1024ULL * 1024ULL;
inline constexpr double maxDurationSeconds = 30.0 * 60.0;
struct Audio {
    double sampleRate = 96000.0;
    std::vector<std::vector<float>> channels;
    [[nodiscard]] std::size_t frames() const noexcept {
        return channels.empty() ? 0 : channels.front().size();
    }
};
namespace audioDetail {
inline void validateShape(double rate, std::size_t channels, std::size_t frames) {
    if (!std::isfinite(rate) || rate < 8000.0 || rate > 384000.0)
        throw std::invalid_argument("Sample rate must be finite and between 8000 and 384000 Hz.");
    if (channels < 1 || channels > 2)
        throw std::invalid_argument("Audio must contain one or two channels.");
    if (frames > maxAudioBytes / sizeof(float) / channels)
        throw std::length_error("Audio exceeds the 512 MiB sample-payload limit.");
    if (static_cast<long double>(frames) > static_cast<long double>(rate) * maxDurationSeconds)
        throw std::length_error("Audio exceeds the 30-minute duration limit.");
}
inline float checkedFloat(double value) {
    if (!std::isfinite(value) || std::abs(value) > static_cast<double>(std::numeric_limits<float>::max()))
        throw std::overflow_error("Audio processing would produce a non-finite float sample.");
    return static_cast<float>(value);
}
}
// A correctly shaped empty buffer is valid. Float overs above +/-1 stay intact.
// Cancellation is checked in bounded chunks, including validation of long files.
inline void validate(const Audio& audio, const std::atomic<bool>* cancelled = nullptr) {
    audioDetail::validateShape(audio.sampleRate, audio.channels.size(), audio.frames());
    for (const auto& channel : audio.channels) {
        if (channel.size() != audio.frames())
            throw std::invalid_argument("Audio channels have unequal frame counts.");
        for (std::size_t frame = 0; frame < channel.size(); ++frame) {
            if ((frame & 4095u) == 0 && cancelled != nullptr && cancelled->load(std::memory_order_relaxed))
                throw std::runtime_error("Cancelled");
            if (!std::isfinite(channel[frame]))
                throw std::invalid_argument("Audio contains NaN or infinite samples.");
        }
    }
}
} // namespace hem::spectral
