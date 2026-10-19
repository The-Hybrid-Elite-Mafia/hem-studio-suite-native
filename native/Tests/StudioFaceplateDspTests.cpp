// Numerical checks for the 2026-10 faceplate engines. No JUCE, no GUI.
// Every process() call below runs with a global allocation counter armed.
#include "../Source/DSP/VocalDeReverbCore.h"
#include "../Source/DSP/SipaArchitectCore.h"
#include "../Source/DSP/TransientShaperCore.h"
#include "../Source/DSP/BreathMouthStudioCore.h"
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <random>
#include <string>

namespace { std::atomic<bool> armed{false}; std::atomic<int> allocations{0}; }
void* operator new(std::size_t n) { if (armed.load()) allocations.fetch_add(1); if (void* p = std::malloc(n ? n : 1)) return p; throw std::bad_alloc(); }
void* operator new[](std::size_t n) { if (armed.load()) allocations.fetch_add(1); if (void* p = std::malloc(n ? n : 1)) return p; throw std::bad_alloc(); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }

using namespace hem::studio;
static int failures = 0;
static void check(bool ok, const std::string& what) { std::printf("%s %s\n", ok ? "PASS" : "FAIL", what.c_str()); if (!ok) ++failures; }
static double db(double ratio) { return 10 * std::log10(std::max(ratio, 1.e-30)); }

template <class Core, class Settings>
static void run(Core& core, std::vector<float>& l, std::vector<float>& r, const Settings& p, int block, ScopeTap* tap = nullptr) {
    armed = true;
    for (size_t i = 0; i + size_t(block) <= l.size(); i += size_t(block)) core.process(l.data() + i, r.data() + i, block, p, tap);
    armed = false;
}

static void deReverb() {
    for (double sr : {44100.0, 48000.0, 96000.0, 192000.0}) {
        VocalDeReverbCore core; check(core.prepare(sr), "de-reverb prepares at " + std::to_string(int(sr)));
        const int latency = core.latency();
        std::mt19937 g(1); std::normal_distribution<float> d(0, .1f);
        const int n = int(sr);
        std::vector<float> l(static_cast<size_t>(n), 0.f), r(static_cast<size_t>(n), 0.f);
        for (int i = 0; i < n; ++i) { l[size_t(i)] = d(g); r[size_t(i)] = .5f * l[size_t(i)] + d(g) * .5f; }
        auto a = l, b = r; DeReverbSettings p; p.reduction = 0;
        ScopeTap tap;
        run(core, a, b, p, 480, &tap);
        double err = 0;
        for (int i = latency + core.fftSize(); i + 480 < n; ++i) err = std::max(err, double(std::abs(a[size_t(i)] - l[size_t(i - latency)]) + std::abs(b[size_t(i)] - r[size_t(i - latency)])));
        check(err < 1.e-5, "de-reverb at 0% is the input delayed by its reported latency (" + std::to_string(latency) + " samples), err " + std::to_string(err));
    }
    // Synthetic room: 50 ms noise bursts every 0.5 s through a T60 = 1.0 s noise tail.
    const double sr = 48000; const int n = int(sr * 6);
    std::mt19937 g(2); std::normal_distribution<float> d(0, 1);
    std::vector<float> dry(static_cast<size_t>(n), 0.f), wet(static_cast<size_t>(n), 0.f);
    for (int i = 0; i < n; ++i) dry[size_t(i)] = (i % 24000) < 2400 ? d(g) * .2f : 0.f;
    const int irLength = int(sr * 1.5);
    std::vector<float> ir(static_cast<size_t>(irLength), 0.f);
    for (int i = 0; i < irLength; ++i) ir[size_t(i)] = (i == 0 ? 1.f : 0.f) + (i > 480 ? d(g) * .08f * std::pow(10.f, -3.f * float(i) / float(sr)) : 0.f);
    for (int i = 0; i < n; ++i) if (dry[size_t(i)] != 0) for (int j = 0; j < irLength && i + j < n; ++j) wet[size_t(i + j)] += dry[size_t(i)] * ir[size_t(j)];
    for (int algorithm = 0; algorithm < 3; ++algorithm) {
        VocalDeReverbCore core; core.prepare(sr);
        DeReverbSettings p; p.reduction = 100; p.algorithm = algorithm; p.learn = algorithm == 2;
        p.roomSize = algorithm == 0 ? 30.f : 50.f; p.damping = algorithm == 0 ? 50.f : 0.f;
        auto a = wet, b = wet;
        run(core, a, b, p, 256);
        const int latency = core.latency();
        double tailIn = 0, tailOut = 0;
        for (int i = int(sr * 2); i < n - latency; ++i) if (i % 24000 > 4800) { tailIn += double(wet[size_t(i)]) * wet[size_t(i)]; tailOut += double(a[size_t(i + latency)]) * a[size_t(i + latency)]; }
        check(db(tailOut / tailIn) < -6, "de-reverb algorithm " + std::to_string(algorithm) + " removes > 6 dB of reverb tail (" + std::to_string(db(tailOut / tailIn)) + " dB)");
        if (algorithm == 1) { const float t60 = core.t60Seconds.load(); check(t60 > .6f && t60 < 1.5f, "adaptive T60 estimate near the true 1.0 s: " + std::to_string(t60)); }
        if (algorithm == 2) check(core.learnedReady.load(), "LEARN captures a room profile");
    }
    {
        VocalDeReverbCore core; core.prepare(48000);
        std::vector<float> a(48000, .1f), b(48000, .1f); DeReverbSettings p; p.bypass = true; run(core, a, b, p, 480);
        check(std::abs(a[40000] - .1f) < 1.e-6f, "de-reverb bypass passes the latency-aligned dry signal");
    }
}

