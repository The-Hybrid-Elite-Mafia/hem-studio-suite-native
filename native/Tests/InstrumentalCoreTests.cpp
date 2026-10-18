// SOURCE-ONLY REGRESSION FIXTURES. Added 2026-10-16; not compiled or run.
#include "../Source/Instrumental/InstrumentalCore.h"
#include <iostream>
#include <stdexcept>
#include <utility>

namespace
{
using namespace hem::instrumental;

void check (bool condition, const char* message)
{
    if (! condition)
        throw std::runtime_error (message);
}
void near (double actual, double expected, double tolerance, const char* message)
{
    check (std::abs (actual - expected) <= tolerance, message);
}
std::shared_ptr<const SampleData> sample (std::vector<float> left, double rate = kOutputSampleRate,
                                          std::vector<float> right = {})
{
    auto result = std::make_shared<SampleData>();
    result->sampleRate = rate;
    result->left = std::move (left);
    result->right = std::move (right);
    return result;
}
RenderPlan shortPlan()
{
    RenderPlan plan;
    plan.bpm = 240.0;
    plan.tailSeconds = 0.0;
    return plan;
}
struct Buffers
{
    std::array<std::vector<float>, kOutputChannels> values;
    std::array<float*, kOutputChannels> pointers {};
    explicit Buffers (int capacity)
    {
        for (std::size_t i = 0; i < values.size(); ++i)
        {
            values[i].resize (static_cast<std::size_t> (capacity));
            pointers[i] = values[i].data();
        }
    }
};
struct Capture
{
    std::array<std::vector<float>, kOutputChannels> channels;
    std::int64_t musical = 0, total = 0;
};
Capture render (const RenderPlan& plan, int block, int capacity = 0)
{
    OfflineRenderer renderer;
    const auto prepared = renderer.prepare (plan, block);
    check (prepared.status == Status::ok, prepared.message);
    if (capacity == 0)
        capacity = block;
    Buffers buffers (capacity);
    Capture capture;
    capture.musical = renderer.musicalFrames();
    capture.total = renderer.totalFrames();
    for (auto& channel : capture.channels)
        channel.reserve (static_cast<std::size_t> (capture.total));
    std::int64_t next = 0;
    for (;;)
    {
        const auto result = renderer.renderNext (buffers.pointers, capacity);
        check (result.firstFrame == next, "Blocks must share one contiguous exact-frame origin");
        if (result.status == Status::complete)
        {
            check (result.frames == 0, "Completed renderer must return no new data");
            break;
        }
        check (result.status == Status::ok && result.frames > 0 && result.frames <= capacity,
               "Invalid streamed block result");
        for (std::size_t channel = 0; channel < capture.channels.size(); ++channel)
        {
            for (int i = 0; i < result.frames; ++i)
                check (std::isfinite (buffers.values[channel][static_cast<std::size_t> (i)]), "Non-finite rendered float");
            capture.channels[channel].insert (capture.channels[channel].end(),
                buffers.values[channel].begin(), buffers.values[channel].begin() + result.frames);
        }
        next += result.frames;
    }
    check (next == capture.total && renderer.renderedFrames() == capture.total, "Exact shared final frame count");
    for (const auto& channel : capture.channels)
        check (channel.size() == static_cast<std::size_t> (next), "All 13 lane stems must have exactly equal lengths");
    check (renderer.renderNext (buffers.pointers, capacity).status == Status::complete, "Completion must be terminal");
    return capture;
}
void allLanesAligned()
{
    auto plan = shortPlan();
    const auto impulse = sample ({ 1.0f, 0.0f, 0.0f, 0.0f });
    for (auto& lane : plan.lanes)
    {
        lane.sample = impulse;
        lane.steps = 1u | (1u << 31u);
    }
    const auto out = render (plan, 1024, 997);
    check (out.total == 192000 && out.musical == 192000, "Two-bar boundary at 240 BPM / 96 kHz");
    for (int lane = 0; lane < kLaneCount; ++lane)
    {
        const auto& left = out.channels[static_cast<std::size_t> (2 * lane)];
        const auto& right = out.channels[static_cast<std::size_t> (2 * lane + 1)];
        near (left[0], 1.0, 1.0e-6, "No residual two-sample preroll at step zero");
        near (left[186000], 1.0, 1.0e-6, "Highest bit is step 31, on shared frame 186000");
        check (left[1] == 0.0f && left[2] == 0.0f, "No delayed duplicate onset");
        check (left == out.channels[0], "All 13 lanes retain exact sample alignment");
        for (std::size_t i = 0; i < left.size(); ++i)
            near (left[i], right[i], 1.0e-6, "Mono is phase-aligned in both output channels");
    }
}
void fractionalTimingAndPartition()
{
    auto plan = shortPlan();
    plan.bpm = 97.123;
    plan.swingPercent = 66.25;
    plan.tailSeconds = 0.003;
    plan.lanes[0].sample = sample ({ 1.0f, 0.0f, 0.0f });
    plan.lanes[0].steps = 1u << 1u;
    const auto a = render (plan, 17);
    const auto b = render (plan, 1024, 997);
    check (a.channels == b.channels, "Output must be bit-identical across block partitions");
    const auto step = stepLengthSamples (plan.bpm);
    const auto expected = step + swingOffsetSamples (step, plan.swingPercent);
    double sum = 0.0, moment = 0.0;
    for (std::size_t i = 0; i < a.channels[0].size(); ++i)
    {
        sum += a.channels[0][i];
        moment += static_cast<double> (i) * a.channels[0][i];
    }
    near (sum, 1.0, 2.0e-6, "Fractional event preserves signed impulse area");
    near (moment / sum, expected, 0.001, "Noninteger BPM and swing retain sub-sample timing");
    check (a.total == static_cast<std::int64_t> (std::ceil (step * 32.0)) + 288,
           "Tail follows the once-rounded common musical boundary");
}
void repeatsAndTail()
{
    auto plan = shortPlan();
    plan.repeats = 2;
    plan.lanes[0].sample = sample ({ 1.0f, 0.0f, 0.0f });
    plan.lanes[0].steps = 1u;
    auto out = render (plan, 4096);
    near (out.channels[0][0], 1.0, 1.0e-6, "First repeat trigger");
    near (out.channels[0][192000], 1.0, 1.0e-6, "Second repeat trigger");
    check (out.total == 384000, "Repeats add exactly 32 steps each");
    plan = shortPlan();
    plan.lanes[0].sample = sample (std::vector<float> (20000, 0.25f));
    plan.lanes[0].steps = 1u << 31u;
    out = render (plan, 1024);
    check (out.total == 192000 && out.channels[0].back() > 0.24f, "Zero tail cuts exactly at musical boundary");
    plan.tailSeconds = 0.01;
    out = render (plan, 1024);
    check (out.total == 192960 && out.channels[0].back() > 0.24f, "Explicit tail is retained without silence trimming");
    // Step 0 is not scheduled again after the finite repeat limit, even in a long tail.
    plan.lanes[0].sample = sample ({ 1.0f, 0.0f });
    plan.lanes[0].steps = 1u;
    out = render (plan, 1024);
    for (std::size_t i = 192000; i < out.channels[0].size(); ++i)
        check (out.channels[0][i] == 0.0f, "Tail must never schedule a new repeat");
}
void stereoPitchAndPan()
{
    auto plan = shortPlan();
    plan.lanes[0].sample = sample ({ 0.25f, -0.5f, 0.75f, 0.0f }, 48000.0,
                                   { -0.25f, 0.5f, -0.75f, 0.0f });
    plan.lanes[0].pitchSemitones = 12.0f; // 48k source * 2 at 96k is exact unity increment.
    plan.lanes[0].steps = 1u;
    plan.lanes[1].sample = sample ({ 0.5f, 0.0f });
    plan.lanes[1].steps = 1u;
    plan.lanes[1].pan = -1.0f;
    plan.lanes[2] = plan.lanes[1];
    plan.lanes[2].pan = 1.0f;
    plan.lanes[2].gainDb = -6.0f;
    const auto out = render (plan, 31);
    near (out.channels[0][0], 0.25, 1.0e-6, "Pitch/source-rate ratio uses original sample rate");
    near (out.channels[0][1], -0.5, 1.0e-6, "Pitch control advances exact source position");
    for (std::size_t i = 0; i < out.channels[0].size(); ++i)
        near (out.channels[0][i] + out.channels[1][i], 0.0, 1.0e-6, "Stereo phase polarity remains intact");
    near (out.channels[2][0], 0.5 * std::sqrt (2.0), 1.0e-6, "Mono hard-left centre-unity pan law");
    near (out.channels[3][0], 0.0, 1.0e-6, "Hard-left right channel is silent");
    near (out.channels[4][0], 0.0, 1.0e-6, "Hard-right left channel is silent");
    near (out.channels[5][0], 0.5 * std::sqrt (2.0) * std::pow (10.0, -6.0 / 20.0), 1.0e-6,
          "Per-lane gain and hard-right pan are applied before stem output");
}
void chokeMuteAndSolo()
{
    auto plan = shortPlan();
    plan.lanes[0].sample = sample (std::vector<float> (30000, 0.5f));
    plan.lanes[0].steps = 1u;
    plan.lanes[0].chokeGroup = 1;
    plan.lanes[1].sample = sample ({ 1.0f, 0.0f });
    plan.lanes[1].steps = 1u << 1u;
    plan.lanes[1].chokeGroup = 1;
    plan.lanes[1].mute = true;
    const auto out = render (plan, 29);
    near (out.channels[0][5900], 0.5, 1.0e-6, "Sustained lane is active before cross-lane choke");
    check (out.channels[0][6100] > 0.0f && out.channels[0][6100] < 0.5f, "Choke applies a micro-fade");
    check (out.channels[0][6400] == 0.0f, "Muted lane still triggers cross-lane choke before mix gain");
    check (std::all_of (out.channels[2].begin(), out.channels[2].end(), [] (float x) { return x == 0.0f; }),
           "Muted lane output is silent");
    plan.lanes[1].mute = false;
    plan.lanes[0].solo = true;
    const auto solo = render (plan, 1024);
    check (solo.channels[0][6400] == 0.0f, "Non-soloed lane still triggers upstream-compatible choke");
    check (std::all_of (solo.channels[2].begin(), solo.channels[2].end(), [] (float x) { return x == 0.0f; }),
           "Solo suppresses non-solo lane gain");
    plan.lanes[0].mute = true;
    const auto mutedSolo = render (plan, 1024);
    check (std::all_of (mutedSolo.channels[0].begin(), mutedSolo.channels[0].end(), [] (float x) { return x == 0.0f; }),
           "Mute overrides solo");

    plan = shortPlan();
    plan.lanes[0].sample = sample (std::vector<float> (10000, 0.5f));
    plan.lanes[0].steps = 1u;
    plan.lanes[0].chokeGroup = 2;
    plan.lanes[1] = plan.lanes[0];
    const auto together = render (plan, 1024);
    check (together.channels[0][400] == 0.0f && together.channels[2][400] > 0.49f,
           "Simultaneous choke order is stable ascending lane order, last lane survives");
    plan.lanes[0].chokeGroup = 0;
    plan.lanes[0].steps = 3u;
    plan.lanes[1] = {};
    const auto retrigger = render (plan, 1024);
    check (retrigger.channels[0][6050] > 0.5f && retrigger.channels[0][6400] < 0.501f,
           "Same-lane retrigger fades the old voice instead of abandoning it");
}
void validationAndCancellation()
{
    OfflineRenderer renderer;
    Buffers buffers (1024);
    check (renderer.renderNext (buffers.pointers, 1024).status == Status::notPrepared, "Unprepared render cannot produce audio");
    auto plan = shortPlan();
    auto rejects = [&renderer] (const RenderPlan& bad)
    {
        check (renderer.prepare (bad).status == Status::invalidPlan, "Invalid plan admitted");
    };
    auto bad = plan;
    bad.bpm = std::numeric_limits<double>::quiet_NaN(); rejects (bad);
    bad = plan; bad.bpm = 39.9; rejects (bad);
    bad = plan; bad.swingPercent = 75.1; rejects (bad);
    bad = plan; bad.repeats = 0; rejects (bad);
    bad = plan; bad.repeats = 129; rejects (bad);
    bad = plan; bad.tailSeconds = -0.001; rejects (bad);
    bad = plan; bad.lanes[0].pan = std::numeric_limits<float>::infinity(); rejects (bad);
    bad = plan; bad.lanes[0].pitchSemitones = 24.1f; rejects (bad);
    bad = plan; bad.lanes[0].gainDb = 12.1f; rejects (bad);
    bad = plan; bad.lanes[0].chokeGroup = 5; rejects (bad);
    bad = plan; bad.lanes[0].sample = sample ({ 1.0f }, 7999.0); rejects (bad);
    bad = plan; bad.lanes[0].sample = sample ({ 1.0f }, 96000.0, { 1.0f, 0.0f }); rejects (bad);
    bad = plan; bad.lanes[0].sample = sample ({}); rejects (bad);
    bad = plan; bad.lanes[0].sample = sample ({ std::numeric_limits<float>::quiet_NaN() }); rejects (bad);
    bad = plan; bad.lanes[0].sample = sample ({ 1000001.0f }); rejects (bad);
    bad = plan; bad.bpm = 40.0; bad.repeats = 128; rejects (bad); // Combined 4 GiB output bound.
    check (! validSampleFrameCounts (kMaxSampleFrames + 1u, 0), "Oversized source length rejected before allocation");
    check (! validSampleFrameCounts (kMaxDecodedValues / 2u + 1u, kMaxDecodedValues / 2u + 1u),
           "Oversized stereo decoded payload rejected before allocation");
    check (! validSampleFrameCounts (std::numeric_limits<std::size_t>::max(), 0), "Hostile size cannot wrap validation");
    check (renderer.prepare (plan, 0).status == Status::invalidPlan, "Zero block size rejected");
    check (renderer.prepare (plan, kMaxBlockFrames + 1).status == Status::invalidPlan, "Oversized block rejected");

    check (renderer.prepare (plan).status == Status::ok, "Valid preparation after failure");
    auto pointers = buffers.pointers;
    pointers[0] = nullptr;
    check (renderer.renderNext (pointers, 1024).status == Status::invalidOutput, "Null output rejected");
    pointers = buffers.pointers;
    pointers[1] = pointers[0] + 1;
    check (renderer.renderNext (pointers, 1024).status == Status::invalidOutput, "Overlapping stereo buffers rejected");
    check (renderer.renderedFrames() == 0, "Invalid output cannot advance the common timeline");
    check (renderer.renderNext (buffers.pointers, 1024).status == Status::ok, "Corrected output may be retried");

    std::atomic<bool> cancel { true };
    check (renderer.prepare (plan, 1024, &cancel).status == Status::cancelled, "Cancellation before preparation");
    cancel.store (false);
    check (renderer.prepare (plan, 1024, &cancel).status == Status::ok, "Preparation after cancellation");
    check (renderer.renderNext (buffers.pointers, 1024, &cancel).frames == 1024, "Initial uncancelled block");
    cancel.store (true);
    const auto cancelled = renderer.renderNext (buffers.pointers, 1024, &cancel);
    check (cancelled.status == Status::cancelled && cancelled.frames == 0 && cancelled.firstFrame == 1024,
           "Cancellation produces no partial block and retains prior completed origin");
    cancel.store (false);
    check (renderer.renderNext (buffers.pointers, 1024, &cancel).status == Status::cancelled, "Cancelled renderer requires reprepare");
    check (renderer.prepare (plan).status == Status::ok && renderer.renderedFrames() == 0, "Reprepare restarts exact timebase");

    // Empty lanes are intentionally silent, even with a nonzero stored pattern.
    plan.lanes[12].steps = 0xffffffffu;
    const auto missing = render (plan, 1024);
    for (const auto& channel : missing.channels)
        check (std::all_of (channel.begin(), channel.end(), [] (float x) { return x == 0.0f; }), "Missing sample lane must stay silent");
}
} // namespace

int main()
{
    try
    {
        allLanesAligned();
        fractionalTimingAndPartition();
        repeatsAndTail();
        stereoPitchAndPan();
        chokeMuteAndSolo();
        validationAndCancellation();
        std::cout << "PASS Instrumental core: 13 aligned float32 lanes, partition invariance, fractional swing, repeats/tail, stereo/pitch/pan, choke/mute/solo, bounds and cancellation\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "FAIL Instrumental core: " << error.what() << '\n';
        return 1;
    }
}
