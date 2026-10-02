#pragma once
#include "Processors.h"
#include "../DSP/SpecialtyCore.h"
#include "../DSP/RestorerCore.h"
namespace hem {
class DhtaProcessor final : public ModuleProcessor {
public:
    DhtaProcessor();
    void prepareToPlay(double,int) override;
    void processBlock(juce::AudioBuffer<float>&,juce::MidiBuffer&) override;
    juce::AudioProcessorEditor* createEditor() override;
    float meter() const noexcept override { return core.reduction.load(std::memory_order_relaxed); }
    juce::String meterLabel() const override { return "GAIN REDUCTION / dB"; }
private: DhtaCore core;
};
class TransientProcessor final : public ModuleProcessor {
public:
    TransientProcessor();
    void prepareToPlay(double,int) override;
    void processBlock(juce::AudioBuffer<float>&,juce::MidiBuffer&) override;
    juce::AudioProcessorEditor* createEditor() override;
    float meter() const noexcept override { return core.gainChange.load(std::memory_order_relaxed); }
    juce::String meterLabel() const override { return "ENVELOPE GAIN / dB"; }
private: TransientCore core;
};
class SubBassProcessor final : public ModuleProcessor {
public:
    SubBassProcessor();
    void prepareToPlay(double,int) override;
    void processBlock(juce::AudioBuffer<float>&,juce::MidiBuffer&) override;
    juce::AudioProcessorEditor* createEditor() override;
    float meter() const noexcept override { return core.peak.load(std::memory_order_relaxed); }
    juce::String meterLabel() const override { return "OUTPUT SAMPLE PEAK / dBFS"; }
private: SubBassCore core;
};
class RestorerProcessor final : public ModuleProcessor {
public:
    RestorerProcessor();
    void prepareToPlay(double,int) override;
    void processBlock(juce::AudioBuffer<float>&,juce::MidiBuffer&) override;
    juce::AudioProcessorEditor* createEditor() override;
    float meter() const noexcept override { return core.reduction.load(std::memory_order_relaxed); }
    juce::String meterLabel() const override { return "MAX BAND REDUCTION / dB"; }
private: RestorerCore core;
};
}
