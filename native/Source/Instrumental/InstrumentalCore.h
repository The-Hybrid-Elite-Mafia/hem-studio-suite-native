#pragma once

// Adapted from the user-supplied Hybrid Studio 2.0.0 sample sequencer.
// See ATTRIBUTION.md for source identity, adaptation details and license status.
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <initializer_list>
#include <memory>
#include <numbers>
#include <vector>

namespace hem::instrumental
{
inline constexpr int kLaneCount = 13;
inline constexpr int kStepCount = 32;
inline constexpr int kOutputSampleRate = 96000;
inline constexpr int kOutputChannels = kLaneCount * 2;
inline constexpr int kMaxBlockFrames = 4096;
inline constexpr int kMaxVoices = 64;
inline constexpr int kChokeFadeFrames = 288; // Original 3 ms micro-fade at 96 kHz.
inline constexpr int kMaxRepeats = 128;
inline constexpr std::size_t kMaxSampleFrames = 384000u * 300u;
inline constexpr std::size_t kMaxDecodedValues = 128u * 1024u * 1024u; // 512 MiB, unique samples.
inline constexpr std::uint64_t kMaxRenderedBytes = std::uint64_t { 4 } * 1024u * 1024u * 1024u;
inline constexpr std::uint64_t kRenderHeaderAllowance = 4u * 1024u * 1024u; // WAV headers + bounded manifests.
inline constexpr std::int64_t kMaxTotalFrames = static_cast<std::int64_t> (
    (kMaxRenderedBytes - kRenderHeaderAllowance) / (kOutputChannels * sizeof (float)));
static_assert (sizeof (float) == 4 && std::numeric_limits<float>::is_iec559,
               "Instrumental output requires IEEE 754 float32");

/** Build off the audio/message threads, then publish as shared_ptr<const>.
    No mutable aliases may modify the vectors while any renderer owns this data.
    An empty right channel means mono. Stereo channels must have equal lengths.
    Samples keep their original rate; the renderer reads at the appropriate ratio. */
struct SampleData
{
    double sampleRate = kOutputSampleRate;
    std::vector<float> left, right;
};

struct Lane
{
    std::shared_ptr<const SampleData> sample;
    std::uint32_t steps = 0;
    float gainDb = 0.0f;
    float pan = 0.0f;
    float pitchSemitones = 0.0f;
    bool mute = false;
    bool solo = false;
    int chokeGroup = 0;
};

/** One repeat is all 32 sixteenth notes: two bars in 4/4.
    tailSeconds is an explicit render tail, not automatic silence trimming.
    A null sample produces a silent lane, including when pattern bits are set. */
struct RenderPlan
{
    std::array<Lane, kLaneCount> lanes {};
    double bpm = 120.0;
    double swingPercent = 50.0;
    int repeats = 1;
    double tailSeconds = 2.0;
};

enum class Status { ok, complete, cancelled, invalidPlan, invalidOutput, notPrepared };
struct RenderResult
{
    Status status = Status::notPrepared;
    const char* message = "Renderer has not been prepared";
};
struct BlockResult
{
    Status status = Status::notPrepared;
    int frames = 0;
    std::int64_t firstFrame = 0;
};

/** Shared structural admission predicate permits hostile length metadata to be
    rejected by a decoder before allocating large vectors. */
inline bool validSampleFrameCounts (std::size_t left, std::size_t right) noexcept
{
    return left > 0 && left <= kMaxSampleFrames && (right == 0 || right == left)
        && right <= kMaxDecodedValues && left <= kMaxDecodedValues - right;
}

inline double stepLengthSamples (double bpm) noexcept
{
    return static_cast<double> (kOutputSampleRate) * 60.0 / bpm / 4.0;
}
inline double swingOffsetSamples (double stepLength, double swingPercent) noexcept
{
    return (swingPercent / 50.0 - 1.0) * stepLength;
}

/** A worker-only, sequential, block-streaming renderer. This is deliberately NOT
    an AudioProcessor, live transport, MIDI engine or master effects chain.

    prepare() snapshots controls and retains immutable samples. prepare() and
    destruction can release sample storage: neither belongs on the audio callback.
    renderNext() uses fixed voices and caller-owned planar float32 output; it does
    not allocate, lock, construct strings or perform I/O. All 13 stereo lanes are
    rendered TOGETHER so cross-lane choke groups retain their original meaning.

    Output channels 2 * lane / 2 * lane + 1 are left/right. All 26 nonoverlapping
    output buffers must hold frameCapacity floats. Only BlockResult.frames are
    valid. Every nonempty successful block returns Status::ok, including the last;
    the following call returns complete with zero frames. Invalid output arguments
    may be corrected and retried. Cancellation is terminal until prepare() again;
    any partially generated cancelled block is invalid and must not be written.
    Methods are single-worker-thread only; the cancel flag may be set elsewhere. */
class OfflineRenderer final
{
public:
    [[nodiscard]] RenderResult prepare (const RenderPlan& plan, int blockFrames = 1024,
                                        const std::atomic<bool>* cancel = nullptr) noexcept
    {
        state_ = Status::notPrepared;
        voices_ = {};
        plan_ = {}; // Release the previous render's samples on this worker only.
        frame_ = totalFrames_ = musicalFrames_ = nextStep_ = stepCount_ = 0;
        age_ = 0;
        if (isCancelled (cancel))
            return fail (Status::cancelled, "Instrumental render cancelled");
        if (blockFrames < 1 || blockFrames > kMaxBlockFrames)
            return fail (Status::invalidPlan, "Render block must contain 1 to 4096 frames");
        if (! bounded (plan.bpm, 40.0, 240.0)
            || ! bounded (plan.swingPercent, 50.0, 75.0)
            || plan.repeats < 1 || plan.repeats > kMaxRepeats
            || ! bounded (plan.tailSeconds, 0.0, 30.0))
            return fail (Status::invalidPlan, "Tempo, swing, repeats or tail is outside its supported range");

        blockFrames_ = blockFrames;
        stepLength_ = stepLengthSamples (plan.bpm);
        swingOffset_ = swingOffsetSamples (stepLength_, plan.swingPercent);
        stepCount_ = static_cast<std::int64_t> (plan.repeats) * kStepCount;
        // Round the common musical boundary once, never independently per lane.
        musicalFrames_ = static_cast<std::int64_t> (std::ceil (stepLength_ * static_cast<double> (stepCount_)));
        totalFrames_ = musicalFrames_ + static_cast<std::int64_t> (std::llround (plan.tailSeconds * kOutputSampleRate));
        if (totalFrames_ > kMaxTotalFrames)
            return fail (Status::invalidPlan, "Thirteen stereo stems exceed the combined 4 GiB output budget");

        std::array<const SampleData*, kLaneCount> checked {};
        std::size_t checkedCount = 0, decodedValues = 0;
        bool anySolo = false;
        for (const auto& lane : plan.lanes)
        {
            if (isCancelled (cancel))
                return fail (Status::cancelled, "Instrumental render cancelled");
            if (! bounded (lane.gainDb, -60.0, 12.0) || ! bounded (lane.pan, -1.0, 1.0)
                || ! bounded (lane.pitchSemitones, -24.0, 24.0)
                || lane.chokeGroup < 0 || lane.chokeGroup > 4)
                return fail (Status::invalidPlan, "A lane control is outside its supported range");
            anySolo = anySolo || lane.solo;
            const auto* sample = lane.sample.get();
            if (sample == nullptr)
                continue;
            if (std::find (checked.begin(), checked.begin() + checkedCount, sample)
                != checked.begin() + checkedCount)
                continue;
            if (! bounded (sample->sampleRate, 8000.0, 384000.0)
                || ! validSampleFrameCounts (sample->left.size(), sample->right.size()))
                return fail (Status::invalidPlan, "Sample rate, length or stereo channel layout is invalid");
            const auto values = sample->left.size() + sample->right.size();
            if (values > kMaxDecodedValues - decodedValues)
                return fail (Status::invalidPlan, "Decoded samples exceed the 512 MiB render budget");
            decodedValues += values;
            checked[checkedCount++] = sample;
            for (const auto* channel : { &sample->left, &sample->right })
                for (std::size_t i = 0; i < channel->size(); ++i)
                {
                    if ((i & 4095u) == 0u && isCancelled (cancel))
                        return fail (Status::cancelled, "Instrumental render cancelled");
                    // Float WAV may legitimately exceed unity. Reject corrupt
                    // extremes instead of clipping, normalising or risking overflow.
                    if (! bounded ((*channel)[i], -1000000.0, 1000000.0))
                        return fail (Status::invalidPlan, "Sample contains non-finite or unsafe numeric values");
                }
        }

        plan_ = plan;
        for (std::size_t i = 0; i < plan_.lanes.size(); ++i)
        {
            const auto& lane = plan_.lanes[i];
            ratios_[i] = lane.sample ? std::exp2 (lane.pitchSemitones / 12.0)
                                       * lane.sample->sampleRate / kOutputSampleRate : 1.0;
            const bool audible = ! lane.mute && (! anySolo || lane.solo);
            // Preserve the upstream -60 dB silence floor and centre-unity pan law.
            const auto gain = audible && lane.gainDb > -59.9f ? std::pow (10.0f, lane.gainDb * 0.05f) : 0.0f;
            const auto angle = (lane.pan + 1.0f) * std::numbers::pi_v<float> * 0.25f;
            gainsL_[i] = gain * std::numbers::sqrt2_v<float> * std::cos (angle);
            gainsR_[i] = gain * std::numbers::sqrt2_v<float> * std::sin (angle);
        }
        if (isCancelled (cancel))
            return fail (Status::cancelled, "Instrumental render cancelled");
        state_ = Status::ok;
        return { Status::ok, "Ready" };
    }

