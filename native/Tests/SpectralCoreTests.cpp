// SPDX-License-Identifier: AGPL-3.0-or-later
// Adapted from HEM Stem Workbench tests/SpectralTests.cpp.
// See ../Source/Spectral/ATTRIBUTION.md and LICENSE-AGPL-3.0.txt.
// Prepared source only on 2026-10-16; not compiled or executed for MAIN.
#include "../Source/Spectral/SpectralCore.h"
#include <bit>
#include <string>

#include <chrono>
#include <iostream>
#include <random>
#include <thread>
#include <utility>

namespace
{
std::size_t checks = 0;
void check(const bool condition, const char *message)
{
    ++checks;
    if (!condition)
        throw std::runtime_error(message);
}
void near(const double actual, const double expected, const double tolerance, const char *message)
{
    check(std::isfinite(actual) && std::abs(actual - expected) <= tolerance, message);
}
template <class Function> void rejects(Function &&function, const char *message)
{
    bool threw = false;
    try
    {
        std::forward<Function>(function)();
    }
    catch (const std::exception &)
    {
        threw = true;
    }
    check(threw, message);
}
hem::spectral::Audio makeAudio(const std::size_t frames, const std::size_t channels = 1,
                     const double rate = 96000.0)
{
    return {rate, std::vector<std::vector<float>>(channels, std::vector<float>(frames, 0.0f))};
}
void addTone(hem::spectral::Audio &audio, const double hz, const double amplitude,
             const std::size_t channel = 0, const double phase = 0.0)
{
    for (std::size_t i = 0; i < audio.frames(); ++i)
        audio.channels[channel][i] +=
            static_cast<float>(amplitude * std::sin(2.0 * std::numbers::pi * hz *
                                                        static_cast<double>(i) / audio.sampleRate +
                                                    phase));
}
double toneAmplitude(const hem::spectral::Audio &audio, const double hz, const std::size_t start,
                     const std::size_t end, const std::size_t channel = 0)
{
    double real = 0.0, imaginary = 0.0;
    for (auto frame = start; frame < end; ++frame)
    {
        const double angle =
            2.0 * std::numbers::pi * hz * static_cast<double>(frame) / audio.sampleRate;
        real += audio.channels[channel][frame] * std::cos(angle);
        imaginary += audio.channels[channel][frame] * std::sin(angle);
    }
    return 2.0 * std::hypot(real, imaginary) / static_cast<double>(end - start);
}
std::size_t bandFor(const hem::spectral::Grid &grid, const float frequency)
{
    for (std::size_t b = 0; b < grid.freqBins; ++b)
        if (frequency >= grid.frequencyEdgesHz[b] && frequency <= grid.frequencyEdgesHz[b + 1])
            return b;
    throw std::runtime_error("Test tone not inside displayed frequency bounds.");
}

void analysis()
{
    const auto empty = hem::spectral::analyze(makeAudio(0, 2));
    check(empty.timeBins == 0 && empty.freqBins == 0 && empty.magnitudeDb.empty(),
          "Empty analysis should be empty.");
    auto signal = makeAudio(96000, 2);
    auto silence = hem::spectral::analyze(signal);
    check(silence.stereo && silence.timeBins <= 96 && silence.freqBins <= 64,
          "Analysis grid is not bounded.");
    check(silence.magnitudeDb.size() == silence.timeBins * silence.freqBins &&
              silence.sideDb.size() == silence.magnitudeDb.size() &&
              silence.correlations.size() == silence.timeBins,
          "Analysis grid shape mismatch.");
    check(std::all_of(silence.magnitudeDb.begin(), silence.magnitudeDb.end(),
                      [](const float v) { return v == hem::spectral::floorDb; }),
          "Silence magnitude is not the display floor.");
    check(std::all_of(silence.sideDb.begin(), silence.sideDb.end(),
                      [](const float v) { return v == hem::spectral::floorDb; }),
          "Silence side magnitude is not the display floor.");
    check(std::all_of(silence.correlations.begin(), silence.correlations.end(),
                      [](const float v) { return v == 0.0f; }),
          "Silence correlation should be undefined/zero.");
    check(silence.startFrames.front() == 0 && silence.endFrames.back() == signal.frames(),
          "Time intervals do not cover the source exactly.");
    for (std::size_t t = 0; t < silence.timeBins; ++t)
    {
        check(silence.startFrames[t] < silence.endFrames[t], "Empty display time interval.");
        if (t != 0)
            check(silence.startFrames[t] == silence.endFrames[t - 1],
                  "Non-contiguous display intervals.");
    }
    for (std::size_t b = 0; b < silence.freqBins; ++b)
    {
        check(silence.frequencyEdgesHz[b] < silence.frequencyEdgesHz[b + 1],
              "Frequency edges are not increasing.");
        check(silence.frequenciesHz[b] >= silence.frequencyEdgesHz[b] &&
                  silence.frequenciesHz[b] <= silence.frequencyEdgesHz[b + 1],
              "FFT band centre is outside its displayed interval.");
    }
    addTone(signal, 3000.0, 0.4, 0);
    addTone(signal, 3000.0, 0.4, 1);
    const auto sourceSnapshot = signal.channels;
    const auto inPhase = hem::spectral::analyze(signal);
    check(signal.channels == sourceSnapshot, "Analysis modified source.");
    const auto b = bandFor(inPhase, 3000.0f);
    const auto middle = inPhase.timeBins / 2;
    near(inPhase.magnitudeDb[middle * inPhase.freqBins + b], 20.0 * std::log10(0.4), 0.02,
         "Bin-centred sine level is not calibrated dBFS.");
    for (const float correlation : inPhase.correlations)
        near(correlation, 1.0, 1.0e-6, "Identical stereo should correlate +1.");
    for (const float side : inPhase.sideDb)
        near(side, hem::spectral::floorDb, 0.0, "Identical stereo should have zero side energy.");
    for (auto &value : signal.channels[1])
        value = -value;
    const auto antiPhase = hem::spectral::analyze(signal);
    for (const float correlation : antiPhase.correlations)
        near(correlation, -1.0, 1.0e-6, "Anti-phase stereo should correlate -1.");
    near(antiPhase.magnitudeDb[middle * antiPhase.freqBins + b],
         inPhase.magnitudeDb[middle * inPhase.freqBins + b], 1.0e-5,
         "Anti-phase channels should not cancel the magnitude display.");
    near(antiPhase.sideDb[middle * antiPhase.freqBins + b],
         antiPhase.magnitudeDb[middle * antiPhase.freqBins + b], 1.0e-5,
         "Anti-phase side energy should equal channel amplitude.");
    std::fill(signal.channels[1].begin(), signal.channels[1].end(), 0.0f);
    const auto oneSided = hem::spectral::analyze(signal);
    near(oneSided.magnitudeDb[middle * oneSided.freqBins + b],
         20.0 * std::log10(0.4 / std::sqrt(2.0)), 0.02,
         "Stereo magnitude should use average channel power.");
    near(oneSided.sideDb[middle * oneSided.freqBins + b], 20.0 * std::log10(0.2), 0.02,
         "L-only side energy should be half left amplitude.");
    for (const float correlation : oneSided.correlations)
        near(correlation, 0.0, 0.0,
             "Silent stereo channel should have undefined/zero correlation.");
    signal.channels.resize(1);
    const auto mono = hem::spectral::analyze(signal);
    check(!mono.stereo, "Mono incorrectly marked as stereo.");
    for (const float side : mono.sideDb)
        near(side, hem::spectral::floorDb, 0.0, "Mono must not invent stereo side information.");
    for (const double rate : {8000.0, 44100.0, 192000.0, 384000.0})
    {
        const auto grid = hem::spectral::analyze(makeAudio(4097, 1, rate));
        check(grid.freqBins > 0 && grid.freqBins <= 64,
              "Frequency bin budget changed across sample rates.");
        near(grid.frequencyEdgesHz.back(), rate * 0.5, 0.0, "Final frequency edge is not Nyquist.");
    }
}

void reconstruction()
{
    std::mt19937 generator(13);
    std::uniform_real_distribution<float> noise(-0.8f, 0.8f);
    for (const auto frames :
         {std::size_t{1}, std::size_t{17}, std::size_t{511}, std::size_t{512}, std::size_t{513},
          std::size_t{2047}, std::size_t{4097}, std::size_t{12000}})
    {
        auto signal = makeAudio(frames, 2);
        for (auto &channel : signal.channels)
            for (auto &value : channel)
                value = noise(generator);
        const auto snapshot = signal.channels;
        // gain=1 still traverses FFT, inverse FFT and overlap-add, including
        // all padded boundaries. It deliberately has no identity shortcut.
        const auto output = hem::spectral::attenuate(signal, {0, frames, 0.0f, 48000.0f, 1.0f});
        check(signal.channels == snapshot, "Attenuation mutated original audio.");
        check(output.frames() == frames && output.sampleRate == signal.sampleRate &&
                  output.channels.size() == signal.channels.size(),
              "Reconstruction changed shape or rate.");
        for (std::size_t c = 0; c < signal.channels.size(); ++c)
            for (std::size_t i = 0; i < frames; ++i)
                near(output.channels[c][i], signal.channels[c][i], 1.5e-6,
                     "Unity STFT reconstruction exceeded error tolerance.");
    }
    auto impulse = makeAudio(4097, 2);
    for (const auto position :
         {std::size_t{0}, std::size_t{1}, std::size_t{511}, std::size_t{2048}, std::size_t{4096}})
    {
        for (auto &channel : impulse.channels)
            std::fill(channel.begin(), channel.end(), 0.0f);
        impulse.channels[0][position] = 0.8f;
        impulse.channels[1][position] = -0.4f;
        const auto output =
            hem::spectral::attenuate(impulse, {0, impulse.frames(), 0.0f, 48000.0f, 1.0f});
        for (std::size_t c = 0; c < output.channels.size(); ++c)
        {
            const auto maximum = std::max_element(
                output.channels[c].begin(), output.channels[c].end(),
                [](const float a, const float b) { return std::abs(a) < std::abs(b); });
            check(static_cast<std::size_t>(std::distance(output.channels[c].begin(), maximum)) ==
                      position,
                  "Impulse alignment changed.");
            for (std::size_t frame = 0; frame < impulse.frames(); ++frame)
                near(output.channels[c][frame], impulse.channels[c][frame], 5.0e-7,
                     "Impulse reconstruction changed a sample.");
        }
    }
    const auto silent = makeAudio(4097, 2);
    const auto silenceOutput =
        hem::spectral::attenuate(silent, {200, 3000, 1000.0f, 6000.0f, 0.5f});
    check(silenceOutput.channels == silent.channels, "Attenuation introduced signal into silence.");
}

void attenuation()
{
    auto signal = makeAudio(96000, 2);
    addTone(signal, 3000.0, 0.4, 0);
    addTone(signal, 9000.0, 0.2, 0);
    for (std::size_t i = 0; i < signal.frames(); ++i)
        signal.channels[1][i] = -0.5f * signal.channels[0][i];
    const auto snapshot = signal.channels;
    const hem::spectral::Selection selection{24000, 72000, 2000.0f, 4000.0f, 0.5f};
    const auto output = hem::spectral::attenuate(signal, selection);
    check(signal.channels == snapshot, "Selected edit changed source.");
    near(toneAmplitude(output, 3000.0, 30000, 66000), 0.2, 0.0001,
         "Selected tone was not attenuated by six dB.");
    near(toneAmplitude(output, 9000.0, 30000, 66000), 0.2, 0.0001,
         "Tone outside selected frequency band changed.");
    for (std::size_t i = 0; i < output.frames(); ++i)
    {
        near(output.channels[1][i], -0.5 * output.channels[0][i], 1.0e-6,
             "Shared attenuation altered stereo relationship.");
        if (i < selection.startFrame || i >= selection.endFrame)
        {
            check(std::bit_cast<std::uint32_t>(output.channels[0][i]) == std::bit_cast<std::uint32_t>(signal.channels[0][i]) &&
                      std::bit_cast<std::uint32_t>(output.channels[1][i]) == std::bit_cast<std::uint32_t>(signal.channels[1][i]),
                  "Samples outside selected time range changed.");
        }
    }
    check(output.channels[0][selection.startFrame] == signal.channels[0][selection.startFrame] &&
              output.channels[0][selection.endFrame - 1] ==
                  signal.channels[0][selection.endFrame - 1],
          "Selected time shoulders are not tapered to unchanged endpoints.");
    const auto allBand =
        hem::spectral::attenuate(signal, {0, signal.frames(), 0.0f, 48000.0f, 0.5f});
    for (std::size_t c = 0; c < signal.channels.size(); ++c)
        for (std::size_t i = 0; i < signal.frames(); ++i)
            near(allBand.channels[c][i], 0.5 * signal.channels[c][i], 1.0e-6,
                 "All-band attenuation does not preserve waveform and phase.");
    const auto muted = hem::spectral::attenuate(signal, {0, signal.frames(), 0.0f, 48000.0f, 0.0f});
    for (const auto &channel : muted.channels)
        for (const auto sample : channel)
            near(sample, 0.0, 0.0, "Zero full-band gain should mute exactly.");
}

void monoAndDelta()
{
    for (const double rate : {44100.0, 48000.0, 96000.0, 192000.0})
    {
        auto signal = makeAudio(4097, 1, rate);
        addTone(signal, rate / 32.0, 0.35);
        signal.channels[0][0] = -0.0f;
        signal.channels[0].back() = -0.0f;
        const auto snapshot = signal.channels;
        const hem::spectral::Selection selection{512, 3585, float(rate / 64.0), float(rate / 16.0), 0.4f};
        const auto output = hem::spectral::attenuate(signal, selection);
        check(output.frames() == signal.frames() && output.channels.size() == 1 && output.sampleRate == rate,
              "Mono selected render changed source shape/rate.");
        check(signal.channels == snapshot, "Mono selected render changed original.");
        for (std::size_t frame = 0; frame < signal.frames(); ++frame)
        {
            const float original = signal.channels[0][frame], processed = output.channels[0][frame];
            const float delta = original - processed;
            near(double(processed) + delta, original, 1.0e-7,
                 "Processed plus original-minus-processed delta did not reconstruct original.");
            if (frame < selection.startFrame || frame >= selection.endFrame)
            {
                check(std::bit_cast<std::uint32_t>(processed) == std::bit_cast<std::uint32_t>(original),
                      "Outside-selection sample bits changed, including signed zero.");
                near(delta, 0.0, 0.0, "Delta is nonzero outside selected time range.");
            }
        }
    }
}

void cancellation()
{
    std::atomic<bool> cancel{true};
    const auto stopped = [&](auto &&operation)
    {
        bool correct = false;
        try
        {
            operation();
        }
        catch (const std::runtime_error &error)
        {
            correct = std::string(error.what()) == "Cancelled";
        }
        return correct;
    };
    // Cancellation wins even over input validation; closing a panel never
    // requires processing an already-cancelled job's audio first.
    check(stopped([&] { (void)hem::spectral::analyze(hem::spectral::Audio{}, &cancel); }),
          "Analysis did not honor pre-existing cancellation before validation.");
    check(stopped([&] { (void)hem::spectral::attenuate(hem::spectral::Audio{}, {}, &cancel); }),
          "Attenuation did not honor pre-existing cancellation before validation.");
    auto signal = makeAudio(96000 * 5, 2);
    addTone(signal, 3000, 0.25);
    const auto snapshot = signal.channels;
    for (const bool analyze : {true, false})
    {
        cancel.store(false, std::memory_order_relaxed);
        std::jthread canceller(
            [&]
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
                cancel.store(true, std::memory_order_relaxed);
            });
        const bool cancelled = stopped(
            [&]
            {
                if (analyze)
                    (void)hem::spectral::analyze(signal, &cancel);
                else
                    (void)hem::spectral::attenuate(
                        signal, {0, signal.frames(), 2000.0f, 4000.0f, 0.5f}, &cancel);
            });
        canceller.join();
        check(cancelled, "Running spectral work did not honor asynchronous cancellation.");
        check(signal.channels == snapshot, "Cancelled work changed the source.");
    }
    cancel.store(false, std::memory_order_relaxed);
    const auto small = makeAudio(23);
    check(hem::spectral::analyze(small, &cancel).timeBins == 1,
          "False cancellation flag stopped analysis.");
    check(hem::spectral::attenuate(small, {0, 23, 0.0f, 48000.0f, 1.0f}, &cancel).frames() == 23,
          "False cancellation flag stopped reconstruction.");
}

