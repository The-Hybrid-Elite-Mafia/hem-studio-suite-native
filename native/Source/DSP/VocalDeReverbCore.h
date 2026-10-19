#pragma once
// Vocal De-Reverb: STFT reverberation suppression.
//
// Model (statistical late-reverberation estimator, Lebart/Habets style):
//   late reverb power  R_late(k,t)  = 10^(-6*Td/T60(k)) * Psmooth(k, t - Td)
//   early reflections  R_early(k,t) = 0.5 * 10^(-6*hop/T60(k)) * Psmooth(k, t - 1)
// A spectral-subtraction gain with a floor (Ambience) removes the estimate,
// smoothed in time and frequency against musical noise. T60 comes from Room Size
// (SPECTRAL), from an online decay-slope estimator (ADAPTIVE), or from a profile
// captured while LEARN is on (LEARNED). This is signal processing only: there is no
// neural network or "AI" model in this code.
//
// M and S are transformed together in one complex FFT (m + j*s).
// Latency = FFT size (1024 @ <=50 kHz, 2048 @ <=100 kHz, 4096 above).
// prepare() allocates; process() never allocates, locks or blocks.
#include "StudioDsp.h"
#include "PreparedConvolution.h"
#include <complex>
#include <memory>

namespace hem::studio {

struct DeReverbSettings {
    float reduction = 50, early = 45, tail = 60, ambience = 38, roomSize = 25, damping = 50, clarity = 62, presence = 0, mix = 100, output = 0;
    int mode = 0;       // 0 natural, 1 aggressive, 2 transparent
    int algorithm = 0;  // 0 spectral, 1 adaptive, 2 learned profile
    float focusHz = 200, sensitivity = 50;
    int stereo = 2;     // 0 mid, 1 side, 2 mid + side
    bool learn = false, resetLearn = false, bypass = false;
};

class VocalDeReverbCore {
public:
    static constexpr int bands = 24;

    static int fftSizeFor(double sr) noexcept { return sr <= 50000 ? 1024 : sr <= 100000 ? 2048 : 4096; }

    bool prepare(double sampleRate) {
        ready = false;
        if (!rateValid(sampleRate)) return false;
        sr = sampleRate; size = fftSizeFor(sr); hop = size / 4; bins = size / 2 + 1;
        fft = std::make_unique<reverb::PreparedFFT>(size_t(size));
        window.assign(size_t(size), 0.f);
        for (int n = 0; n < size; ++n) window[size_t(n)] = float(.5 - .5 * std::cos(2 * pi * n / size));
        frame.assign(size_t(size), {});
        staged.assign(size_t(size), {});
        for (auto& ring : input) ring.assign(size_t(size), 0.f);
        for (auto& acc : accumulator) acc.assign(size_t(size), 0.f);
        for (auto& ring : dry) ring.assign(size_t(size), 0.f);
        delayFrames = std::max(2, int(std::lround(.05 * sr / hop)));
        for (int c = 0; c < 2; ++c) {
            smooth[c].assign(size_t(bins), 0.f); previousGain[c].assign(size_t(bins), 1.f); gain[c].assign(size_t(bins), 1.f);
            history[c].assign(size_t(bins) * size_t(delayFrames), 0.f);
        }
        smoothedGain.assign(size_t(bins), 1.f);
        decayLate.assign(size_t(bins), 0.f); decayEarly.assign(size_t(bins), 0.f); focusWeight.assign(size_t(bins), 1.f);
        bandOf.assign(size_t(bins), 0);
        for (int k = 0; k < bins; ++k) {
            const double hz = std::max(20.0, double(k) * sr / size);
            bandOf[size_t(k)] = std::clamp(int(std::floor(std::log(hz / 20.0) / std::log(1000.0) * bands)), 0, bands - 1);
        }
        lastFocus = -1; lastRoom = -1; lastDamping = -1; lastAlgorithm = -1;
        resetState();
        presenceFilter.reset(); presenceDb = -1;
        mixRamp.reset(sr, 1, 20); outRamp.reset(sr, 1, 20); bypassRamp.reset(sr, 1, 15); presenceRamp.reset(sr, 0, 30);
        ready = true;
        return true;
    }
    // Dry and wet both leave exactly size - 1 samples after they arrive.
    int latency() const noexcept { return ready ? size - 1 : 0; }
    int fftSize() const noexcept { return size; }

