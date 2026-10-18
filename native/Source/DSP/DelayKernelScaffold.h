#pragma once
// ISOLATED REVIEW SCAFFOLD. Not registered or integrated into the application.
// Fixed-setting wet echo primitive only: no UI, filters, modulation, smoothing,
// host timing, oversampling, APVTS or complete production module in this file.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <utility>
#include <vector>

namespace hem::delayScaffold {
inline constexpr double minimumRate = 8000.0;
inline constexpr double maximumRate = 384000.0;
inline constexpr double maximumHistorySeconds = 4.006;
inline constexpr double maximumFeedback = 0.95;
// Proposed revision: 16 would clip full-scale sustained input at 95% feedback.
inline constexpr double historyCeiling = 32.0;
inline constexpr double liveSafetyTailSeconds = 2500.0;
inline constexpr double residualAmplitude = 1.0e-9;

struct StereoSample { float left = 0, right = 0; };

class StereoHistory final {
public:
    // Only allocation site. Serialized, off the processing thread.
    void prepare(double sampleRate) {
        ready_ = false;
        if (!std::isfinite(sampleRate) || sampleRate < minimumRate || sampleRate > maximumRate)
            throw std::invalid_argument("Delay sample rate must be finite, 8000..384000 Hz");
        const auto maximumAge = static_cast<std::size_t>(std::ceil(sampleRate * maximumHistorySeconds));
        std::array<std::vector<float>, 2> staged;
        for (auto& channel : staged) channel.assign(maximumAge + 2, 0.0f);
        storage_ = std::move(staged);
        maximumAge_ = maximumAge;
        rate_ = sampleRate;
        invalidate();
        ready_ = true;
    }

    // O(1) logical reset: previously written storage cannot be read afterward.
    void invalidate() noexcept { position_ = 0; valid_ = 0; }
    bool ready() const noexcept { return ready_; }
    double sampleRate() const noexcept { return rate_; }
    std::size_t capacity() const noexcept { return storage_[0].size(); }
    std::size_t validSamples() const noexcept { return valid_; }

    StereoSample read(double samplesAgo) const noexcept {
        if (!ready_ || !std::isfinite(samplesAgo)) return {};
        samplesAgo = std::clamp(samplesAgo, 1.0, static_cast<double>(maximumAge_));
        const auto newer = static_cast<std::size_t>(std::floor(samplesAgo));
        const auto fraction = samplesAgo - static_cast<double>(newer);
        StereoSample result;
        result.left = interpolate(0, newer, fraction);
        result.right = interpolate(1, newer, fraction);
        return result;
    }

