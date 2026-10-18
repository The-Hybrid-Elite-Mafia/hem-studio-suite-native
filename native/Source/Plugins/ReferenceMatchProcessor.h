#pragma once
#include "Processors.h"
#include "../DSP/ReferenceMatchCore.h"
#include "../Systems/NativeResourceState.h"
#include "NativeResourceStateProvider.h"
#include <mutex>
namespace hem {
// Resource descriptors and fitted profiles have separate typed representations.
// NativeProcessorSnapshot carries MATCH_PROFILE through session and export state.
class ReferenceMatchProcessor final : public ModuleProcessor, public NativeResourceStateProvider, private juce::Thread {
public:
    struct Snapshot {
        juce::ValueTree parameters;
        reference_match::Profile source, reference, previousSource, previousReference;
        bool committed=false, previous=false;
        float previousAmount=1,previousSmoothing=.5f;
        uint64_t revision=0;
        nativeResource::ResourceDescriptor referenceIdentity{"reference",0};
        nativeResource::ResourceDescriptor previousReferenceIdentity{"reference",0};
    };
    ReferenceMatchProcessor();
    ~ReferenceMatchProcessor() override;
    void prepareToPlay(double,int) override;
    void processBlock(juce::AudioBuffer<float>&,juce::MidiBuffer&) override;
    bool hasEditor() const override {return true;}
    juce::AudioProcessorEditor* createEditor() override;
    float meter() const noexcept override {return peak.load();}
    double getTailLengthSeconds()const override{return preparedTail.load();}
    juce::String meterLabel() const override {return "OUTPUT SAMPLE PEAK";}
    // Off-thread commands. Reference PCM already decoded/identity-checked by host.
    // Replacing reference or match/revert requires a private unpublished node.
    juce::Result setReferenceUnpublished(const float* const*,size_t,size_t,double);
    juce::Result loadReferenceFileUnpublished(const juce::File&);
    nativeResource::ResourceDescriptor referenceIdentityOffThread() const;
    juce::Result setSourceUnpublished(const reference_match::Profile&);
    juce::Result matchUnpublished();
    juce::Result revertUnpublished();
    void markPublished() noexcept {published.store(true);}
    // Live bounded PRE-MATCH source capture, 10 s; after completion clone capture
    // into private candidate and Match there. No auto-commit of captured audio.
    bool startSourceCapture() noexcept;
    void cancelSourceCapture() noexcept {cancelCapture.store(true);}
    // Stopped host graph exclusion only; joins worker off-thread and releases a
    // capture even when this insert will no longer receive processBlock calls.
    void cancelCaptureWhileCallbackExcluded();
    void stopSourceCapture() noexcept {stopCapture.store(true);}
    // Call immediately before processBlock from the same callback. Discontinuous
    // frame positions invalidate capture; plain processBlock assumes continuity.
    void sourceTimeline(uint64_t frame) noexcept;
    bool takeCapturedProfile(reference_match::Profile&); // worker-produced, off-thread
    juce::String captureProgressText() const; // message thread only
    bool captureInProgress() const noexcept {return captureState.load()!=0;}
    juce::Result captureSnapshot(Snapshot&);
    juce::Result restoreSnapshotUnpublished(const Snapshot&,double);
    static juce::Result encodeSnapshot(const Snapshot&,juce::ValueTree&);
    static juce::Result decodeSnapshot(const juce::ValueTree&,Snapshot&);
    static std::unique_ptr<juce::XmlElement> exactProfileXml(const juce::ValueTree&);
    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*,int) override;
    juce::String diagnostic() const;
    const nativeResource::ExpectedLayout& expectedNativeResourceLayout()const noexcept override;
    bool hasPendingNativeStateWork()const noexcept override;
    void sealNativeStateForPublication()noexcept override {markPublished();}
    juce::Result tryCaptureNativeState(NativeProcessorSnapshot&)override;
    juce::Result validateNativeState(const NativeProcessorSnapshot&)const override;
    juce::Result restoreNativeStateOffThread(const NativeProcessorSnapshot&,const NativeRestoreContext&)override;
    bool processingReady()const noexcept override{return prepared.load();}
    struct EditorActions {
        std::function<void()> loadReference, match, revert, captureSource;
        std::function<void(bool reference)> audition;
        std::function<void()> back;
        std::function<void(const reference_match::Profile&)> sourceCaptured;
        std::function<juce::String()> referenceLabel,status;
    };
    // Message-thread-only setup before creating editor; callbacks owned by host.
    EditorActions editorActions;
    struct Levels {std::array<float,2> input{},output{};uint32_t serial=0;};
    Levels levels() const noexcept;
private:
    void run() override;
    static std::vector<ParameterSpec> specs();
    struct Mail {std::atomic<int> state{0};reference_match::Solution solution;float amount=1,smoothing=.5f;};
    std::array<Mail,3> mailbox;
    reference_match::Processor core;
    mutable std::mutex mutex;
    Snapshot saved;
    reference_match::Profile captured;
    bool capturedReady=false;
    std::atomic<bool> editing{false};
    void beginEditLocked();
    std::vector<float> captureLeft,captureRight;
    size_t captureFrames=0;
    std::atomic<unsigned> captureProgressSamples{0};
    uint64_t captureStartFrame=0,captureGeneration=0;
    std::atomic<int> captureState{0}; // 0 idle,1 armed,2 callback writing,3 worker reads
    std::atomic<bool> published{false},prepared{false},committed{false},working{false};
    std::atomic<float> appliedAmount{1},appliedSmoothing{.5f},peak{0};
    std::atomic<int> transitionFrames{0};
    std::atomic<double> preparedTail{0};
    std::atomic<bool> cancelCapture{false},stopCapture{false};
    uint64_t expectedTimeline=0;bool timelineKnown=false;
    std::array<std::atomic<float>,4> levelValues{};
    std::atomic<uint32_t> levelSerial{0};
    juce::String error;
};
}
