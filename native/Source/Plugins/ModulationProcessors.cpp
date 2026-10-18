#include "ModulationProcessors.h"
#include "ModulationEditors.h"
#include <cstdlib>
namespace hem {
namespace {
void initialiseCanonicalDefaults(ModulationProcessor& processor){
    // JUCE's initial APVTS adapter cache can contain an unsnapped skew value.
    // Use the unchanged parameter conversion contract once before publication,
    // then synchronise its cached tree with the legal raw values.
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

void ModulationProcessor::setBlockTempo(double bpm) noexcept {
    nativeBpm.store(modulation::sanitize({bpm,true}).bpm,std::memory_order_relaxed);
}
modulation::Transport ModulationProcessor::hostTempo()const noexcept{
    const double bpm=nativeBpm.load(std::memory_order_relaxed);
    if(bpm!=-1)return modulation::sanitize({bpm,true});
    modulation::Transport t;
    if(auto*head=getPlayHead())if(const auto position=head->getPosition())if(const auto bpm=position->getBpm()){t.bpm=*bpm;t.valid=true;}
    return modulation::sanitize(t);
}
juce::ValueTree ModulationProcessor::migrateParameterState(const juce::ValueTree&t)const{
    if((!t.hasType("PARAMETERS")&&!t.hasType(state.state.getType()))||t.getNumProperties()!=0||t.getNumChildren()!=int(parameterSpecs().size()))return {};
    std::vector<bool>seen(parameterSpecs().size(),false);
    for(const auto child:t){if(!child.hasType("PARAM")||child.getNumChildren()!=0||child.getNumProperties()!=2||!child.hasProperty("id")||!child.hasProperty("value"))return {};size_t i=0;while(i<parameterSpecs().size()&&child["id"].toString()!=parameterSpecs()[i].id)++i;if(i==seen.size()||seen[i])return {};seen[i]=true;const auto text=child["value"].toString().toStdString();char*end=nullptr;const double number=std::strtod(text.c_str(),&end);const auto&s=parameterSpecs()[i];const double epsilon=4*std::numeric_limits<float>::epsilon()*std::max(1.,std::abs(number));if(end==text.c_str()||*end||!std::isfinite(number)||number<s.minimum-epsilon||number>s.maximum+epsilon||(s.step>=1&&std::abs(number-std::round(number))>epsilon))return {};}
    return t.createCopy();
}
void ModulationProcessor::setStateInformation(const void*data,int bytes){if(!data||bytes<=0||bytes>1024*1024)return;if(auto xml=getXmlFromBinary(data,bytes)){auto tree=migrateParameterState(juce::ValueTree::fromXml(*xml));if(tree.isValid()&&tree.hasType(state.state.getType()))state.replaceState(tree);}}
ChorusFlangerPhaserProcessor::ChorusFlangerPhaserProcessor():ModulationProcessor("CHORUS_FLANGER_PHASER","Chorus / Flanger / Phaser",{
 {"depth","DEPTH",0,100,0,.1f,"%"},{"rate","RATE",.02f,10,.5f,.01f,"Hz"},{"feedback","FEEDBACK",-85,85,0,.1f,"%"},
 {"mix","MIX",0,100,0,.1f,"%"},{"output","OUTPUT",-24,6,0,.1f,"dB"},{"delay","DELAY",.1f,40,12,.01f,"ms"},{"center","CENTER",20,8000,1000,1,"Hz"},
 {"mode","EFFECT MODE",0,2,0,1,""},{"shape","LFO SHAPE",0,2,0,1,""},{"stages","STAGES",0,3,1,1,""},
 {"sync","TEMPO SYNC",0,1,0,1,"",true},{"bypass","BYPASS",0,1,0,1,"",true}
 }){initialiseCanonicalDefaults(*this);}
ChorusFlangerPhaserSettings ChorusFlangerPhaserProcessor::settings()const noexcept{ChorusFlangerPhaserSettings p;p.depth=value(0);p.rate=value(1);p.feedback=value(2);p.mix=value(3);p.output=value(4);p.delay=value(5);p.center=value(6);p.mode=int(std::lround(value(7)));p.shape=int(std::lround(value(8)));p.stages=int(std::lround(value(9)));p.sync=value(10)>.5f;p.bypass=value(11)>.5f;return p;}
void ChorusFlangerPhaserProcessor::prepareToPlay(double rate,int){ready=false;resetMeterTelemetry();actualRate.store(rate);core.prepare(rate);setLatencySamples(0);publishMeterPreparation();ready=true;}
void ChorusFlangerPhaserProcessor::processBlock(juce::AudioBuffer<float>&b,juce::MidiBuffer&){juce::ScopedNoDenormals guard;if(!processingReady()||b.getNumChannels()<2||b.getNumSamples()<=0)return;captureInput(b);core.process(b.getWritePointer(0),b.getWritePointer(1),b.getNumSamples(),settings(),hostTempo());publishMeterBlock(b);}
bool ChorusFlangerPhaserProcessor::parameterActive(const juce::String&id)const{const bool phaser=value(7)>1.5f;if(id=="center"||id=="stages")return phaser;if(id=="delay")return !phaser;return true;}
juce::String ChorusFlangerPhaserProcessor::helpText()const{return "Two-voice chorus, single modulated-delay flanger, or 2/4/6/8-stage allpass phaser. Feedback is limited to +/-85%. 20 ms control and mode ramps. Zero added host latency; delay is effect time. Mix 0%, output 0 dB is dry. Sync uses actual host BPM: RATE is cycles per quarter-note while synced, Hz otherwise. If BPM is unavailable, free-Hz processing continues and TEMPO shows NO HOST BPM. LFO phase free-runs and reset starts phase zero. Plot is actual audio-clock modulation, L cyan / R orange; mono uses left modulation.";}
juce::AudioProcessorEditor*ChorusFlangerPhaserProcessor::createEditor(){return new ModulationEditor(*this,23);}
RotaryTremoloAutopanProcessor::RotaryTremoloAutopanProcessor():ModulationProcessor("ROTARY_TREMOLO_AUTOPAN","Rotary / Tremolo / Autopan",{
 {"speed","SPEED",.02f,10,4,.01f,"Hz"},{"depth","DEPTH",0,100,0,.1f,"%"},{"width","WIDTH",0,100,100,.1f,"%"},
 {"mix","MIX",0,100,0,.1f,"%"},{"output","OUTPUT",-24,6,0,.1f,"dB"},{"ramp","ROTARY RAMP",.05f,10,1,.01f,"s"},
 {"mode","EFFECT MODE",0,2,0,1,""},{"rotary","ROTARY SPEED",0,2,0,1,""},{"shape","MOD SHAPE",0,2,0,1,""},
 {"sync","TEMPO SYNC",0,1,0,1,"",true},{"bypass","BYPASS",0,1,0,1,"",true}
 }){initialiseCanonicalDefaults(*this);}
RotaryTremoloAutopanSettings RotaryTremoloAutopanProcessor::settings()const noexcept{RotaryTremoloAutopanSettings p;p.speed=value(0);p.depth=value(1);p.width=value(2);p.mix=value(3);p.output=value(4);p.ramp=value(5);p.mode=int(std::lround(value(6)));p.rotary=int(std::lround(value(7)));p.shape=int(std::lround(value(8)));p.sync=value(9)>.5f;p.bypass=value(10)>.5f;return p;}
void RotaryTremoloAutopanProcessor::prepareToPlay(double rate,int){ready=false;resetMeterTelemetry();actualRate.store(rate);core.prepare(rate);setLatencySamples(0);publishMeterPreparation();ready=true;}
void RotaryTremoloAutopanProcessor::processBlock(juce::AudioBuffer<float>&b,juce::MidiBuffer&){juce::ScopedNoDenormals guard;if(!processingReady()||b.getNumChannels()<2||b.getNumSamples()<=0)return;captureInput(b);core.process(b.getWritePointer(0),b.getWritePointer(1),b.getNumSamples(),settings(),hostTempo());publishMeterBlock(b);}
bool RotaryTremoloAutopanProcessor::parameterActive(const juce::String&id)const{if(id=="rotary"||id=="ramp")return value(6)<.5f;return true;}
juce::String RotaryTremoloAutopanProcessor::helpText()const{return "Rotary is a dual-band Doppler/amplitude approximation, not a measured cabinet model. SLOW is 15% of SPEED, FAST is SPEED, BRAKE decelerates to zero; drum runs 82% of horn speed. RAMP is the horn speed time constant (drum 1.35x). Tremolo attenuates; autopan uses equal-power gains with unity centre. WIDTH sets stereo motion/phase. Depth 0% is neutral. Zero host latency. Sync uses real host BPM, SPEED in cycles per quarter-note; unavailable BPM falls back to Hz and is labelled. Phase free-runs; plot shows actual L/R modulation (rotary horn).";}
juce::AudioProcessorEditor*RotaryTremoloAutopanProcessor::createEditor(){return new ModulationEditor(*this,24);}
}
