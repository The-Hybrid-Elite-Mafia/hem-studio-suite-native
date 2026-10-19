#pragma once
// Breath / Mouth Control (2026-10 faceplate). Heuristic detectors, not a trained
// classifier: they can also react to hiss, fricatives or percussive clicks, so
// listen with LISTEN > REDUCTION while tuning.
//
// Detect: on the mono sum, inside the Frequency Focus region (Low 120 Hz-1.5 kHz,
//   Mid 800 Hz-6 kHz, High 3.5-16 kHz):
//   breath    = spectral flatness of 6 focus bands x level 12-50 dB below the vocal
//   mouth     = impulsive spikes above 5.5 kHz (1 ms envelope vs 10 ms average)
//   sibilance = share of focus-band energy at 4.5-10 kHz relative to the whole signal
//   Sensitivity bends the score curve; Mode sets detector speed.
// Filter: the reduction is a dynamic bell cut at Frequency / Q (or broadband if the
//   FILTER stage is off). Dynamic: reduction = min(Reduction, band level - Threshold)
//   x score (full Reduction x score if the DYNAMIC stage is off), smoothed by
//   Attack / Release. 2 ms lookahead (latency) so clicks are caught from their start.
// Output: Mix and Output gain (ignored when the OUTPUT stage is off).
#include "StudioDsp.h"

namespace hem::studio {

struct BreathMouthStudioSettings {
    int detection = 0;  // 0 breath, 1 mouth noise, 2 sibilance, 3 all
    int focus = 1;      // 0 low, 1 mid, 2 high
    int mode = 0;       // 0 vocal, 1 speech, 2 rap
    float sensitivity = 50, threshold = -24, attack = 5, release = 250, reduction = 12, frequencyHz = 3500, q = 1.2f, mix = 100, output = 0;
    bool stageDetect = true, stageFilter = true, stageDynamic = true, stageOutput = true;
    bool listenReduction = false, solo = false, bypass = false;
};

class BreathMouthStudioCore {
public:
    static constexpr int focusBands = 6;
    bool prepare(double sampleRate) {
        ready = false;
        if (!rateValid(sampleRate)) return false;
        sr = sampleRate;
        lookahead = std::max(1, int(std::lround(sr * .002)));
        for (auto& line : delayLine) line.assign(size_t(lookahead) + 1, 0.f);
        reset();
        ready = true;
        return true;
    }
    int latency() const noexcept { return ready ? lookahead : 0; }
    void reset() noexcept {
        for (auto& line : delayLine) std::fill(line.begin(), line.end(), 0.f);
        writeIndex = 0; phase = 0;
        for (auto& b : bandFilters) b.reset();
        bell.reset(); for (auto& f : clickHp) f.reset(); sibilanceBand.reset(); bandLevelFilter.reset();
        bandEnergy = {}; total = sibilantEnergy = 0; vocalPeak.reset(); clickFast.reset(); clickSlow.reset(); bandLevel.reset();
        score = breathScore = mouthScore = sibilanceScore = 0; reductionEnv = 0; lastFocus = -1; lastFreq = lastQ = -1; lastMode = -1;
        mixRamp.reset(sr, 1); outRamp.reset(sr, 1); bypassRamp.reset(sr, 1); listenRamp.reset(sr, 0); soloRamp.reset(sr, 0); filterRamp.reset(sr, 1);
        sibilanceBand.design(Biquad::bandPass, sr, std::min(7000.0, sr * .4), .9);
        for (auto& f : clickHp) f.design(Biquad::highPass, sr, std::min(5500.0, sr * .4), .70710678);
        appliedDb.store(0); detection.store(0); events.store(0); eventCount = 0; eventLatch = false;
    }

