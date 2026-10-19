#pragma once
// S.I.P.A. Stereo Image & Phase Architect (2026-10 faceplate).
// Zero latency, all IIR. Signal order:
//   phase control (normal / invert L / invert R / swap) -> phase rotate (Hilbert, R vs L)
//   -> M/S -> side: mono bass (LR4 HP), HPF/LPF side, center focus, air, harmonic width, depth
//   -> dynamic width x width -> pan law (center level) -> M/S balance -> stereo mode
//   -> output -> mono check -> output trim -> bypass crossfade.
#include "StudioDsp.h"

namespace hem::studio {

struct SipaSettings {
    float width = 100, panLaw = 0, centerFocus = 0, depth = 0, monoBass = 0, output = 0;
    int phaseMode = 0;  // 0 normal, 1 invert L, 2 invert R, 3 swap L/R
    float phaseRotate = 0;
    int stereoMode = 0; // 0 stereo, 1 mid/side (M on L, S on R), 2 side only, 3 mono
    float msBalance = 0, harmonicWidth = 0, air = 0, hpfSide = 20, lpfSide = 20000, dynamicWidth = 0, trim = 0;
    bool monoCheck = false, bypass = false;
};

class SipaArchitectCore {
public:
    bool prepare(double sampleRate) {
        ready = false;
        if (!rateValid(sampleRate)) return false;
        sr = sampleRate;
        depthLine.assign(size_t(std::ceil(sr * .03)) + 4, 0.f);
        depthDelay = int(std::lround(sr * .012));
        reset();
        ready = true;
        return true;
    }
    void reset() noexcept {
        for (auto* h : {&hilbertL, &hilbertR}) h->reset();
        bassSplit.reset(); hpf.reset(); lpf.reset(); focus.reset(); airShelf.reset(); harmonicHp.reset(); depthTone.reset();
        std::fill(depthLine.begin(), depthLine.end(), 0.f); depthWrite = 0;
        fast.reset(); slow.reset();
        fast.setTimes(sr, 1, 30); slow.setTimes(sr, 30, 300);
        widthRamp.reset(sr, 1); panRamp.reset(sr, 1); outRamp.reset(sr, 1); trimRamp.reset(sr, 1); midBal.reset(sr, 1); sideBal.reset(sr, 1);
        focusRamp.reset(sr, 0); depthRamp.reset(sr, 0); harmRamp.reset(sr, 0); dynRamp.reset(sr, 0); bassMix.reset(sr, 0);
        rotateMix.reset(sr, 0); rotCos.reset(sr, 1); rotSin.reset(sr, 0); monoRamp.reset(sr, 0); bypassRamp.reset(sr, 1);
        invL.reset(sr, 1); invR.reset(sr, 1); swapRamp.reset(sr, 0);
        for (auto& m : modeMix) m.reset(sr, 0);
        modeMix[0].reset(sr, 1);
        lastBass = lastHpf = lastLpf = lastAir = -1; designed = false;
        correlation.store(0); peak.store(0);
    }

