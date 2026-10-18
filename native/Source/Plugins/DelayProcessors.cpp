#include "DelayProcessors.h"
#include "DelayEditors.h"
#include <cstdlib>
#include <set>
namespace hem {
namespace {
struct Catalog {
    std::array<std::string,100> ids,names;std::vector<ParameterSpec> specs;
    void add(std::string id,std::string name,float lo,float hi,float initial,float step,const char* unit,bool toggle=false){
        const auto i=specs.size();ids[i]=std::move(id);names[i]=std::move(name);
        specs.push_back({ids[i].c_str(),names[i].c_str(),lo,hi,initial,step,unit,toggle});
    }
    explicit Catalog(delay::Kind kind){
        specs.reserve(100);
        if(kind==delay::Kind::multitap){for(int tap=1;tap<=8;++tap){const auto p="tap"+std::to_string(tap)+".";const auto label="TAP "+std::to_string(tap)+" ";
            add(p+"time_ms",label+"TIME",1,4000,float(250*tap),.01f,"ms");
            add(p+"division",label+"DIVISION",0,20,12,1,"division");
            add(p+"feedback",label+"FEEDBACK",0,95,0,.01f,"%");add(p+"pan",label+"PAN",-100,100,0,.1f,"");
            add(p+"level",label+"LEVEL",-60,12,0,.01f,"dB");
            add(p+"on",label+"ON",0,1,0,1,"",true);add(p+"solo",label+"SOLO",0,1,0,1,"",true);add(p+"mute",label+"MUTE",0,1,0,1,"",true);
            add(p+"low_hz",label+"LOW CUT",20,20000,20,.1f,"Hz");add(p+"high_hz",label+"HIGH CUT",20,20000,20000,.1f,"Hz");
            add(p+"low_on",label+"LOW CUT ENABLE",0,1,0,1,"",true);add(p+"high_on",label+"HIGH CUT ENABLE",0,1,0,1,"",true);
        }add("mix","MIX",0,100,0,.1f,"%");add("output","OUTPUT",-24,12,0,.01f,"dB");
        add("sync","TEMPO SYNC",0,1,0,1,"",true);add("bypass","BYPASS",0,1,0,1,"",true);
        }else{
            add("time_ms","DELAY TIME",1,4000,250,.01f,"ms");add("division","DIVISION",0,20,12,1,"division");
            add("feedback","FEEDBACK",0,95,0,.01f,"%");add("wow","WOW",0,100,0,.1f,"%");add("flutter","FLUTTER",0,100,0,.1f,"%");
            add("drive","DRIVE",0,24,0,.01f,"dB");add("low_hz","LOW CUT",20,20000,20,.1f,"Hz");add("high_hz","HIGH CUT",20,20000,20000,.1f,"Hz");
            add("mix","MIX",0,100,0,.1f,"%");add("output","OUTPUT",-24,12,0,.01f,"dB");
            add("low_on","LOW CUT ON",0,1,0,1,"",true);add("high_on","HIGH CUT ON",0,1,0,1,"",true);
            add("sync","TEMPO SYNC",0,1,0,1,"",true);add("bypass","BYPASS",0,1,0,1,"",true);
        }
    }
};
const Catalog& catalog(delay::Kind kind){static const Catalog multi(delay::Kind::multitap),tape(delay::Kind::tape);return kind==delay::Kind::tape?tape:multi;}
}
DelayProcessor::DelayProcessor(delay::Kind k):ModuleProcessor(k==delay::Kind::tape?"TAPE_DELAY":"MULTITAP_DELAY",k==delay::Kind::tape?"Tape Delay":"Multitap Delay",catalog(k).specs),kind(k),core(k){
    actualRate.store(0);state.state.setProperty("delayStateVersion",1,nullptr);for(auto& v:wave)v.store(0);
}
delay::Settings DelayProcessor::settings()const noexcept {
    auto p=delay::defaults(kind);
    const auto division=[this](std::size_t n){return delay::BeatDivision(int(delay::finite(value(n),0,20,12)));};
    if(kind==delay::Kind::multitap){for(std::size_t i=0;i<8;++i){auto& t=p.taps[i];const auto n=i*12;
        t.time.manualMilliseconds=value(n);t.time.division=division(n+1);
        t.feedback=value(n+2);t.pan=value(n+3);t.levelDb=value(n+4);t.on=value(n+5)>.5f;t.solo=value(n+6)>.5f;t.mute=value(n+7)>.5f;
        t.lowHz=value(n+8);t.highHz=value(n+9);t.lowOn=value(n+10)>.5f;t.highOn=value(n+11)>.5f;
    }p.mix=value(96);p.outputDb=value(97);p.sync=value(98)>.5f;p.bypass=value(99)>.5f;
    }else{auto& t=p.taps[0];t.time.manualMilliseconds=value(0);t.time.division=division(1);t.feedback=value(2);
        p.wow=value(3);p.flutter=value(4);p.driveDb=value(5);t.lowHz=value(6);t.highHz=value(7);p.mix=value(8);p.outputDb=value(9);
        t.lowOn=value(10)>.5f;t.highOn=value(11)>.5f;p.sync=value(12)>.5f;p.bypass=value(13)>.5f;
    }return p;
}
void DelayProcessor::setBlockTempo(double b)noexcept {if(!frozen)publishedBpm.store(std::isfinite(b)&&b>=20&&b<=400?b:120,std::memory_order_relaxed);}
void DelayProcessor::configureFrozenExport(double b,bool audible){
    if(!std::isfinite(b)||b<20||b>400)throw std::invalid_argument("Invalid frozen project BPM");
    if(ready.load())throw std::logic_error("Configure frozen delay before preparation");
    publishedBpm.store(b);frozen=true;frozenAudible=audible;
}
delay::CapturedTail DelayProcessor::capturedTail(double rate,double b,bool audible)const noexcept{return delay::capturedTail(kind,settings(),{b},rate,audible);}
void DelayProcessor::stagePreparation(double rate,int){
    if(staged&&stagedRate==rate)return;
    auto candidate=std::make_unique<delay::Core>(kind);candidate->prepare(rate,settings(),{bpm()},frozen,frozenAudible);
    staged=std::move(candidate);stagedRate=rate;
}
void DelayProcessor::prepareToPlay(double rate,int){
    const auto p=settings();const auto b=bpm();
    if(staged&&stagedRate==rate){core.swapRuntime(*staged);staged.reset();}
    else core.prepare(rate,p,{b},frozen,frozenAudible);
    resetMeterTelemetry();actualRate.store(rate);setLatencySamples(core.latency());outputPeak=0;protection=0;conflict=false;
    waveSequence.fetch_add(1);for(auto& v:wave)v.store(0);waveDuration=0;waveSequence.fetch_add(1);
    preparedTail.store(frozen?delay::capturedTail(kind,p,{b},rate,frozenAudible).seconds:delay::liveSafetyTailSeconds);
    previousTap=-1;publishMeterPreparation();ready.store(true);publication.fetch_add(1);
}
void DelayProcessor::releaseResources(){
    ready.store(false);outputPeak.store(0);actualRate.store(0);resetMeterTelemetry();
    waveSequence.fetch_add(1);for(auto& v:wave)v.store(0);waveDuration.store(0);waveSequence.fetch_add(1);
    publication.fetch_add(1); // Host serializes release/prepare/process. Keep storage off callback.
}
void DelayProcessor::processBlock(juce::AudioBuffer<float>& buffer,juce::MidiBuffer&){
    juce::ScopedNoDenormals guard;if(!ready.load()){buffer.clear();return;}
    if(buffer.getNumChannels()<2||buffer.getNumSamples()<=0)return;
    captureInput(buffer);
    const auto m=core.process(buffer.getWritePointer(0),buffer.getWritePointer(1),buffer.getNumSamples(),settings(),{bpm()});
    outputPeak.store(m.output);conflict.store(m.syncConflict);protection.store(m.protection);
    waveSequence.fetch_add(1);
    for(int i=0;i<64;++i){const int n=std::min(buffer.getNumSamples()-1,int(std::int64_t(i)*buffer.getNumSamples()/64));wave[std::size_t(i)].store(buffer.getSample(0,n));}
    waveDuration.store(1000.0*buffer.getNumSamples()/actualRate.load());waveSequence.fetch_add(1);
    publishMeterBlock(buffer,m.input);publication.fetch_add(1);
}
bool DelayProcessor::validateTree(const juce::ValueTree& tree,bool root)const {
    if(root){if(!tree.hasType(state.state.getType())||tree.getNumProperties()!=1||int(tree["delayStateVersion"])!=1||tree["delayStateVersion"].toString()!="1")return false;}
    else if(!tree.hasType("PARAMETERS")||tree.getNumProperties()!=0)return false;
    if(tree.getNumChildren()!=int(parameterSpecs().size()))return false;
    std::set<juce::String> seen;
    for(const auto child:tree){
        if(!child.hasType("PARAM")||child.getNumChildren()!=0||child.getNumProperties()!=2||!child.hasProperty("id")||!child.hasProperty("value"))return false;
        const auto id=child["id"].toString();if(!seen.insert(id).second)return false;
        const auto& specs=parameterSpecs();const auto found=std::find_if(specs.begin(),specs.end(),[&](const auto& s){return id==s.id;});if(found==specs.end())return false;
        const auto text=child["value"].toString().toStdString();char* end=nullptr;const double d=std::strtod(text.c_str(),&end);
        // Validate the serialized double before APVTS can round it to float.
        if(end==text.c_str()||*end||!std::isfinite(d)||d<double(found->minimum)||d>double(found->maximum))return false;
        if(found->toggle&&d!=0&&d!=1)return false;
        if(id.endsWith("division")&&d!=std::floor(d))return false;
    }return true;
}
bool DelayProcessor::validateStoredParameters(const juce::ValueTree& tree)const{return validateTree(tree,false);}
void DelayProcessor::setStateInformation(const void* data,int bytes){
    if(!data||bytes<=0||bytes>8*1024*1024)return;
    if(auto xml=getXmlFromBinary(data,bytes)){const auto tree=juce::ValueTree::fromXml(*xml);if(validateTree(tree,true))state.replaceState(tree);}
}
bool DelayProcessor::tapTempo(double now){
    if(kind!=delay::Kind::tape||settings().sync||!std::isfinite(now)){previousTap=-1;return false;}
    const double delta=now-previousTap;const bool valid=previousTap>=0&&delta>=100&&delta<=4000;previousTap=now;
    if(!valid)return false;auto* parameter=state.getParameter("time_ms");parameter->beginChangeGesture();parameter->setValueNotifyingHost(parameter->convertTo0to1(float(delta)));parameter->endChangeGesture();return true;
}
DelayProcessor::Wave DelayProcessor::waveform()const noexcept {
    for(int attempt=0;attempt<3;++attempt){const auto before=waveSequence.load();if(before&1)continue;Wave result;
        for(std::size_t i=0;i<64;++i)result.samples[i]=wave[i].load();result.durationMs=waveDuration.load();result.valid=result.durationMs>0;
        if(before==waveSequence.load())return result;}return {};
}
juce::String DelayProcessor::helpText()const{return "Real project BPM; sync choices outside 1..4000 ms are unavailable (manual time retained; audible unresolved export rejected). "
    "Feedback history ceiling +30.1 dBFS: ordinary inputs up to 0 dBFS remain unclipped by this guard. Very hot signals may engage nonlinear protection. "
    "Tape-inspired coloration is applied to wet output, not progressively to each circulation. Live safety tail 2500 s; frozen export uses actual settings. Filter cutoff <=0.45 x sample rate.";}
juce::AudioProcessorEditor* DelayProcessor::createEditor(){return new DelayEditor(*this);}
}
