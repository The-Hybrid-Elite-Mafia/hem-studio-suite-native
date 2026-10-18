#include "PitchTimeProcessors.h"
#include "PitchTimeEditors.h"
#include <stdexcept>
namespace hem {
std::vector<ParameterSpec> PitchTimeProcessor::specifications(bool stretch){
 std::vector<ParameterSpec> p{{"pitchSemitones","Pitch",-24,24,0,1,"st"},{"pitchCents","Fine Pitch",-100,100,0,.1f,"ct"},{"output","Output",-24,12,0,.01f,"dB"},{"bypass","Audition Original",0,1,0,1,"",true}};
 if(stretch){const std::vector<ParameterSpec> extra{{"stretchRatio","Stretch",.5f,2,1,.001f,"x"},{"formantPreserve","Formant Preserve",0,1,1,1,"",true},{"sourceTempo","Source Tempo",20,400,120,.1f,"BPM"},{"targetTempo","Target Tempo",20,400,120,.1f,"BPM"},{"tempoSync","Project Tempo Sync",0,1,0,1,"",true},{"timebase","Timebase",0,1,0,1,""},{"algorithm","Algorithm",0,2,2,1,""},{"grid","Display Grid",0,1,0,1,""}};p.insert(p.end(),extra.begin(),extra.end());}
 else {const std::vector<ParameterSpec> extra{{"formantSemitones","Formant",-12,12,0,1,"st"},{"formantCents","Fine Formant",-100,100,0,.1f,"ct"},{"key","Key",0,11,0,1,""},{"scale","Scale",0,2,0,1,""},{"correctionEnabled","Monophonic Correction",0,1,0,1,"",true},{"correctionAmount","Correction Amount",0,1,1,.001f,""},{"correctionSpeedMs","Correction Speed",0,1000,80,.1f,"ms"},{"mix","Mix",0,1,1,.001f,""}};p.insert(p.end(),extra.begin(),extra.end());}
 return p;
}
PitchTimeProcessor::PitchTimeProcessor(bool stretch):ModuleProcessor(stretch?"TIME_PITCH_STRETCH":"PITCH_FORMANT",stretch?"Time / Pitch Stretch":"Pitch / Formant",specifications(stretch)),timeStretch(stretch){}
void PitchTimeProcessor::prepareToPlay(double,int){throw std::runtime_error("Pitch/time is offline-only; use the source render workflow, never a realtime insert");}
juce::AudioProcessorEditor* PitchTimeProcessor::createEditor(){return new PitchTimeEditor(*this);}
double PitchTimeProcessor::parameter(const char* id)const{auto* p=state.getRawParameterValue(id);return p?p->load():0;}
juce::Result PitchTimeProcessor::settings(double rate,int channels,double bpm,pitchtime::Settings& out)const{
 for(const auto& spec:parameterSpecs()){const double v=parameter(spec.id);if(!std::isfinite(v)||v<spec.minimum||v>spec.maximum)return juce::Result::fail("Invalid offline parameter: "+juce::String(spec.id));}
 pitchtime::Settings s;s.sampleRate=rate;s.channels=channels;s.pitchSemitones=parameter("pitchSemitones")+parameter("pitchCents")*.01;s.outputDb=parameter("output");s.wetMix=timeStretch?1:parameter("mix");
 if(!std::isfinite(rate)||rate<8000||rate>192000||channels<1||channels>2)return juce::Result::fail("Source must be mono/stereo, 8-192 kHz");
 if(std::abs(s.pitchSemitones)>24)return juce::Result::fail("Combined coarse and fine pitch exceeds +/-24 semitones");
 if(timeStretch){
  const bool sync=parameter("tempoSync")>.5;const double target=sync?bpm:parameter("targetTempo");
  if(!std::isfinite(target)||target<20||target>400)return juce::Result::fail("Target/project tempo must be 20-400 BPM");
  s.durationRatio=sync?parameter("sourceTempo")/target:parameter("stretchRatio");
  if(s.durationRatio<.5||s.durationRatio>2)return juce::Result::fail("Duration ratio must be 0.5-2; tempo ratio is outside supported range");
  if(!sync&&std::abs(parameter("sourceTempo")/s.durationRatio-target)>.11)return juce::Result::fail("Tempo and stretch ratio disagree or exceed the 20-400 BPM range; adjust source tempo/ratio");
  s.preserveFormants=parameter("formantPreserve")>.5;s.strategy=static_cast<pitchtime::Strategy>(int(parameter("algorithm")));
 }else{
  s.formantSemitones=parameter("formantSemitones")+parameter("formantCents")*.01;
  if(std::abs(s.formantSemitones)>12)return juce::Result::fail("Combined formant shift exceeds +/-12 semitones");
  s.correction.enabled=parameter("correctionEnabled")>.5;s.correction.key=int(parameter("key"));s.correction.scale=static_cast<pitchtime::Scale>(int(parameter("scale")));
  s.correction.amount=parameter("correctionAmount");s.correction.speedMs=parameter("correctionSpeedMs");
 }
 out=s;return juce::Result::ok();
}
}