void validation()
{
    auto signal = makeAudio(3000);
    const auto call = [&signal](const hem::spectral::Selection &selection)
    { (void)hem::spectral::attenuate(signal, selection); };
    rejects([&] { call({0, 3001, 100.0f, 1000.0f, 0.5f}); },
            "Beyond-end frame selection accepted.");
    rejects([&] { call({3001, 4000, 100.0f, 1000.0f, 0.5f}); },
            "Outside frame selection accepted.");
    rejects([&] { call({20, 20, 100.0f, 1000.0f, 0.5f}); }, "Empty frame selection accepted.");
    rejects([&] { call({30, 20, 100.0f, 1000.0f, 0.5f}); }, "Reversed frame selection accepted.");
    rejects([&] { call({0, 100, -1.0f, 1000.0f, 0.5f}); }, "Negative frequency accepted.");
    rejects([&] { call({0, 100, 1000.0f, 1000.0f, 0.5f}); }, "Empty band accepted.");
    rejects([&] { call({0, 100, 1000.0f, 100.0f, 0.5f}); }, "Reversed band accepted.");
    rejects([&] { call({0, 100, 1000.0f, 48001.0f, 0.5f}); }, "Above-Nyquist band accepted.");
    rejects([&] { call({0, 100, 1000.0f, 4000.0f, -0.1f}); }, "Negative gain accepted.");
    rejects([&] { call({0, 100, 1000.0f, 4000.0f, 1.1f}); }, "Amplification accepted.");
    for (const float invalid :
         {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(),
          -std::numeric_limits<float>::infinity()})
    {
        rejects([&] { call({0, 100, invalid, 4000.0f, 0.5f}); }, "Nonfinite low edge accepted.");
        rejects([&] { call({0, 100, 1000.0f, invalid, 0.5f}); }, "Nonfinite high edge accepted.");
        rejects([&] { call({0, 100, 1000.0f, 4000.0f, invalid}); }, "Nonfinite gain accepted.");
        signal.channels[0][0] = invalid;
        rejects([&] { (void)hem::spectral::analyze(signal); },
                "Analysis accepted nonfinite sample.");
        rejects([&] { call({0, 100, 1000.0f, 4000.0f, 0.5f}); },
                "Attenuation accepted nonfinite sample.");
        signal.channels[0][0] = 0.0f;
    }
    signal.channels.push_back({});
    rejects([&] { (void)hem::spectral::analyze(signal); },
            "Analysis accepted unequal channel lengths.");
    rejects([&] { call({0, 100, 1000.0f, 4000.0f, 0.5f}); },
            "Attenuation accepted unequal channel lengths.");
    auto oversizedSamples = makeAudio(4097, 2);
    for (auto &channel : oversizedSamples.channels)
        for (auto &sample : channel)
            sample = std::numeric_limits<float>::max() * 0.25f;
    const auto extremeGrid = hem::spectral::analyze(oversizedSamples);
    for (const auto value : extremeGrid.magnitudeDb)
        check(std::isfinite(value), "Valid large float samples overflowed analysis.");
    const auto scaled = hem::spectral::attenuate(
        oversizedSamples, {0, oversizedSamples.frames(), 0.0f, 48000.0f, 0.5f});
    for (const auto &channel : scaled.channels)
        for (const auto sample : channel)
            near(static_cast<double>(sample) / std::numeric_limits<float>::max(), 0.125, 1.0e-6,
                 "Large finite float reconstruction overflowed.");
}
} // namespace

int main()
{
    try
    {
        analysis();
        reconstruction();
        attenuation();
        monoAndDelta();
        validation();
        cancellation();
        std::cout << "PASS: " << checks
                  << " spectral checks: calibrated bounded FFT grid, stereo side/correlation, "
                     "unity overlap-add, boundary impulses, selective attenuation, exact "
                     "outside-time preservation, "
                     "source immutability, silence, input validation and cancellation\n";
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << "FAIL after " << checks << " checks: " << error.what() << '\n';
        return 1;
    }
}
