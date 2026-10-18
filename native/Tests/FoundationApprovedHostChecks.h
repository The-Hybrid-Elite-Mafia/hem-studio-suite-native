#pragma once
#include "../Source/Systems/LiveVocalManager.h"
namespace approvedHost {
inline void check(bool b,const char* m){if(!b)throw std::runtime_error(m);}
inline void set(hem::ModuleProcessor& p,const char* id,float x){auto* v=p.state.getParameter(id);check(v!=nullptr,"New parameter absent");v->setValueNotifyingHost(v->convertTo0to1(x));}
inline void states(){
 for(const auto* id:{"smart-gate","plosive-control","hum-buzz"}){
    auto p=hem::PluginRegistry::create(id);const int legacy=juce::String(id)=="smart-gate"?10:juce::String(id)=="plosive-control"?8:6;
    auto old=p->state.copyState();for(int i=old.getNumChildren()-1;i>=0;--i){bool keep=false;for(int j=0;j<legacy;++j)keep|=old.getChild(i)["id"].toString()==p->parameterSpecs()[size_t(j)].id;if(!keep)old.removeChild(i,nullptr);}
    // Match IDs, never JUCE's incidental child ordering.
    for(int i=legacy;i<int(p->parameterSpecs().size());++i)set(*p,p->parameterSpecs()[size_t(i)].id,p->parameterSpecs()[size_t(i)].maximum);
    auto load=[&](const juce::ValueTree& tree){juce::MemoryBlock bytes;auto xml=tree.createXml();juce::AudioProcessor::copyXmlToBinary(*xml,bytes);p->setStateInformation(bytes.getData(),int(bytes.getSize()));};
    load(old);for(int i=legacy;i<int(p->parameterSpecs().size());++i)check(p->state.getRawParameterValue(p->parameterSpecs()[size_t(i)].id)->load()==p->parameterSpecs()[size_t(i)].initial,"Legacy recall retained dirty addition");
    const auto good=p->state.copyState();
    for(int mutation=0;mutation<6;++mutation){auto bad=good.createCopy();
        if(mutation==0)for(int i=bad.getNumChildren()-1;i>=0;--i)if(bad.getChild(i)["id"].toString()=="output")bad.removeChild(i,nullptr);
        if(mutation==1)bad.getChild(0).setProperty("id","unknown",nullptr);
        if(mutation==2)bad.getChild(0).setProperty("id",bad.getChild(1)["id"],nullptr);
        if(mutation==3)bad.getChild(0).setProperty("value","nan",nullptr);
        if(mutation==4)bad.getChild(0).setProperty("value",1e30,nullptr);
        if(mutation==5){auto broken=old.createCopy();broken.getChild(0).setProperty("id",p->parameterSpecs().back().id,nullptr);bad=broken;}
        load(bad);check(good.isEquivalentTo(p->state.copyState()),"Invalid appended-state schema mutated processor");
    }
    for(const auto& spec:p->parameterSpecs())if(spec.toggle){auto bad=good.createCopy();for(auto child:bad)if(child["id"].toString()==spec.id)child.setProperty("value",.5,nullptr);load(bad);check(good.isEquivalentTo(p->state.copyState()),"Fractional bool accepted");}
    auto restored=p->migrateParameterState(old);check(restored.getNumChildren()==int(p->parameterSpecs().size()),"Shared migration omitted additions");
 }
}
inline void chains(){
 for(double rate:{44100.,48000.,88200.,96000.})for(bool bypass:{false,true}){
    const int d=int(std::ceil(rate*.006));hem::InsertChain main;main.prepare(rate,64);auto p=hem::PluginRegistry::create("smart-gate");set(*p,"keyListen",1);set(*p,"output",-24);main.installInitial(0,main.stageNode(std::move(p),rate,64,main.slotLatency()),bypass);
    check(main.latencySamples()==8*d,"Main fixed reserve changed");
    auto tracking=std::make_unique<hem::TrackingInsertChain>();tracking->prepare(rate,64);auto node=tracking->makeNode("smart-gate",rate,64);set(*node->processor,"output",-24);set(*node->processor,"keyListen",1);node->processor->prepareToPlay(rate,64);node->bypassed=bypass;node->wet.reset(rate,bypass?0:1);tracking->exchange(0,std::move(node));check(tracking->latency()==d,"Tracking gate delay missing");
    for(int n=0;n<8*d+64;n+=64){std::array<float,64> a{},b{},c{},e{};if(n==0)a[0]=b[0]=c[0]=e[0]=1;main.process(a.data(),b.data(),64);tracking->process(c.data(),e.data(),64);
        for(int i=0;i<64;++i){check(std::abs(a[size_t(i)]-(n+i==8*d?1.f:0.f))<1e-6,"Main bypass/key alignment");check(std::abs(c[size_t(i)]-(n+i==d?1.f:0.f))<1e-6,"Tracking bypass/key alignment");}}
 }
}
inline void printed(bool endOfStream){
 const auto dir=juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("hem-approved-delay-"+juce::Uuid().toString());check(dir.createDirectory().wasOk(),"Take fixture directory");struct Cleanup{juce::File path;~Cleanup(){path.deleteRecursively();}} cleanup{dir};
 auto manager=std::make_unique<hem::LiveVocalManager>();manager->prepare(48000,64,1);manager->rack.exchange(0,manager->rack.makeNode("smart-gate",48000,64));manager->setPrintInserts(true);
 hem::TakeRecorder::Setup setup;setup.file=dir.getChildFile("printed.wav");check(manager->configureTake(setup)&&setup.printedLatency==288,"Printed latency metadata staging");check(manager->recorder.start(setup),"Take worker start");
 for(int i=0;i<500&&manager->recorder.state()!=hem::TakeRecorder::State::Armed;++i)juce::Thread::sleep(4);check(manager->recorder.state()==hem::TakeRecorder::State::Armed,"Take arm timeout");
 juce::AudioBuffer<float> input(1,64);for(int b=0;b<8;++b){input.clear();if(b==7)input.setSample(0,63,.5f);manager->capture(input,0,64,10+b*64./48000);}
 manager->stopTake();manager->stopTake(); // idempotent drainage
 if(endOfStream)manager->release();else for(int b=0;b<5;++b){for(int i=0;i<64;++i)input.setSample(0,i,.75f);manager->capture(input,0,64,10+(512+b*64.)/48000);}
 for(int i=0;i<500&&manager->recorder.busy();++i)juce::Thread::sleep(4);if(manager->recorder.state()!=hem::TakeRecorder::State::Complete)throw std::runtime_error("Printed take completion: state="+std::to_string(int(manager->recorder.state()))+" result="+manager->recorder.result().toStdString());
 check(manager->recorder.framesWritten()==800,"Missing or double-counted D-frame drain");juce::AudioFormatManager formats;formats.registerBasicFormats();std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(setup.file));check(reader&&reader->lengthInSamples==800,"Take frame count");juce::AudioBuffer<float> saved(2,800);reader->read(&saved,0,800,0,true,true);
 for(int i=0;i<800;++i)check(std::abs(saved.getSample(0,i)-(i==799?.5f:0.f))<1e-7,"Final printed impulse lost/misaligned");
 const auto metadata=juce::JSON::parse(setup.file.getSiblingFile("printed.wav.take.json"));check(int(metadata["printedInsertLatencySamples"])==288,"Sidecar printed delay");check(std::abs(double(metadata["estimatedPlacementSeconds"])-(10-288./48000))<1e-10,"Printed placement double compensation");
}
inline void run(){states();chains();printed(false);printed(true);std::cout<<"PASS exact legacy/new state schemas, main/tracking delay+bypass, continuous-stop/end-stream printed impulse and sidecar placement\n";}
}
