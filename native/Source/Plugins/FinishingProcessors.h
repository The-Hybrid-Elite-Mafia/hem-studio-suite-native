#pragma once
#include "Processors.h"
#include "NonlinearTelemetry.h"
#include "../DSP/FinishingCore.h"
#include "../Systems/VocalSpectrum.h"
namespace hem {
class VocalProcessor final : public ModuleProcessor {
public:
    VocalProcessor();
    void releaseResources() override { spectrum->suspend(); ModuleProcessor::releaseResources(); }
    VocalSpectrum& vocalSpectrum() noexcept { return *spectrum; }
    void prepareToPlay(double,int) override;
    void processBlock(juce::AudioBuffer<float>&,juce::MidiBuffer&) override;
    juce::AudioProcessorEditor* createEditor() override;
    float meter() const noexcept override { return core.peak.load(std::memory_order_relaxed); }
    juce::String meterLabel() const override { return "OUTPUT SAMPLE PEAK / dBFS"; }
private: VocalCore core; std::unique_ptr<VocalSpectrum> spectrum=std::make_unique<VocalSpectrum>();
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
public:
    bool processingReady() const noexcept override{return ready.load();}
    void releaseResources() override{ready.store(false);actualRate.store(0);telemetry.reset();resetMeterTelemetry();ModuleProcessor::releaseResources();}
    NonlinearTelemetry telemetry;
private: std::atomic<bool> ready{false};TapeCore core;
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
