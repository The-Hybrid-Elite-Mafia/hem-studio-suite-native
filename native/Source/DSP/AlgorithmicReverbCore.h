#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <vector>

namespace hem {

// Units are the faceplate's engineering units, not normalized parameter values.
struct AlgorithmicReverbSettings {
    float size = 100.0f;       // 25 .. 200 percent
    float decay = 2.0f;        // 0.1 .. 20 seconds, nominal low-frequency RT60
    float predelay = 0.0f;     // 0 .. 250 milliseconds, wet path only
    float diffusion = 70.0f;   // 0 .. 100 percent
    float modDepth = 0.0f;     // 0 .. 10 milliseconds
    float modRate = 0.5f;      // 0.01 .. 5 Hz
    float damping = 8000.0f;   // 500 .. 20000 Hz
    float mix = 0.0f;          // 0 .. 100 percent
    float output = 0.0f;       // -24 .. +6 dB
    bool soloWet = false;
    bool bypass = false;
    int algorithm = 0;        // 0 Hall, 1 Room, 2 Plate
};

// Eight-line stereo FDN. Storage is allocated only in prepare(), never in the
// audio callback. The three algorithms have different delay distributions,
// orthogonal feedback matrices, diffusion tuning, and early reflections.
// No dependency on JUCE, the master limiter, or the host's block size.
class AlgorithmicReverbCore {
public:
    void prepare(double sampleRate) {
        sr = std::isfinite(sampleRate) ? std::clamp(sampleRate, 8000.0, 192000.0) : 48000.0;
        for (auto& d : network) d.prepare(std::size_t(std::ceil(sr * 0.19)) + 8);
        for (auto& d : pre) d.prepare(std::size_t(std::ceil(sr * 0.36)) + 8);
        for (auto& channel : diffusers)
            for (auto& d : channel) d.prepare(std::size_t(std::ceil(sr * 0.035)) + 8);
        prepared = true;
        reset();
    }

    // Call on the audio thread or while processing is suspended. Concurrent
    // prepare/reset and process is deliberately not supported.
    void reset() noexcept {
        for (auto& d : network) d.reset();
        for (auto& d : pre) d.reset();
        for (auto& channel : diffusers) for (auto& d : channel) d.reset();
        dampState.fill(0.0f);
        feedback.fill(0.0f);
        rotationCos.fill(1.0f);
        rotationSin.fill(0.0f);
        for (std::size_t i = 0; i < 8; ++i) {
            const double angle = twoPi * (double(i) + 0.25) / 8.0;
            lfoSin[i] = float(std::sin(angle));
            lfoCos[i] = float(std::cos(angle));
        }
        initialized = false;
        controlPhase = 0;
        oscillatorPhase = 0;
        energy = 0.0f;
        peak.store(0.0f, std::memory_order_relaxed);
        wetPeak.store(0.0f, std::memory_order_relaxed);
        tailLevelDb.store(-120.0f, std::memory_order_relaxed);
    }

    constexpr int latency() const noexcept { return 0; }

    // A conservative -90 dB tail estimate. Damping generally shortens this;
    // predelay and diffusion/network travel time are included. Mix/bypass do
    // not change the available internal tail or the host's tail estimate.
    static double tailSeconds(const AlgorithmicReverbSettings& p) noexcept {
        return double(clean(p.predelay, 0.0f, 250.0f, 0.0f)) * 0.001
             + double(clean(p.decay, 0.1f, 20.0f, 2.0f)) * 1.5
             + double(clean(p.size, 25.0f, 200.0f, 100.0f)) * 0.0015 + 0.1;
    }

