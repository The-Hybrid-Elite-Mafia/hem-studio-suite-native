#pragma once
// SPDX-License-Identifier: AGPL-3.0-or-later
// Adapted from HEM Stem Workbench. See ATTRIBUTION.md.

// Offline, measured spectral analysis and conservative spectral attenuation.
// These functions allocate, may throw, and perform FFT work. Invoke them only
// from a worker (e.g. juce::ThreadPool), never processBlock() or the UI thread.
// No source recognition, de-reverberation, or AI restoration is performed.

#include "SpectralAudio.h"
#include <atomic>
#include <juce_dsp/juce_dsp.h>
#include <numbers>

namespace hem::spectral
{

inline constexpr int fftOrder = 11;
inline constexpr std::size_t fftSize = std::size_t{1} << fftOrder;
inline constexpr std::size_t hopSize = 512;
inline constexpr std::size_t maxTimeBins = 96;
inline constexpr std::size_t maxFrequencyBins = 64;
inline constexpr float floorDb = -120.0f;

struct Grid
{
    std::size_t timeBins = 0;
    std::size_t freqBins = 0;
    double sampleRate = 0.0;
    double durationSeconds = 0.0;
    bool stereo = false;
    // Actual FFT-band centres, increasing, at or above 20 Hz. Empty logarithmic
    // bands are removed: this does not imply resolution finer than rate / 2048.
    std::vector<float> frequenciesHz;
    // Cell boundaries (freqBins + 1), for accurate painting/selection mapping.
    std::vector<float> frequencyEdgesHz;
    // Time-major [time * freqBins + frequency]. Each cell is the RMS across
    // its analysis windows of the strongest FFT bin in that logarithmic band.
    // A Hann-window-corrected, bin-centred sine of amplitude 1 reads 0 dBFS
    // away from file edges. Stereo magnitude uses mean L/R spectral power.
    std::vector<float> magnitudeDb;
    // The same measurement for S = (L - R) / 2; mono has no side channel.
    std::vector<float> sideDb;
    // Normalized L/R dot product over each exact time interval, [-1, 1].
    // Mono, silence, and a silent channel return 0 (undefined, not "in phase").
    std::vector<float> correlations;
    std::vector<std::size_t> startFrames;
    std::vector<std::size_t> endFrames;
};
using SpectralGrid = Grid;

struct Selection
{
    std::size_t startFrame = 0;
    std::size_t endFrame = 0; // exclusive
    float lowHz = 20.0f;
    float highHz = 20000.0f;
    float gain = 0.5f;
};

namespace detail
{

inline void checkCancelled(const std::atomic<bool> *cancelled)
{
    if (cancelled != nullptr && cancelled->load(std::memory_order_relaxed))
        throw std::runtime_error("Cancelled");
}

inline std::array<float, fftSize> makeWindow()
{
    std::array<float, fftSize> window{};
    for (std::size_t i = 0; i < fftSize; ++i)
        window[i] = static_cast<float>(
            0.5 - 0.5 * std::cos(2.0 * std::numbers::pi * static_cast<double>(i) /
                                 static_cast<double>(fftSize)));
    return window;
}

inline float decibels(const double power)
{
    return static_cast<float>(
        std::max(static_cast<double>(floorDb), 10.0 * std::log10(std::max(power, 1.0e-12))));
}

inline double smoothstep(const double x) noexcept
{
    const double v = std::clamp(x, 0.0, 1.0);
    return v * v * (3.0 - 2.0 * v);
}

inline void validateSelection(const Audio &input, const Selection &selection)
{
    if (selection.startFrame >= selection.endFrame || selection.endFrame > input.frames())
        throw std::invalid_argument(
            "Spectral selection must be a non-empty frame range inside the audio.");
    if (!std::isfinite(selection.lowHz) || !std::isfinite(selection.highHz) ||
        selection.lowHz < 0.0f || selection.highHz <= selection.lowHz ||
        static_cast<double>(selection.highHz) > input.sampleRate * 0.5)
        throw std::invalid_argument(
            "Spectral selection must be a finite frequency range within Nyquist.");
    if (!std::isfinite(selection.gain) || selection.gain < 0.0f || selection.gain > 1.0f)
        throw std::invalid_argument(
            "Spectral attenuation gain must be finite and between zero and one.");
}

// Raised shoulders stay inside the selected band. One FFT-bin transition at
// each edge is used when the bandwidth permits, otherwise each shoulder is
// one quarter of the selected bandwidth. DC/Nyquist edges remain fully active.
inline double frequencyWeight(const double hz, const Selection &s, const double binHz,
                              const double nyquist) noexcept
{
    if (hz < static_cast<double>(s.lowHz) || hz > static_cast<double>(s.highHz))
        return 0.0;
    const double width = static_cast<double>(s.highHz) - static_cast<double>(s.lowHz);
    const double taper = std::min(binHz, width * 0.25);
    const double low = s.lowHz <= 0.0f ? 1.0 : smoothstep((hz - s.lowHz) / taper);
    const double high =
        static_cast<double>(s.highHz) >= nyquist ? 1.0 : smoothstep((s.highHz - hz) / taper);
    return low * high;
}

// A final sample-domain blend guarantees samples outside [start,end) stay
// exactly unchanged. The shoulders reduce abrupt edit boundaries. Entire-file
// edges need no blend to an outside sample and therefore retain full strength.
inline double timeWeight(const std::size_t frame, const std::size_t totalFrames,
                         const Selection &s) noexcept
{
    if (frame < s.startFrame || frame >= s.endFrame)
        return 0.0;
    const auto length = s.endFrame - s.startFrame;
    const double taper = std::max(
        1.0, std::min(static_cast<double>(fftSize / 2), static_cast<double>(length) * 0.25));
    const double left =
        s.startFrame == 0 ? 1.0 : smoothstep(static_cast<double>(frame - s.startFrame) / taper);
    const double right = s.endFrame == totalFrames
                             ? 1.0
                             : smoothstep(static_cast<double>(s.endFrame - 1 - frame) / taper);
    return left * right;
}

} // namespace detail

[[nodiscard]] inline Grid analyze(const Audio &input, const std::atomic<bool> *cancelled = nullptr)
{
    detail::checkCancelled(cancelled);
    validate(input, cancelled);
    detail::checkCancelled(cancelled);
    Grid result;
    result.sampleRate = input.sampleRate;
    result.durationSeconds = static_cast<double>(input.frames()) / input.sampleRate;
    result.stereo = input.channels.size() == 2;
    if (input.frames() == 0)
        return result;

    const double binHz = input.sampleRate / static_cast<double>(fftSize);
    const double nyquist = input.sampleRate * 0.5;
    const double logarithmicRange = std::log(nyquist / 20.0);
    std::array<std::size_t, maxFrequencyBins> firstBin{};
    std::array<std::size_t, maxFrequencyBins> lastBin{};
    for (std::size_t k = 1; k <= fftSize / 2; ++k)
    {
        const double hz = static_cast<double>(k) * binHz;
        if (hz < 20.0)
            continue;
        const auto bucket = std::min(maxFrequencyBins - 1,
                                     static_cast<std::size_t>(std::max(
                                         0.0, std::floor(std::log(hz / 20.0) / logarithmicRange *
                                                         static_cast<double>(maxFrequencyBins)))));
        if (firstBin[bucket] == 0)
            firstBin[bucket] = k;
        lastBin[bucket] = k;
    }
    std::vector<std::pair<std::size_t, std::size_t>> bands;
    for (std::size_t bucket = 0; bucket < maxFrequencyBins; ++bucket)
    {
        if (firstBin[bucket] == 0)
            continue;
        bands.emplace_back(firstBin[bucket], lastBin[bucket]);
        result.frequenciesHz.push_back(static_cast<float>(
            std::sqrt(static_cast<double>(firstBin[bucket] * lastBin[bucket])) * binHz));
        result.frequencyEdgesHz.push_back(
            static_cast<float>(20.0 * std::exp(logarithmicRange * static_cast<double>(bucket) /
                                               static_cast<double>(maxFrequencyBins))));
    }
    result.frequencyEdgesHz.front() = 20.0f;
    result.frequencyEdgesHz.push_back(static_cast<float>(nyquist));
    result.freqBins = bands.size();
    const auto analysisWindows = (input.frames() - 1) / hopSize + 1;
    result.timeBins = std::min(maxTimeBins, analysisWindows);
    const auto cells = result.timeBins * result.freqBins;
    std::vector<double> magnitudePower(cells, 0.0);
    std::vector<double> sidePower(cells, 0.0);
    std::vector<std::size_t> counts(result.timeBins, 0);
    result.magnitudeDb.resize(cells, floorDb);
    result.sideDb.resize(cells, floorDb);
    result.correlations.resize(result.timeBins, 0.0f);
    for (std::size_t t = 0; t < result.timeBins; ++t)
    {
        detail::checkCancelled(cancelled);
        result.startFrames.push_back(input.frames() * t / result.timeBins);
        result.endFrames.push_back(input.frames() * (t + 1) / result.timeBins);
        if (result.stereo)
        {
            double cross = 0.0, leftPower = 0.0, rightPower = 0.0;
            for (auto frame = result.startFrames.back(); frame < result.endFrames.back(); ++frame)
            {
                if ((frame & 4095u) == 0)
                    detail::checkCancelled(cancelled);
                const double left = input.channels[0][frame];
                const double right = input.channels[1][frame];
                cross += left * right;
                leftPower += left * left;
                rightPower += right * right;
            }
            if (leftPower > 0.0 && rightPower > 0.0)
                result.correlations[t] = static_cast<float>(
                    std::clamp(cross / (std::sqrt(leftPower) * std::sqrt(rightPower)), -1.0, 1.0));
        }
    }

    juce::dsp::FFT fft(fftOrder);
    const auto window = detail::makeWindow();
    double windowSum = 0.0;
    for (const float value : window)
        windowSum += value;
    std::array<float, fftSize * 2> left{};
    std::array<float, fftSize * 2> right{};
    for (std::size_t centre = 0; centre < input.frames(); centre += hopSize)
    {
        detail::checkCancelled(cancelled);
        const auto start =
            static_cast<std::int64_t>(centre) - static_cast<std::int64_t>(fftSize / 2);
        double scale = 0.0;
        for (std::size_t n = 0; n < fftSize; ++n)
        {
            const auto frame = start + static_cast<std::int64_t>(n);
            if (frame < 0 || static_cast<std::size_t>(frame) >= input.frames())
                continue;
            for (const auto &channel : input.channels)
                scale = std::max(
                    scale, std::abs(static_cast<double>(channel[static_cast<std::size_t>(frame)])));
        }
        // Scaling prevents FFT overflow on otherwise valid finite float audio.
        if (scale <= 0.0)
            scale = 1.0;
        left.fill(0.0f);
        right.fill(0.0f);
        for (std::size_t n = 0; n < fftSize; ++n)
        {
            const auto frame = start + static_cast<std::int64_t>(n);
            if (frame < 0 || static_cast<std::size_t>(frame) >= input.frames())
                continue;
            const auto f = static_cast<std::size_t>(frame);
            left[n] =
                static_cast<float>(static_cast<double>(input.channels[0][f]) / scale) * window[n];
            if (result.stereo)
                right[n] = static_cast<float>(static_cast<double>(input.channels[1][f]) / scale) *
                           window[n];
        }
        fft.performRealOnlyForwardTransform(left.data(), true);
        if (result.stereo)
            fft.performRealOnlyForwardTransform(right.data(), true);
        const auto t = std::min(result.timeBins - 1, centre * result.timeBins / input.frames());
        ++counts[t];
        for (std::size_t b = 0; b < bands.size(); ++b)
        {
            double magnitudePeak = 0.0, sidePeak = 0.0;
            for (auto k = bands[b].first; k <= bands[b].second; ++k)
            {
                const double factor = (k == fftSize / 2 ? 1.0 : 2.0) * scale / windowSum;
                const double lr = left[2 * k], li = left[2 * k + 1];
                double power = lr * lr + li * li;
                if (result.stereo)
                {
                    const double rr = right[2 * k], ri = right[2 * k + 1];
                    power = (power + rr * rr + ri * ri) * 0.5;
                    const double sr = (lr - rr) * 0.5, si = (li - ri) * 0.5;
                    sidePeak = std::max(sidePeak, (sr * sr + si * si) * factor * factor);
                }
                magnitudePeak = std::max(magnitudePeak, power * factor * factor);
            }
            const auto index = t * result.freqBins + b;
            magnitudePower[index] += magnitudePeak;
            sidePower[index] += sidePeak;
        }
    }
    for (std::size_t t = 0; t < result.timeBins; ++t)
    {
        detail::checkCancelled(cancelled);
        for (std::size_t b = 0; b < result.freqBins; ++b)
        {
            const auto index = t * result.freqBins + b;
            if (counts[t] != 0)
            {
                result.magnitudeDb[index] =
                    detail::decibels(magnitudePower[index] / static_cast<double>(counts[t]));
                result.sideDb[index] =
                    detail::decibels(sidePower[index] / static_cast<double>(counts[t]));
            }
        }
    }
    return result;
}

// Performs full forward/inverse STFT even for gain 1, with periodic Hann
// analysis/synthesis windows, 75% overlap, and zero padding at both boundaries.
// Shared real-valued gains preserve inter-channel phase relationships. There is
// no delay, resampling, channel conversion, or mutation of the source. The
// selected band is tapered in frequency; the reconstructed result is blended
// through a tapered time mask. Samples outside the selection are bit-identical.
// Narrow bands below FFT resolution may have little/no effect. This is a
// user-directed attenuation tool, not automatic artifact recognition.
[[nodiscard]] inline Audio attenuate(const Audio &input, const Selection &selection,
                                     const std::atomic<bool> *cancelled = nullptr)
{
    detail::checkCancelled(cancelled);
    validate(input, cancelled);
    detail::validateSelection(input, selection);
    detail::checkCancelled(cancelled);
    Audio result;
    result.sampleRate = input.sampleRate;
    result.channels.resize(input.channels.size());
    for (auto& channel : result.channels)
    {
        detail::checkCancelled(cancelled);
        channel.resize(input.frames(), 0.0f);
    }
    juce::dsp::FFT fft(fftOrder);
    const auto window = detail::makeWindow();
    std::array<double, hopSize> normalization{};
    for (std::size_t n = 0; n < fftSize; ++n)
        normalization[n % hopSize] += static_cast<double>(window[n]) * window[n];
    std::array<float, fftSize / 2 + 1> gains{};
    for (std::size_t k = 0; k < gains.size(); ++k)
    {
        const double weight = detail::frequencyWeight(
            static_cast<double>(k) * input.sampleRate / static_cast<double>(fftSize), selection,
            input.sampleRate / static_cast<double>(fftSize), input.sampleRate * 0.5);
        gains[k] = static_cast<float>(1.0 - (1.0 - static_cast<double>(selection.gain)) * weight);
    }
    std::array<float, fftSize * 2> block{};
    std::array<double, fftSize> accumulator{};
    for (std::size_t channel = 0; channel < input.channels.size(); ++channel)
    {
        detail::checkCancelled(cancelled);
        accumulator.fill(0.0);
        for (std::int64_t start = -static_cast<std::int64_t>(fftSize - hopSize);
             start < static_cast<std::int64_t>(input.frames());
             start += static_cast<std::int64_t>(hopSize))
        {
            detail::checkCancelled(cancelled);
            block.fill(0.0f);
            double scale = 0.0;
            for (std::size_t n = 0; n < fftSize; ++n)
            {
                const auto frame = start + static_cast<std::int64_t>(n);
                if (frame >= 0 && static_cast<std::size_t>(frame) < input.frames())
                    scale = std::max(
                        scale, std::abs(static_cast<double>(
                                   input.channels[channel][static_cast<std::size_t>(frame)])));
            }
            if (scale <= 0.0)
                scale = 1.0;
            for (std::size_t n = 0; n < fftSize; ++n)
            {
                const auto frame = start + static_cast<std::int64_t>(n);
                if (frame >= 0 && static_cast<std::size_t>(frame) < input.frames())
                    block[n] = static_cast<float>(
                                   static_cast<double>(
                                       input.channels[channel][static_cast<std::size_t>(frame)]) /
                                   scale) *
                               window[n];
            }
            fft.performRealOnlyForwardTransform(block.data(), true);
            for (std::size_t k = 0; k < gains.size(); ++k)
            {
                block[k * 2] *= gains[k];
                block[k * 2 + 1] *= gains[k];
            }
            fft.performRealOnlyInverseTransform(block.data());
            for (std::size_t n = 0; n < fftSize; ++n)
            {
                const auto frame = start + static_cast<std::int64_t>(n);
                if (frame >= 0 && static_cast<std::size_t>(frame) < input.frames())
                    accumulator[static_cast<std::size_t>(frame) % fftSize] +=
                        static_cast<double>(block[n]) * scale * window[n];
            }
            // All windows that contribute to this hop have now been processed.
            const auto first = static_cast<std::size_t>(std::max<std::int64_t>(0, start));
            const auto last = static_cast<std::size_t>(
                std::max<std::int64_t>(0, std::min(static_cast<std::int64_t>(input.frames()),
                                                   start + static_cast<std::int64_t>(hopSize))));
            for (auto frame = first; frame < last; ++frame)
            {
                const double processed =
                    accumulator[frame % fftSize] / normalization[frame % hopSize];
                accumulator[frame % fftSize] = 0.0;
                const double weight = detail::timeWeight(frame, input.frames(), selection);
                if (weight <= 0.0)
                {
                    result.channels[channel][frame] = input.channels[channel][frame];
                }
                else
                {
                    const double original = input.channels[channel][frame];
                    result.channels[channel][frame] =
                        audioDetail::checkedFloat(original + weight * (processed - original));
                }
            }
        }
    }
    return result;
}

} // namespace hem::spectral
