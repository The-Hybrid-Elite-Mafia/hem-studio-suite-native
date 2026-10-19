#pragma once
// Shared real-time primitives for the 2026-10 Studio faceplate processors
// (Vocal De-Reverb, S.I.P.A. Architect, Transient Shaper, Breath / Mouth Control).
// Pure C++20, no JUCE dependency. Everything is sized in prepare(); process paths
// never allocate, lock or block.
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

namespace hem::studio {

constexpr double pi = 3.14159265358979323846;

inline float dbToGain(float db) noexcept { return std::pow(10.f, db * .05f); }
inline float gainToDb(float g) noexcept { return 20.f * std::log10(std::max(g, 1.e-9f)); }
inline float finite(float x) noexcept { return std::isfinite(x) ? x : 0.f; }
inline bool rateValid(double sr) noexcept { return std::isfinite(sr) && sr >= 8000 && sr <= 384000; }
// One-pole coefficient for a time constant in milliseconds (0 = instant).
inline float timeCoefficient(float ms, double sr) noexcept {
    if (!(ms > 0) || !(sr > 0)) return 0.f;
    return float(std::exp(-1.0 / (double(ms) * .001 * sr)));
}

// Exponential parameter smoother (per-sample), avoids zipper noise.
struct Smoothed {
    float value = 0, target = 0, coefficient = 0;
    void reset(double sr, float initial, float ms = 20.f) noexcept { value = target = initial; coefficient = timeCoefficient(ms, sr); }
    void set(float t) noexcept { target = t; }
    float next() noexcept { value = target + (value - target) * coefficient; if (std::abs(value - target) < 1.e-7f) value = target; return value; }
};

// Attack/release envelope follower on a non-negative input.
struct Follower {
    float env = 0, attack = 0, release = 0;
    void setTimes(double sr, float attackMs, float releaseMs) noexcept { attack = timeCoefficient(attackMs, sr); release = timeCoefficient(releaseMs, sr); }
    float process(float x) noexcept { const float c = x > env ? attack : release; env = x + (env - x) * c; return env; }
    void reset() noexcept { env = 0; }
};

// Transposed direct form II biquad, two channels, RBJ cookbook designs.
class Biquad {
public:
    enum Type { lowPass, highPass, bandPass, peak, lowShelf, highShelf, allPass };
    void reset() noexcept { z = {}; }
    void design(Type type, double sr, double hz, double q, double gainDb = 0) noexcept {
        hz = std::clamp(hz, 5.0, sr * .49);
        q = std::max(q, .025);
        const double w = 2 * pi * hz / sr, c = std::cos(w), s = std::sin(w), alpha = s / (2 * q), A = std::pow(10.0, gainDb / 40);
        double b0 = 1, b1 = 0, b2 = 0, a0 = 1, a1 = 0, a2 = 0;
        switch (type) {
            case lowPass: b0 = (1 - c) / 2; b1 = 1 - c; b2 = b0; a0 = 1 + alpha; a1 = -2 * c; a2 = 1 - alpha; break;
            case highPass: b0 = (1 + c) / 2; b1 = -(1 + c); b2 = b0; a0 = 1 + alpha; a1 = -2 * c; a2 = 1 - alpha; break;
            case bandPass: b0 = alpha; b1 = 0; b2 = -alpha; a0 = 1 + alpha; a1 = -2 * c; a2 = 1 - alpha; break; // 0 dB peak
            case peak: b0 = 1 + alpha * A; b1 = -2 * c; b2 = 1 - alpha * A; a0 = 1 + alpha / A; a1 = -2 * c; a2 = 1 - alpha / A; break;
            case lowShelf: { const double r = 2 * std::sqrt(A) * alpha;
                b0 = A * ((A + 1) - (A - 1) * c + r); b1 = 2 * A * ((A - 1) - (A + 1) * c); b2 = A * ((A + 1) - (A - 1) * c - r);
                a0 = (A + 1) + (A - 1) * c + r; a1 = -2 * ((A - 1) + (A + 1) * c); a2 = (A + 1) + (A - 1) * c - r; break; }
            case highShelf: { const double r = 2 * std::sqrt(A) * alpha;
                b0 = A * ((A + 1) + (A - 1) * c + r); b1 = -2 * A * ((A - 1) + (A + 1) * c); b2 = A * ((A + 1) + (A - 1) * c - r);
                a0 = (A + 1) - (A - 1) * c + r; a1 = 2 * ((A - 1) - (A + 1) * c); a2 = (A + 1) - (A - 1) * c - r; break; }
            case allPass: b0 = 1 - alpha; b1 = -2 * c; b2 = 1 + alpha; a0 = 1 + alpha; a1 = -2 * c; a2 = 1 - alpha; break;
        }
        k = {float(b0 / a0), float(b1 / a0), float(b2 / a0), float(a1 / a0), float(a2 / a0)};
    }
    float tick(float x, int ch) noexcept {
        auto& s = z[size_t(ch)];
        const float y = k[0] * x + s[0];
        s[0] = k[1] * x - k[3] * y + s[1];
        s[1] = k[2] * x - k[4] * y;
        if (!std::isfinite(y)) { s = {}; return 0.f; }
        return y;
    }
private:
    std::array<float, 5> k{1, 0, 0, 0, 0};
    std::array<std::array<float, 2>, 2> z{};
};

// 4th-order Linkwitz-Riley split (LP + HP sum to a 2nd-order allpass).
// A second instance summed (low + high) gives the matching allpass for aligning
// a band that was not split at this frequency.
class LinkwitzRiley {
public:
    void design(double sr, double hz) noexcept {
        for (auto& f : lp) f.design(Biquad::lowPass, sr, hz, .70710678);
        for (auto& f : hp) f.design(Biquad::highPass, sr, hz, .70710678);
    }
    void reset() noexcept { for (auto& f : lp) f.reset(); for (auto& f : hp) f.reset(); }
    void split(float x, int ch, float& low, float& high) noexcept {
        low = lp[1].tick(lp[0].tick(x, ch), ch);
        high = hp[1].tick(hp[0].tick(x, ch), ch);
    }
    float allpass(float x, int ch) noexcept { float a, b; split(x, ch, a, b); return a + b; }
    float highOnly(float x, int ch) noexcept { return hp[1].tick(hp[0].tick(x, ch), ch); }
private:
    std::array<Biquad, 2> lp, hp;
};

// Wideband 90-degree phase splitter: two chains of 4 second-order polyphase
// allpasses (Olli Niemitalo's published coefficients). Output pair (i, q) has a
// near-constant 90-degree difference across the audio band; both outputs are allpass.
class HilbertPair {
public:
    void reset() noexcept { for (auto& s : state) s = {}; delayed = 0; }
    void process(float x, float& inPhase, float& quadrature) noexcept {
        float a = x, b = x;
        for (size_t i = 0; i < 4; ++i) a = stage(a, coefficientsA[i] * coefficientsA[i], state[i]);
        for (size_t i = 0; i < 4; ++i) b = stage(b, coefficientsB[i] * coefficientsB[i], state[i + 4]);
        inPhase = delayed; // path A is delayed one sample (verified 90 deg, 30 Hz - 20 kHz @ 48 kHz)
        delayed = a;
        quadrature = b;
    }
private:
    struct State { float x1 = 0, x2 = 0, y1 = 0, y2 = 0; };
    static float stage(float x, float c, State& s) noexcept {
        const float y = c * (x + s.y2) - s.x2;
        s.x2 = s.x1; s.x1 = x; s.y2 = s.y1; s.y1 = std::isfinite(y) ? y : 0.f;
        return s.y1;
    }
    static constexpr std::array<float, 4> coefficientsA{.6923878f, .9360654322959f, .9882295226860f, .9987488452737f};
    static constexpr std::array<float, 4> coefficientsB{.4021921162426f, .8561710882420f, .9722909545651f, .9952884791278f};
    std::array<State, 8> state{};
    float delayed = 0;
};

// Lock-free single-producer/single-consumer tap. The audio thread pushes frames
// of `channels` floats; the editor (message thread) drains them. When the editor
// is slow or absent, newest data is dropped (never blocks, never allocates).
class ScopeTap {
public:
    static constexpr int channels = 6;   // inL, inR, outL, outR, aux0, aux1
    static constexpr uint32_t capacity = 1u << 15;
    ScopeTap() : data(size_t(capacity) * channels, 0.f) {}
    void push(const float* frame) noexcept {
        const uint32_t w = writeIndex.load(std::memory_order_relaxed);
        const uint32_t r = readIndex.load(std::memory_order_acquire);
        if (w - r >= capacity) { dropped.fetch_add(1, std::memory_order_relaxed); return; }
        std::memcpy(&data[size_t(w & (capacity - 1)) * channels], frame, sizeof(float) * channels);
        writeIndex.store(w + 1, std::memory_order_release);
    }
    // Consumer: copies up to maxFrames into out (interleaved), returns count.
    int pull(float* out, int maxFrames) noexcept {
        const uint32_t r = readIndex.load(std::memory_order_relaxed);
        const uint32_t w = writeIndex.load(std::memory_order_acquire);
        const int available = int(std::min<uint32_t>(w - r, uint32_t(std::max(0, maxFrames))));
        for (int i = 0; i < available; ++i)
            std::memcpy(out + size_t(i) * channels, &data[size_t((r + uint32_t(i)) & (capacity - 1)) * channels], sizeof(float) * channels);
        readIndex.store(r + uint32_t(available), std::memory_order_release);
        return available;
    }
    // Consumer-side discard (editor opening): skip stale frames.
    void discard() noexcept { readIndex.store(writeIndex.load(std::memory_order_acquire), std::memory_order_release); }
    std::atomic<uint32_t> dropped{0};
private:
    std::vector<float> data;
    std::atomic<uint32_t> writeIndex{0}, readIndex{0};
    static_assert(std::atomic<uint32_t>::is_always_lock_free);
};

} // namespace hem::studio
