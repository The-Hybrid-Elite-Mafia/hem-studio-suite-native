#include "PitchTimeBackend.h"
#include <signalsmith-stretch.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace hem::pitchtime {
Result render(const Settings& s, std::int64_t frames, const Reader& read, const Writer& write, const Cancel& cancel, const CorrectionObserver& observe) {
    Result r;
    // Bounded configuration, long-file memory independent of duration. Limit
    // source to one week to make all cumulative frame arithmetic exact/safe.
    if (!read || !write || frames <= 0 || !std::isfinite(s.sampleRate) || s.sampleRate < 8000 || s.sampleRate > 192000
        || frames > std::int64_t(s.sampleRate * 604800) || s.channels < 1 || s.channels > 2
        || !std::isfinite(s.durationRatio) || s.durationRatio < .5 || s.durationRatio > 2
        || !std::isfinite(s.pitchSemitones) || std::abs(s.pitchSemitones) > 24
        || !std::isfinite(s.formantSemitones) || std::abs(s.formantSemitones) > 12
        || !std::isfinite(s.formantBaseHz) || s.formantBaseHz < 0 || s.formantBaseHz > 2000
        || !std::isfinite(s.outputDb) || s.outputDb < -24 || s.outputDb > 12
        || !std::isfinite(s.wetMix) || s.wetMix < 0 || s.wetMix > 1 || (s.wetMix < 1 && s.durationRatio != 1)
        || !s.correction.valid()) return r;
    double window = 0, hop = 0;
    switch (s.strategy) {
        case Strategy::tonal: window = .12; hop = .02; break; // frequency resolution
        case Strategy::rhythmic: window = .05; hop = .005; break; // temporal resolution
        case Strategy::mixed: window = .08; hop = .01; break;
        default: return r;
    }
    signalsmith::stretch::SignalsmithStretch<float> engine(0);
    engine.configure(s.channels, int(std::lround(s.sampleRate * window)), int(std::lround(s.sampleRate * hop)), false);
    engine.setTransposeSemitones(float(s.pitchSemitones));
    engine.setFormantSemitones(float(s.formantSemitones), s.preserveFormants);
    engine.setFormantBase(float(s.formantBaseHz / s.sampleRate));
    r.inputLatency = engine.inputLatency(); r.outputLatency = engine.outputLatency();
    r.windowFrames = engine.blockSamples(); r.hopFrames = engine.intervalSamples();
    r.outputFrames = std::llround(double(frames) * s.durationRatio);
    constexpr int quantum = 512; // stable internal schedule, independent of host I/O blocks
    const int capacity = std::max({r.inputLatency, r.outputLatency, quantum * 2 + 2});
    std::array<std::vector<float>, 2> in, out, dry;
    std::array<float*, 2> ip{}, op{}, dp{};
    const double outputGain = std::pow(10., s.outputDb/20.);
    for (int c = 0; c < s.channels; ++c) {
        in[c].resize(capacity); out[c].resize(capacity);
        ip[c] = in[c].data(); op[c] = out[c].data();
        if (s.wetMix < 1) { dry[c].resize(capacity); dp[c] = dry[c].data(); }
    }
    MonophonicDetector detector;
    MonophonicCorrection correction;
    const int analysisFrames = int(std::ceil(s.sampleRate*.08));
    std::array<std::vector<float>, 2> analysis;
    std::array<float*, 2> ap{};
    std::array<const float*, 2> ac{};
    if (s.correction.enabled || observe) for (int c = 0; c < s.channels; ++c) {
        analysis[c].resize(analysisFrames); ap[c] = analysis[c].data(); ac[c] = ap[c];
    }
    auto cancelled = [&] { if (cancel && cancel()) { r.status = Result::Status::cancelled; return true; } return false; };
    auto fill = [&](std::int64_t offset, int count) {
        for (int c = 0; c < s.channels; ++c) std::fill_n(ip[c], count, 0.f);
        const int available = int(std::min<std::int64_t>(count, std::max<std::int64_t>(0, frames - offset)));
        if (available && !read(offset, ip.data(), available)) { r.status = Result::Status::readFailed; return false; }
        for (int c = 0; c < s.channels; ++c) for (int i = 0; i < available; ++i)
            if (!std::isfinite(ip[c][i])) { r.status = Result::Status::nonFinite; return false; }
        // Process stereo in mid/side coordinates so a centred or anti-phase
        // source remains exactly on that axis even during spectral estimation.
        if (s.channels == 2) for (int i = 0; i < count; ++i) {
            const float l = ip[0][i], rr = ip[1][i];
            ip[0][i] = .5f*l + .5f*rr; ip[1][i] = .5f*l - .5f*rr;
        }
        return true;
    };
    std::int64_t generated = 0;
    auto emit = [&](int count) {
        if (s.channels == 2) for (int i = 0; i < count; ++i) {
            const float mid = op[0][i], side = op[1][i];
            op[0][i] = mid + side; op[1][i] = mid - side;
        }
        const int skip = int(std::clamp<std::int64_t>(r.outputLatency - generated, 0, count));
        const int keep = int(std::min<std::int64_t>(count - skip, r.outputFrames - r.writtenFrames));
        if (keep && s.wetMix < 1 && !read(r.writtenFrames, dp.data(), keep)) {
            r.status = Result::Status::readFailed; return false;
        }
        std::array<const float*, 2> ptr{};
        for (int c = 0; c < s.channels; ++c) {
            ptr[c] = op[c] + skip;
            for (int i = 0; i < keep; ++i) {
                const double wet = op[c][skip+i], original = s.wetMix < 1 ? dp[c][i] : 0;
                if (!std::isfinite(wet) || !std::isfinite(original)) { r.status = Result::Status::nonFinite; return false; }
                op[c][skip+i] = float((s.wetMix*wet+(1-s.wetMix)*original)*outputGain);
                if (!std::isfinite(op[c][skip+i])) { r.status = Result::Status::nonFinite; return false; }
            }
        }
        if (keep && !write(ptr.data(), keep)) { r.status = Result::Status::writeFailed; return false; }
        r.writtenFrames += keep; generated += count; return true;
    };
    if (cancelled() || !fill(0, r.inputLatency)) return r;
    engine.seek(ip.data(), r.inputLatency, 1 / s.durationRatio);
    std::int64_t consumed = 0, produced = 0;
    while (produced < r.outputFrames) {
        if (cancelled()) return r;
        const int nout = int(std::min<std::int64_t>(quantum, r.outputFrames - produced));
        const auto next = std::llround(double(produced + nout) * double(frames) / double(r.outputFrames));
        const int nin = int(next - consumed);
        if (s.correction.enabled || observe) {
            const auto centre = consumed + nin/2;
            const auto start = centre - analysisFrames/2;
            const int leading = int(std::max<std::int64_t>(0, -start));
            const auto offset = std::max<std::int64_t>(0, start);
            const int available = int(std::min<std::int64_t>(analysisFrames-leading, std::max<std::int64_t>(0, frames-offset)));
            std::array<float*, 2> destinations{};
            for (int c = 0; c < s.channels; ++c) {
                std::fill(analysis[c].begin(), analysis[c].end(), 0.f);
                destinations[c] = ap[c]+leading;
            }
            if (available && !read(offset, destinations.data(), available)) { r.status = Result::Status::readFailed; return r; }
            for (int c = 0; c < s.channels; ++c) for (int i = 0; i < analysisFrames; ++i)
                if (!std::isfinite(ap[c][i])) { r.status = Result::Status::nonFinite; return r; }
            const auto estimate = detector.analyse(ac.data(), s.channels, analysisFrames, s.sampleRate);
            const auto decision = correction.update(estimate, s.correction, nout/s.sampleRate);
            engine.setTransposeSemitones(float(s.pitchSemitones + decision.correctionSemitones));
            if (observe) observe(centre, decision);
        }
        if (!fill(r.inputLatency + consumed, nin)) return r;
        engine.process(ip.data(), nin, op.data(), nout);
        if (!emit(nout)) return r;
        consumed = next; produced += nout;
    }
    if (cancelled()) return r;
    engine.flush(op.data(), r.outputLatency, float(1 / s.durationRatio));
    if (!emit(r.outputLatency)) return r;
    r.status = r.writtenFrames == r.outputFrames ? Result::Status::complete : Result::Status::writeFailed;
    return r;
}
}


