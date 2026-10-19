#pragma once
// Transient Shaper (2026-10 faceplate). Zero latency.
// Detector: focus-range band filter (Low 40-250 Hz / Mid 250 Hz-4 kHz / High 4-16 kHz)
// -> fast/slow envelopes whose times follow Detection Mode (Transient / Sustain /
// Adaptive by crest factor) -> signed contrast scaled by Sensitivity.
// Gain: Attack dB on onsets (held for Transient Length), Sustain dB on decays,
// shaped by Process Mode. Applied on a 3-band LR4 split (200 Hz / 4 kHz) with
// Low Focus / High Focus scaling the dB per band. Clip Guard is a soft-knee
// clipper below -0.1 dBFS after Mix and Output. Solo = detector band, Delta = shaped minus dry.
#include "StudioDsp.h"

namespace hem::studio {

struct TransientShaperSettings {
    float attack = 0, sustain = 0, sensitivity = 0, length = 0, lowFocus = 0, highFocus = 0, clipGuard = 0, mix = 100, output = 0;
    int detection = 0;   // 0 transient, 1 sustain, 2 adaptive
    int focus = 1;       // 0 low, 1 mid, 2 high
    int processMode = 0; // 0 clean, 1 punch, 2 smooth, 3 aggressive
    bool solo = false, delta = false, bypass = false;
};

class TransientShaperCore {
public:
    static constexpr float maxDb = 18.f;
    bool prepare(double sampleRate) noexcept {
        ready = false;
        if (!rateValid(sampleRate)) return false;
        sr = sampleRate;
        lowSplit.design(sr, 200); highSplit.design(sr, 4000); lowAlign.design(sr, 4000);
        reset();
        ready = true;
        return true;
    }
    void reset() noexcept {
        lowSplit.reset(); highSplit.reset(); lowAlign.reset(); detectHp.reset(); detectLp.reset();
        fast.reset(); slow.reset(); peakEnv.reset(); rmsEnv.reset(); attackHold = 0; smoothDb = 0; lastFocus = -1; lastDetection = -1;
        mixRamp.reset(sr, 1); outRamp.reset(sr, 1); bypassRamp.reset(sr, 1); soloRamp.reset(sr, 0); deltaRamp.reset(sr, 0);
        attackRamp.reset(sr, 0); sustainRamp.reset(sr, 0); lowW.reset(sr, 1); highW.reset(sr, 1); guardRamp.reset(sr, 0); splitRamp.reset(sr, 0);
        peakEnv.setTimes(sr, .1f, 50); rmsEnv.setTimes(sr, 10, 200);
        transientLevel.store(-120); sustainLevel.store(-120); outputLevel.store(-120); gainDb.store(0);
    }

    void process(float* l, float* r, int n, const TransientShaperSettings& p, ScopeTap* tap = nullptr) noexcept {
        if (!l || !r || n <= 0) return;
        if (!ready) { std::fill_n(l, n, 0.f); std::fill_n(r, n, 0.f); return; }
        configure(p);
        float tPeak = 0, sPeak = 0, oPeak = 0, maxGain = 0;
        const int mode = std::clamp(p.processMode, 0, 3);
        static constexpr std::array<float, 4> shapeExponent{1.f, .7f, 1.f, .5f}, amountScale{1.f, 1.25f, .8f, 1.5f};
        const float holdCoefficient = p.length > 0 ? timeCoefficient(std::clamp(p.length, 0.f, 500.f), sr) : 0.f;
        const float smoothCoefficient = mode == 2 ? timeCoefficient(5, sr) : 0.f;
        const float k = std::pow(2.f, std::clamp(p.sensitivity, -100.f, 100.f) / 50.f);
        const int detection = std::clamp(p.detection, 0, 2);
        for (int i = 0; i < n; ++i) {
            const float dl = finite(l[i]), dr = finite(r[i]);
            // Detector band.
            const float mono = .5f * (dl + dr);
            const float band = detectLp.tick(detectHp.tick(mono, 0), 0);
            const float x = std::abs(band);
            if (detection == 2) {
                // Adaptive: crest factor between ~2 (steady) and ~8 (percussive) morphs times.
                const float crest = peakEnv.process(x) / std::max(rmsEnv.process(x), 1.e-6f);
                const float t = std::clamp((crest - 2.f) / 6.f, 0.f, 1.f);
                if ((i & 31) == 0) setDetectorTimes(t);
            }
            const float f = fast.process(x), s = slow.process(x);
            const float contrast = std::clamp((f - s) / std::max(std::max(f, s), 1.e-7f) * k, -1.f, 1.f);
            float onset = std::max(0.f, contrast), decay = std::min(1.f, 3.f * std::max(0.f, -contrast)); // decays are shallow contrasts
            onset = std::pow(onset, shapeExponent[size_t(mode)]);
            float aDb = attackRamp.next() * maxDb * amountScale[size_t(mode)] * onset;
            const float sDb = sustainRamp.next() * maxDb * amountScale[size_t(mode)] * decay;
            if (holdCoefficient > 0) { // Transient Length: hold the onset gain, release over `length`.
                if (std::abs(aDb) >= std::abs(attackHold)) attackHold = aDb; else attackHold = aDb + (attackHold - aDb) * holdCoefficient;
                aDb = attackHold;
            }
            float totalDb = std::clamp(aDb + sDb, -24.f, 24.f);
            if (smoothCoefficient > 0) { smoothDb = totalDb + (smoothDb - totalDb) * smoothCoefficient; totalDb = smoothDb; }
            const float wl = lowW.next(), wh = highW.next();
            const float gMid = dbToGain(totalDb), gLow = dbToGain(totalDb * wl), gHigh = dbToGain(totalDb * wh);
            const float split = splitRamp.next();
            const float guard = guardRamp.next(), mix = mixRamp.next(), solo = soloRamp.next(), delta = deltaRamp.next(), out = outRamp.next(), active = bypassRamp.next();
            const float in[2]{dl, dr};
            float y[2];
            for (int c = 0; c < 2; ++c) {
                float low, rest, mid, high;
                lowSplit.split(in[c], c, low, rest);
                highSplit.split(rest, c, mid, high);
                low = lowAlign.allpass(low, c);
                // Split path: allpass-aligned dry and per-band gain. Broadband path: exact dry.
                const float reference = in[c] + split * (low + mid + high - in[c]);
                float shaped = in[c] * gMid + split * (low * gLow + mid * gMid + high * gHigh - in[c] * gMid);
                if (mode == 1) shaped += .15f * onset * (std::tanh(2.f * shaped) * .5f - shaped); // punch: harmonics on onsets
                float wet = reference + mix * (shaped - reference);
                wet += delta * ((shaped - reference) * mix - wet);
                wet += solo * (band - wet);
                y[c] = wet * out;
                if (guard > 0) y[c] = softClip(y[c], guard); // last stage: protects the output
                y[c] = in[c] + active * (y[c] - in[c]);
            }
            l[i] = finite(y[0]); r[i] = finite(y[1]);
            const float level = std::max(std::abs(dl), std::abs(dr));
            if (onset > .2f) tPeak = std::max(tPeak, level); else if (decay > .2f) sPeak = std::max(sPeak, level);
            oPeak = std::max({oPeak, std::abs(l[i]), std::abs(r[i])});
            if (std::abs(totalDb) > std::abs(maxGain)) maxGain = totalDb;
            if (tap) { const float frame[ScopeTap::channels]{dl, dr, l[i], r[i], onset, decay}; tap->push(frame); }
        }
        transientLevel.store(gainToDb(tPeak), std::memory_order_relaxed);
        sustainLevel.store(gainToDb(sPeak), std::memory_order_relaxed);
        outputLevel.store(gainToDb(oPeak), std::memory_order_relaxed);
        gainDb.store(maxGain, std::memory_order_relaxed);
    }
    std::atomic<float> transientLevel{-120}, sustainLevel{-120}, outputLevel{-120}, gainDb{0};

