#pragma once
#include "Processors.h"
#include "NativeResourceStateProvider.h"
#include <mutex>
#include "../DSP/AmpCabinetCore.h"
namespace hem {
// Resource changes require unpublished replacement instances; processing never locks.
class AmpCabinetProcessor final : public ModuleProcessor, public NativeResourceStateProvider {
public:
    AmpCabinetProcessor();
    void prepareToPlay(double,int) override;
    void releaseResources() override;
    void processBlock(juce::AudioBuffer<float>&,juce::MidiBuffer&) override;
    bool processingReady() const noexcept override { return ready.load(std::memory_order_relaxed); }
    bool hasEditor() const override { return true; }
    juce::AudioProcessorEditor* createEditor() override;
    float meter() const noexcept override;
    juce::String meterLabel() const override {return "OUTPUT SAMPLE PEAK / dBFS";}
    juce::String helpText() const override;
    double getTailLengthSeconds() const override {return 1.;}
    bool validateStoredParameters(const juce::ValueTree&) const override;
    void setStateInformation(const void*,int) override;
    AmpCabinetSettings settings() const noexcept;
    static constexpr bool supportsCustomIR() noexcept {return true;}
    juce::Result loadIRUnpublished(const juce::File&,double);
    const nativeResource::ExpectedLayout& expectedNativeResourceLayout() const noexcept override;
    bool hasPendingNativeStateWork() const noexcept override {return pending.load();}
    void sealNativeStateForPublication() noexcept override {published.store(true);}
    juce::Result tryCaptureNativeState(NativeProcessorSnapshot&) override;
    juce::Result validateNativeState(const NativeProcessorSnapshot&) const override;
    juce::Result restoreNativeStateOffThread(const NativeProcessorSnapshot&,const NativeRestoreContext&) override;
    void getStateInformation(juce::MemoryBlock&) override;
    nativeResource::ResourceDescriptor irDescriptor() const;
    juce::String lastResourceError() const;
    std::array<float,128> irResponse() const;
    static const char* ampModelName(int) noexcept;
    static const char* cabinetModelName(int) noexcept;
    static const char* micModelName(int) noexcept;
private:
    AmpCabinetCore core;
    mutable std::mutex resourceMutex;
    std::atomic<bool> pending{false},published{false};
    nativeResource::ResourceDescriptor descriptor{"cabinet-ir",0};
    juce::AudioBuffer<float> source;
    std::unique_ptr<reverb::PreparedConvolution> convolution;
    std::array<float,128> response{};
    juce::String error;
    uint64_t revision=0;
    bool prepareLocked(double);
    static juce::Result decodeIR(const juce::File&,juce::AudioBuffer<float>&,nativeResource::ResourceDescriptor&,const nativeResource::ResourceDescriptor*);
    std::atomic<bool> ready{false};
    bool validateTree(const juce::ValueTree&,bool) const;
};
}
