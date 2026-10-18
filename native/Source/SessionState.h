#pragma once
#include <JuceHeader.h>
#include "TrackEngine.h"
#include "Plugins/PluginRegistry.h"
#include "Plugins/ExternalPluginState.h"
#include "Systems/NativeProcessorState.h"

namespace hem {
// The entire bank has one owner. A staged bank can replace the live bank with
// one unique_ptr swap under the device callback lock, without deleting audio
// sources or allocating memory in that lock or on the audio thread.
struct TrackBank { std::array<TrackEngine,13> tracks; };
struct SlotSnapshot : NativeProcessorSnapshot {
    juce::String pluginId;
    bool bypassed=false;
    ExternalPluginState external;
    // Dirty-state comparison only. Never persisted in a session or preset.
    uint64_t externalRevision=0;
};
struct TrackSnapshot {
    juce::File source;
    juce::String name{"EMPTY TRACK"};
    float gainDb=0;
    bool muted=false,solo=false;
    std::array<SlotSnapshot,8> slots;
    std::optional<clipEdit::State> clipEdit;
};
struct SessionSnapshot {
    std::array<TrackSnapshot,13> tracks;
    float inputDb=0,outputDb=0;
    double positionSeconds=0;
    double tempoBpm=120; // additive persisted property; legacy sessions explicitly use 120
    bool looping=false;
    juce::ValueTree monitorParameters{"PARAMETERS"};
};
class SessionState final {
public:
    static constexpr size_t maxSerializedBytes=8*1024*1024;
    static constexpr const char* externalPluginId="external-vst3";
    // Capture on the message thread, while insert publication is stable.
    // No XML, file access, APVTS copying, or processor construction belongs in
    // getNextAudioBlock(). Parameter reads here use APVTS atomic values.
    // For external opaque capture the caller must also gate external processing
    // and establish quiescence. false reads descriptor/revision metadata only.
    static SessionSnapshot capture(const TrackBank&,float inputDb,float outputDb,
                                   double position,bool looping,ModuleProcessor& monitor,
                                   bool includeExternalState=true);
    static juce::ValueTree encode(const SessionSnapshot&,const juce::File& sessionFile);
    static SessionSnapshot decode(const juce::ValueTree&,const juce::File& sessionFile);
    static void save(const SessionSnapshot&,const juce::File&);
    static SessionSnapshot load(const juce::File&);
    static bool hasExternal(const SessionSnapshot&) noexcept;
    static bool hasNativeResources(const SessionSnapshot&) noexcept;
    // Pure scalar schema validation/migration, also used by whole-state helpers.
    static juce::ValueTree validatedParameters(ModuleProcessor&,const juce::ValueTree&);
    static juce::ValueTree captureParameters(ModuleProcessor&);
    // Reject missing, duplicate, unknown, non-finite and out-of-range values.
    static void restoreParameters(ModuleProcessor&,const juce::ValueTree&);
};

// Single-operation background worker. Public methods and completion callbacks
// run on the message thread. busy stays true until off-thread retirement ends.
// Destructor joins the worker before the shared read-ahead thread is stopped.
class SessionWorker final : private juce::Thread,private juce::Timer {
public:
    struct Context {double sampleRate=0;int blockSize=0;unsigned generation=0;};
    using Commit=std::function<juce::Result(std::unique_ptr<TrackBank>&,const SessionSnapshot&,Context)>;
    using Report=std::function<void(const juce::String&)>;
    using Completion=std::function<void(bool saving,const juce::File&,const juce::Result&)>;
    // Runs on the message thread, after native/file staging and before commit.
    // The callback must stage every external slot into this unpublished bank.
    using StageExternal=std::function<juce::Result(TrackBank&,const SessionSnapshot&,Context)>;
    SessionWorker(juce::TimeSliceThread&,Commit,Report,Completion={},StageExternal={});
    ~SessionWorker() override;
    bool save(SessionSnapshot,const juce::File&);
    bool load(const juce::File&,Context);
    bool isBusy() const noexcept {return busy.load(std::memory_order_acquire);}
private:
    enum class Operation {Save,Load,Retire};
    void run() override;
    void timerCallback() override;
    juce::TimeSliceThread& disk;
    Commit commit;Report report;Completion completion;StageExternal stageExternal;
    Operation operation=Operation::Save;
    juce::File file;
    SessionSnapshot snapshot;
    Context context;
    std::unique_ptr<TrackBank> staged;
    juce::String error;
    std::atomic<bool> busy{false},done{false};
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SessionWorker)
};
}