    void process(float* l, float* r, int n, const DeReverbSettings& p, ScopeTap* tap = nullptr) noexcept {
        if (!l || !r || n <= 0) return;
        if (!ready) { std::fill_n(l, n, 0.f); std::fill_n(r, n, 0.f); return; }
        settings = p;
        if (p.resetLearn && !resetLatch) clearLearning();
        resetLatch = p.resetLearn;
        mixRamp.set(std::clamp(p.mix, 0.f, 100.f) * .01f);
        outRamp.set(dbToGain(std::clamp(p.output, -24.f, 24.f)));
        bypassRamp.set(p.bypass ? 0.f : 1.f);
        const float presence = std::clamp(p.presence, 0.f, 100.f) * .06f;
        if (std::abs(presence - presenceDb) > .01f) { presenceDb = presence; presenceFilter.design(Biquad::peak, sr, 4000, .8, presenceDb); }
        for (int i = 0; i < n; ++i) {
            const float xl = finite(l[i]), xr = finite(r[i]);
            const size_t w = size_t(position % uint64_t(size));
            input[0][w] = .5f * (xl + xr); input[1][w] = .5f * (xl - xr);
            dry[0][w] = xl; dry[1][w] = xr;
            ++position;
            if (++hopCounter >= hop) { hopCounter = 0; if (position >= uint64_t(size)) processFrame(); }
            // Output for sample (position - size): no later frame covers it, so its slot is final.
            const size_t read = size_t(position % uint64_t(size));
            const float m = accumulator[0][read], s = accumulator[1][read];
            accumulator[0][read] = 0; accumulator[1][read] = 0;
            const float dl = dry[0][read], dr = dry[1][read]; // written size - 1 samples ago
            float wl = m + s, wr = m - s;
            wl = presenceFilter.tick(wl, 0); wr = presenceFilter.tick(wr, 1);
            const float mix = mixRamp.next(), out = outRamp.next(), active = bypassRamp.next();
            const float yl = (dl + mix * (wl - dl)) * out, yr = (dr + mix * (wr - dr)) * out;
            l[i] = dl + active * (yl - dl);
            r[i] = dr + active * (yr - dr);
            if (tap) { const float f[ScopeTap::channels]{dl, dr, l[i], r[i], lastReductionDb, 0}; tap->push(f); }
        }
        reductionDb.store(lastReductionDb, std::memory_order_relaxed);
        t60Seconds.store(lastT60, std::memory_order_relaxed);
        learnedReady.store(learnedValid, std::memory_order_relaxed);
    }

