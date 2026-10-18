#pragma once
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>

namespace hem {
// Final-output SAMPLE-peak and exponentially weighted RMS meter. This does not
// calculate true peak or LUFS. Its detector state is owned by the audio thread;
// the UI sees only atomic snapshots and may request a latch reset.
class AudioMeters {
public:
    static constexpr float silenceFloorDb = -120.0f;
    static constexpr double rmsTimeSeconds = 0.300;
    static constexpr double peakReleaseDbPerSecond = 24.0;
    static constexpr double holdTimeSeconds = 1.0;
    static constexpr double holdReleaseDbPerSecond = 18.0;

    struct ChannelSnapshot {
        float peakDb = silenceFloorDb;
        float rmsDb = silenceFloorDb;
        float heldPeakDb = silenceFloorDb;
        bool clipped = false;
    };
    struct Snapshot {
        ChannelSnapshot left, right;
        std::uint64_t nonFiniteSamples = 0;
        bool ready = false;
        std::uint64_t generation = 0, processedSamples = 0, publication = 0;
    };

    // Call while callback processing is stopped (e.g. prepareToPlay). An invalid
    // rate produces an unavailable meter, never a guessed time scale.
    bool prepare(double actualRate) noexcept {
        ++preparationGeneration;
        totalProcessedSamples = 0;
        leftState = {};
        rightState = {};
        invalidSamples = 0;
        clearRequested.store(false, std::memory_order_relaxed);
        prepared = std::isfinite(actualRate) && actualRate > 0.0;
        if (prepared) {
            rmsMemory = std::exp(-1.0 / (actualRate * rmsTimeSeconds));
            peakMemory = std::pow(10.0, -peakReleaseDbPerSecond / (20.0 * actualRate));
            holdMemory = std::pow(10.0, -holdReleaseDbPerSecond / (20.0 * actualRate));
            holdSamples = static_cast<std::uint64_t>(std::max(1.0, std::round(actualRate * holdTimeSeconds)));
        }
        publish();
        return prepared;
    }

    // Null channels are silence. A missing right channel is NOT silently copied
    // from left: the caller must pass the actual emitted mono signal twice.
    // No allocation, lock, per-sample atomic operation or GUI callback occurs here.
    void push(const float* left, const float* right, int samples) noexcept {
        if (!prepared || samples < 0) return;
        if (clearRequested.exchange(false, std::memory_order_relaxed)) {
            leftState.clipped = rightState.clipped = false;
            invalidSamples = 0;
        }
        for (int i = 0; i < samples; ++i) {
            advance(leftState, left ? left[i] : 0.0f);
            advance(rightState, right ? right[i] : 0.0f);
        }
        totalProcessedSamples += static_cast<std::uint64_t>(samples);
        publish();
    }

    // Consumed at the next push, including a zero-length push. A new clipped
    // sample in that same callback legitimately re-latches the indicator.
    void resetClip() noexcept { clearRequested.store(true, std::memory_order_relaxed); }

    Snapshot snapshot() const noexcept {
        Snapshot result;
        // The audio thread never waits. UI reads retry at most three times if a
        // block publication overlaps. All individual fields remain data-race-free.
        for (int attempt = 0; attempt < 3; ++attempt) {
            const auto before = revision.load(std::memory_order_seq_cst);
            result.left = load(leftPublished);
            result.right = load(rightPublished);
            result.nonFiniteSamples = invalidPublished.load(std::memory_order_seq_cst);
            result.generation = generationPublished.load(std::memory_order_seq_cst);
            result.processedSamples = samplesPublished.load(std::memory_order_seq_cst);
            result.ready = readyPublished.load(std::memory_order_seq_cst);
            const auto after = revision.load(std::memory_order_seq_cst);
            if (before == after && (after & 1u) == 0) {result.publication = after;return result;}
        }
        return {}; // Bounded contention must never expose a mixed live frame.
    }

private:
    static_assert(std::atomic<bool>::is_always_lock_free, "Meter flags must be lock-free");
    static_assert(std::atomic<float>::is_always_lock_free, "Meter float snapshots must be lock-free");
    static_assert(std::atomic<std::uint64_t>::is_always_lock_free, "Meter counters must be lock-free");
    struct ChannelState {
        double peak = 0.0, square = 0.0, held = 0.0;
        std::uint64_t holdRemaining = 0;
        bool clipped = false;
    };
    struct PublishedChannel {
        std::atomic<float> peak{silenceFloorDb}, rms{silenceFloorDb}, held{silenceFloorDb};
        std::atomic<bool> clipped{false};
    };
    static float db(double amplitude) noexcept {
        return amplitude > 0.000001 ? static_cast<float>(20.0 * std::log10(amplitude)) : silenceFloorDb;
    }
    void advance(ChannelState& state, float sample) noexcept {
        const bool finite = std::isfinite(sample);
        if (!finite && invalidSamples != UINT64_MAX) ++invalidSamples;
        const double magnitude = finite ? std::abs(static_cast<double>(sample)) : 0.0;
        state.clipped = state.clipped || magnitude >= 1.0;
        state.peak = std::max(magnitude, state.peak * peakMemory);
        // Double squaring remains finite even for the largest finite float.
        state.square = rmsMemory * state.square + (1.0 - rmsMemory) * magnitude * magnitude;
        if (magnitude >= state.held && magnitude > 0.0) {
            state.held = magnitude;
            state.holdRemaining = holdSamples;
        } else if (state.holdRemaining > 0) {
            --state.holdRemaining;
        } else {
            state.held = std::max(magnitude, state.held * holdMemory);
        }
        // Avoid long-running denormals after silence without changing the floor.
        if (state.peak < 1.0e-12) state.peak = 0.0;
        if (state.held < 1.0e-12) state.held = 0.0;
        if (state.square < 1.0e-24) state.square = 0.0;
    }
    static void publishChannel(PublishedChannel& to, const ChannelState& from) noexcept {
        to.peak.store(db(from.peak), std::memory_order_seq_cst);
        to.rms.store(db(std::sqrt(from.square)), std::memory_order_seq_cst);
        to.held.store(db(from.held), std::memory_order_seq_cst);
        to.clipped.store(from.clipped, std::memory_order_seq_cst);
    }
    static ChannelSnapshot load(const PublishedChannel& from) noexcept {
        return {from.peak.load(std::memory_order_seq_cst), from.rms.load(std::memory_order_seq_cst),
                from.held.load(std::memory_order_seq_cst), from.clipped.load(std::memory_order_seq_cst)};
    }
    void publish() noexcept {
        revision.fetch_add(1, std::memory_order_seq_cst);
        publishChannel(leftPublished, leftState);
        publishChannel(rightPublished, rightState);
        invalidPublished.store(invalidSamples, std::memory_order_seq_cst);
        readyPublished.store(prepared, std::memory_order_seq_cst);
        generationPublished.store(preparationGeneration, std::memory_order_seq_cst);
        samplesPublished.store(totalProcessedSamples, std::memory_order_seq_cst);
        revision.fetch_add(1, std::memory_order_seq_cst);
    }
    ChannelState leftState, rightState;
    PublishedChannel leftPublished, rightPublished;
    double rmsMemory = 0.0, peakMemory = 0.0, holdMemory = 0.0;
    std::uint64_t holdSamples = 0, invalidSamples = 0;
    std::uint64_t preparationGeneration = 0, totalProcessedSamples = 0;
    bool prepared = false;
    std::atomic<bool> clearRequested{false}, readyPublished{false};
    std::atomic<std::uint64_t> invalidPublished{0}, revision{0}, generationPublished{0}, samplesPublished{0};
};
} // namespace hem
