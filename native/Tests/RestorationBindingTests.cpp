#include "../Source/Plugins/RestorationProcessors.h"
#include "../Source/InsertChain.h"
#include "../Source/SessionState.h"
#include "../Source/Systems/PresetManager.h"
#include "FoundationAllocationProbe.h"
#include <iostream>
#include <stdexcept>
namespace {
uint64_t checks=0;
void check(bool result,const char*what){++checks;if(!result)throw std::runtime_error(what);}
std::unique_ptr<hem::RestorationProcessor>make(int n){if(n==2)return std::make_unique<hem::DynamicResonanceSuppressorProcessor>();if(n==6)return std::make_unique<hem::BreathMouthControlProcessor>();return std::make_unique<hem::ClipRepairProcessor>();}
void set(hem::ModuleProcessor&p,const char*id,float value){auto*parameter=p.state.getParameter(id);check(parameter!=nullptr,"parameter missing");parameter->setValueNotifyingHost(parameter->convertTo0to1(value));}
struct CallbackProbe { CallbackProbe(){allocationProbe::testThread=true;allocationProbe::enabled=true;}~CallbackProbe(){allocationProbe::enabled=false;allocationProbe::testThread=false;} };
void run(hem::ModuleProcessor&p,juce::AudioBuffer<float>&b){juce::MidiBuffer midi;CallbackProbe probe;p.processBlock(b,midi);}
void probePositiveControls(){
    const auto before=allocationProbe::count.load();
    {CallbackProbe probe;void* p=::operator new(33);::operator delete(p);void* a=::operator new(97,std::align_val_t(64));::operator delete(a,std::align_val_t(64));}
    check(allocationProbe::count.load()==before+2,"scalar/aligned allocation probe positive controls failed");allocationProbe::count=0;
}
void lifecycle(){
    for(int n:{2,6,9}){auto p=make(n);
        for(double rate:{8000.,44100.,48000.,96000.,192000.,384000.})for(int size:{1,31,256,1025}){
            p->prepareToPlay(rate,128);check(p->processingReady(),"readiness");
            const int expected=n==2?0:n==6?int(std::ceil(rate*.0025)):int(std::floor(rate*.006));
            check(p->getLatencySamples()==expected,"module latency contract");
            juce::AudioBuffer<float>b(2,size);for(int c=0;c<2;++c)for(int i=0;i<size;++i)b.setSample(c,i,float(.13*std::sin(i*.02+c)));run(*p,b);
            for(int c=0;c<2;++c)for(int i=0;i<size;++i)check(std::isfinite(b.getSample(c,i)),"finite output");
            hem::PluginTelemetry::Levels level;check(p->pluginTelemetry()&&p->pluginTelemetry()->read(level)&&level.valid,"real shared meters missing");
            p->releaseResources();check(!p->processingReady(),"release readiness");
        }
        for(double rate:{0.,-1.,std::numeric_limits<double>::quiet_NaN(),std::numeric_limits<double>::infinity(),7999.,384001.}){p->prepareToPlay(rate,128);check(!p->processingReady(),"invalid prepare accepted");check(p->actualRate==0,"invalid rate leaked into actualRate");check(p->getLatencySamples()==0,"invalid prepare retained latency");}
        p->prepareToPlay(48000,128);auto editor=std::unique_ptr<juce::AudioProcessorEditor>(p->createEditor());check(editor!=nullptr,"editor factory");
    }
    std::cout<<"PASS 72 JUCE rate/block lifecycle cases, invalid rates, real telemetry and editor factories\n";
}
void state(){
    const std::array<const char*,3> roots{"DYNAMIC_RESONANCE_SUPPRESSOR","BREATH_MOUTH_CONTROL","CLIP_REPAIR"};
    const std::array<std::vector<const char*>,3> ids{{{"sensitivity","low","high","selectivity","maxReduction","attack","release","mix","output","profile","soloDelta","bypass"},{"breathDetection","breathReduction","breathLength","mouthDetection","mouthReduction","mouthLength","low","high","listenBreath","listenMouth","bypass"},{"threshold","length","amount","output","algorithm","listenRepair","bypass"}}};
    int index=0;
    for(int n:{2,6,9}){auto p=make(n);check(p->state.state.hasType(roots[size_t(index)]),"APVTS root changed");check(p->parameterSpecs().size()==ids[size_t(index)].size(),"parameter count changed");
        for(size_t k=0;k<p->parameterSpecs().size();++k)check(juce::String(p->parameterSpecs()[k].id)==ids[size_t(index)][k],"parameter order/ID changed");++index;
        juce::MemoryBlock saved;p->getStateInformation(saved);auto copy=make(n);copy->setStateInformation(saved.getData(),int(saved.getSize()));
        for(const auto&s:p->parameterSpecs()){auto*parameter=p->state.getParameter(s.id);const auto canonical=parameter->convertFrom0to1(parameter->convertTo0to1(s.initial));check(p->state.getRawParameterValue(s.id)->load()==canonical,"constructor default not canonical");check(copy->state.getRawParameterValue(s.id)->load()==p->state.getRawParameterValue(s.id)->load(),"default recall not exact");}
        const auto defaults=p->state.copyState();check(p->migrateParameterState(defaults).isValid(),"own root rejected");
        juce::ValueTree session("PARAMETERS");for(auto child:defaults)session.addChild(child.createCopy(),-1,nullptr);check(p->migrateParameterState(session).isValid(),"session parameters rejected");
        for(const auto&s:p->parameterSpecs())set(*p,s.id,s.maximum);
        p->getStateInformation(saved);copy->setStateInformation(saved.getData(),int(saved.getSize()));
        for(const auto&s:p->parameterSpecs())check(copy->state.getRawParameterValue(s.id)->load()==p->state.getRawParameterValue(s.id)->load(),"maximum recall differs");
        const auto unchanged=copy->state.copyState();
        auto reject=[&](const juce::ValueTree&bad){if(copy->migrateParameterState(bad).isValid()){std::cerr<<bad.toXmlString()<<"\n";check(false,"malformed migration accepted");}auto xml=bad.createXml();juce::MemoryBlock binary;juce::AudioProcessor::copyXmlToBinary(*xml,binary);copy->setStateInformation(binary.getData(),int(binary.getSize()));check(copy->state.copyState().isEquivalentTo(unchanged),"malformed state partially changed APVTS");};
        for(const char* token:{"3oops","nan","inf","-inf","0x1p0","1e9999","1e-9999",""," 1","1 "}){auto bad=defaults.createCopy();bad.getChild(0).setProperty("value",token,nullptr);reject(bad);}
        for(int badness=0;badness<10;++badness){auto bad=defaults.createCopy();
            if(badness==0)bad.setProperty("unexpected",1,nullptr);
            if(badness==1)bad.getChildWithProperty("id",p->parameterSpecs()[0].id).setProperty("value",double(p->parameterSpecs()[0].maximum)+1,nullptr);
            if(badness==2)bad.getChildWithProperty("id",p->parameterSpecs()[0].id).setProperty("value",double(p->parameterSpecs()[0].minimum)-1,nullptr);
            if(badness==3)bad.getChild(0).setProperty("unexpected",1,nullptr);
            if(badness==4)bad.addChild(bad.getChild(0).createCopy(),-1,nullptr);
            if(badness==5)bad.removeChild(0,nullptr);
            if(badness==6)bad.getChild(0).setProperty("id","foreign",nullptr);
            if(badness==7)bad.getChild(1).setProperty("id",bad.getChild(0)["id"],nullptr);
            if(badness==8)bad.getChild(0).addChild(juce::ValueTree("NESTED"),-1,nullptr);
            if(badness==9)bad.getChild(0).removeProperty("value",nullptr);
            reject(bad);
        }
        for(const auto&s:p->parameterSpecs())if(s.step>=1){auto bad=defaults.createCopy();bad.getChildWithProperty("id",s.id).setProperty("value",double(s.minimum)+.5,nullptr);reject(bad);}
        // A changed valid field plus an invalid discrete field is one rejected transaction.
        auto transaction=defaults.createCopy();transaction.getChildWithProperty("id",p->parameterSpecs()[0].id).setProperty("value",p->parameterSpecs()[0].maximum,nullptr);transaction.getChildWithProperty("id","bypass").setProperty("value",.5,nullptr);reject(transaction);
        auto xml=session.createXml();juce::AudioProcessor::copyXmlToBinary(*xml,saved);copy->setStateInformation(saved.getData(),int(saved.getSize()));check(copy->state.copyState().isEquivalentTo(unchanged),"standalone accepted generic PARAMETERS root");
        const char invalid[]{1,2,3,4};copy->setStateInformation(invalid,4);copy->setStateInformation(nullptr,0);copy->setStateInformation(invalid,1024*1024+1);check(copy->state.copyState().isEquivalentTo(unchanged),"malformed binary changed state");
        // Reset all controls through the same canonical host conversion.
        for(const auto&s:copy->parameterSpecs())set(*copy,s.id,s.initial);
        check(copy->state.copyState().isEquivalentTo(defaults),"canonical parameter reset differs from constructor");copy->getStateInformation(saved);p->setStateInformation(saved.getData(),int(saved.getSize()));
        check(p->state.copyState().isEquivalentTo(defaults),"canonical reset binary recall differs");
    }
    std::cout<<"PASS exact parameter IDs/order, canonical defaults/maximum/reset binary recall, strict atomic state rejection\n";
}
void sessionRecall(){
    hem::SessionSnapshot snapshot;hem::MonitorProcessor monitor;snapshot.monitorParameters=hem::SessionState::captureParameters(monitor);
    // "breath-mouth-control" is now the 2026-10 faceplate processor (StudioFaceplateEditorTests).
    const std::array<const char*,2>ids{"dynamic-resonance-suppressor","clip-repair"};
    for(size_t i=0;i<ids.size();++i){auto p=hem::PluginRegistry::create(ids[i]);check(dynamic_cast<hem::RestorationProcessor*>(p.get())!=nullptr,"registry restoration class missing");check(hem::PluginRegistry::idFor(*p)==ids[i],"registry persistence ID mismatch");for(const auto&s:p->parameterSpecs())set(*p,s.id,s.maximum);auto&slot=snapshot.tracks[0].slots[i];slot.pluginId=ids[i];slot.bypassed=i==1;slot.parameters=hem::SessionState::captureParameters(*p);}
    auto xmlFile=juce::File::getSpecialLocation(juce::File::tempDirectory).getNonexistentChildFile("hem-restoration-state",".hem.xml");auto jsonFile=xmlFile.withFileExtension("json");
    struct Cleanup {juce::File a,b;~Cleanup(){a.deleteFile();b.deleteFile();}}cleanup{xmlFile,jsonFile};
    hem::SessionState::save(snapshot,xmlFile);hem::PresetManager::save(snapshot,jsonFile);
    for(auto recalled:{hem::SessionState::load(xmlFile),hem::PresetManager::load(jsonFile)})for(size_t i=0;i<ids.size();++i){const auto&slot=recalled.tracks[0].slots[i];check(slot.pluginId==ids[i]&&slot.bypassed==(i==1),"session identity/order/bypass recall");auto p=hem::PluginRegistry::create(slot.pluginId);hem::SessionState::restoreParameters(*p,slot.parameters);check(hem::SessionState::captureParameters(*p).isEquivalentTo(snapshot.tracks[0].slots[i].parameters),"XML/JSON restoration parameter recall");
        auto original=hem::SessionState::captureParameters(*p);auto bad=original.createCopy();bad.getChild(0).setProperty("value","garbage",nullptr);bool rejected=false;try{hem::SessionState::restoreParameters(*p,bad);}catch(const std::exception&){rejected=true;}check(rejected&&hem::SessionState::captureParameters(*p).isEquivalentTo(original),"session malformed restore not atomic");}
    std::cout<<"PASS real registry + SessionState XML and PresetManager JSON recall for the registered restoration modules\n";
}
void chain(){
    for(bool bypass:{false,true}){hem::InsertChain chain;chain.prepare(48000,128);for(int slot=0;slot<3;++slot){auto p=make(slot==0?2:slot==1?6:9);set(*p,"bypass",bypass?1:0);chain.installInitial(slot,chain.stageNode(std::move(p),48000,128,chain.slotLatency()),false);}
        for(double rate:{8000.,44100.,48000.,96000.,192000.,384000.}){chain.prepare(rate,128);const int latency=8*int(std::ceil(rate*.006));check(chain.latencySamples()==latency,"rack latency changed");std::array<float,37>l{},r{};for(int start=0;start<latency+512;start+=37){l.fill(0);r.fill(0);if(start==0){l[0]=.5f;r[0]=.25f;}{CallbackProbe probe;chain.process(l.data(),r.data(),37);}for(int i=0;i<37;++i){check(l[size_t(i)]==(start+i==latency?.5f:0.f),"insert compensation left mismatch");check(r[size_t(i)]==(start+i==latency?.25f:0.f),"insert compensation right mismatch");}}}
    }
    std::cout<<"PASS actual insert-chain impulse alignment, neutral and bypass, all three modules at six rates\n";
}
}
int main(){try{juce::ScopedJuceInitialiser_GUI gui;probePositiveControls();lifecycle();state();sessionRecall();chain();check(allocationProbe::count==0,"audio callback allocated");std::cout<<"PASS "<<checks<<" assertions; zero measured callback allocations; scalar/aligned positive controls\n";return 0;}catch(const std::exception&e){allocationProbe::enabled=false;allocationProbe::testThread=false;std::cerr<<"FAIL "<<e.what()<<"\n";return 1;}}
