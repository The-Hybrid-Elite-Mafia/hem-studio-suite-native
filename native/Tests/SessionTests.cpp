#include "../Source/SessionState.h"
#include "../Source/Systems/PresetManager.h"
#include <iostream>
#include <stdexcept>
static void require(bool b,const char* message){if(!b)throw std::runtime_error(message);}
int main(){juce::ScopedJuceInitialiser_GUI juceRuntime;
 try{
    const auto file=juce::File::getSpecialLocation(juce::File::tempDirectory).getNonexistentChildFile("hem-session-test",".hem.xml");
    const auto irFile=file.withFileExtension("ir.wav");
    { auto stream=irFile.createOutputStream();require(stream!=nullptr,"Cannot create session IR fixture");
      juce::WavAudioFormat format;auto writer=std::unique_ptr<juce::AudioFormatWriter>(format.createWriterFor(stream.get(),48000,2,24,{},0));
      require(writer!=nullptr,"Cannot encode session IR fixture");stream.release();juce::AudioBuffer<float> ir(2,128);ir.clear();ir.setSample(0,0,.5f);ir.setSample(1,1,.25f);
      require(writer->writeFromAudioSampleBuffer(ir,0,128),"Cannot write session IR fixture"); }
    hem::SessionSnapshot s;s.inputDb=-3;s.outputDb=-6;s.positionSeconds=123.5;s.looping=true;
    hem::MonitorProcessor monitor;s.monitorParameters=hem::SessionState::captureParameters(monitor);
    std::vector<const hem::PluginRegistry::Entry*> inserts;
    for(const auto& entry:hem::PluginRegistry::entries())if(!entry.monitoringOnly)inserts.push_back(&entry);
    for(size_t t=0;t<13;++t){auto& track=s.tracks[t];track.gainDb=-float(t);track.muted=(t%2)==0;track.solo=t==4;track.name="Track "+juce::String(int(t)+1);
        for(size_t j=0;j<8;++j){auto& slot=track.slots[j];const auto& entry=*inserts[(t*8+j)%inserts.size()];slot.pluginId=entry.id;slot.bypassed=(j%2)==1;auto p=entry.create();
            require(hem::PluginRegistry::idFor(*p)==entry.id,"Registry persistence ID mismatch");
            auto set=[&](const char* id,float value){auto* parameter=p->state.getParameter(id);require(parameter!=nullptr,"Missing new-module session parameter");parameter->setValueNotifyingHost(parameter->convertTo0to1(value));};
            if(auto* convolution=dynamic_cast<hem::ConvolutionReverbProcessor*>(p.get())){require(convolution->loadIRFileOffThread(irFile).wasOk(),"Cannot load real session IR");set("mix",33);set("irSize",125);}
            if(slot.pluginId=="algorithmic-reverb"){set("algorithm",2);set("decay",3.5f);}
            if(slot.pluginId=="chorus-flanger-phaser"){set("mode",1);set("sync",1);set("rate",.75f);}
            if(slot.pluginId=="rotary-tremolo-autopan"){set("shape",2);set("ramp",2.25f);}
            slot.parameters=hem::SessionState::captureParameters(*p);
        }
    }
    hem::SessionState::save(s,file);auto recalled=hem::SessionState::load(file);
    require(recalled.inputDb==s.inputDb&&recalled.positionSeconds==123.5&&recalled.looping,"Master state round trip");
    for(size_t t=0;t<13;++t){const auto& a=s.tracks[t];const auto& b=recalled.tracks[t];require(a.gainDb==b.gainDb&&a.muted==b.muted&&a.solo==b.solo,"Track state round trip");
        for(size_t j=0;j<8;++j){require(a.slots[j].pluginId==b.slots[j].pluginId&&a.slots[j].bypassed==b.slots[j].bypassed,"Insert order round trip");auto p=hem::PluginRegistry::create(b.slots[j].pluginId);hem::SessionState::restoreParameters(*p,b.slots[j].parameters);require(hem::SessionState::captureParameters(*p).isEquivalentTo(a.slots[j].parameters),"Parameter round trip");}}
    auto rejected=[&](juce::ValueTree tree){try{hem::SessionState::decode(tree,file);}catch(const std::exception&){return true;}return false;};
    auto tree=hem::SessionState::encode(s,file);tree.setProperty("version",999,nullptr);require(rejected(tree),"Unknown version accepted");
    tree=hem::SessionState::encode(s,file);tree.getChild(1).setProperty("gainDb",999,nullptr);require(rejected(tree),"Invalid gain accepted");
    tree=hem::SessionState::encode(s,file);tree.getChild(2).setProperty("index",0,nullptr);require(rejected(tree),"Duplicate track accepted");
    tree=hem::SessionState::encode(s,file);tree.getChild(1).getChild(0).setProperty("pluginId","missing-plugin",nullptr);require(rejected(tree),"Unknown plugin accepted");
    tree=hem::SessionState::encode(s,file);tree.getChild(1).setProperty("source",file.getFileName()+"-missing.wav",nullptr);require(rejected(tree),"Missing stem silently accepted");
    hem::SipaProcessor processor;auto values=hem::SessionState::captureParameters(processor);values.getChild(0).setProperty("value",999999,nullptr);bool bad=false;try{hem::SessionState::restoreParameters(processor,values);}catch(const std::exception&){bad=true;}require(bad,"Invalid plugin value accepted");
    hem::InsertChain chain;chain.prepare(96000,256);auto node=chain.stageNode(std::make_unique<hem::SipaProcessor>(),96000,256,chain.slotLatency());chain.installInitial(7,std::move(node),true);require(chain.activeProcessor(7)&&chain.isBypassed(7)&&!chain.isBusy(7),"Staged chain installation failed");
    auto json=file.withFileExtension("json");hem::PresetManager::save(s,json);auto jsonState=hem::PresetManager::load(json);
    require(jsonState.outputDb==s.outputDb&&jsonState.tracks[12].slots[7].pluginId==s.tracks[12].slots[7].pluginId,"JSON session round trip");
    for(size_t t=0;t<13;++t)for(size_t j=0;j<8;++j){auto p=hem::PluginRegistry::create(jsonState.tracks[t].slots[j].pluginId);hem::SessionState::restoreParameters(*p,jsonState.tracks[t].slots[j].parameters);require(hem::SessionState::captureParameters(*p).isEquivalentTo(s.tracks[t].slots[j].parameters),"JSON parameter round trip");}
    require(irFile.deleteFile(),"Cannot remove owned IR fixture for missing-resource test");
    auto unavailable=hem::PresetManager::load(json);bool checkedMissingIR=false;
    for(const auto& track:unavailable.tracks)for(const auto& slot:track.slots)if(slot.pluginId=="convolution-reverb"){
        auto p=hem::PluginRegistry::create(slot.pluginId);hem::SessionState::restoreParameters(*p,slot.parameters);
        auto* convolution=dynamic_cast<hem::ConvolutionReverbProcessor*>(p.get());require(convolution!=nullptr,"Real convolution registry identity missing");
        const auto resource=convolution->resourceInfo();require(!resource.ready&&resource.path==irFile.getFullPathName()&&resource.status.containsIgnoreCase("missing"),"Missing IR did not retain explicit unavailable resource state");
        require(hem::SessionState::captureParameters(*p).isEquivalentTo(slot.parameters),"Unavailable IR metadata did not survive session recall");checkedMissingIR=true;
    }
    require(checkedMissingIR,"Registry session fixture never exercised convolution resource persistence");
    require(hem::PresetManager::list(json.getParentDirectory()).contains(json),"Preset listing omitted JSON");
    require(hem::PresetManager::rename(json,"../bad").failed(),"Invalid rename accepted");
    require(hem::PresetManager::remove(json).wasOk()&&!json.existsAsFile(),"Preset deletion failed");
    file.deleteFile();std::cout<<"PASS: 13 x 8 XML/JSON state, all registered processor parameters, real IR path/hash recall and explicit missing-resource state, schema rejection, missing-stem rejection, staged routing\n";
 }catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}return 0;}