static void sipa() {
    const double sr = 48000;
    for (double f : {30.0, 100.0, 1000.0, 5000.0, 15000.0, 20000.0}) {
        HilbertPair h; double si = 0, sq = 0, iq = 0;
        for (int i = 0; i < 48000; ++i) { float a, b; h.process(float(std::sin(2 * pi * f * i / sr)), a, b); if (i > 24000) { si += a * a; sq += b * b; iq += a * b; } }
        check(std::abs(iq / std::sqrt(si * sq)) < .03 && std::abs(std::sqrt(si / sq) - 1) < .01, "Hilbert pair is 90 degrees at " + std::to_string(int(f)) + " Hz");
    }
    std::mt19937 g(3); std::normal_distribution<float> d(0, .2f);
    std::vector<float> l(48000), r(48000);
    for (size_t i = 0; i < l.size(); ++i) { l[i] = d(g); r[i] = .6f * l[i] + .4f * d(g); }
    auto measure = [&](SipaSettings p, double& mid, double& side) {
        SipaArchitectCore core; core.prepare(sr); auto a = l, b = r; run(core, a, b, p, 480);
        double m = 0, s = 0, m0 = 0, s0 = 0;
        for (size_t i = 24000; i < a.size(); ++i) { m += std::pow(a[i] + b[i], 2); s += std::pow(a[i] - b[i], 2); m0 += std::pow(l[i] + r[i], 2); s0 += std::pow(l[i] - r[i], 2); }
        mid = db(m / m0); side = db(s / s0);
        return std::pair{a, b};
    };
    double m, s;
    { auto [a, b] = measure({}, m, s); double e = 0; for (size_t i = 0; i < a.size(); ++i) e = std::max(e, double(std::abs(a[i] - l[i]) + std::abs(b[i] - r[i]))); check(e < 1.e-5, "S.I.P.A. defaults are transparent"); }
    SipaSettings p; p.width = 200; measure(p, m, s); check(std::abs(s - 6.02) < .1 && std::abs(m) < .05, "WIDTH 200% = +6 dB side, mid unchanged");
    p = {}; p.width = 0; measure(p, m, s); check(s < -80, "WIDTH 0% removes the side");
    p = {}; p.stereoMode = 3; measure(p, m, s); check(s < -80, "MONO mode removes the side");
    p = {}; p.panLaw = 6; measure(p, m, s); check(std::abs(m - 6) < .05 && std::abs(s) < .05, "PAN LAW +6 dB raises the center only");
    p = {}; p.phaseRotate = 180; double m2, s2; measure(p, m, s); p = {}; p.phaseMode = 2; measure(p, m2, s2);
    check(std::abs(m - m2) < .2 && std::abs(s - s2) < .2, "PHASE ROTATE 180 matches INVERT R");
    p = {}; p.hpfSide = 1000; p.lpfSide = 1000; measure(p, m, s); check(s < -10 && std::abs(m) < .05, "FILTER SIDE band-limits only the side");
    for (auto field : {&SipaSettings::centerFocus, &SipaSettings::depth, &SipaSettings::harmonicWidth, &SipaSettings::air, &SipaSettings::dynamicWidth}) {
        p = {}; p.*field = 100; measure(p, m, s); check(std::abs(s) > .3 && std::abs(m) < .05, "side-only control changes the side, not the mid");
    }
    p = {}; p.monoCheck = true; measure(p, m, s); check(s < -80, "MONO CHECK sums to mono");
}