    // Double arithmetic -> one bounded float write, with explicit tiny flushing.
    // Returns whether nonlinear protection was invoked (not nonfinite cleanup).
    bool write(double left, double right) noexcept {
        if (!ready_) return false;
        const bool protectedSample = (std::isfinite(left) && std::abs(left) > historyCeiling)
                                  || (std::isfinite(right) && std::abs(right) > historyCeiling);
        storage_[0][position_] = stored(left);
        storage_[1][position_] = stored(right);
        if (++position_ == capacity()) position_ = 0;
        valid_ = std::min(valid_ + 1, capacity());
        return protectedSample;
    }

private:
    static float stored(double value) noexcept {
        if (!std::isfinite(value) || std::abs(value) < 1.0e-20) return 0;
        return static_cast<float>(std::clamp(value, -historyCeiling, historyCeiling));
    }
    float age(std::size_t channel, std::size_t samplesAgo) const noexcept {
        if (samplesAgo == 0 || samplesAgo > valid_) return 0;
        return storage_[channel][(position_ + capacity() - samplesAgo) % capacity()];
    }
    float interpolate(std::size_t channel, std::size_t newer, double fraction) const noexcept {
        const double a = age(channel, newer);
        const double b = age(channel, newer + 1);
        return static_cast<float>((1.0 - fraction) * a + fraction * b);
    }
    std::array<std::vector<float>, 2> storage_;
    std::size_t position_ = 0, valid_ = 0, maximumAge_ = 0;
    double rate_ = 0;
    bool ready_ = false;
};

// Deliberately raw: arbitrary changes to delay are NOT smoothed here.
// The future module must add the reviewed dual-head/slew controller.
class FixedEchoPrimitive final {
public:
    void prepare(double sampleRate) { history_.prepare(sampleRate); protectionEvents_ = 0; }
    void reset() noexcept { history_.invalidate(); protectionEvents_ = 0; }
    StereoSample tick(StereoSample input, double delaySamples, double feedback) noexcept {
        if (!history_.ready()) return {};
        feedback = std::isfinite(feedback) ? std::clamp(feedback, 0.0, maximumFeedback) : 0.0;
        const auto wet = history_.read(delaySamples);
        const double left = std::isfinite(input.left) ? input.left : 0.0;
        const double right = std::isfinite(input.right) ? input.right : 0.0;
        if (history_.write(left + feedback * wet.left, right + feedback * wet.right)) {
            if (protectionEvents_ != UINT64_MAX) ++protectionEvents_;
        }
        return wet;
    }
    std::uint64_t protectionEvents() const noexcept { return protectionEvents_; }
private:
    StereoHistory history_;
    std::uint64_t protectionEvents_ = 0;
};

// Frozen, fresh-history export ONLY. A live module's previous settings/history
// cannot be inferred from this value; it retains the conservative safety cap.
struct FrozenTailRequest {
    double effectiveDelaySeconds = 0.25; // include modulation maximum if active
    double feedback = 0;
    double postHistoryPeakGain = 1; // includes active tap summation, filters/color/output
    double finiteOutputSupportSeconds = 0; // FIR/dry-alignment support, conservatively
    bool filtersEnabled = false;
    double slowestEnabledFilterHz = 20;
    unsigned enabledFilterStages = 2; // caller supplies actual count, 1 or 2
    bool audibleWet = true; // only false for a proven frozen dry/bypassed/muted path
};
struct FrozenTailBound {
    bool valid = false;
    bool needsResidualTermination = false;
    std::uint64_t residualBoundSamples = 0; // fade may begin only AFTER this
    std::uint64_t samples = 0;
    double seconds = 0;
    unsigned feedbackWindows = 0;
};

inline FrozenTailBound frozenTail(const FrozenTailRequest& request, double sampleRate) noexcept {
    FrozenTailBound result;
    if (!std::isfinite(sampleRate) || sampleRate < minimumRate || sampleRate > maximumRate
        || !std::isfinite(request.effectiveDelaySeconds) || request.effectiveDelaySeconds <= 0
        || request.effectiveDelaySeconds > maximumHistorySeconds
        || !std::isfinite(request.feedback) || request.feedback < 0 || request.feedback > maximumFeedback
        || !std::isfinite(request.postHistoryPeakGain) || request.postHistoryPeakGain < 0
        || request.postHistoryPeakGain > 256
        || !std::isfinite(request.finiteOutputSupportSeconds) || request.finiteOutputSupportSeconds < 0
        || request.finiteOutputSupportSeconds > 0.01
        || (request.filtersEnabled && (!std::isfinite(request.slowestEnabledFilterHz)
            || request.slowestEnabledFilterHz < 20 || request.slowestEnabledFilterHz > 20000
            || request.enabledFilterStages < 1 || request.enabledFilterStages > 2))) return result;
    result.valid = true;
    if (!request.audibleWet || request.postHistoryPeakGain == 0) return result;
    const auto delaySamples = static_cast<std::uint64_t>(std::ceil(request.effectiveDelaySeconds * sampleRate));
    if (request.feedback > 0) {
        const double q = request.feedback * (1.0 + 1.0e-6);
        const double ratio = residualAmplitude / (4.0 * historyCeiling * request.postHistoryPeakGain);
        const double windows = ratio >= 1 ? 0 : std::ceil(std::log(ratio) / std::log(q));
        result.feedbackWindows = static_cast<unsigned>(std::max(0.0, windows));
    }
    result.needsResidualTermination = request.feedback > 0 || request.filtersEnabled;
    std::uint64_t filterSamples = 0;
    if (request.filtersEnabled) {
        // Captured, immutable coefficients. Bound one/two first-order state
        // transients by 4*A*(n+1)*a^(n-1), reserving epsilon/4 for them.
        const double amplitude = historyCeiling * request.postHistoryPeakGain;
        if (amplitude > residualAmplitude / 16) {
            constexpr double twoPi = 6.28318530717958647693;
            const double logPole = -twoPi * std::min(request.slowestEnabledFilterHz, 0.45 * sampleRate) / sampleRate;
            const double threshold = std::log(residualAmplitude) - std::log(16.0 * amplitude);
            auto settled = [&](std::uint64_t n) noexcept {
                const double polynomial = request.enabledFilterStages == 2 ? std::log(static_cast<double>(n) + 1) : 0;
                return polynomial + (static_cast<double>(n) - 1) * logPole <= threshold;
            };
            auto low = static_cast<std::uint64_t>(std::ceil(1 / -logPole)) + 1;
            auto high = static_cast<std::uint64_t>(std::ceil(sampleRate));
            if (!settled(high)) { result.valid = false; return result; }
            while (low < high) {
                const auto middle = low + (high - low) / 2;
                if (settled(middle)) high = middle; else low = middle + 1;
            }
            filterSamples = high;
        }
    }
    result.residualBoundSamples = delaySamples * (static_cast<std::uint64_t>(result.feedbackWindows) + 1)
                               + filterSamples
                               + static_cast<std::uint64_t>(std::ceil(request.finiteOutputSupportSeconds * sampleRate));
    result.samples = result.residualBoundSamples
                   + (result.needsResidualTermination ? static_cast<std::uint64_t>(std::ceil(0.02 * sampleRate)) : 0);
    result.seconds = static_cast<double>(result.samples) / sampleRate;
    // Invalid means contract/preflight failure, never a silent capped estimate.
    if (result.seconds > liveSafetyTailSeconds) result.valid = false;
    return result;
}

// Schedule primitive only. The future module must multiply ALL wet paths and
// invalidate history/fixed states when discardHistory is true. No allocations.
class FrozenSilenceSchedule final {
public:
    struct Decision { double wetGain = 1; bool discardHistory = false; };
    bool configure(FrozenTailBound bound) noexcept {
        bound.valid = bound.valid && bound.residualBoundSamples <= bound.samples
                   && (!bound.needsResidualTermination || bound.residualBoundSamples < bound.samples);
        bound_ = bound; silentSamples_ = 0;
        return bound.valid;
    }
    Decision advance(bool inputNonzero) noexcept {
        if (!bound_.valid) return {}; // invalid settings must fail preflight
        if (inputNonzero) { silentSamples_ = 0; return {}; }
        if (silentSamples_ != UINT64_MAX) ++silentSamples_;
        // Preserve the one-shot sample exactly at its final delay index.
        if (silentSamples_ > bound_.samples) return {0, true};
        if (!bound_.needsResidualTermination || silentSamples_ <= bound_.residualBoundSamples) return {};
        const auto fadeSamples = bound_.samples - bound_.residualBoundSamples;
        if (fadeSamples == 0) return {0, true};
        return {1.0 - static_cast<double>(silentSamples_ - bound_.residualBoundSamples)
                     / static_cast<double>(fadeSamples), false};
    }
private:
    FrozenTailBound bound_;
    std::uint64_t silentSamples_ = 0;
};
} // namespace hem::delayScaffold
