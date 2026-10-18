#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace hem::pitchtime {
enum class Scale { chromatic, major, naturalMinor };
struct CorrectionSettings {
    bool enabled = false;
    int key = 0; // C=0 ... B=11
    Scale scale = Scale::chromatic;
    double amount = 1, speedMs = 80;
    bool valid() const {
        return key >= 0 && key <= 11 && (scale == Scale::chromatic || scale == Scale::major || scale == Scale::naturalMinor)
            && std::isfinite(amount) && amount >= 0 && amount <= 1
            && std::isfinite(speedMs) && speedMs >= 0 && speedMs <= 1000;
    }
};
struct PitchEstimate {
    double hz = 0, confidence = 0, rms = 0;
    bool voiced = false;
    int detectorChannel = 0;
};

// Bounded offline monophonic YIN-style difference detector. Analyse 80 ms of
// original source, never already transposed audio. Select the louder L/R channel
// (left on ties) to avoid anti-phase cancellation; apply one result to both.
// This cannot establish that arbitrary audio is monophonic. Harmonic mixtures
// can resemble a periodic single voice: host must label monophonic-only scope.
class MonophonicDetector {
public:
    PitchEstimate analyse(const float* const* audio, int channels, int frames, double sampleRate) {
        PitchEstimate result;
        if (!audio || channels < 1 || channels > 2 || !std::isfinite(sampleRate)
            || sampleRate < 8000 || sampleRate > 192000 || frames < int(sampleRate*.04)
            || frames > int(std::ceil(sampleRate*.08))) return result;
        double energies[2]{};
        for (int c = 0; c < channels; ++c) {
            if (!audio[c]) return result;
            for (int i = 0; i < frames; ++i) {
                if (!std::isfinite(audio[c][i])) return result;
                energies[c] += double(audio[c][i])*audio[c][i];
            }
        }
        result.detectorChannel = channels == 2 && energies[1] > energies[0] ? 1 : 0;
        // Group-averaging bounds analysis rate and attenuates high frequencies.
        // Not a general resampler or an antialiasing-quality claim.
        const int stride = int(std::ceil(sampleRate/12000));
        const double rate = sampleRate/stride;
        const int n = frames/stride;
        if (n > int(samples.size()) || n < 100) return result;
        const float* source = audio[result.detectorChannel];
        double mean = 0;
        for (int i = 0; i < n; ++i) {
            double value = 0;
            for (int j = 0; j < stride; ++j) value += source[i*stride+j];
            samples[i] = value/stride; mean += samples[i];
        }
        mean /= n;
        double energy = 0;
        for (int i = 0; i < n; ++i) { samples[i] -= mean; energy += samples[i]*samples[i]; }
        result.rms = std::sqrt(energy/n);
        if (result.rms < .0001) return result; // -80 dBFS analysis noise floor
        const int minLag = std::max(2, int(rate/1200));
        const int maxLag = std::min(int(rate/60), n/3);
        const int span = n - maxLag;
        double cumulative = 0;
        norm[0] = 1;
        for (int lag = 1; lag <= maxLag; ++lag) {
            double difference = 0;
            for (int i = 0; i < span; ++i) { const double d = samples[i]-samples[i+lag]; difference += d*d; }
            cumulative += difference;
            norm[lag] = cumulative > 1e-20 ? difference*lag/cumulative : 1;
        }
        int chosen = 0;
        for (int lag = minLag; lag < maxLag; ++lag) {
            if (norm[lag] < .15) {
                while (lag + 1 <= maxLag && norm[lag+1] < norm[lag]) ++lag;
                chosen = lag; break;
            }
        }
        if (!chosen) return result;
        double refined = chosen;
        if (chosen > 1 && chosen < maxLag) {
            const double denominator = norm[chosen-1] - 2*norm[chosen] + norm[chosen+1];
            if (std::abs(denominator) > 1e-12)
                refined += std::clamp(.5*(norm[chosen-1]-norm[chosen+1])/denominator, -.5, .5);
        }
        result.hz = rate/refined;
        // Average many periods to remove small parabolic-lag bias at high F0.
        // Only use crossings when they agree with the periodicity estimate;
        // overtone crossings must never select a different octave.
        double firstCrossing = 0, lastCrossing = 0;
        int crossings = 0;
        for (int i = 1; i < n; ++i) if (samples[i-1] <= 0 && samples[i] > 0) {
            const double position = i-1 - samples[i-1]/(samples[i]-samples[i-1]);
            if (crossings++ == 0) firstCrossing = position;
            lastCrossing = position;
        }
        if (crossings >= 3 && lastCrossing > firstCrossing) {
            const double crossingHz = rate*(crossings-1)/(lastCrossing-firstCrossing);
            if (std::abs(crossingHz/result.hz-1) < .03) result.hz = crossingHz;
        }
        result.confidence = std::clamp(1-norm[chosen], 0., 1.);
        result.voiced = result.hz >= 60 && result.hz <= 1200 && result.confidence >= .85;
        if (!result.voiced) result.hz = 0;
        return result;
    }
private:
    std::array<double, 1024> samples{}, norm{};
};

