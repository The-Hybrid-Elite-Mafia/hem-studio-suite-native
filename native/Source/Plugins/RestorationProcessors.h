#pragma once
#include "Processors.h"
#include "../DSP/RestorationCore.h"
#include "../DSP/ClipRepairCore.h"
namespace hem {
class RestorationProcessor:public ModuleProcessor {
public:
    RestorationProcessor(juce::String identifier,juce::String title,std::vector<ParameterSpec> specs)
        :ModuleProcessor(std::move(identifier),std::move(title),std::move(specs)){actualRate.store(0);}
    // Lifecycle calls and processBlock have one serialized audio/host writer.
    // Published meter payloads remain atomic; no callback allocation or GUI work.
    void reset()override;
    void releaseResources()override;
    bool processingReady()const noexcept override{return ready.load(std::memory_order_acquire);}
    juce::ValueTree migrateParameterState(const juce::ValueTree&)const override;
    void setStateInformation(const void*,int)override;
    juce::String meterLabel()const override{return "OUTPUT SAMPLE PEAK / dBFS";}
    virtual restoration::Display restorationDisplay()const noexcept=0;
protected:
    void beginPreparation()noexcept;
    void finishPreparation(double,int,bool);
    virtual void resetCoreTelemetry()noexcept=0;
    template<class Snapshot>restoration::Display availableDisplay(Snapshot snapshot)const noexcept{
        const auto epoch=lifecycleEpoch.load();
        if(!processingReady())return {};
        auto display=snapshot();
        if(!display.valid||!processingReady()||epoch!=lifecycleEpoch.load())return {};
        return display;
    }
    std::atomic<bool>ready{false};
private:std::atomic<uint64_t>lifecycleEpoch{0};
};
class DynamicResonanceSuppressorProcessor final:public RestorationProcessor {
public:
    DynamicResonanceSuppressorProcessor();void prepareToPlay(double,int)override;void processBlock(juce::AudioBuffer<float>&,juce::MidiBuffer&)override;
    juce::AudioProcessorEditor*createEditor()override;float meter()const noexcept override{return processingReady()?core.peak.load():0.f;}juce::String helpText()const override;
    DynamicResonanceSettings settings()const noexcept;restoration::Display restorationDisplay()const noexcept override{return availableDisplay([this]{return core.display();});}
private:void resetCoreTelemetry()noexcept override{core.reset();}DynamicResonanceCore core;
};
class BreathMouthControlProcessor final:public RestorationProcessor {
public:
    BreathMouthControlProcessor();void prepareToPlay(double,int)override;void processBlock(juce::AudioBuffer<float>&,juce::MidiBuffer&)override;
    juce::AudioProcessorEditor*createEditor()override;float meter()const noexcept override{return processingReady()?core.peak.load():0.f;}juce::String helpText()const override;
    BreathMouthSettings settings()const noexcept;restoration::Display restorationDisplay()const noexcept override{return availableDisplay([this]{return core.display();});}
private:void resetCoreTelemetry()noexcept override{core.reset();}BreathMouthCore core;
};
class ClipRepairProcessor final:public RestorationProcessor {
public:
    ClipRepairProcessor();void prepareToPlay(double,int)override;void processBlock(juce::AudioBuffer<float>&,juce::MidiBuffer&)override;
    juce::AudioProcessorEditor*createEditor()override;float meter()const noexcept override{return processingReady()?core.peak.load():0.f;}juce::String helpText()const override;
    ClipRepairSettings settings()const noexcept;restoration::Display restorationDisplay()const noexcept override{return availableDisplay([this]{return telemetry.snapshot();});}
private:void resetCoreTelemetry()noexcept override{core.reset();telemetry.reset();}ClipRepairCore core;restoration::Telemetry telemetry;
};
}
