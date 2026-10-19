#include "Processors.h"
#include "Editors.h"
#include "InsertFactory.h"
namespace hem {
juce::AudioProcessorValueTreeState::ParameterLayout ModuleProcessor::layout(const std::vector<ParameterSpec>& specs){
    juce::AudioProcessorValueTreeState::ParameterLayout result;
    for(const auto& s:specs){const juce::ParameterID key(s.id,1);
        if(s.toggle)result.add(std::make_unique<juce::AudioParameterBool>(key,s.name,s.initial>.5f));
        else if(s.skewCentre<0&&s.minimum>0){
            // Logarithmic travel: equal knob movement per octave.
            const float lo=s.minimum,hi=s.maximum,step=s.step;
            juce::NormalisableRange<float> range(lo,hi,
                [](float a,float b,float t){return a*std::pow(b/a,t);},
                [](float a,float b,float v){return std::log(v/a)/std::log(b/a);},
                [step](float a,float b,float v){return juce::jlimit(a,b,step>0?std::round(v/step)*step:v);});
            result.add(std::make_unique<juce::AudioParameterFloat>(key,s.name,range,s.initial,
                juce::AudioParameterFloatAttributes().withLabel(s.unit)));}
        else {juce::NormalisableRange<float> range(s.minimum,s.maximum,s.step);
            if(s.skewCentre>s.minimum&&s.skewCentre<s.maximum)range.setSkewForCentre(s.skewCentre);
            else if(juce::String(s.unit)=="Hz"&&s.minimum>0&&s.maximum/s.minimum>20)range.setSkewForCentre(std::sqrt(s.minimum*s.maximum));
            result.add(std::make_unique<juce::AudioParameterFloat>(key,s.name,range,s.initial,
                juce::AudioParameterFloatAttributes().withLabel(s.unit)));}}
    return result;
}
ModuleProcessor::ModuleProcessor(juce::String identifier,juce::String name,std::vector<ParameterSpec> s,PluginMeterKind meterType)
 :AudioProcessor(BusesProperties().withInput("Input",juce::AudioChannelSet::stereo(),true).withOutput("Output",juce::AudioChannelSet::stereo(),true)),
 state(*this,nullptr,juce::Identifier(identifier),layout(s)),id(std::move(identifier)),title(std::move(name)),specs(std::move(s)),kind(meterType) {
    for(const auto& p:specs)parameters.push_back(state.getRawParameterValue(p.id));
    const juce::StringArray supported{"SIPA","SURGICAL_EQ","VCA","TRUE_PEAK","DHTA","TRANSIENT","SUB_BASS","RESTORER","VPR_ARCHITECT","TRIODE_TUBE","TAPE","MONITOR_EQ","DC_SUBSONIC","SMART_GATE","VOCAL_RIDER","PLOSIVE_CONTROL","HUM_BUZZ","CONVOLUTION_REVERB","ALGORITHMIC_REVERB","CHORUS_FLANGER_PHASER","ROTARY_TREMOLO_AUTOPAN","MULTITAP_DELAY","TAPE_DELAY","DYNAMIC_EQ_8","MID_SIDE_EQ","MULTIBAND_DYNAMICS","MULTIBAND_IMAGER","PARALLEL_DISTORTION","EXCITER","DYNAMIC_RESONANCE_SUPPRESSOR","BREATH_MOUTH_CONTROL","CLIP_REPAIR","DRUM_REPLACER","AMP_CABINET","REFERENCE_MATCH","VOCAL_DE_REVERB"};
    const bool spectrum=id=="SURGICAL_EQ"||id=="SUB_BASS"||id=="VPR_ARCHITECT"||id=="MONITOR_EQ";
    // These native editors own their core visualization; only lock-free levels are needed.
    const bool ownVisualization=id=="DYNAMIC_EQ_8"||id=="MID_SIDE_EQ"||id=="MULTIBAND_DYNAMICS"||id=="MULTIBAND_IMAGER"||id=="PARALLEL_DISTORTION"||id=="EXCITER"||id=="DYNAMIC_RESONANCE_SUPPRESSOR"||id=="BREATH_MOUTH_CONTROL"||id=="CLIP_REPAIR"||id=="DRUM_REPLACER"||id=="AMP_CABINET"||id=="REFERENCE_MATCH"||id=="VOCAL_DE_REVERB";
    if(supported.contains(id))telemetry=std::make_unique<PluginTelemetry>(spectrum,!ownVisualization);
}
void ModuleProcessor::resetMeterTelemetry() noexcept {
    meterSequence.fetch_add(1);
    meterFlags.store(0);meterValue.store(0);meterAuxiliary.store(0);
    meterSamples.store(0);meterBlocks.store(0);meterRate.store(0);meterLatency.store(0);
    meterSequence.fetch_add(1);
}
void ModuleProcessor::publishMeterPreparation() noexcept {
    if(telemetry)telemetry->prepare(actualRate.load(std::memory_order_relaxed));
    // Preparation and processing share one serialized writer. Read JUCE's
    // non-atomic latency only here, after setLatencySamples/core.prepare, never
    // on the GUI thread. Rate and latency join the same coherent meter snapshot.
    meterSequence.fetch_add(1);
    meterRate.store(actualRate.load(std::memory_order_relaxed));
    meterLatency.store(getLatencySamples());meterFlags.store(8u);
    meterSequence.fetch_add(1);
}
void ModuleProcessor::publishMeterBlock(const juce::AudioBuffer<float>& buffer,float auxiliary) noexcept {
    const int n=buffer.getNumSamples();
    if(n<=0 || buffer.getNumChannels()<2)return;
    const float measured=meter();
    if(telemetry)telemetry->output(buffer.getReadPointer(0),buffer.getReadPointer(1),n,capturedGains,measured,auxiliary);
    bool valid=std::isfinite(measured)&&std::isfinite(auxiliary),active=false;
    if(kind==PluginMeterKind::correlation){
        // Match the EXISTING StereoCore denominator guard exactly. Its zero
        // fallback means undefined at silence/one silent channel, not measured
        // decorrelation. This extra scan is bounded and only used by S.I.P.A.
        double ll=0,rr=0;const auto* l=buffer.getReadPointer(0);const auto* r=buffer.getReadPointer(1);
        for(int i=0;i<n;++i){ll+=l[i]*l[i];rr+=r[i]*r[i];}
        active=ll>0 || rr>0;
        valid=valid&&std::isfinite(ll)&&std::isfinite(rr)&&ll*rr>1.e-20;
    }else if(kind==PluginMeterKind::samplePeak || kind==PluginMeterKind::firTruePeak){
        // Reuse the core's real detector; do not rescan six peak processors.
        valid=valid&&measured>=0;active=valid&&measured>0;
    }else{
        // Dynamics values may remain nonzero during release into silence.
        // An early-exit output scan distinguishes that from signal activity.
        const auto* l=buffer.getReadPointer(0);const auto* r=buffer.getReadPointer(1);
        for(int i=0;i<n&&!active;++i)active=(std::isfinite(l[i])&&l[i]!=0)||(std::isfinite(r[i])&&r[i]!=0);
    }
    meterSequence.fetch_add(1);
    meterValue.store(measured);meterAuxiliary.store(auxiliary);
    meterFlags.store((meterFlags.load()&8u)|1u|(active?2u:0u)|(valid?4u:0u));
    meterSamples.store(uint32_t(n));
    const auto blocks=meterBlocks.load();meterBlocks.store(blocks==UINT32_MAX?blocks:blocks+1);
    meterSequence.fetch_add(1);
}
PluginMeterSnapshot ModuleProcessor::meterSnapshot() const noexcept {
    for(int attempt=0;attempt<3;++attempt){
        const auto before=meterSequence.load();if(before&1u)continue;
        const auto flags=meterFlags.load();
        PluginMeterSnapshot result;
        result.isPrepared=(flags&8u)!=0;result.hasProcessedAudio=(flags&1u)!=0;result.signalPresent=(flags&2u)!=0;result.valueAvailable=(flags&4u)!=0;
        result.value=meterValue.load();result.auxiliary=meterAuxiliary.load();
        result.blockSamples=meterSamples.load();result.processedBlocks=meterBlocks.load();
        result.sampleRate=meterRate.load();result.latencySamples=meterLatency.load();
        if(before==meterSequence.load())return result;
    }
    // Never spin waiting on the audio thread or show a torn measurement.
    return {};
}
bool ModuleProcessor::isBusesLayoutSupported(const BusesLayout& l) const {return l.getMainInputChannelSet()==juce::AudioChannelSet::stereo()&&l.getMainOutputChannelSet()==juce::AudioChannelSet::stereo();}
void ModuleProcessor::getStateInformation(juce::MemoryBlock& block){auto xml=state.copyState().createXml();copyXmlToBinary(*xml,block);}
void ModuleProcessor::setStateInformation(const void* data,int bytes){if(auto xml=getXmlFromBinary(data,bytes))if(xml->hasTagName(id))state.replaceState(juce::ValueTree::fromXml(*xml));}
SipaProcessor::SipaProcessor():ModuleProcessor("SIPA","S.I.P.A.  -  Stereo Image & Phase Architect",{
 {"width","WIDTH",0,200,100,.1f,"%"},{"bass","MONO BASS",60,200,120,1,"Hz"},{"balance","BALANCE",-100,100,0,.1f,"%"},{"output","OUTPUT",-60,6,0,.1f,"dB"},
 {"bassOn","MONO BASS ENABLE",0,1,0,1,"",true},{"mono","MONO",0,1,0,1,"",true},{"invertL","INVERT L",0,1,0,1,"",true},{"invertR","INVERT R",0,1,0,1,"",true},{"swap","SWAP L/R",0,1,0,1,"",true}},PluginMeterKind::correlation){}
void SipaProcessor::prepareToPlay(double sr,int){resetMeterTelemetry();actualRate.store(sr);setLatencySamples(0);core.prepare(sr);publishMeterPreparation();}
void SipaProcessor::processBlock(juce::AudioBuffer<float>& b,juce::MidiBuffer&){juce::ScopedNoDenormals guard;if(b.getNumChannels()<2||b.getNumSamples()<=0)return;captureInput(b);
 StereoParameters p;p.width=value(0);p.monoBass=value(1);p.balance=value(2);p.output=value(3);p.bassMono=value(4)>.5f;p.mono=value(5)>.5f;p.invertL=value(6)>.5f;p.invertR=value(7)>.5f;p.swap=value(8)>.5f;core.process(b.getWritePointer(0),b.getWritePointer(1),b.getNumSamples(),p);publishMeterBlock(b);}
SurgicalEqProcessor::SurgicalEqProcessor():ModuleProcessor("SURGICAL_EQ","SURGICAL MASTER EQ",{
 {"hpOn","SUB HPF ENABLE",0,1,0,1,"",true},{"hp","SUB CUTOFF",20,80,30,.1f,"Hz"},{"slope","HP CASCADES (12 dB EACH)",1,4,1,1,""},
 {"mudFreq","MUD FREQ",150,600,300,.1f,"Hz"},{"mudGain","MUD GAIN",-12,6,0,.1f,"dB"},{"mudQ","MUD Q",.5f,10,1,.01f,"Q"},
 {"midFreq","CORE FREQ",600,3500,1500,1,"Hz"},{"midGain","CORE GAIN",-9,9,0,.1f,"dB"},{"midQ","CORE Q",.5f,6,1,.01f,"Q"},
 {"harshFreq","HARSHNESS FREQ",4000,8500,6500,1,"Hz"},{"harshGain","HARSHNESS GAIN",-12,3,0,.1f,"dB"},{"harshQ","HARSHNESS Q",1,8,2,.01f,"Q"},
 {"airFreq","AIR FREQ",10000,20000,12000,1,"Hz"},{"airGain","AIR GAIN",-6,8,0,.1f,"dB"},{"output","OUTPUT",-60,6,0,.1f,"dB"}}){}
EqParameters SurgicalEqProcessor::settings() const noexcept {EqParameters p;p.highPass=value(0)>.5f;p.cutoff=value(1);p.slope=int(value(2))-1;
 for(size_t i=0;i<3;++i){p.hz[i]=value(3+i*3);p.db[i]=value(4+i*3);p.q[i]=value(5+i*3);}p.hz[3]=value(12);p.db[3]=value(13);p.output=value(14);return p;}
void SurgicalEqProcessor::prepareToPlay(double sr,int){resetMeterTelemetry();actualRate.store(sr);setLatencySamples(0);core.prepare(sr);publishMeterPreparation();}
void SurgicalEqProcessor::processBlock(juce::AudioBuffer<float>& b,juce::MidiBuffer&){juce::ScopedNoDenormals guard;if(b.getNumChannels()<2||b.getNumSamples()<=0)return;captureInput(b);core.process(b.getWritePointer(0),b.getWritePointer(1),b.getNumSamples(),settings());publishMeterBlock(b);}
VcaProcessor::VcaProcessor():ModuleProcessor("VCA","VCA COMPRESSOR",{
 {"threshold","THRESHOLD",-40,0,0,.1f,"dB"},{"ratio","RATIO",1,10,1,.01f,":1"},{"attack","ATTACK",1,100,30,.1f,"ms"},{"release","RELEASE",20,1200,100,1,"ms"},
 {"knee","KNEE",0,20,0,.1f,"dB"},{"makeup","MAKEUP",-6,12,0,.1f,"dB"},{"mix","WET / DRY",0,100,100,.1f,"%"},{"scFreq","SC HPF",30,300,80,1,"Hz"},
 {"scOn","SC HPF ENABLE",0,1,0,1,"",true},{"autoRelease","AUTO RELEASE",0,1,0,1,"",true}},PluginMeterKind::gainReduction){}
void VcaProcessor::prepareToPlay(double sr,int){resetMeterTelemetry();actualRate.store(sr);setLatencySamples(0);core.prepare(sr);publishMeterPreparation();}
void VcaProcessor::processBlock(juce::AudioBuffer<float>& b,juce::MidiBuffer&){juce::ScopedNoDenormals guard;if(b.getNumChannels()<2||b.getNumSamples()<=0)return;captureInput(b);
 CompressorParameters p;p.threshold=value(0);p.ratio=value(1);p.attack=value(2);p.release=value(3);p.knee=value(4);p.makeup=value(5);p.mix=value(6);p.sidechain=value(7);p.scEnabled=value(8)>.5f;p.autoRelease=value(9)>.5f;core.process(b.getWritePointer(0),b.getWritePointer(1),b.getNumSamples(),p);publishMeterBlock(b);}
TruePeakProcessor::TruePeakProcessor():ModuleProcessor("TRUE_PEAK","TRUE-PEAK LIMITER",{
 {"input","INPUT DRIVE",0,12,0,.1f,"dB"},{"ceiling","OUTPUT CEILING",-12,0,-1,.1f,"dBTP"},{"release","RELEASE",10,500,100,1,"ms"}},PluginMeterKind::firTruePeak){}
void TruePeakProcessor::prepareToPlay(double sr,int){resetMeterTelemetry();actualRate.store(sr);core.prepare(sr);setLatencySamples(core.latency());publishMeterPreparation();}
void TruePeakProcessor::processBlock(juce::AudioBuffer<float>& b,juce::MidiBuffer&){juce::ScopedNoDenormals guard;if(b.getNumChannels()<2||b.getNumSamples()<=0)return;captureInput(b);
 LimiterParameters p;p.input=value(0);p.ceiling=value(1);p.release=value(2);core.process(b.getWritePointer(0),b.getWritePointer(1),b.getNumSamples(),p);publishMeterBlock(b,reduction());}
juce::AudioProcessorEditor* SipaProcessor::createEditor(){return new SipaEditor(*this);}
juce::AudioProcessorEditor* SurgicalEqProcessor::createEditor(){return new SurgicalEqEditor(*this);}
juce::AudioProcessorEditor* VcaProcessor::createEditor(){return new VcaEditor(*this);}
juce::AudioProcessorEditor* TruePeakProcessor::createEditor(){return new TruePeakEditor(*this);}
std::unique_ptr<ModuleProcessor> createModule(int index) {
    return InsertFactory::create(index);
}
} // namespace hem