    [[nodiscard]] BlockResult renderNext (const std::array<float*, kOutputChannels>& outputs,
                                          int frameCapacity,
                                          const std::atomic<bool>* cancel = nullptr) noexcept
    {
        if (state_ != Status::ok)
            return { state_, 0, frame_ };
        if (isCancelled (cancel))
            return cancelBlock();
        if (frame_ >= totalFrames_)
        {
            state_ = Status::complete;
            return { Status::complete, 0, frame_ };
        }
        if (frameCapacity < 1 || frameCapacity > kMaxBlockFrames || ! validOutputs (outputs, frameCapacity))
            return { Status::invalidOutput, 0, frame_ };

        const auto first = frame_;
        const auto count = static_cast<int> (std::min<std::int64_t> (
            std::min (blockFrames_, frameCapacity), totalFrames_ - frame_));
        const auto end = first + count;
        for (auto* channel : outputs)
            std::fill_n (channel, count, 0.0f);

        int cursor = 0;
        while (nextStep_ < stepCount_)
        {
            // Absolute step index avoids block-size-dependent accumulation drift.
            // Omitting the original common two-sample pre-roll places step 0 at
            // output frame 0. Fractional Catmull-Rom support begins one frame early.
            const double tau = static_cast<double> (nextStep_) * stepLength_
                             + ((nextStep_ & 1) != 0 ? swingOffset_ : 0.0);
            const auto onset = std::max<std::int64_t> (0, static_cast<std::int64_t> (std::floor (tau)) - 1);
            if (onset >= end)
                break;
            const auto at = static_cast<int> (std::max (first, onset) - first);
            if (! renderVoices (outputs, cursor, at, cancel))
                return cancelBlock();
            cursor = at;
            const auto bit = std::uint32_t { 1 } << static_cast<unsigned> (nextStep_ % kStepCount);
            for (int lane = 0; lane < kLaneCount; ++lane)
                if ((plan_.lanes[static_cast<std::size_t> (lane)].steps & bit) != 0u)
                    startVoice (lane, (static_cast<double> (first + at) - tau)
                                      * ratios_[static_cast<std::size_t> (lane)]);
            ++nextStep_;
        }
        if (! renderVoices (outputs, cursor, count, cancel))
            return cancelBlock();
        for (int lane = 0; lane < kLaneCount; ++lane)
        {
            auto* left = outputs[static_cast<std::size_t> (2 * lane)];
            auto* right = outputs[static_cast<std::size_t> (2 * lane + 1)];
            const auto l = gainsL_[static_cast<std::size_t> (lane)];
            const auto r = gainsR_[static_cast<std::size_t> (lane)];
            for (int i = 0; i < count; ++i)
            {
                left[i] *= l;
                right[i] *= r;
            }
        }
        if (isCancelled (cancel))
            return cancelBlock();
        frame_ = end;
        return { Status::ok, count, first };
    }