    void process(float* left, float* right, int samples,
                 const AlgorithmicReverbSettings& p) noexcept {
        if (left == nullptr || samples <= 0) return;
        const bool stereo = right != nullptr && right != left;
        if (!prepared) {
            float maximum = 0.0f;
            for (int i = 0; i < samples; ++i) {
                left[i] = finiteInput(left[i]);
                maximum = std::max(maximum, std::abs(left[i]));
                if (stereo) {
                    right[i] = finiteInput(right[i]);
                    maximum = std::max(maximum, std::abs(right[i]));
                }
            }
            peak.store(maximum, std::memory_order_relaxed);
            return;
        }

        const auto targets = settingsTargets(p);
        const int rampSamples = std::max(1, int(sr * 0.04));
        if (!initialized) bypassRamp.snap(p.bypass ? 1.0f : 0.0f);
        else bypassRamp.set(p.bypass ? 1.0f : 0.0f, std::max(1, int(sr * 0.01)));
        for (std::size_t j = 0; j < controls.size(); ++j) {
            if (!initialized) controls[j].snap(targets[j]);
            else controls[j].set(targets[j], rampSamples);
        }
        if (!initialized) controlPhase = 0;
        initialized = true;
        float maximum = 0.0f, maximumWet = 0.0f;
        const float energyAlpha = float(1.0 - std::exp(-1.0 / (sr * 0.05)));
        for (int n = 0; n < samples; ++n) {
            std::array<float, 12> c{};
            for (std::size_t j = 0; j < c.size(); ++j) c[j] = controls[j].next();
            for (std::size_t j = 9; j < 12; ++j) c[j] = std::clamp(c[j], 0.0f, 1.0f);
            const float modeSum = c[9]+c[10]+c[11];
            for (std::size_t j = 9; j < 12; ++j) c[j] /= modeSum;
            const float bypassAmount = bypassRamp.next();
            if (controlPhase == 0) updateCoefficients(c);
            controlPhase = (controlPhase + 1u) & 15u;

            const float dryL = finiteInput(left[n]);
            const float dryR = stereo ? finiteInput(right[n]) : dryL;
            // Headroom fences protect the recursive state from hostile input;
            // the normal dry path is untouched, including when bypassed.
            const float inputL = p.bypass ? 0.0f : std::clamp(dryL, -8.0f, 8.0f);
            const float inputR = p.bypass ? 0.0f : std::clamp(dryR, -8.0f, 8.0f);
            const float preSamples = float(sr * 0.001) * c[2];
            float diffuseL = pre[0].readWithCurrent(inputL, preSamples);
            float diffuseR = pre[1].readWithCurrent(inputR, preSamples);
            const float earlyScale = float(sr * 0.001) * c[0];
            const float early1 = (14.0f*c[9] + 4.3f*c[10] + 2.7f*c[11]) * earlyScale;
            const float early2 = (29.0f*c[9] + 9.1f*c[10] + 5.1f*c[11]) * earlyScale;
            const float early3 = (41.0f*c[9] + 17.7f*c[10] + 8.3f*c[11]) * earlyScale;
            const float earlyL = 0.5f * (pre[0].read(preSamples + early1)
                                      + 0.6f*pre[1].read(preSamples + early2)
                                      + 0.35f*pre[0].read(preSamples + early3));
            const float earlyR = 0.5f * (pre[1].read(preSamples + early1*1.13f)
                                      + 0.6f*pre[0].read(preSamples + early2*0.89f)
                                      - 0.35f*pre[1].read(preSamples + early3*1.07f));
            pre[0].write(inputL);
            pre[1].write(inputR);

            const float diffusionGain = c[3] * (0.68f*c[9] + 0.5f*c[10] + 0.74f*c[11]);
            const float diffuserScale = c[0] * (c[9] + 0.57f*c[10] + 0.81f*c[11]);
            for (std::size_t j = 0; j < 4; ++j) {
                diffuseL = allpass(diffusers[0][j], diffuseL, diffuserMs[j] * float(sr*0.001) * diffuserScale, diffusionGain);
                diffuseR = allpass(diffusers[1][j], diffuseR, diffuserMs[j] * float(sr*0.001) * diffuserScale * 1.113f, diffusionGain);
            }

            std::array<float, 8> delayed{}, damped{};
            for (std::size_t j = 0; j < 8; ++j) {
                const float delayMs = c[0] * (hallMs[j]*c[9] + roomMs[j]*c[10] + plateMs[j]*c[11]);
                // A bounded fractional delay avoids read-head collisions at
                // the smallest sizes, even at maximum modulation depth.
                const float depthMs = std::min(c[4], delayMs*0.35f);
                const float delay = float(sr * 0.001) * (delayMs + depthMs*lfoSin[j]);
                delayed[j] = network[j].read(delay);
                dampState[j] = zap(dampState[j] + dampingAlpha*(delayed[j] - dampState[j]));
                damped[j] = dampState[j] * feedback[j];
                const float oldSin = lfoSin[j];
                lfoSin[j] = oldSin*rotationCos[j] + lfoCos[j]*rotationSin[j];
                lfoCos[j] = lfoCos[j]*rotationCos[j] - oldSin*rotationSin[j];
            }
            if (++oscillatorPhase == 4096u) {
                oscillatorPhase = 0;
                for (std::size_t j = 0; j < 8; ++j) {
                    const float inverseLength = 1.0f / std::sqrt(std::max(0.0001f, lfoSin[j]*lfoSin[j] + lfoCos[j]*lfoCos[j]));
                    lfoSin[j] *= inverseLength;
                    lfoCos[j] *= inverseLength;
                }
            }

            const auto hall = hadamard(damped);
            float sum = 0.0f;
            for (float x : damped) sum += x;
            std::array<float, 8> plate{};
            for (std::size_t j = 0; j < 8; j += 2) {
                const float a = damped[(j+3u)&7u], b = damped[(j+4u)&7u];
                plate[j] = (a+b)*inverseSqrt2;
                plate[j+1] = (a-b)*inverseSqrt2;
            }
            float wetL = 0.0f, wetR = 0.0f;
            for (std::size_t j = 0; j < 8; ++j) {
                const float room = damped[j] - sum*0.25f;
                const float scattered = hall[j]*c[9] + room*c[10] + plate[j]*c[11];
                const float injected = (diffuseL*inputSignL[j] + diffuseR*inputSignR[j])*inverseSqrt8;
                network[j].write(fence(scattered + injected));
                wetL += delayed[j]*outputSignL[j]*inverseSqrt8;
                wetR += delayed[j]*outputSignR[j]*inverseSqrt8;
            }
            const float earlyGain = 0.12f*c[9] + 0.28f*c[10] + 0.055f*c[11];
            wetL = fence(wetL*0.7f + earlyL*earlyGain);
            wetR = fence(wetR*0.7f + earlyR*earlyGain);
            if (!stereo) wetL = (wetL + wetR)*0.5f;
            maximumWet = std::max({maximumWet, std::abs(wetL), std::abs(wetR)});
            const float wetEnergy = (wetL*wetL + wetR*wetR)*0.5f;
            energy = zap(energy + energyAlpha*(wetEnergy-energy));

            if (bypassAmount == 1.0f) {
                left[n] = dryL;
                if (stereo) right[n] = dryR;
            } else {
                const float active = 1.0f-bypassAmount;
                const float amount = c[7]*active;
                const float outputGain = 1.0f + (c[8]-1.0f)*active;
                left[n] = blend(dryL, wetL, amount, outputGain);
                if (stereo) right[n] = blend(dryR, wetR, amount, outputGain);
            }
            maximum = std::max(maximum, std::abs(left[n]));
            if (stereo) maximum = std::max(maximum, std::abs(right[n]));
        }
        peak.store(maximum, std::memory_order_relaxed);
        wetPeak.store(maximumWet, std::memory_order_relaxed);
        tailLevelDb.store(std::clamp(10.0f*std::log10(std::max(energy, 1.0e-12f)), -120.0f, 48.0f), std::memory_order_relaxed);
    }