    void process(float* l, float* r, int n, const BreathMouthStudioSettings& p, ScopeTap* tap = nullptr) noexcept {
        if (!l || !r || n <= 0) return;
        if (!ready) { std::fill_n(l, n, 0.f); std::fill_n(r, n, 0.f); return; }
        configure(p);
        const float exponent = std::pow(2.f, (50.f - std::clamp(p.sensitivity, 0.f, 100.f)) / 50.f); // 100 -> sqrt, 0 -> square
        const float attackC = timeCoefficient(std::clamp(p.attack, .1f, 100.f), sr), releaseC = timeCoefficient(std::clamp(p.release, 10.f, 2000.f), sr);
        const float reductionMax = std::clamp(p.reduction, 0.f, 30.f), threshold = std::clamp(p.threshold, -60.f, 0.f);
        const int which = std::clamp(p.detection, 0, 3);
        float maxApplied = 0, maxScore = 0;
        for (int i = 0; i < n; ++i) {
            const float xl = finite(l[i]), xr = finite(r[i]), mono = .5f * (xl + xr);
            // --- Detect (undelayed signal = lookahead) ---
            const float power = mono * mono;
            total = energyDecay * total + (1 - energyDecay) * power;
            for (int b = 0; b < focusBands; ++b) { const float v = bandFilters[size_t(b)].tick(mono, 0); bandEnergy[size_t(b)] = energyDecay * bandEnergy[size_t(b)] + (1 - energyDecay) * v * v; }
            const float sib = sibilanceBand.tick(mono, 0);
            sibilantEnergy = energyDecay * sibilantEnergy + (1 - energyDecay) * sib * sib;
            // Clicks are broadband impulses; voiced harmonics are weak above 5.5 kHz.
            const float click = std::abs(clickHp[1].tick(clickHp[0].tick(mono, 0), 0));
            const float cf = clickFast.process(click), cs = clickSlow.process(click);
            const float peak = vocalPeak.process(std::abs(mono));
            if ((phase++ & 15) == 0) updateScores(peak, cf, cs);
            float s = which == 0 ? breathScore : which == 1 ? mouthScore : which == 2 ? sibilanceScore : std::max({breathScore, mouthScore, sibilanceScore});
            s = std::pow(std::clamp(s, 0.f, 1.f), exponent);
            if (!p.stageDetect) s = 1;
            score = s;
            // --- Dynamic ---
            const float bandSignal = bandLevelFilter.tick(mono, 0);
            const float levelDb = gainToDb(std::sqrt(bandLevel.process(bandSignal * bandSignal)) * 1.41421356f);
            const float wanted = (p.stageDynamic ? std::min(reductionMax, std::max(0.f, levelDb - threshold)) : reductionMax) * s;
            reductionEnv = wanted + (reductionEnv - wanted) * (wanted > reductionEnv ? attackC : releaseC);
            const float g = dbToGain(-reductionEnv);
            if (s > .5f && !eventLatch) { eventLatch = true; ++eventCount; } else if (s < .25f) eventLatch = false;
            // --- Filter on the delayed audio ---
            const size_t read = (writeIndex + delayLine[0].size() - size_t(lookahead)) % delayLine[0].size();
            delayLine[0][writeIndex] = xl; delayLine[1][writeIndex] = xr;
            const float dl = delayLine[0][read], dr = delayLine[1][read];
            writeIndex = (writeIndex + 1) % delayLine[0].size();
            const float filterOn = filterRamp.next(), listen = listenRamp.next(), solo = soloRamp.next(), mix = mixRamp.next(), out = outRamp.next(), active = bypassRamp.next();
            const float in[2]{dl, dr};
            float y[2];
            for (int c = 0; c < 2; ++c) {
                const float bandPart = bell.tick(in[c], c);
                const float bellOut = in[c] - (1 - g) * bandPart;
                const float processed = bellOut + (1 - filterOn) * (in[c] * g - bellOut);
                float wet = processed;
                wet += listen * ((in[c] - processed) - wet);
                wet += solo * (bandPart - wet);
                float o = in[c] + mix * (wet - in[c]);
                o *= out;
                y[c] = in[c] + active * (o - in[c]);
            }
            l[i] = finite(y[0]); r[i] = finite(y[1]);
            maxApplied = std::max(maxApplied, reductionEnv); maxScore = std::max(maxScore, s);
            if (tap) { const float f[ScopeTap::channels]{dl, dr, l[i], r[i], -reductionEnv * active, s}; tap->push(f); }
        }
        appliedDb.store(maxApplied, std::memory_order_relaxed);
        detection.store(maxScore, std::memory_order_relaxed);
        events.store(eventCount, std::memory_order_relaxed);
    }
    // Focus regions and the processed band (for the display).
    static std::pair<float, float> focusRegion(int focus) noexcept {
        static constexpr std::array<std::pair<float, float>, 3> regions{{{120.f, 1500.f}, {800.f, 6000.f}, {3500.f, 16000.f}}};
        return regions[size_t(std::clamp(focus, 0, 2))];
    }
    std::atomic<float> appliedDb{0}, detection{0};
    std::atomic<uint32_t> events{0};

private:
    void updateScores(float peak, float clickFastEnv, float clickSlowEnv) noexcept {
        double arithmetic = 0, logs = 0;
        for (int b = 0; b < focusBands; ++b) { const double v = std::max(1.e-20, double(bandEnergy[size_t(b)]) / bandWeight[size_t(b)]); arithmetic += v; logs += std::log(v); }
        arithmetic /= focusBands;
        const double flatness = std::exp(logs / focusBands) / std::max(1.e-20, arithmetic);
        const float levelDb = gainToDb(std::sqrt(float(total)) * 1.41421356f), peakDb = gainToDb(peak);
        const float below = peakDb - levelDb;
        // Breath: noise-like and 12-50 dB below the recent vocal peak.
        const float flat = float(std::clamp((flatness - .45) / .4, 0.0, 1.0));
        breathScore = flat * std::clamp((below - 12.f) / 6.f, 0.f, 1.f) * std::clamp((50.f - below) / 6.f, 0.f, 1.f) * (levelDb > -70 ? 1.f : 0.f);
        // Mouth noise: short spikes well above the 10 ms average.
        const float ratio = clickFastEnv / std::max(clickSlowEnv, 1.e-7f);
        mouthScore = std::clamp((ratio - 3.f) / 3.f, 0.f, 1.f) * (gainToDb(clickFastEnv) > -66 ? 1.f : 0.f);
        // Sibilance: share of energy in the 4.5-10 kHz band.
        const float share = float(sibilantEnergy / std::max(1.e-20, total));
        sibilanceScore = std::clamp((share - .25f) / .35f, 0.f, 1.f) * (levelDb > -60 ? 1.f : 0.f);
    }
    void configure(const BreathMouthStudioSettings& p) noexcept {
        mixRamp.set(p.stageOutput ? std::clamp(p.mix, 0.f, 100.f) * .01f : 1.f);
        outRamp.set(p.stageOutput ? dbToGain(std::clamp(p.output, -24.f, 24.f)) : 1.f);
        bypassRamp.set(p.bypass ? 0.f : 1.f);
        listenRamp.set(p.listenReduction ? 1.f : 0.f);
        soloRamp.set(p.solo ? 1.f : 0.f);
        filterRamp.set(p.stageFilter ? 1.f : 0.f);
        const int focus = std::clamp(p.focus, 0, 2);
        if (focus != lastFocus) {
            lastFocus = focus;
            const auto [lo, hi] = focusRegion(focus);
            const float top = std::min(hi, float(sr * .45));
            for (int b = 0; b < focusBands; ++b) {
                const float hz = lo * std::pow(top / lo, float(b) / float(focusBands - 1));
                bandFilters[size_t(b)].design(Biquad::bandPass, sr, hz, 2.0);
                bandWeight[size_t(b)] = hz; // pink-noise normalisation for flatness
            }
        }
        const float hz = std::clamp(p.frequencyHz, 20.f, float(sr * .45)), q = std::clamp(p.q, .1f, 10.f);
        if (hz != lastFreq || q != lastQ) { lastFreq = hz; lastQ = q; bell.design(Biquad::bandPass, sr, hz, q); bandLevelFilter.design(Biquad::bandPass, sr, hz, q); }
        const int mode = std::clamp(p.mode, 0, 2);
        if (mode != lastMode) {
            lastMode = mode;
            static constexpr std::array<float, 3> energyMs{10.f, 6.f, 4.f}, peakRelease{1500.f, 900.f, 600.f};
            energyDecay = timeCoefficient(energyMs[size_t(mode)], sr);
            vocalPeak.setTimes(sr, .5f, peakRelease[size_t(mode)]);
            clickFast.setTimes(sr, .05f, 1.f); clickSlow.setTimes(sr, 10.f, 10.f);
            bandLevel.setTimes(sr, 1.f, 30.f);
        }
    }
    double sr = 48000; bool ready = false, eventLatch = false; int lookahead = 1, lastFocus = -1, lastMode = -1; unsigned phase = 0; uint32_t eventCount = 0;
    std::array<std::vector<float>, 2> delayLine; size_t writeIndex = 0;
    std::array<Biquad, focusBands> bandFilters; std::array<float, focusBands> bandEnergy{}, bandWeight{};
    Biquad bell, sibilanceBand, bandLevelFilter; std::array<Biquad, 2> clickHp;
    double total = 0, sibilantEnergy = 0; float energyDecay = 0, lastFreq = -1, lastQ = -1;
    Follower vocalPeak, clickFast, clickSlow, bandLevel;
    float score = 0, breathScore = 0, mouthScore = 0, sibilanceScore = 0, reductionEnv = 0;
    Smoothed mixRamp, outRamp, bypassRamp, listenRamp, soloRamp, filterRamp;
};

} // namespace hem::studio
