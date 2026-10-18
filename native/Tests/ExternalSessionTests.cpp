#include "../Source/SessionState.h"
#include "../Source/Systems/PresetManager.h"
#include <iostream>
#include <stdexcept>

namespace {
void require(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
template<class Action>bool rejected(Action action){try{action();}catch(const std::exception&){return true;}return false;}
template<class Condition>void until(Condition done,const char* message){
    for(int i=0;i<2000&&!done();++i){juce::Thread::sleep(5);juce::Timer::callPendingTimersSynchronously();}
    require(done(),message);
}
hem::SessionSnapshot nativeSnapshot(){
    hem::SessionSnapshot state;hem::MonitorProcessor monitor;
    state.monitorParameters=hem::SessionState::captureParameters(monitor);
    auto& native=state.tracks[0].slots[1];native.pluginId="sipa";
    hem::SipaProcessor processor;native.parameters=hem::SessionState::captureParameters(processor);
    return state;
}
hem::ExternalPluginState fixtureState(const juce::File& file){
    juce::PluginDescription description;description.name="Container-only test effect";
    description.descriptiveName=description.name;description.pluginFormatName="VST3";
    description.category="Fx";description.manufacturerName="HEM tests";description.version="1";
    description.fileOrIdentifier=file.getFullPathName();description.uniqueId=0x12345678;
    description.numInputChannels=2;description.numOutputChannels=2;description.isInstrument=false;
    hem::ExternalPluginState state;state.descriptionXml=description.createXml()->toString();
    const unsigned char opaque[]={0,0xff,0x10,0,0x7f,0x80,0x42,0x42,1};
    state.state=juce::MemoryBlock(opaque,sizeof(opaque));return state;
}
juce::ValueTree slot(juce::ValueTree root){return root.getChild(1).getChild(0);}
juce::ValueTree external(juce::ValueTree root){return slot(root).getChildWithName("EXTERNAL");}
// This is a test-owned ModuleProcessor, not a dynamically loaded plug-in. It
// proves staging/capture routing without scanning, loading, or running a binary.
class MetadataOnlyProcessor final:public hem::ModuleProcessor {
public:
    explicit MetadataOnlyProcessor(juce::String xml):ModuleProcessor("test-external","Metadata only",{}),description(std::move(xml)){}
    bool isExternal() const noexcept override{return true;}
    juce::String externalDescriptionXml() const override{return description;}
    uint64_t externalStateRevision() const noexcept override{return 73;}
    bool processingReady() const noexcept override{return prepared;}
    void prepareToPlay(double,int) override{prepared=true;}
    void processBlock(juce::AudioBuffer<float>&,juce::MidiBuffer&) override{}
    juce::AudioProcessorEditor* createEditor() override{return nullptr;}
    float meter() const noexcept override{return 0;}
    juce::String meterLabel() const override{return {};}
private:
    juce::String description;bool prepared=false;
};
void workerTests(const juce::File& file,const hem::SessionSnapshot& expected){
    juce::TimeSliceThread disk("Session container tests");require(disk.startThread(),"Cannot start test read-ahead thread");
    auto live=std::make_unique<hem::TrackBank>();live->tracks[0].name="UNCHANGED";auto* original=live.get();
    int commits=0,completions=0,stages=0;bool completedOk=false;
    auto commit=[&](std::unique_ptr<hem::TrackBank>& bank,const hem::SessionSnapshot&,hem::SessionWorker::Context){
        ++commits;live.swap(bank);return juce::Result::ok();
    };
    auto complete=[&](bool saving,const juce::File&,const juce::Result& result){require(!saving,"Unexpected save callback");++completions;completedOk=result.wasOk();};
    {
        hem::SessionWorker worker(disk,commit,[](const juce::String&){},complete);
        require(worker.load(file,{48000,256,1}),"Missing-callback load not admitted");
        until([&]{return !worker.isBusy();},"Missing-callback load did not settle");
        require(completions==1&&!completedOk&&commits==0&&live.get()==original,"Missing staging callback changed live bank");
    }
    {
        hem::SessionWorker worker(disk,commit,[](const juce::String&){},complete,
            [&](hem::TrackBank& bank,const hem::SessionSnapshot& state,hem::SessionWorker::Context)->juce::Result{
                ++stages;require(juce::MessageManager::getInstance()->isThisTheMessageThread(),"Staging ran off main thread");
                require(bank.tracks[0].chain.activeProcessor(0)==nullptr,"Worker instantiated an external slot");
                require(bank.tracks[0].chain.activeProcessor(1)!=nullptr,"Worker omitted native staging");
                require(state.tracks[0].slots[0].external.state==expected.tracks[0].slots[0].external.state,"Opaque state lost before callback");
                throw std::runtime_error("Intentional test staging failure");
            });
        require(worker.load(file,{48000,256,1}),"Throwing-stager load not admitted");
        until([&]{return !worker.isBusy();},"Throwing-stager load did not settle");
        require(completions==2&&!completedOk&&commits==0&&stages==1&&live.get()==original,"Staging exception changed live bank");
    }
    {
        hem::SessionWorker worker(disk,commit,[](const juce::String&){},complete,
            [](hem::TrackBank&,const hem::SessionSnapshot&,hem::SessionWorker::Context){return juce::Result::ok();});
        require(worker.load(file,{48000,256,1}),"Empty-stager load not admitted");
        until([&]{return !worker.isBusy();},"Empty-stager load did not settle");
        require(completions==3&&!completedOk&&commits==0&&live.get()==original,"Empty staging silently dropped external state");
    }
    {
        hem::SessionWorker worker(disk,commit,[](const juce::String&){},complete,
            [&](hem::TrackBank& bank,const hem::SessionSnapshot& state,hem::SessionWorker::Context context){
                ++stages;auto& track=bank.tracks[0];
                auto processor=std::make_unique<MetadataOnlyProcessor>(state.tracks[0].slots[0].external.descriptionXml);
                auto node=track.chain.stageNode(std::move(processor),context.sampleRate,context.blockSize,track.chain.slotLatency());
                track.chain.installInitial(0,std::move(node),state.tracks[0].slots[0].bypassed);
                return juce::Result::ok();
            });
        require(worker.load(file,{48000,256,1}),"Successful staging load not admitted");
        until([&]{return !worker.isBusy();},"Successful staging load did not settle");
        require(completions==4&&completedOk&&commits==1&&stages==2&&live.get()!=original,"Staged external bank did not commit");
        require(live->tracks[0].chain.isBypassed(0),"External bypass state lost");
        hem::MonitorProcessor monitor;
        const auto fast=hem::SessionState::capture(*live,0,0,0,false,monitor,false);
        const auto& captured=fast.tracks[0].slots[0];
        require(captured.pluginId==hem::SessionState::externalPluginId&&captured.externalRevision==73,"Fast external metadata capture failed");
        require(captured.external.descriptionXml==expected.tracks[0].slots[0].external.descriptionXml&&captured.external.state.getSize()==0
                &&captured.parameters.getNumChildren()==0,"Fast capture fabricated state or omitted descriptor");
    }
    // Destroying a just-admitted load joins its worker without waiting for any
    // message-thread staging callback. The old/live bank remains intact.
    const int before=commits;
    {hem::SessionWorker worker(disk,commit,[](const juce::String&){},complete);require(worker.load(file,{48000,256,1}),"Cancellation load not admitted");}
    require(commits==before,"Destructor cancellation committed a session");
    live.reset();disk.stopThread(-1);
}
}

int main(){juce::ScopedJuceInitialiser_GUI runtime;try{
    const auto directory=juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("HEM-external-session-"+juce::Uuid().toString());
    require(directory.createDirectory().wasOk(),"Cannot create test directory");
    struct Cleanup{juce::File directory;~Cleanup(){directory.deleteRecursively();}} cleanup{directory};
    const auto plugin=directory.getChildFile("container-only.vst3");
    require(plugin.replaceWithText("Container-only descriptor fixture. This is not a plug-in binary."),"Cannot create inert file fixture");
    const auto xml=directory.getChildFile("session.xml"),json=directory.getChildFile("session.json");
    auto native=nativeSnapshot();
    require(int(hem::SessionState::encode(native,xml)["version"])==1,"Native-only sessions no longer write v1");
    hem::PresetManager::save(native,xml);require(!hem::SessionState::hasExternal(hem::PresetManager::load(xml)),"Native v1 recall failed");
    auto state=native;auto& ext=state.tracks[0].slots[0];ext.pluginId=hem::SessionState::externalPluginId;
    ext.external=fixtureState(plugin);ext.externalRevision=901;ext.bypassed=true;
    auto base=hem::SessionState::encode(state,xml);require(int(base["version"])==2,"External session did not write v2");
    require(slot(base).getNumChildren()==2&&slot(base).getChildWithName("PARAMETERS").getNumChildren()==0,"External schema fabricated native parameters");
    require(!slot(base).hasProperty("externalRevision"),"Transient revision leaked into session");
    for(const auto& file:{xml,json}){
        hem::PresetManager::save(state,file);const auto loaded=hem::PresetManager::load(file);const auto& restored=loaded.tracks[0].slots[0];
        require(restored.external.state==ext.external.state&&restored.external.descriptionXml==ext.external.descriptionXml&&restored.bypassed,"External opaque round trip failed");
        require(restored.externalRevision==0&&restored.parameters.getNumChildren()==0,"Recall persisted a revision or fabricated APVTS");
    }
    auto rejects=[&](juce::ValueTree tree){return rejected([&]{hem::SessionState::decode(tree,xml);});};
    auto tree=base.createCopy();tree.setProperty("version",1,nullptr);require(rejects(tree),"v1 accepted external state");
    tree=base.createCopy();tree.setProperty("version",4,nullptr);require(rejects(tree),"Unknown schema accepted");
    tree=base.createCopy();tree.setProperty("version",3,nullptr);
    require(hem::SessionState::decode(tree,xml).tracks[0].slots[0].external.state==ext.external.state,"v3 lost external compatibility");
    tree=base.createCopy();slot(tree).setProperty("pluginId","sipa",nullptr);require(rejects(tree),"Native ID accepted external container");
    tree=base.createCopy();slot(tree).setProperty("pluginId","",nullptr);require(rejects(tree),"Empty ID accepted external container");
    tree=base.createCopy();slot(tree).removeChild(external(tree),nullptr);require(rejects(tree),"External ID accepted absent state");
    tree=base.createCopy();slot(tree).getChildWithName("PARAMETERS").setProperty("notEmpty",1,nullptr);require(rejects(tree),"External slot accepted native parameter properties");
    tree=base.createCopy();slot(tree).getChildWithName("PARAMETERS").addChild(juce::ValueTree("PARAM"),-1,nullptr);require(rejects(tree),"External slot accepted native parameter children");
    tree=base.createCopy();external(tree).setProperty("unknown",1,nullptr);require(rejects(tree),"Unknown external property accepted");
    tree=base.createCopy();external(tree).removeProperty("stateBase64",nullptr);require(rejects(tree),"Missing external property accepted");
    tree=base.createCopy();external(tree).setProperty("stateBytes",3,nullptr);external(tree).setProperty("stateBase64",1234,nullptr);require(rejects(tree),"Numeric Base64 property accepted");
    tree=base.createCopy();external(tree).addChild(juce::ValueTree("PARAMETERS"),-1,nullptr);require(rejects(tree),"External child accepted");
    tree=base.createCopy();external(tree).setProperty("stateBytes",100,nullptr);require(rejects(tree),"Mismatched state length accepted");
    tree=base.createCopy();external(tree).setProperty("stateBytes",-1,nullptr);require(rejects(tree),"Negative state length accepted");
    tree=base.createCopy();external(tree).setProperty("stateBytes",1.5,nullptr);require(rejects(tree),"Fractional state length accepted");
    tree=base.createCopy();external(tree).setProperty("stateBytes",juce::int64(hem::ExternalPluginState::maxStateBytes+1),nullptr);require(rejects(tree),"Oversize declared state accepted");
    tree=base.createCopy();external(tree).setProperty("stateBase64","!!!!!!!!!!!!",nullptr);require(rejects(tree),"Invalid Base64 accepted");
    tree=base.createCopy();external(tree).setProperty("stateBytes",1,nullptr);external(tree).setProperty("stateBase64","AB==",nullptr);require(rejects(tree),"Noncanonical Base64 padding bits accepted");
    tree=base.createCopy();external(tree).setProperty("stateBytes",1,nullptr);external(tree).setProperty("stateBase64","AA= ",nullptr);require(rejects(tree),"Whitespace Base64 accepted");
    tree=base.createCopy();external(tree).setProperty("descriptionXml","<OTHER/>",nullptr);require(rejects(tree),"Malformed descriptor accepted");
    tree=base.createCopy();external(tree).setProperty("descriptionXml",juce::String::repeatedString("x",int(hem::ExternalPluginState::maxDescriptionBytes+1)),nullptr);require(rejects(tree),"Oversized descriptor accepted");
    tree=base.createCopy();external(tree).setProperty("descriptionXml","<!DOCTYPE PLUGIN [<!ENTITY test 'bad'>]>"+ext.external.descriptionXml,nullptr);require(rejects(tree),"Descriptor declaration accepted");
    auto badDescription=juce::XmlDocument::parse(ext.external.descriptionXml);require(badDescription!=nullptr,"Fixture descriptor parse failed");
    badDescription->setAttribute("format","AudioUnit");tree=base.createCopy();external(tree).setProperty("descriptionXml",badDescription->toString(),nullptr);require(rejects(tree),"Non-VST3 descriptor accepted");
    for(const auto& field:{"uniqueId","numInputs","numOutputs","isInstrument","isShell","hasARAExtension","uid","fileTime","infoUpdateTime"}){
        badDescription=juce::XmlDocument::parse(ext.external.descriptionXml);badDescription->setAttribute(field,"junk");
        tree=base.createCopy();external(tree).setProperty("descriptionXml",badDescription->toString(),nullptr);
        require(rejects(tree),"Malformed descriptor number accepted");
    }
    for(const auto& field:{"uniqueId","numInputs","numOutputs","isInstrument"}){
        badDescription=juce::XmlDocument::parse(ext.external.descriptionXml);badDescription->removeAttribute(field);
        tree=base.createCopy();external(tree).setProperty("descriptionXml",badDescription->toString(),nullptr);
        require(rejects(tree),"Missing descriptor number accepted");
    }
    auto noBlob=state;noBlob.tracks[0].slots[0].external.state.reset();
    for(const auto& file:{xml,json}){hem::PresetManager::save(noBlob,file);require(hem::PresetManager::load(file).tracks[0].slots[0].external.state.getSize()==0,"Valid zero-length state rejected");}
    // Oversize output must fail before replacing either existing file.
    auto tooLarge=state;tooLarge.tracks[0].slots[0].external.state.setSize(7*1024*1024,true);
    for(const auto& file:{xml,json}){
        require(file.replaceWithText("KEEP EXISTING SESSION"),"Cannot seed previous session");
        require(rejected([&]{hem::PresetManager::save(tooLarge,file);}),"Oversize output accepted");
        require(file.loadFileAsString()=="KEEP EXISTING SESSION","Oversize save replaced previous session");
    }
    auto tooLargeJson=native;tooLargeJson.tracks[0].name=juce::String::repeatedString("\\",4*1024*1024);
    require(rejected([&]{hem::PresetManager::save(tooLargeJson,json);}),"JSON-specific escaping exceeded size limit");
    require(json.loadFileAsString()=="KEEP EXISTING SESSION","Oversize JSON replaced previous session");
    const auto largeText=juce::String::repeatedString(" ",int(hem::SessionState::maxSerializedBytes+1));
    for(const auto& file:{xml,json}){require(file.replaceWithText(largeText),"Cannot write oversized test input");require(rejected([&]{hem::PresetManager::load(file);}),"Oversize input accepted");}
    // Persist while a selected binary is gone is allowed; recall must fail
    // before constructing any processors or staging into the live graph.
    require(plugin.deleteFile(),"Cannot remove inert fixture");
    for(const auto& file:{xml,json}){hem::PresetManager::save(state,file);require(rejected([&]{hem::PresetManager::load(file);}),"Missing binary accepted on recall");}
    require(plugin.replaceWithText("Inert fixture restored"),"Cannot restore inert fixture");
    hem::PresetManager::save(state,xml);workerTests(xml,state);
    std::cout<<"PASS external session XML/JSON: opaque bytes, v1 compatibility, malformed/mixed/oversize rejection, missing-file recall, atomic save preservation, main-thread staging transaction and cancellation; no plug-in binary loaded\n";
    return 0;
}catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}}