static void transient() {
    const double sr = 48000; const int n = 96000;
    std::vector<float> x(static_cast<size_t>(n), 0.f);
    for (int i = 0; i < n; ++i) { const int ph = i % 12000; x[size_t(i)] = .5f * std::exp(-ph / (.08f * 48000)) * float(std::sin(2 * pi * 180 * ph / sr)) + .2f * std::exp(-ph / (.003f * 48000)) * float(std::sin(2 * pi * 3000 * ph / sr)); }
    auto energy = [&](TransientShaperSettings p, double& head, double& tail, float& peak) {
        TransientShaperCore core; core.prepare(sr); auto a = x, b = x; run(core, a, b, p, 480);
        head = tail = 0; peak = 0;
        for (int i = 24000; i < n; ++i) { const int ph = i % 12000; const double v = double(a[size_t(i)]) * a[size_t(i)]; if (ph < 240) head += v; else if (ph > 2400) tail += v; peak = std::max(peak, std::abs(a[size_t(i)])); }
    };
    double h0, t0, h, t; float pk;
    energy({}, h0, t0, pk);
    for (int mode = 0; mode < 4; ++mode) { TransientShaperSettings p; p.attack = 100; p.processMode = mode; energy(p, h, t, pk); check(db(h / h0) > (mode == 2 ? 2.5 : 6), "ATTACK +100 raises onsets in process mode " + std::to_string(mode) + " (" + std::to_string(db(h / h0)) + " dB; Smooth is gentler)"); }
    { TransientShaperSettings p; p.attack = -100; energy(p, h, t, pk); check(db(h / h0) < -3, "ATTACK -100 softens onsets"); }
    { TransientShaperSettings p; p.sustain = -100; energy(p, h, t, pk); check(db(t / t0) < -3, "SUSTAIN -100 lowers the tail"); }
    { TransientShaperSettings p; p.sustain = 100; energy(p, h, t, pk); check(db(t / t0) > 2, "SUSTAIN +100 raises the tail"); }
    { TransientShaperSettings p; p.attack = 100; p.output = 12; p.clipGuard = 100; energy(p, h, t, pk); check(pk <= .9886f, "CLIP GUARD holds peaks under -0.1 dBFS after OUTPUT"); }
    { TransientShaperSettings p; p.attack = 100; p.length = 200; double h2, t2; energy(p, h2, t2, pk); TransientShaperSettings q; q.attack = 100; energy(q, h, t, pk); check(t2 > t * 1.5, "TRANSIENT LENGTH extends the attack gain"); }
    { TransientShaperSettings p; TransientShaperCore core; core.prepare(sr); auto a = x, b = x; run(core, a, b, p, 480); double e = 0; for (int i = 0; i < n; ++i) e = std::max(e, double(std::abs(a[size_t(i)] - x[size_t(i)]))); check(e < 1.e-6, "Transient defaults are exactly neutral (no crossover phase)"); }
    { TransientShaperSettings p; p.lowFocus = 50; p.attack = 100; double h2, t2; energy(p, h2, t2, pk); check(db(h2 / h0) > 6, "LOW FOCUS uses the band split and still shapes"); }
    { TransientShaperSettings p; p.attack = 100; p.bypass = true; TransientShaperCore core; core.prepare(sr); auto a = x, b = x; run(core, a, b, p, 480); double e = 0; for (int i = 48000; i < n; ++i) e = std::max(e, double(std::abs(a[size_t(i)] - x[size_t(i)]))); check(e == 0, "Transient BYPASS is exact"); }
    { TransientShaperSettings p; p.attack = 100; p.delta = true; p.mix = 100; TransientShaperCore core; core.prepare(sr); auto a = x, b = x; run(core, a, b, p, 480); double e = 0; for (int i = 48000; i < n; ++i) if (i % 12000 > 6000) e = std::max(e, double(std::abs(a[size_t(i)]))); check(e < .05, "DELTA is near silent where nothing is shaped"); }
}

