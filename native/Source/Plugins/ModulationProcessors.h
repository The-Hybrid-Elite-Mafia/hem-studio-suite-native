#pragma once
#include "Processors.h"
#include "../DSP/ModulationCore.h"
namespace hem {
class ModulationProcessor:public ModuleProcessor {
public:
    using ModuleProcessor::ModuleProcessor;
    // One atomic publishes both presence and value coherently. -1 means the
    // native host has never supplied tempo; 0 is an explicitly invalid tempo.
    void setBlockTempo(double bpm) noexcept override;
    double nativeTempoBpm()const noexcept{return nativeBpm.load(std::memory_order_relaxed);}
    bool processingReady()const noexcept override{return ready.load();}
    void releaseResources()override{ready.store(false);ModuleProcessor::releaseResources();}
    juce::ValueTree migrateParameterState(const juce::ValueTree&)const override;
    void setStateInformation(const void*,int)override;
    juce::String meterLabel()const override{return "OUTPUT SAMPLE PEAK / dBFS";}
    virtual modulation::Display modulationDisplay()const noexcept=0;
protected:
    modulation::Transport hostTempo()const noexcept;
    std::atomic<bool>ready{false};
private:
    std::atomic<double> nativeBpm{-1};
    static_assert(std::atomic<double>::is_always_lock_free);
};
class ChorusFlangerPhaserProcessor final:public ModulationProcessor {
public:
    ChorusFlangerPhaserProcessor();
    void prepareToPlay(double,int)override;
    void processBlock(juce::AudioBuffer<float>&,juce::MidiBuffer&)override;
    void reset()override{core.reset();}
    juce::AudioProcessorEditor*createEditor()override;
    float meter()const noexcept override{return core.peak.load();}
    double getTailLengthSeconds()const override{return 4;}
    bool parameterActive(const juce::String&)const override;
    juce::String helpText()const override;
    ChorusFlangerPhaserSettings settings()const noexcept;
    modulation::Display modulationDisplay()const noexcept override{return core.display();}
private:ChorusFlangerPhaserCore core;
};
class RotaryTremoloAutopanProcessor final:public ModulationProcessor {
public:
    RotaryTremoloAutopanProcessor();
    void prepareToPlay(double,int)override;
    void processBlock(juce::AudioBuffer<float>&,juce::MidiBuffer&)override;
    void reset()override{core.reset();}
    juce::AudioProcessorEditor*createEditor()override;
    float meter()const noexcept override{return core.peak.load();}
    double getTailLengthSeconds()const override{return .012;}
    bool parameterActive(const juce::String&)const override;
    juce::String helpText()const override;
    RotaryTremoloAutopanSettings settings()const noexcept;
    modulation::Display modulationDisplay()const noexcept override{return core.display();}
private:RotaryTremoloAutopanCore core;
};
}
