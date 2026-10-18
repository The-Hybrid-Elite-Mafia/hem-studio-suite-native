#pragma once
#include <cstdint>
#include <functional>
#include "PitchTimeCorrection.h"

namespace hem::pitchtime {
enum class Strategy { tonal, rhythmic, mixed };
struct Settings {
    double sampleRate = 48000, durationRatio = 1, pitchSemitones = 0, formantSemitones = 0;
    double formantBaseHz = 0;
    double wetMix = 1, outputDb = 0; // dry mix permitted only at durationRatio == 1
    int channels = 2;
    Strategy strategy = Strategy::mixed;
    bool preserveFormants = true;
    CorrectionSettings correction;
};
struct Result {
    enum class Status { complete, cancelled, invalid, readFailed, writeFailed, nonFinite };
    Status status = Status::invalid;
    std::int64_t outputFrames = 0, writtenFrames = 0;
    int inputLatency = 0, outputLatency = 0, windowFrames = 0, hopFrames = 0;
    // Offline-only; never admit this backend to either realtime rack.
    bool realtimeQualified = false;
};
// Called synchronously on an offline worker. Reader fills exactly count frames
// from offset; writer must stage privately and publish ONLY on complete.
// All buffers planar; callbacks may retain neither pointers nor references.
using Reader = std::function<bool(std::int64_t offset, float* const* channels, int count)>;
using Writer = std::function<bool(const float* const* channels, int count)>;
using Cancel = std::function<bool()>;
// Worker-side observations at original source frame positions, not live GUI
// callbacks and not measured output pitch. An owning service can queue snapshots.
using CorrectionObserver = std::function<void(std::int64_t sourceFrame, const CorrectionFrame&)>;
inline constexpr const char* backendIdentity = "signalsmith-stretch-1.3.2-a670068d9aeb64913331d5cc29337b19a457a7df/hem-offline-v3";
Result render(const Settings&, std::int64_t inputFrames, const Reader&, const Writer&, const Cancel& = {}, const CorrectionObserver& = {});
}