    std::atomic<float> reductionDb{0}, t60Seconds{0};
    std::atomic<bool> learnedReady{false};

private:
    void resetState() noexcept {
        position = 0; hopCounter = 0; frames = 0;
        for (int c = 0; c < 2; ++c) {
            std::fill(input[c].begin(), input[c].end(), 0.f); std::fill(accumulator[c].begin(), accumulator[c].end(), 0.f);
            std::fill(dry[c].begin(), dry[c].end(), 0.f); std::fill(smooth[c].begin(), smooth[c].end(), 0.f);
            std::fill(previousGain[c].begin(), previousGain[c].end(), 1.f); std::fill(history[c].begin(), history[c].end(), 0.f);
        }
        clearLearning();
        lastReductionDb = 0; lastT60 = 0;
    }
    void clearLearning() noexcept {
        for (int b = 0; b < bands; ++b) { estimate[size_t(b)] = .6f; learned[size_t(b)] = .6f; previousDb[size_t(b)] = -120; run[size_t(b)] = 0; runStartDb[size_t(b)] = -120; recentMax[size_t(b)] = -120; }
        learnedValid = false; learnedUpdates = 0;
    }
    // Online decay estimator. Band levels are smoothed across frames; a decay run
    // continues while the level stays within 2 dB of its running minimum. The first
    // two frames (the direct-sound offset) are skipped; runs of >= 6 frames that fall
    // >= 10 dB are treated as free reverberant decay and give a T60 sample.
    void estimateDecay(const std::array<float, bands>& bandDb) noexcept {
        const float hopSeconds = float(hop / sr);
        for (int b = 0; b < bands; ++b) {
            auto& level = previousDb[size_t(b)];
            level = level < -119 ? bandDb[size_t(b)] : .85f * level + .15f * bandDb[size_t(b)];
            auto& count = run[size_t(b)]; auto& minimum = recentMax[size_t(b)]; auto& start = runStartDb[size_t(b)];
            const bool continuing = count > 0 && level < minimum + 2.f && level > -100;
            if (continuing) {
                ++count;
                if (count == 3) start = level;
                minimum = std::min(minimum, level);
            } else {
                if (count >= 8) {
                    const float drop = start - minimum, seconds = float(count - 3) * hopSeconds;
                    if (drop >= 10 && seconds > 0) {
                        const float sample = std::clamp(60.f * seconds / drop, .1f, 5.f);
                        estimate[size_t(b)] += .15f * (sample - estimate[size_t(b)]);
                        if (settings.learn) { learned[size_t(b)] = estimate[size_t(b)]; ++learnedUpdates; if (learnedUpdates > bands * 2) learnedValid = true; }
                    }
                }
                count = 1; minimum = level; start = level;
            }
        }
    }
    void updateTables(const std::array<float, bands>& bandT60) noexcept {
        const float damping = std::clamp(settings.damping, 0.f, 100.f) * .007f;
        const float hopSeconds = float(hop / sr), lateSeconds = float(delayFrames) * hopSeconds;
        float sum = 0;
        for (int k = 0; k < bins; ++k) {
            const float hz = float(k * sr / size);
            const float t60 = std::max(.05f, bandT60[size_t(bandOf[size_t(k)])] * (1 - damping * hz / (hz + 2500)));
            decayLate[size_t(k)] = std::pow(10.f, -6 * lateSeconds / t60);
            decayEarly[size_t(k)] = .5f * std::pow(10.f, -6 * hopSeconds / t60);
            sum += t60;
        }
        lastT60 = sum / float(bins);
        const float focus = std::clamp(settings.focusHz, 20.f, 20000.f);
        if (focus != lastFocus) {
            lastFocus = focus;
            for (int k = 0; k < bins; ++k) {
                const float hz = float(k * sr / size);
                // Full processing at and above Focus, fading to none one octave below.
                const float t = std::clamp(std::log2(std::max(hz, 1.f) / (focus * .5f)), 0.f, 1.f);
                focusWeight[size_t(k)] = t * t * (3 - 2 * t);
            }
        }
    }
    void processFrame() noexcept {
        // Window the newest `size` samples (oldest first) into one complex frame.
        const uint64_t start = position - uint64_t(size);
        for (int n = 0; n < size; ++n) {
            const size_t idx = size_t((start + uint64_t(n)) % uint64_t(size));
            frame[size_t(n)] = {input[0][idx] * window[size_t(n)], input[1][idx] * window[size_t(n)]};
        }
        fft->transform(frame.data(), false);

        // Band energies for the decay estimator (M + S power).
        std::array<float, bands> bandPower{};
        std::array<int, bands> bandCount{};
        for (int k = 1; k < bins; ++k) {
            const auto [mk, sk] = separate(k);
            bandPower[size_t(bandOf[size_t(k)])] += std::norm(mk) + std::norm(sk);
            ++bandCount[size_t(bandOf[size_t(k)])];
        }
        std::array<float, bands> bandDb{};
        for (int b = 0; b < bands; ++b) bandDb[size_t(b)] = 10 * std::log10(bandPower[size_t(b)] / float(std::max(1, bandCount[size_t(b)])) + 1.e-20f);
        estimateDecay(bandDb);

        std::array<float, bands> bandT60{};
        const float room = std::clamp(settings.roomSize, 0.f, 100.f);
        const float roomT60 = .15f + 2.85f * room * .01f, roomScale = std::pow(2.f, (room - 50.f) / 50.f);
        for (int b = 0; b < bands; ++b) {
            if (settings.algorithm == 1) bandT60[size_t(b)] = estimate[size_t(b)] * roomScale;
            else if (settings.algorithm == 2) bandT60[size_t(b)] = (learnedValid ? learned[size_t(b)] : estimate[size_t(b)]) * roomScale;
            else bandT60[size_t(b)] = roomT60;
            bandT60[size_t(b)] = std::clamp(bandT60[size_t(b)], .08f, 8.f);
        }
        updateTables(bandT60);

        const int mode = std::clamp(settings.mode, 0, 2);
        static constexpr std::array<float, 3> overSubtraction{1.f, 1.6f, .7f}, release{.55f, .35f, .75f}, floorOffset{0.f, -6.f, 6.f};
        const float alpha = overSubtraction[size_t(mode)] * std::pow(2.f, (std::clamp(settings.sensitivity, 0.f, 100.f) - 50.f) / 50.f);
        // 50% removes the modelled component once; 100% over-subtracts it twice.
        const float wEarly = std::clamp(settings.early, 0.f, 100.f) * .02f, wLate = std::clamp(settings.tail, 0.f, 100.f) * .02f;
        const float amount = std::clamp(settings.reduction, 0.f, 100.f) * .01f;
        const float floorDb = std::min(-1.f, -36.f + .33f * std::clamp(settings.ambience, 0.f, 100.f) + floorOffset[size_t(mode)]);
        const float floorPower = std::pow(10.f, floorDb * .1f);
        const float clarity = std::clamp(settings.clarity, 0.f, 100.f) * .01f;
        const float down = release[size_t(mode)] * (1 - .4f * clarity), up = .25f * (1 - clarity);
        const int stereo = std::clamp(settings.stereo, 0, 2);
        const size_t slot = size_t(frames % uint64_t(delayFrames));
        double before = 0, after = 0;

        for (int c = 0; c < 2; ++c) {
            const bool processed = stereo == 2 || stereo == c;
            auto& ps = smooth[c]; auto& hist = history[c]; auto& g = gain[c]; auto& gPrev = previousGain[c];
            for (int k = 0; k < bins; ++k) {
                const auto [mk, sk] = separate(k);
                const float power = std::norm(c == 0 ? mk : sk);
                const float previousSmooth = ps[size_t(k)];
                const float delayed = hist[slot * size_t(bins) + size_t(k)]; // Psmooth from delayFrames ago
                const float newSmooth = .5f * previousSmooth + .5f * power;
                hist[slot * size_t(bins) + size_t(k)] = newSmooth;
                ps[size_t(k)] = newSmooth;
                if (!processed) { g[size_t(k)] = 1; continue; }
                const float reverbPower = alpha * (wLate * decayLate[size_t(k)] * delayed + wEarly * decayEarly[size_t(k)] * previousSmooth);
                float powerGain = std::max(floorPower, 1 - reverbPower / (power + 1.e-12f));
                float target = std::sqrt(powerGain);
                // Onset protection (Clarity): rising energy is direct sound.
                const float onset = power / (previousSmooth + 1.e-12f);
                target += clarity * std::clamp((onset - 2.f) / 6.f, 0.f, 1.f) * (1 - target);
                const float coefficient = target > gPrev[size_t(k)] ? up : down;
                const float smoothed = target + (gPrev[size_t(k)] - target) * coefficient;
                gPrev[size_t(k)] = smoothed;
                g[size_t(k)] = smoothed;
            }
            if (!processed) continue;
            // Frequency smoothing (3 bins; 5 in transparent mode).
            const int radius = mode == 2 ? 2 : 1;
            for (int k = 0; k < bins; ++k) {
                float total = 0; int count = 0;
                for (int j = std::max(0, k - radius); j <= std::min(bins - 1, k + radius); ++j) { total += g[size_t(j)]; ++count; }
                smoothedGain[size_t(k)] = total / float(count);
            }
            for (int k = 0; k < bins; ++k) g[size_t(k)] = 1 - amount * focusWeight[size_t(k)] * (1 - smoothedGain[size_t(k)]);
        }
        // Apply real, symmetric gains to M and S and rebuild the packed spectrum.
        for (int k = 0; k < bins; ++k) {
            const auto [mk, sk] = separate(k);
            const std::complex<float> m = mk * gain[0][size_t(k)], s = sk * gain[1][size_t(k)];
            before += double(std::norm(mk) + std::norm(sk));
            after += double(std::norm(m) + std::norm(s));
            const std::complex<float> j(0, 1);
            const auto yk = m + j * s;
            const auto ynk = std::conj(m) + j * std::conj(s);
            spectrumOut(k, yk, ynk);
        }
        commitSpectrum();
        fft->transform(frame.data(), true);
        constexpr float synthesisNorm = 1.f / 1.5f; // periodic Hann^2 at 75% overlap sums to 1.5
        const uint64_t base = position - uint64_t(size);
        for (int n = 0; n < size; ++n) {
            const size_t idx = size_t((base + uint64_t(n)) % uint64_t(size));
            const float w = window[size_t(n)] * synthesisNorm;
            accumulator[0][idx] += frame[size_t(n)].real() * w;
            accumulator[1][idx] += frame[size_t(n)].imag() * w;
        }
        const float measured = before > 1.e-14 ? float(10 * std::log10(std::max(after, 1.e-30) / before)) : 0.f;
        lastReductionDb = std::clamp(measured, -60.f, 0.f);
        ++frames;
    }
    std::pair<std::complex<float>, std::complex<float>> separate(int k) const noexcept {
        const auto xk = spectrumIn(k), xn = spectrumIn((size - k) % size);
        const auto m = .5f * (xk + std::conj(xn));
        const auto s = std::complex<float>(0, -.5f) * (xk - std::conj(xn));
        return {m, s};
    }
    // The forward spectrum is read from `frame` while the output spectrum is
    // staged in `staged` and copied back before the inverse transform.
    std::complex<float> spectrumIn(int k) const noexcept { return frame[size_t(k)]; }
    void spectrumOut(int k, std::complex<float> yk, std::complex<float> ynk) noexcept {
        staged[size_t(k)] = yk; staged[size_t((size - k) % size)] = ynk;
    }
    void commitSpectrum() noexcept { std::copy(staged.begin(), staged.begin() + size, frame.begin()); }

    double sr = 48000; int size = 1024, hop = 256, bins = 513, delayFrames = 10, hopCounter = 0; uint64_t position = 0, frames = 0;
    bool ready = false, resetLatch = false, learnedValid = false; int learnedUpdates = 0;
    std::unique_ptr<reverb::PreparedFFT> fft;
    std::vector<float> window;
    std::vector<std::complex<float>> frame;
    std::vector<std::complex<float>> staged;
    std::array<std::vector<float>, 2> input, accumulator, dry, smooth, previousGain, gain, history;
    std::vector<float> smoothedGain, decayLate, decayEarly, focusWeight;
    std::vector<int> bandOf;
    std::array<float, bands> estimate{}, learned{}, previousDb{}, runStartDb{}, recentMax{};
    std::array<int, bands> run{};
    float lastFocus = -1, lastRoom = -1, lastDamping = -1, lastReductionDb = 0, lastT60 = 0, presenceDb = -1; int lastAlgorithm = -1;
    DeReverbSettings settings;
    Biquad presenceFilter;
    Smoothed mixRamp, outRamp, bypassRamp, presenceRamp;
};

} // namespace hem::studio