static void breath() {
    const double sr = 48000; const int n = 48000 * 3;
    std::vector<float> x(static_cast<size_t>(n), 0.f); std::vector<int> kind(static_cast<size_t>(n), 0);
    std::mt19937 g(3); std::normal_distribution<float> d(0, 1);
    for (int i = 0; i < n; ++i) {
        const int ph = i % 48000; float v = 0;
        if (ph < 28800) { for (int h = 1; h < 12; ++h) v += .3f / float(h) * float(std::sin(2 * pi * 220 * h * ph / sr)); kind[size_t(i)] = 0; }
        else if (ph < 43200) { v = .02f * d(g); kind[size_t(i)] = 1; }
        else { kind[size_t(i)] = 2; if (ph % 1200 == 0) v = .3f; if (ph % 1200 == 1) v = -.25f; }
        x[size_t(i)] = v;
    }
    for (int detection = 0; detection < 3; ++detection) {
        BreathMouthStudioCore core; core.prepare(sr); ScopeTap tap;
        BreathMouthStudioSettings p; p.detection = detection;
        auto a = x, b = x; std::vector<float> frames(480 * ScopeTap::channels);
        std::array<double, 3> score{}, count{}; int position = 0; const int latency = core.latency();
        armed = true;
        for (int i = 0; i + 480 <= n; i += 480) {
            core.process(a.data() + i, b.data() + i, 480, p, &tap);
            armed = false;
            const int got = tap.pull(frames.data(), 480);
            for (int j = 0; j < got; ++j, ++position) if (position >= 48000 && position >= latency) { score[size_t(kind[size_t(position - latency)])] += frames[size_t(j) * ScopeTap::channels + 5]; ++count[size_t(kind[size_t(position - latency)])]; }
            armed = true;
        }
        armed = false;
        const double tone = score[0] / count[0], noise = score[1] / count[1];
        if (detection == 0) check(noise > .5 && tone < .05, "breath detector fires on breath noise (" + std::to_string(noise) + "), not on the tone (" + std::to_string(tone) + ")");
        if (detection == 1) check(core.events.load() >= 8 && tone < .1, "mouth detector catches clicks (" + std::to_string(core.events.load()) + " events)");
        if (detection == 2) check(tone < .05, "sibilance detector ignores a low harmonic tone");
    }
    {
        BreathMouthStudioCore core; core.prepare(sr); BreathMouthStudioSettings p; p.threshold = -60; p.reduction = 20; p.stageFilter = false;
        auto a = x, b = x; run(core, a, b, p, 480); const int L = core.latency();
        double in = 0, out = 0; for (int i = 48000; i < n - L; ++i) if (kind[size_t(i)] == 1) { in += double(x[size_t(i)]) * x[size_t(i)]; out += double(a[size_t(i + L)]) * a[size_t(i + L)]; }
        check(db(out / in) < -6, "broadband breath reduction removes > 6 dB of breath (" + std::to_string(db(out / in)) + " dB)");
    }
    {
        BreathMouthStudioCore core; core.prepare(sr); BreathMouthStudioSettings p; p.reduction = 0;
        auto a = x, b = x; run(core, a, b, p, 480); const int L = core.latency(); double e = 0;
        for (int i = 24000; i < n - L; ++i) e = std::max(e, double(std::abs(a[size_t(i + L)] - x[size_t(i)])));
        check(e < 1.e-6, "REDUCTION 0 dB is the input delayed by the 2 ms lookahead");
    }
}

int main() {
    deReverb(); sipa(); transient(); breath();
    check(allocations.load() == 0, "no allocation inside any process() call (" + std::to_string(allocations.load()) + ")");
    std::printf("%s: %d failure(s)\n", failures ? "FAILED" : "ALL PASSED", failures);
    return failures ? 1 : 0;
}