    void process(float* l, float* r, int n, const SipaSettings& p, ScopeTap* tap = nullptr) noexcept {
        if (!l || !r || n <= 0) return;
        if (!ready) { std::fill_n(l, n, 0.f); std::fill_n(r, n, 0.f); return; }
        configure(p);
        double ll = 0, rr = 0, lr = 0; float maximum = 0;
        for (int i = 0; i < n; ++i) {
            const float dl = finite(l[i]), dr = finite(r[i]);
            // Phase control.
            const float il = invL.next(), ir = invR.next(), sw = swapRamp.next();
            float a = dl * il, b = dr * ir;
            const float sa = a + sw * (b - a), sb = b + sw * (a - b);
            a = sa; b = sb;
            // Phase rotate: R rotated by theta relative to L; both through matched allpasses.
            const float rm = rotateMix.next(), co = rotCos.next(), si = rotSin.next();
            float iL, qL, iR, qR;
            hilbertL.process(a, iL, qL); hilbertR.process(b, iR, qR);
            a += rm * (iL - a);
            b += rm * ((co * iR - si * qR) - b);
            float m = .5f * (a + b), s = .5f * (a - b);
            // Side shaping.
            float bassLow, bassHigh; bassSplit.split(s, 0, bassLow, bassHigh);
            s += bassMix.next() * (bassHigh - s);
            if (hpfOn) s = hpf.tick(s, 0);
            if (lpfOn) s = lpf.tick(s, 0);
            s -= focusRamp.next() * .9f * focus.tick(s, 0);
            s = airShelf.tick(s, 0);
            const float hm = harmonicHp.tick(m, 0);
            s += harmRamp.next() * 2.f * (std::tanh(6.f * hm) / 6.f - hm);
            depthLine[depthWrite] = m;
            const size_t readIndex = (depthWrite + depthLine.size() - size_t(depthDelay)) % depthLine.size();
            depthWrite = (depthWrite + 1) % depthLine.size();
            s += depthRamp.next() * .35f * depthTone.tick(depthLine[readIndex], 0);
            // Dynamic width: narrower on transients, wider on sustain.
            const float level = std::abs(m);
            const float f = fast.process(level), sl = slow.process(level);
            const float transient = std::clamp((f - sl) / std::max(f, 1.e-6f), 0.f, 1.f);
            const float d = dynRamp.next();
            const float widthMod = (1 - .6f * d * transient) * (1 + .3f * d * (1 - transient));
            s *= widthRamp.next() * widthMod;
            m *= panRamp.next();
            m *= midBal.next(); s *= sideBal.next();
            // Stereo mode (crossfaded).
            const float k0 = modeMix[0].next(), k1 = modeMix[1].next(), k2 = modeMix[2].next(), k3 = modeMix[3].next();
            float yl = k0 * (m + s) + k1 * m + k2 * s + k3 * m;
            float yr = k0 * (m - s) + k1 * s - k2 * s + k3 * m;
            const float out = outRamp.next();
            yl *= out; yr *= out;
            const float mono = monoRamp.next(), sum = .5f * (yl + yr);
            yl += mono * (sum - yl); yr += mono * (sum - yr);
            const float trim = trimRamp.next();
            yl *= trim; yr *= trim;
            const float active = bypassRamp.next();
            yl = dl + active * (yl - dl); yr = dr + active * (yr - dr);
            l[i] = finite(yl); r[i] = finite(yr);
            ll += double(l[i]) * l[i]; rr += double(r[i]) * r[i]; lr += double(l[i]) * r[i];
            maximum = std::max({maximum, std::abs(l[i]), std::abs(r[i])});
            if (tap) { const float frame[ScopeTap::channels]{dl, dr, l[i], r[i], transient, 0}; tap->push(frame); }
        }
        correlation.store(ll * rr > 1.e-20 ? float(lr / std::sqrt(ll * rr)) : 0.f, std::memory_order_relaxed);
        peak.store(maximum, std::memory_order_relaxed);
    }
    std::atomic<float> correlation{0}, peak{0};

private:
    void configure(const SipaSettings& p) noexcept {
        const int phase = std::clamp(p.phaseMode, 0, 3);
        invL.set(phase == 1 ? -1.f : 1.f); invR.set(phase == 2 ? -1.f : 1.f); swapRamp.set(phase == 3 ? 1.f : 0.f);
        const float theta = std::clamp(p.phaseRotate, -180.f, 180.f) * float(pi / 180);
        rotateMix.set(std::abs(p.phaseRotate) > .05f ? 1.f : 0.f); rotCos.set(std::cos(theta)); rotSin.set(std::sin(theta));
        widthRamp.set(std::clamp(p.width, 0.f, 200.f) * .01f);
        panRamp.set(dbToGain(std::clamp(p.panLaw, -6.f, 6.f)));
        focusRamp.set(std::clamp(p.centerFocus, 0.f, 100.f) * .01f);
        depthRamp.set(std::clamp(p.depth, 0.f, 100.f) * .01f);
        harmRamp.set(std::clamp(p.harmonicWidth, 0.f, 100.f) * .01f);
        dynRamp.set(std::clamp(p.dynamicWidth, 0.f, 100.f) * .01f);
        const float bal = std::clamp(p.msBalance, -100.f, 100.f) * .01f;
        midBal.set(bal > 0 ? 1 - bal : 1.f); sideBal.set(bal < 0 ? 1 + bal : 1.f);
        const int mode = std::clamp(p.stereoMode, 0, 3);
        for (int i = 0; i < 4; ++i) modeMix[size_t(i)].set(i == mode ? 1.f : 0.f);
        outRamp.set(dbToGain(std::clamp(p.output, -24.f, 24.f)));
        trimRamp.set(dbToGain(std::clamp(p.trim, -24.f, 24.f)));
        monoRamp.set(p.monoCheck ? 1.f : 0.f);
        bypassRamp.set(p.bypass ? 0.f : 1.f);
        const float bass = std::clamp(p.monoBass, 0.f, 200.f);
        bassMix.set(bass >= 20 ? 1.f : 0.f);
        if (bass != lastBass) { lastBass = bass; bassSplit.design(sr, std::max(20.f, bass)); }
        const float hp = std::clamp(p.hpfSide, 20.f, 1000.f), lp = std::clamp(p.lpfSide, 1000.f, 20000.f);
        hpfOn = hp > 20.5f; lpfOn = lp < 19999.f;
        if (hp != lastHpf) { lastHpf = hp; hpf.design(Biquad::highPass, sr, hp, .70710678); }
        if (lp != lastLpf) { lastLpf = lp; lpf.design(Biquad::lowPass, sr, std::min<double>(lp, sr * .45), .70710678); }
        const float air = std::clamp(p.air, 0.f, 100.f) * .09f;
        if (air != lastAir) { lastAir = air; airShelf.design(Biquad::highShelf, sr, std::min(9000.0, sr * .4), .70710678, air); }
        if (!designed) { designed = true; focus.design(Biquad::bandPass, sr, 1100, .5); harmonicHp.design(Biquad::highPass, sr, 1200, .70710678); depthTone.design(Biquad::lowPass, sr, std::min(6000.0, sr * .4), .70710678); }
    }
    double sr = 48000; bool ready = false, designed = false, hpfOn = false, lpfOn = false;
    HilbertPair hilbertL, hilbertR;
    LinkwitzRiley bassSplit;
    Biquad hpf, lpf, focus, airShelf, harmonicHp, depthTone;
    std::vector<float> depthLine; size_t depthWrite = 0; int depthDelay = 0;
    Follower fast, slow;
    Smoothed widthRamp, panRamp, outRamp, trimRamp, midBal, sideBal, focusRamp, depthRamp, harmRamp, dynRamp, bassMix, rotateMix, rotCos, rotSin, monoRamp, bypassRamp, invL, invR, swapRamp;
    std::array<Smoothed, 4> modeMix;
    float lastBass = -1, lastHpf = -1, lastLpf = -1, lastAir = -1;
};

} // namespace hem::studio
