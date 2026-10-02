#include "../Source/SessionState.h"
#include "../Source/Systems/PresetManager.h"
#include <iostream>
#include <stdexcept>
static void require(bool b,const char* message){if(!b)throw std::runtime_error(message);}
int main(){juce::ScopedJuceInitialiser_GUI juceRuntime;
 try{
    const auto file=juce::File::getSpecialLocation(juce::File::tempDirectory).getNonexistentChildFile("hem-session-test",".hem.xml");
    hem::SessionSnapshot s;s.inputDb=-3;s.outputDb=-6;s.positionSeconds=123.5;s.looping=true;
    hem::MonitorProcessor monitor;s.monitorParameters=hem::SessionState::captureParameters(monitor);
    for(size_t t=0;t<13;++t){auto& track=s.tracks[t];track.gainDb=-float(t);track.muted=(t%2)==0;track.solo=t==4;track.name="Track "+juce::String(int(t)+1);
        for(size_t j=0;j<8;++j){auto& slot=track.slots[j];const auto& entry=hem::PluginRegistry::entries()[(t+j)%11];slot.pluginId=entry.id;slot.bypassed=(j%2)==1;auto p=entry.create();
            require(hem::PluginRegistry::idFor(*p)==entry.id,"Registry persistence ID mismatch");
            slot.parameters=hem::SessionState::captureParameters(*p);
        }
    }
    hem::SessionState::save(s,file);auto recalled=hem::SessionState::load(file);
    require(recalled.inputDb==s.inputDb&&recalled.positionSeconds==123.5&&recalled.looping,"Master state round trip");
    for(size_t t=0;t<13;++t){const auto& a=s.tracks[t];const auto& b=recalled.tracks[t];require(a.gainDb==b.gainDb&&a.muted==b.muted&&a.solo==b.solo,"Track state round trip");
        for(size_t j=0;j<8;++j){require(a.slots[j].pluginId==b.slots[j].pluginId&&a.slots[j].bypassed==b.slots[j].bypassed,"Insert order round trip");auto p=hem::PluginRegistry::create(b.slots[j].pluginId);hem::SessionState::restoreParameters(*p,b.slots[j].parameters);const auto actual=hem::SessionState::captureParameters(*p);
            if(!actual.isEquivalentTo(a.slots[j].parameters)){
                std::cerr<<"track="<<t<<" slot="<<j<<" plugin="<<b.slots[j].pluginId<<"\n";
                for(int k=0;k<actual.getNumChildren();++k){
                    const auto expected=a.slots[j].parameters.getChild(k),got=actual.getChild(k);
                    std::cerr<<expected["id"].toString()<<" expected="<<std::hexfloat<<double(expected["value"])
                        <<" loaded="<<double(b.slots[j].parameters.getChild(k)["value"])
                        <<" actual="<<double(got["value"])<<"\n";
                }
            }
            require(actual.isEquivalentTo(a.slots[j].parameters),"Parameter round trip");}}
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
    require(hem::PresetManager::list(json.getParentDirectory()).contains(json),"Preset listing omitted JSON");
    require(hem::PresetManager::rename(json,"../bad").failed(),"Invalid rename accepted");
    require(hem::PresetManager::remove(json).wasOk()&&!json.existsAsFile(),"Preset deletion failed");
    file.deleteFile();std::cout<<"PASS: 13 x 8 XML state, processor parameters, schema rejection, missing-file rejection, staged routing\n";
 }catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}return 0;}