    [[nodiscard]] std::int64_t totalFrames() const noexcept { return totalFrames_; }
    [[nodiscard]] std::int64_t musicalFrames() const noexcept { return musicalFrames_; }
    [[nodiscard]] std::int64_t renderedFrames() const noexcept { return frame_; }

private:
    struct Voice
    {
        const SampleData* sample = nullptr; // Retained by plan_, never refcounted in the render loop.
        int lane = 0;
        double position = 0.0, increment = 1.0;
        float fadeGain = 1.0f, fadeStep = 0.0f;
        bool active = false, fading = false;
        std::uint64_t age = 0;
    };

    static bool bounded (double value, double low, double high) noexcept
    {
        return std::isfinite (value) && value >= low && value <= high;
    }
    static bool isCancelled (const std::atomic<bool>* cancel) noexcept
    {
        return cancel != nullptr && cancel->load (std::memory_order_relaxed);
    }
    RenderResult fail (Status status, const char* message) noexcept
    {
        state_ = status;
        return { status, message };
    }
    BlockResult cancelBlock() noexcept
    {
        state_ = Status::cancelled;
        return { Status::cancelled, 0, frame_ };
    }
    static bool validOutputs (const std::array<float*, kOutputChannels>& outputs, int count) noexcept
    {
        const auto bytes = static_cast<std::uintptr_t> (count) * sizeof (float);
        for (std::size_t i = 0; i < outputs.size(); ++i)
        {
            const auto start = reinterpret_cast<std::uintptr_t> (outputs[i]);
            if (outputs[i] == nullptr || start > std::numeric_limits<std::uintptr_t>::max() - bytes)
                return false;
            for (std::size_t j = 0; j < i; ++j)
            {
                const auto other = reinterpret_cast<std::uintptr_t> (outputs[j]);
                if (start < other + bytes && other < start + bytes)
                    return false;
            }
        }
        return true;
    }
    void fadeLaneVoices (int lane) noexcept
    {
        for (auto& voice : voices_)
            if (voice.active && voice.lane == lane && ! voice.fading)
            {
                voice.fading = true;
                voice.fadeStep = voice.fadeGain / static_cast<float> (kChokeFadeFrames);
            }
    }
    void startVoice (int laneIndex, double startPosition) noexcept
    {
        const auto& lane = plan_.lanes[static_cast<std::size_t> (laneIndex)];
        if (! lane.sample)
            return;
        fadeLaneVoices (laneIndex);
        // Muted/unsoloed lanes STILL trigger chokes, matching HybridEngine's
        // trigger-then-mix semantics. Same-frame events retain ascending lane order.
        if (lane.chokeGroup > 0)
            for (int other = 0; other < kLaneCount; ++other)
                if (other != laneIndex
                    && plan_.lanes[static_cast<std::size_t> (other)].chokeGroup == lane.chokeGroup)
                    fadeLaneVoices (other);

        Voice* target = nullptr;
        for (auto& voice : voices_)
            if (! voice.active) { target = &voice; break; }
        if (target == nullptr)
        {
            for (auto& voice : voices_)
                if (voice.fading && (target == nullptr || voice.age < target->age))
                    target = &voice;
            if (target == nullptr)
                for (auto& voice : voices_)
                    if (target == nullptr || voice.age < target->age)
                        target = &voice;
        }
        *target = { lane.sample.get(), laneIndex, startPosition,
                    ratios_[static_cast<std::size_t> (laneIndex)], 1.0f, 0.0f, true, false, ++age_ };
    }
    static float readCatmullRom (const std::vector<float>& channel, double position) noexcept
    {
        const double floorPosition = std::floor (position);
        const auto index = static_cast<std::int64_t> (floorPosition);
        const auto t = static_cast<float> (position - floorPosition);
        const auto at = [&channel] (std::int64_t i) noexcept
        {
            return i >= 0 && static_cast<std::uint64_t> (i) < channel.size()
                ? channel[static_cast<std::size_t> (i)] : 0.0f;
        };
        const float xm1 = at (index - 1), x0 = at (index), x1 = at (index + 1), x2 = at (index + 2);
        return x0 + 0.5f * t * (x1 - xm1 + t * (2.0f * xm1 - 5.0f * x0 + 4.0f * x1 - x2
                                            + t * (3.0f * (x0 - x1) + x2 - xm1)));
    }
    bool renderVoices (const std::array<float*, kOutputChannels>& outputs, int from, int to,
                       const std::atomic<bool>* cancel) noexcept
    {
        if (to <= from)
            return ! isCancelled (cancel);
        for (auto& voice : voices_)
        {
            if (! voice.active)
                continue;
            const auto& sample = *voice.sample;
            auto* left = outputs[static_cast<std::size_t> (2 * voice.lane)];
            auto* right = outputs[static_cast<std::size_t> (2 * voice.lane + 1)];
            for (int i = from; i < to; ++i)
            {
                if (((i - from) & 255) == 0 && isCancelled (cancel))
                    return false;
                if (voice.position >= static_cast<double> (sample.left.size()) + 2.0)
                {
                    voice.active = false;
                    break;
                }
                const float l = readCatmullRom (sample.left, voice.position) * voice.fadeGain;
                left[i] += l;
                right[i] += sample.right.empty() ? l
                    : readCatmullRom (sample.right, voice.position) * voice.fadeGain;
                voice.position += voice.increment;
                if (voice.fading)
                {
                    voice.fadeGain -= voice.fadeStep;
                    if (voice.fadeGain <= 0.0f)
                    {
                        voice.active = false;
                        break;
                    }
                }
            }
        }
        return ! isCancelled (cancel);
    }

    RenderPlan plan_ {};
    std::array<Voice, kMaxVoices> voices_ {};
    std::array<double, kLaneCount> ratios_ {};
    std::array<float, kLaneCount> gainsL_ {}, gainsR_ {};
    Status state_ = Status::notPrepared;
    int blockFrames_ = 1024;
    std::int64_t frame_ = 0, totalFrames_ = 0, musicalFrames_ = 0, nextStep_ = 0, stepCount_ = 0;
    std::uint64_t age_ = 0;
    double stepLength_ = 0.0, swingOffset_ = 0.0;
};
} // namespace hem::instrumental