    std::atomic<float> peak{0.0f};
    std::atomic<float> wetPeak{0.0f};
    // Measured 50 ms wet RMS level, not a synthesized or predicted decay curve.
    std::atomic<float> tailLevelDb{-120.0f};

private:
    static_assert(std::atomic<float>::is_always_lock_free, "Audio telemetry needs lock-free float atomics");
    static constexpr double twoPi = 6.28318530717958647692;
    static constexpr float inverseSqrt8 = 0.35355339059327376220f;
    static constexpr float inverseSqrt2 = 0.70710678118654752440f;
    static constexpr std::array<float,8> hallMs{29.7f, 37.1f, 41.1f, 43.7f, 53.1f, 61.7f, 71.3f, 79.7f};
    static constexpr std::array<float,8> roomMs{11.3f, 13.7f, 17.9f, 19.7f, 23.9f, 29.3f, 31.7f, 37.1f};
    static constexpr std::array<float,8> plateMs{7.7f, 12.3f, 17.3f, 23.3f, 31.1f, 37.9f, 43.3f, 47.9f};
    static constexpr std::array<float,4> diffuserMs{2.3f, 3.7f, 5.3f, 7.1f};
    static constexpr std::array<float,8> lfoRatios{0.73f,0.83f,0.91f,1.0f,1.07f,1.17f,1.29f,1.37f};
    static constexpr std::array<float,8> inputSignL{1,1,-1,-1,1,1,-1,-1};
    static constexpr std::array<float,8> inputSignR{1,-1,1,-1,1,-1,1,-1};
    static constexpr std::array<float,8> outputSignL{1,1,1,1,-1,-1,-1,-1};
    static constexpr std::array<float,8> outputSignR{1,-1,-1,1,1,-1,-1,1};

    struct Delay {
        std::vector<float> data;
        std::size_t position = 0;
        void prepare(std::size_t length) { data.assign(length, 0.0f); position = 0; }
        void reset() noexcept { std::fill(data.begin(), data.end(), 0.0f); position = 0; }
        float read(float delay) const noexcept {
            delay = std::clamp(delay, 1.0f, float(data.size()-2));
            const auto whole = std::size_t(delay);
            const float fraction = delay-float(whole);
            const std::size_t a = position >= whole ? position-whole : position+data.size()-whole;
            const std::size_t b = a == 0 ? data.size()-1 : a-1;
            return data[a] + fraction*(data[b]-data[a]);
        }
        float readWithCurrent(float current, float delay) const noexcept {
            return delay < 1.0f ? current + delay*(read(1.0f)-current) : read(delay);
        }
        void write(float value) noexcept {
            data[position] = value;
            if (++position == data.size()) position = 0;
        }
    };

