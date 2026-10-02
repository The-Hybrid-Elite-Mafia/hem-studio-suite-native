#pragma once
#include "../SessionState.h"
#include <functional>

namespace hem {

struct OfflineMixOptions {
    double sampleRate = 48000;
    int blockSize = 256;
    double tailSeconds = 2;
};

// Created on the message thread; adapters may retire on workers because the
// ExternalPluginHost retains the actual VST3 owners until main-thread collection.
struct OfflinePreparedPlugins {
    std::array<std::array<std::unique_ptr<ModuleProcessor>,8>,13> slots;
};

struct OfflineMixResult {
    juce::Result status = juce::Result::ok();
    // Set only after the complete file has been atomically published.
    juce::int64 frames = 0;
    int latencyRemoved = 0;
};

// Synchronous worker-thread operation. No audio device or read-ahead thread is
// used. The caller must pass an independently captured, immutable snapshot.
// Callbacks run on the calling worker, never on the message/audio thread.
//
// The export starts at zero, ignores transport position/looping and all monitor
// state, and ends at ceil(longest source duration * rate) + ceil(tail * rate).
// Input trim -> 13 stereo 8-slot chains -> track gain/mute/solo -> sum -> output
// gain -> unchanged CoreMasterStage. Fixed insert + actual master latency is
// removed; processing continues through that latency and the requested tail.
//
// A same-directory temporary WAV is flushed and checked before an atomic,
// no-replace hard-link publication. Existing destinations are never replaced.
// Cancellation/failure removes the temporary and leaves no final output.
// A filesystem without hard-link support is rejected safely.
class OfflineMixRenderer final {
public:
    static OfflineMixResult render(
        const SessionSnapshot&, const juce::File&, const OfflineMixOptions&,
        const std::function<bool()>& cancelled = {},
        const std::function<void(double)>& progress = {},
        OfflinePreparedPlugins* preparedExternal = nullptr);
};

} // namespace hem
