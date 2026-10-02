#pragma once
#include "Processors.h"
#include "../DSP/FinishingCore.h"
namespace hem {
class VocalProcessor final : public ModuleProcessor {
public:
    VocalProcessor();
    void prepareToPlay(double,int) override;
    void processBlock(juce::AudioBuffer<float>&,juce::MidiBuffer&) override;
    juce::AudioProcessorEditor* createEditor() override;
    float meter() const noexcept override { return core.peak.load(std::memory_order_relaxed); }
    juce::String meterLabel() const override { return "OUTPUT SAMPLE PEAK / dBFS"; }
private: VocalCore core;
};
class TubeProcessor final : public ModuleProcessor {
public:
    TubeProcessor();
    void prepareToPlay(double,int) override;
    void processBlock(juce::AudioBuffer<float>&,juce::MidiBuffer&) override;
    juce::AudioProcessorEditor* createEditor() override;
    float meter() const noexcept override { return core.peak.load(std::memory_order_relaxed); }
    juce::String meterLabel() const override { return "OUTPUT SAMPLE PEAK / dBFS"; }
private: TubeCore core;
};
class TapeProcessor final : public ModuleProcessor {
public:
    TapeProcessor();
    void prepareToPlay(double,int) override;
    void processBlock(juce::AudioBuffer<float>&,juce::MidiBuffer&) override;
    juce::AudioProcessorEditor* createEditor() override;
    float meter() const noexcept override { return core.peak.load(std::memory_order_relaxed); }
    juce::String meterLabel() const override { return "OUTPUT SAMPLE PEAK / dBFS"; }
private: TapeCore core;
};
class MonitorProcessor final : public ModuleProcessor {
public:
    MonitorProcessor();
    void prepareToPlay(double,int) override;
    void processBlock(juce::AudioBuffer<float>&,juce::MidiBuffer&) override;
    juce::AudioProcessorEditor* createEditor() override;
    float meter() const noexcept override { return core.peak.load(std::memory_order_relaxed); }
    juce::String meterLabel() const override { return "OUTPUT SAMPLE PEAK / dBFS"; }
    bool monitorOnly() const noexcept override {return true;}
private: MonitorCore core;
};
}