    struct Ramp {
        float value = 0, target = 0, step = 0;
        int remaining = 0;
        void snap(float x) noexcept { value = target = x; step = 0; remaining = 0; }
        void set(float x, int length) noexcept {
            if (x == target) return;
            target = x; remaining = length; step = (target-value)/float(length);
        }
        float next() noexcept {
            if (remaining > 0) {
                value += step;
                if (--remaining == 0) value = target;
            }
            return value;
        }
    };

    static float clean(float value, float minimum, float maximum, float fallback) noexcept {
        return std::isfinite(value) ? std::clamp(value, minimum, maximum) : fallback;
    }
    static float finiteInput(float value) noexcept { return std::isfinite(value) ? value : 0.0f; }
    static float zap(float value) noexcept { return std::abs(value) < 1.0e-20f ? 0.0f : value; }
    static float fence(float value) noexcept { return zap(std::clamp(finiteInput(value), -64.0f, 64.0f)); }
    static float blend(float dry, float wet, float amount, float outputGain) noexcept {
        if (amount == 0.0f && outputGain == 1.0f) return dry; // bit-exact neutral insert
        // Double precision avoids overflow if a broken upstream processor
        // supplies finite FLT_MAX. Only this abnormal path reaches the fence.
        const double mixed = ((1.0-double(amount))*double(dry) + double(amount)*double(wet))*double(outputGain);
        return float(std::clamp(mixed, -1.0e6, 1.0e6));
    }
    static std::array<float,12> settingsTargets(const AlgorithmicReverbSettings& p) noexcept {
        const int mode = std::clamp(p.algorithm, 0, 2);
        return {clean(p.size,25,200,100)*0.01f, clean(p.decay,0.1f,20,2), clean(p.predelay,0,250,0),
                clean(p.diffusion,0,100,70)*0.01f, clean(p.modDepth,0,10,0), clean(p.modRate,0.01f,5,0.5f),
                clean(p.damping,500,20000,8000), p.soloWet ? 1.0f : clean(p.mix,0,100,0)*0.01f,
                std::pow(10.0f,clean(p.output,-24,6,0)*0.05f),
                mode == 0 ? 1.0f : 0.0f, mode == 1 ? 1.0f : 0.0f, mode == 2 ? 1.0f : 0.0f};
    }
    static float allpass(Delay& delay, float x, float time, float gain) noexcept {
        const float y = delay.read(time) - gain*x;
        delay.write(fence(x + gain*y));
        return fence(y);
    }
    static std::array<float,8> hadamard(std::array<float,8> values) noexcept {
        for (std::size_t stride = 1; stride < 8; stride *= 2)
            for (std::size_t base = 0; base < 8; base += 2*stride)
                for (std::size_t j = 0; j < stride; ++j) {
                    const float a = values[base+j], b = values[base+j+stride];
                    values[base+j] = a+b;
                    values[base+j+stride] = a-b;
                }
        for (auto& x : values) x *= inverseSqrt8;
        return values;
    }
    void updateCoefficients(const std::array<float,12>& c) noexcept {
        const double cutoff = std::min(double(c[6]), sr*0.45);
        dampingAlpha = float(1.0-std::exp(-twoPi*cutoff/sr));
        for (std::size_t j = 0; j < 8; ++j) {
            const float delayMs = c[0]*(hallMs[j]*c[9] + roomMs[j]*c[10] + plateMs[j]*c[11]);
            feedback[j] = float(std::exp(-6.907755278982137 * double(delayMs)*0.001/double(c[1])));
            const double theta = twoPi*double(c[5]*lfoRatios[j])/sr;
            rotationCos[j] = float(std::cos(theta));
            rotationSin[j] = float(std::sin(theta));
        }
    }

    double sr = 48000.0;
    bool prepared = false, initialized = false;
    std::array<Delay,8> network;
    std::array<Delay,2> pre;
    std::array<std::array<Delay,4>,2> diffusers;
    std::array<Ramp,12> controls{};
    Ramp bypassRamp{};
    std::array<float,8> dampState{}, feedback{}, lfoSin{}, lfoCos{}, rotationSin{}, rotationCos{};
    float dampingAlpha = 0.0f, energy = 0.0f;
    unsigned controlPhase = 0, oscillatorPhase = 0;
};

} // namespace hem