    // Soft knee from (ceiling - 12 dB * guard) up to a -0.1 dBFS ceiling.
    static float softClip(float x, float guard) noexcept {
        constexpr float ceiling = .98855f;
        const float knee = ceiling * dbToGain(-12.f * guard);
        const float a = std::abs(x);
        if (a <= knee) return x;
        const float room = ceiling - knee;
        const float y = knee + room * std::tanh((a - knee) / std::max(room, 1.e-6f));
        return x < 0 ? -y : y;
    }

private:
    void setDetectorTimes(float percussive) noexcept {
        // percussive 1 = transient times, 0 = sustain times.
        const float fa = .5f + 4.5f * (1 - percussive), fr = 20 + 40 * (1 - percussive), sa = 20 + 40 * (1 - percussive), srl = 150 + 250 * (1 - percussive);
        fast.setTimes(sr, fa, fr); slow.setTimes(sr, sa, srl);
    }
    void configure(const TransientShaperSettings& p) noexcept {
        attackRamp.set(std::clamp(p.attack, -100.f, 100.f) * .01f);
        sustainRamp.set(std::clamp(p.sustain, -100.f, 100.f) * .01f);
        lowW.set(1 + std::clamp(p.lowFocus, -100.f, 100.f) * .01f);
        highW.set(1 + std::clamp(p.highFocus, -100.f, 100.f) * .01f);
        // The 3-band split is only used when Low/High Focus differ from the middle;
        // otherwise the gain is broadband and the dry path stays phase-exact.
        splitRamp.set(std::abs(p.lowFocus) > .05f || std::abs(p.highFocus) > .05f ? 1.f : 0.f);
        guardRamp.set(std::clamp(p.clipGuard, 0.f, 100.f) * .01f);
        mixRamp.set(std::clamp(p.mix, 0.f, 100.f) * .01f);
        outRamp.set(dbToGain(std::clamp(p.output, -24.f, 24.f)));
        soloRamp.set(p.solo ? 1.f : 0.f); deltaRamp.set(p.delta ? 1.f : 0.f); bypassRamp.set(p.bypass ? 0.f : 1.f);
        const int detection = std::clamp(p.detection, 0, 2);
        if (detection != lastDetection) { lastDetection = detection; if (detection == 0) setDetectorTimes(1); else if (detection == 1) setDetectorTimes(0); else setDetectorTimes(.5f); }
        const int focus = std::clamp(p.focus, 0, 2);
        if (focus != lastFocus) {
            lastFocus = focus;
            static constexpr std::array<float, 3> lo{40, 250, 4000}, hi{250, 4000, 16000};
            detectHp.design(Biquad::highPass, sr, lo[size_t(focus)], .70710678);
            detectLp.design(Biquad::lowPass, sr, std::min<double>(hi[size_t(focus)], sr * .45), .70710678);
        }
    }
    double sr = 48000; bool ready = false; int lastDetection = -1, lastFocus = -1;
    LinkwitzRiley lowSplit, highSplit, lowAlign;
    Biquad detectHp, detectLp;
    Follower fast, slow, peakEnv, rmsEnv;
    float attackHold = 0, smoothDb = 0;
    Smoothed mixRamp, outRamp, bypassRamp, soloRamp, deltaRamp, attackRamp, sustainRamp, lowW, highW, guardRamp, splitRamp;
};

} // namespace hem::studio
