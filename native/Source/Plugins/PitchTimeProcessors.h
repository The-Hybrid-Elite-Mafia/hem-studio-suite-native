#pragma once
#include "Processors.h"
#include "../DSP/PitchTimeBackend.h"
namespace hem {
class PitchTimeProcessor final : public ModuleProcessor {
public:
 explicit PitchTimeProcessor(bool timeStretch);
 const bool timeStretch;
 static constexpr bool offlineOnly=true;
 bool realtimeInsertAllowed() const noexcept override {return false;}
 void prepareToPlay(double,int) override;
 void processBlock(juce::AudioBuffer<float>& b,juce::MidiBuffer&) override {b.clear();}
 bool processingReady() const noexcept override {return false;}
 float meter() const noexcept override {return 0;}
 juce::String meterLabel() const override {return "OFFLINE ONLY";}
 juce::String helpText() const override {return "Offline whole-source operation. Monophonic correction only. Preview and Apply are host commands; never a realtime insert.";}
 juce::AudioProcessorEditor* createEditor() override;
 // Message-thread capture. Does not clamp invalid compound pitch/BPM recipes.
 juce::Result settings(double sampleRate,int channels,double projectBpm,pitchtime::Settings&) const;
 double parameter(const char*) const;
private:
 static std::vector<ParameterSpec> specifications(bool);
};
}
