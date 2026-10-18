#pragma once
#include "Processors.h"
#include "NativeResourceStateProvider.h"
#include "../DSP/DrumReplacerCore.h"
#include <mutex>

namespace hem {
// Resource changes are whole-node replacements: build a private instance, restore,
// prepare, then publish through the host's existing retirement machinery. Never
// load into a live instance. Host serializes prepare/destruction with callbacks.
class DrumReplacerProcessor final : public ModuleProcessor, public NativeResourceStateProvider {
public:
    DrumReplacerProcessor();
    void prepareToPlay(double,int) override;
    void processBlock(juce::AudioBuffer<float>&,juce::MidiBuffer&) override;
    bool hasEditor() const override { return true; }
    juce::AudioProcessorEditor* createEditor() override;
    float meter() const noexcept override { return outputPeak.load(); }
    juce::String meterLabel() const override { return "OUTPUT SAMPLE PEAK"; }
    double getTailLengthSeconds() const override { return 10.003; }
    bool processingReady() const noexcept override { return ready.load() && !pending.load(); }
    // Call before making the pointer callback-visible; first process also seals.
    void markPublished() noexcept { published.store(true); }
    void sealNativeStateForPublication() noexcept override { markPublished(); }
    juce::Result loadSampleUnpublished(int,const juce::File&,double);
    juce::Result clearSampleUnpublished(int,double);
    const nativeResource::ExpectedLayout& expectedNativeResourceLayout() const noexcept override;
    bool hasPendingNativeStateWork() const noexcept override { return pending.load(); }
    juce::Result tryCaptureNativeState(NativeProcessorSnapshot&) override;
    juce::Result validateNativeState(const NativeProcessorSnapshot&) const override;
    juce::Result restoreNativeStateOffThread(const NativeProcessorSnapshot&,const NativeRestoreContext&) override;
    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*,int) override; // Live binary restore rejected.
    juce::String lastResourceError() const;
    bool detectorHistory(drum::DetectorHistorySnapshot& out) const noexcept { return core.detectorHistory(out); }
    uint64_t hitCount() const noexcept { return hits.load(); }
    float detectorPeak() const noexcept { return detectionPeak.load(); }
    float detectorThresholdDb() const noexcept { return thresholdDb.load(); }
    std::array<nativeResource::ResourceDescriptor,4> sampleDescriptors() const;
private:
    struct Sample { nativeResource::ResourceDescriptor descriptor; juce::AudioBuffer<float> source, prepared; };
    std::array<Sample,4> samples;
    drum::Core core;
    mutable std::mutex resourceMutex; // Off-thread only, never touched in process.
    std::atomic<bool> pending{false},ready{false},published{false};
    std::atomic<float> outputPeak{0},detectionPeak{0},thresholdDb{-33};std::atomic<uint64_t> hits{0};
    uint64_t revision=0; double preparedRate=0; juce::String error;
    static std::vector<ParameterSpec> specifications();
    static juce::Result decode(const juce::File&,int,Sample&,const nativeResource::ResourceDescriptor*);
    static juce::Result resample(Sample&,double);
    bool prepareLocked(double);
    nativeResource::State manifestLocked() const;
};
}




