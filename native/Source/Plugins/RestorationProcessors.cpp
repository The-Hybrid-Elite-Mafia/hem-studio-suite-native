#include "RestorationProcessors.h"
#include "RestorationEditors.h"
#include <cstdlib>
#include <cerrno>
namespace hem {
namespace {
// JUCE normalisation can round even an exactly specified semantic default.
// Store the same canonical raw value that host automation and recall produce.
void initialiseCanonicalDefaults(RestorationProcessor& processor){
    for(const auto& spec:processor.parameterSpecs()){
        auto* parameter=processor.state.getParameter(spec.id);
        parameter->setValueNotifyingHost(parameter->convertTo0to1(spec.initial));
    }
    auto canonical=processor.state.copyState();
    for(auto child:canonical)
        child.setProperty("value",double(processor.state.getRawParameterValue(child["id"].toString())->load()),nullptr);
    processor.state.replaceState(canonical);
}
}
void RestorationProcessor::reset(){
    const bool prepared=ready.exchange(false,std::memory_order_acq_rel);
    lifecycleEpoch.fetch_add(1);
    resetCoreTelemetry();resetMeterTelemetry();ModuleProcessor::releaseResources();
    if(prepared){publishMeterPreparation();ready.store(true,std::memory_order_release);}
}
void RestorationProcessor::releaseResources(){
    ready.store(false,std::memory_order_release);actualRate.store(0);
    reset();
}
void RestorationProcessor::beginPreparation()noexcept{
    releaseResources();setLatencySamples(0);
}
void RestorationProcessor::finishPreparation(double rate,int latency,bool valid){
    if(!valid)return;
    actualRate.store(rate);setLatencySamples(latency);publishMeterPreparation();
    ready.store(true,std::memory_order_release);
}
juce::ValueTree RestorationProcessor::migrateParameterState(const juce::ValueTree&t)const{
    if((!t.hasType("PARAMETERS")&&!t.hasType(state.state.getType()))||t.getNumProperties()!=0||t.getNumChildren()!=int(parameterSpecs().size()))return {};
    std::vector<bool>seen(parameterSpecs().size(),false);
    for(const auto child:t){if(!child.hasType("PARAM")||child.getNumChildren()!=0||child.getNumProperties()!=2||!child.hasProperty("id")||!child.hasProperty("value"))return {};size_t i=0;while(i<parameterSpecs().size()&&child["id"].toString()!=parameterSpecs()[i].id)++i;if(i==seen.size()||seen[i])return {};seen[i]=true;const auto token=child["value"].toString();if(token.isEmpty()||!token.containsOnly("0123456789+-.eE"))return {};const auto text=token.toStdString();char*end=nullptr;errno=0;const double number=std::strtod(text.c_str(),&end);const auto&s=parameterSpecs()[i];const double epsilon=4*std::numeric_limits<float>::epsilon()*std::max(1.,std::abs(number));if(errno==ERANGE||end==text.c_str()||*end||!std::isfinite(number)||number<s.minimum-epsilon||number>s.maximum+epsilon||(s.step>=1&&std::abs(number-std::round(number))>epsilon))return {};}
    return t.createCopy();
}
void RestorationProcessor::setStateInformation(const void*data,int bytes){if(!data||bytes<=0||bytes>1024*1024)return;if(auto xml=getXmlFromBinary(data,bytes)){auto tree=migrateParameterState(juce::ValueTree::fromXml(*xml));if(tree.isValid()&&tree.hasType(state.state.getType()))state.replaceState(tree);}}
DynamicResonanceSuppressorProcessor::DynamicResonanceSuppressorProcessor():RestorationProcessor("DYNAMIC_RESONANCE_SUPPRESSOR","Dynamic Resonance Suppressor",{
 {"sensitivity","SENSITIVITY",0,100,50,.1f,"%"},{"low","LOW LIMIT",80,16000,120,1,"Hz"},{"high","HIGH LIMIT",200,20000,14000,1,"Hz"},{"selectivity","SELECTIVITY",0,100,50,.1f,"%"},{"maxReduction","MAX REDUCTION",0,24,0,.1f,"dB"},{"attack","ATTACK",.5f,200,20,.1f,"ms"},{"release","RELEASE",10,2000,150,1,"ms"},{"mix","MIX",0,100,100,.1f,"%"},{"output","OUTPUT",-24,6,0,.1f,"dB"},{"profile","DETECTION PROFILE",0,2,0,1,""},{"soloDelta","SOLO DELTA",0,1,0,1,"",true},{"bypass","BYPASS",0,1,0,1,"",true}}){initialiseCanonicalDefaults(*this);}
DynamicResonanceSettings DynamicResonanceSuppressorProcessor::settings()const noexcept{DynamicResonanceSettings p;p.sensitivity=value(0);p.low=value(1);p.high=value(2);p.selectivity=value(3);p.maxReduction=value(4);p.attack=value(5);p.release=value(6);p.mix=value(7);p.output=value(8);p.profile=int(std::lround(value(9)));p.soloDelta=value(10)>.5f;p.bypass=value(11)>.5f;return p;}
void DynamicResonanceSuppressorProcessor::prepareToPlay(double rate,int){beginPreparation();const bool ok=core.prepare(rate,settings());finishPreparation(rate,0,ok);}
void DynamicResonanceSuppressorProcessor::processBlock(juce::AudioBuffer<float>&b,juce::MidiBuffer&){juce::ScopedNoDenormals guard;if(!processingReady()){b.clear();return;}if(b.getNumChannels()<2||b.getNumSamples()<=0)return;captureInput(b);core.process(b.getWritePointer(0),b.getWritePointer(1),b.getNumSamples(),settings());publishMeterBlock(b);}
juce::String DynamicResonanceSuppressorProcessor::helpText()const{return "Causal 32-band resonance prominence detector and peaking cuts, stereo linked; 0 samples added latency. BROAD uses the selected bounds, VOCAL additionally limits 200-8000 Hz, HIGH limits to 3000 Hz and above. Selectivity sets cut Q; sensitivity lowers prominence threshold. Maximum Reduction caps the SUM of filter cuts (frozen response bound, not an instantaneous limiter). Onsets inhibit detection for 8 ms. Warm-up 20 ms. Measured constant-Q INPUT/OUTPUT bands and input spectral history; DELTA is the actual removed signal before output gain. Low/high crossed limits are sorted. This processor can attenuate intentional tonal content; tune manually and audition delta. Default maximum reduction 0 dB is neutral.";}
juce::AudioProcessorEditor*DynamicResonanceSuppressorProcessor::createEditor(){return new RestorationEditor(*this,2);}
BreathMouthControlProcessor::BreathMouthControlProcessor():RestorationProcessor("BREATH_MOUTH_CONTROL","Breath / Mouth Control",{
 {"breathDetection","BREATH DETECTION",0,100,50,.1f,"%"},{"breathReduction","BREATH REDUCTION",0,24,0,.1f,"dB"},{"breathLength","BREATH LENGTH",10,500,100,1,"ms"},{"mouthDetection","MOUTH DETECTION",0,100,50,.1f,"%"},{"mouthReduction","MOUTH REDUCTION",0,24,0,.1f,"dB"},{"mouthLength","MOUTH LENGTH",.05f,2,.5f,.01f,"ms"},{"low","LOW FOCUS",100,8000,1500,1,"Hz"},{"high","HIGH FOCUS",2000,20000,12000,1,"Hz"},{"listenBreath","LISTEN BREATHS",0,1,0,1,"",true},{"listenMouth","LISTEN MOUTH",0,1,0,1,"",true},{"bypass","BYPASS",0,1,0,1,"",true}}){initialiseCanonicalDefaults(*this);}
BreathMouthSettings BreathMouthControlProcessor::settings()const noexcept{BreathMouthSettings p;p.breathDetection=value(0);p.breathReduction=value(1);p.breathLength=value(2);p.mouthDetection=value(3);p.mouthReduction=value(4);p.mouthLength=value(5);p.low=value(6);p.high=value(7);p.listenBreath=value(8)>.5f;p.listenMouth=value(9)>.5f;p.bypass=value(10)>.5f;return p;}
void BreathMouthControlProcessor::prepareToPlay(double rate,int){beginPreparation();const bool ok=core.prepare(rate,settings());finishPreparation(rate,core.latency(),ok);}
void BreathMouthControlProcessor::processBlock(juce::AudioBuffer<float>&b,juce::MidiBuffer&){juce::ScopedNoDenormals guard;if(!processingReady()){b.clear();return;}if(b.getNumChannels()<2||b.getNumSamples()<=0)return;captureInput(b);core.process(b.getWritePointer(0),b.getWritePointer(1),b.getNumSamples(),settings());publishMeterBlock(b);}
juce::String BreathMouthControlProcessor::helpText()const{return "Manual-tunable conservative noise/impulse detector, NOT semantic separation. Breath likelihood uses eight focus-band energies and low signal level; it can also detect hiss/fricatives. Mouth likelihood requires an isolated sample with much quieter adjacent shoulders; it can also detect intentional clicks. Sensitivity lowers thresholds; lengths set attenuation release windows. Stereo linked attenuation; 2.5 ms fixed lookahead, also maintained while bypassed. LISTEN buttons audition actual removed breath and mouth contributions; selecting both sums them. OUTPUT is the measured output sample peak. Both reduction defaults are 0 dB, preserving delayed input. Use audition and manual settings, especially for percussion. Mouth detection has 10 ms startup warm-up.";}
juce::AudioProcessorEditor*BreathMouthControlProcessor::createEditor(){return new RestorationEditor(*this,6);}
ClipRepairProcessor::ClipRepairProcessor():RestorationProcessor("CLIP_REPAIR","Clip Repair",{
 {"threshold","THRESHOLD",-24,0,-.3f,.1f,"dBFS"},{"length","INTERPOLATION LENGTH",.1f,4,2,.1f,"ms"},{"amount","REPAIR AMOUNT",0,100,0,.1f,"%"},{"output","OUTPUT GAIN",-24,6,0,.1f,"dB"},{"algorithm","ALGORITHM",0,1,1,1,""},{"listenRepair","LISTEN REPAIR",0,1,0,1,"",true},{"bypass","BYPASS",0,1,0,1,"",true}}){initialiseCanonicalDefaults(*this);}
ClipRepairSettings ClipRepairProcessor::settings()const noexcept{ClipRepairSettings p;p.threshold=value(0);p.interpolationLength=value(1);p.amount=value(2);p.output=value(3);p.algorithm=int(std::lround(value(4)));p.listenRepair=value(5)>.5f;p.bypass=value(6)>.5f;return p;}
void ClipRepairProcessor::prepareToPlay(double rate,int){beginPreparation();core.prepare(rate,settings());telemetry.prepare(restoration::rateValid(rate)?rate:0,.1f);finishPreparation(rate,core.latency(),core.preparedRateWasValid());}
void ClipRepairProcessor::processBlock(juce::AudioBuffer<float>&b,juce::MidiBuffer&){juce::ScopedNoDenormals guard;if(!processingReady()){b.clear();return;}if(b.getNumChannels()<2||b.getNumSamples()<=0)return;captureInput(b);core.processObserved(b.getWritePointer(0),b.getWritePointer(1),b.getNumSamples(),settings(),[this](const auto&dry,const auto&wet,const auto&delta,float){telemetry.sample(dry[0],wet[0],delta[0]);});telemetry.status(core.activity.load(),0,0,0,core.clipEvents.load(),core.repairedSamples.load(),core.skippedEvents.load());publishMeterBlock(b);}
juce::String ClipRepairProcessor::helpText()const{return "Waveform ESTIMATION, not recovery of the original. Requires a short, flat same-sign plateau above threshold and consistent shoulders. FAST uses bounded cubic interpolation; PRECISE validates a local sinusoidal estimate and conservatively skips unsuitable damage. INTERPOLATION LENGTH limits the repair span. Long/ambiguous plateaus are skipped and counted. Added latency stays within the existing 6 ms slot and is maintained in bypass. Repaired samples count only actual applied changes. LISTEN REPAIR auditions the aligned estimated contribution after output gain/bypass. Default amount 0% is neutral. Reconstructed peaks can exceed 0 dBFS; lower output gain as needed.";}
juce::AudioProcessorEditor*ClipRepairProcessor::createEditor(){return new RestorationEditor(*this,9);}
}
