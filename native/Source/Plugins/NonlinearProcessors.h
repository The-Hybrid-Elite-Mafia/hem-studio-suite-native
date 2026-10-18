#pragma once
#include "Processors.h"
#include "NonlinearTelemetry.h"
#include "../DSP/NonlinearCore.h"
namespace hem {
class ParallelProcessor final:public ModuleProcessor {
public:ParallelProcessor();void prepareToPlay(double,int) override;void processBlock(juce::AudioBuffer<float>&,juce::MidiBuffer&) override;
    juce::AudioProcessorEditor* createEditor() override;void setStateInformation(const void*,int) override;
    float meter() const noexcept override {return core.peak.load();}juce::String meterLabel() const override {return "OUTPUT SAMPLE PEAK / dBFS";}
    bool processingReady() const noexcept override{return ready.load();}
    void releaseResources() override{ready.store(false);actualRate.store(0);telemetry.reset();resetMeterTelemetry();ModuleProcessor::releaseResources();}
    NonlinearTelemetry telemetry;
private:std::atomic<bool> ready{false};ParallelCore core;
};
class ExciterProcessor final:public ModuleProcessor {
public:ExciterProcessor();void prepareToPlay(double,int) override;void processBlock(juce::AudioBuffer<float>&,juce::MidiBuffer&) override;
    juce::AudioProcessorEditor* createEditor() override;void setStateInformation(const void*,int) override;
    float meter() const noexcept override {return core.peak.load();}juce::String meterLabel() const override {return "OUTPUT SAMPLE PEAK / dBFS";}
    float activity(size_t band) const noexcept {return core.activity[band].load();}
    bool processingReady() const noexcept override{return ready.load();}
    void releaseResources() override{ready.store(false);actualRate.store(0);telemetry.reset();resetMeterTelemetry();ModuleProcessor::releaseResources();}
    NonlinearTelemetry telemetry;
private:std::atomic<bool> ready{false};ExciterCore core;
};
}