struct CorrectionFrame {
    PitchEstimate input;
    double inputMidi = 0, targetMidi = 0, correctionSemitones = 0;
    bool targetValid = false; // TARGET is a prediction, never measured output.
};
class MonophonicCorrection {
public:
    void reset() { previousTarget = -1; correction = 0; }
    CorrectionFrame update(const PitchEstimate& pitch, const CorrectionSettings& settings, double elapsedSeconds) {
        CorrectionFrame result; result.input = pitch;
        if (pitch.voiced && std::isfinite(pitch.hz) && pitch.hz >= 60 && pitch.hz <= 1200)
            result.inputMidi = 69 + 12*std::log2(pitch.hz/440);
        if (!settings.valid() || !settings.enabled || !pitch.voiced || !std::isfinite(pitch.hz)
            || pitch.hz < 60 || pitch.hz > 1200 || !std::isfinite(pitch.confidence) || pitch.confidence < .85
            || !std::isfinite(elapsedSeconds) || elapsedSeconds <= 0) {
            reset(); return result; // no stale quantization on consonants/silence
        }
        int target = -1; double nearest = std::numeric_limits<double>::infinity();
        for (int note = 0; note <= 127; ++note) {
            if (!allowed(note, settings)) continue;
            const double distance = std::abs(note-result.inputMidi);
            if (distance < nearest) { nearest = distance; target = note; } // lower note wins exact tie
        }
        // Retain previous note only inside a 10-cent hysteresis margin.
        if (previousTarget >= 0 && allowed(previousTarget, settings)
            && std::abs(previousTarget-result.inputMidi) <= nearest+.1) target = previousTarget;
        previousTarget = target;
        const double requested = (target-result.inputMidi)*settings.amount;
        const double alpha = settings.speedMs == 0 ? 1 : -std::expm1(-elapsedSeconds*1000/settings.speedMs);
        correction += alpha*(requested-correction);
        result.targetValid = true; result.targetMidi = target;
        result.correctionSemitones = correction;
        return result;
    }
private:
    static bool allowed(int note, const CorrectionSettings& settings) {
        if (settings.scale == Scale::chromatic) return true;
        const int degree = (note-settings.key+120)%12;
        constexpr unsigned major = (1<<0)|(1<<2)|(1<<4)|(1<<5)|(1<<7)|(1<<9)|(1<<11);
        constexpr unsigned minor = (1<<0)|(1<<2)|(1<<3)|(1<<5)|(1<<7)|(1<<8)|(1<<10);
        return ((settings.scale == Scale::major ? major : minor) & (1u<<degree)) != 0;
    }
    int previousTarget = -1;
    double correction = 0;
};
}
