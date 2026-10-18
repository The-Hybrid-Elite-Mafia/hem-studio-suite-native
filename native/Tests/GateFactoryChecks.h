#pragma once
#include "../Source/Plugins/InsertFactory.h"
#include "../Source/Systems/PresetManager.h"
#include "../Source/Systems/VocalTrackingPanel.h"

// Included after the existing lifecycle test helpers. These exercise production
// callbacks and worker restoration, not a parallel test-only factory.
namespace gateFactoryChecks {
inline void set(hem::ModuleProcessor& p,const char* id,float value){auto* v=p.state.getParameter(id);v->setValueNotifyingHost(v->convertTo0to1(value));}
inline void values(hem::ModuleProcessor& p,float ratio,float hysteresis){
 require(std::abs(p.state.getRawParameterValue("ratio")->load()-ratio)<.001f,"Factory/recall ratio changed");
 require(std::abs(p.state.getRawParameterValue("hysteresis")->load()-hysteresis)<.001f,"Factory/recall hysteresis changed");
}
inline int gateIndex(){for(size_t i=0;i<hem::PluginRegistry::entries().size();++i)if(juce::String(hem::PluginRegistry::entries()[i].id)=="smart-gate")return int(i);throw std::runtime_error("Missing gate");}
inline void snapshots(){
 const auto index=gateIndex();auto raw=hem::PluginRegistry::create("smart-gate");auto reference=hem::createModule(index);auto legacy=hem::createModule(hem::InsertFactory::legacyGateChoice());
 values(*raw,2,6);values(*legacy,2,6);values(*reference,1,0);
 for(const auto& spec:raw->parameterSpecs()){
  const float expected=juce::String(spec.id)=="ratio"?1:juce::String(spec.id)=="hysteresis"?0:spec.initial;
  require(std::abs(reference->state.getRawParameterValue(spec.id)->load()-expected)<.001f,"Reference profile changed another parameter");
  auto* parameter=reference->state.getParameter(spec.id);
  require(std::abs(parameter->convertFrom0to1(parameter->getDefaultValue())-spec.initial)<.001f,"APVTS legacy default changed");
 }
 for(size_t i=0;i<hem::PluginRegistry::entries().size();++i)if(int(i)!=index){auto p=hem::createModule(int(i));auto old=hem::PluginRegistry::entries()[i].create();require(hem::SessionState::captureParameters(*p).isEquivalentTo(hem::SessionState::captureParameters(*old)),"Unrelated factory changed");}
 for(int variant=0;variant<3;++variant){auto p=variant==1?hem::createModule(index):hem::PluginRegistry::create("smart-gate");
  if(variant==2){set(*p,"ratio",7.33f);set(*p,"hysteresis",11.2f);set(*p,"lookaheadMs",3.2f);set(*p,"range",17);}
  const auto expected=hem::SessionState::captureParameters(*p);juce::MemoryBlock bytes;p->getStateInformation(bytes);
  for(bool dirty:{false,true}){auto copy=dirty?hem::createModule(index):hem::PluginRegistry::create("smart-gate");copy->setStateInformation(bytes.getData(),int(bytes.getSize()));require(expected.isEquivalentTo(hem::SessionState::captureParameters(*copy)),"APVTS duplicate reinitialized stored values");
   set(*copy,"ratio",9);hem::SessionState::restoreParameters(*copy,expected.createCopy());require(expected.isEquivalentTo(hem::SessionState::captureParameters(*copy)),"Parameter duplicate reinitialized values");}
 }
 // A ten-parameter historical state must overwrite a dirty Reference v2
 // destination; appended fields migrate normally, never apply a new profile.
 auto old=hem::SessionState::captureParameters(*raw);for(int i=old.getNumChildren()-1;i>=0;--i){const auto id=old.getChild(i)["id"].toString();if(id=="lookaheadMs"||id=="keyListen")old.removeChild(i,nullptr);}
 set(*reference,"lookaheadMs",6);set(*reference,"keyListen",1);hem::SessionState::restoreParameters(*reference,old);values(*reference,2,6);
 require(reference->state.getRawParameterValue("lookaheadMs")->load()==0&&reference->state.getRawParameterValue("keyListen")->load()==0,"Historical migration changed");
 std::unique_ptr<juce::AudioProcessorEditor> editor(reference->createEditor());auto* ratio=dynamic_cast<juce::Slider*>(findControl(*editor,"ratio"));auto* hyst=dynamic_cast<juce::Slider*>(findControl(*editor,"hysteresis"));
 require(ratio&&hyst&&ratio->getDoubleClickReturnValue()==2&&hyst->getDoubleClickReturnValue()==6,"Legacy double-click changed");
 require(ratio->getTooltip().contains("legacy parameter default")&&hyst->getTooltip().contains("not a factory profile reset"),"Reset UI is misleading");
}
// Silent in-process test device: no hardware I/O, threads or real callbacks.
class Device final:public juce::AudioIODevice {
 bool opened=false;juce::AudioIODeviceCallback* callback=nullptr;juce::BigInteger input,output;
public:
 Device():AudioIODevice("Gate factory test","Gate factory test"){}
 juce::StringArray getOutputChannelNames() override{return {"Left","Right"};}
 juce::StringArray getInputChannelNames() override{return {"Input"};}
 juce::Array<double> getAvailableSampleRates() override{return {48000};}
 juce::Array<int> getAvailableBufferSizes() override{return {256};}
 int getDefaultBufferSize() override{return 256;}
 juce::String open(const juce::BigInteger& in,const juce::BigInteger& out,double,int) override{input=in;output=out;opened=true;return {};}
 void close() override{stop();opened=false;}
 bool isOpen() override{return opened;}
 void start(juce::AudioIODeviceCallback* c) override{callback=c;if(c)c->audioDeviceAboutToStart(this);}
 void stop() override{auto* c=callback;callback=nullptr;if(c)c->audioDeviceStopped();}
 bool isPlaying() override{return callback!=nullptr;}
 juce::String getLastError() override{return {};}
 int getCurrentBufferSizeSamples() override{return 256;}
 double getCurrentSampleRate() override{return 48000;}
 int getCurrentBitDepth() override{return 32;}
 juce::BigInteger getActiveOutputChannels() const override{return output;}
 juce::BigInteger getActiveInputChannels() const override{return input;}
 int getOutputLatencyInSamples() override{return 0;}
 int getInputLatencyInSamples() override{return 0;}
};
class DeviceType final:public juce::AudioIODeviceType {
public:
 DeviceType():AudioIODeviceType("Gate factory test"){}
 void scanForDevices() override{}
 juce::StringArray getDeviceNames(bool) const override{return {"Gate factory test"};}
 int getDefaultDeviceIndex(bool) const override{return 0;}
 int getIndexOfDevice(juce::AudioIODevice*,bool) const override{return 0;}
 bool hasSeparateInputsAndOutputs() const override{return false;}
 juce::AudioIODevice* createDevice(const juce::String&,const juce::String&) override{return new Device;}
};
inline void tracking(){
 juce::AudioDeviceManager device;device.addAudioDeviceType(std::make_unique<DeviceType>());device.setCurrentAudioDeviceType("Gate factory test",true);
 auto setup=device.getAudioDeviceSetup();setup.inputDeviceName=setup.outputDeviceName="Gate factory test";setup.sampleRate=48000;setup.bufferSize=256;
 require(device.setAudioDeviceSetup(setup,true).isEmpty()&&device.getCurrentAudioDevice()!=nullptr,"Test device unavailable");
 auto manager=std::make_unique<hem::LiveVocalManager>();manager->prepare(48000,256,1);
 hem::VocalTrackingPanel panel(*manager,device,[]{},[]{return 0;},[]{return 0;},[]{return juce::String();});
 juce::ComboBox* choices=nullptr;for(auto* c:panel.getChildren())if(auto* combo=dynamic_cast<juce::ComboBox*>(c))if(combo->getNumItems()==int(hem::PluginRegistry::entries().size()))choices=combo;
 require(choices&&choices->getItemText(choices->getNumItems()-1).contains("Legacy factory v1"),"Tracking profile choice absent");
 auto* insert=button(panel,"INSERT");require(insert,"Tracking insert button absent");
 for(bool legacy:{false,true}){choices->setSelectedId((legacy?hem::InsertFactory::legacyGateChoice():gateIndex())+1,juce::dontSendNotification);insert->onClick();
  auto* p=manager->rack.processor(0);require(p,"Actual tracking insert callback failed");values(*p,legacy?2.f:1.f,legacy?6.f:0.f);}
 auto raw=manager->rack.makeNode("smart-gate",48000,256);values(*raw->processor,2,6);
 device.closeAudioDevice();std::cout<<"PASS tracking UI new Reference v2 / explicit Legacy v1 and raw restoration factory\n";
}
inline void host(MainComponent& host,const juce::File& dir){
 snapshots();tracking();const int index=gateIndex();
 for(bool legacy:{false,true}){MainComponentTestAccess::insertMenu(host,legacy?hem::InsertFactory::legacyGateChoice():index);auto* p=MainComponentTestAccess::bank(host).tracks[0].chain.activeProcessor(0);require(p,"Host menu insert failed");values(*p,legacy?2.f:1.f,legacy?6.f:0.f);}
 auto& workspace=MainComponentTestAccess::workspace(host);workspace.setPage(hem::NativeWorkspace::Page::vault);workspace.setVaultFilter("smart-gate");
 auto* profile=dynamic_cast<juce::ComboBox*>(findControl(workspace,"gate-insert-profile"));auto* insert=dynamic_cast<juce::Button*>(findControl(workspace,"vault-install-"+juce::String(index)));
 require(profile&&insert&&profile->getSelectedId()==1,"Vault default is not Reference v2");
 for(bool legacy:{false,true}){auto* before=MainComponentTestAccess::bank(host).tracks[0].chain.activeProcessor(0);const auto state=hem::SessionState::captureParameters(*before);profile->setSelectedId(legacy?2:1,juce::sendNotificationSync);require(state.isEquivalentTo(hem::SessionState::captureParameters(*before)),"Profile selection mutated existing insert");insert->onClick();auto* p=MainComponentTestAccess::bank(host).tracks[0].chain.activeProcessor(0);values(*p,legacy?2.f:1.f,legacy?6.f:0.f);}
 for(int variant=0;variant<4;++variant){MainComponentTestAccess::install(host,variant==1?index:hem::InsertFactory::legacyGateChoice());auto* p=MainComponentTestAccess::bank(host).tracks[0].chain.activeProcessor(0);
  if(variant==2){set(*p,"ratio",7.33f);set(*p,"hysteresis",11.2f);set(*p,"range",17);set(*p,"lookaheadMs",3.2f);}
  const auto expected=hem::SessionState::captureParameters(*p);auto snapshot=MainComponentTestAccess::snapshot(host);
  if(variant==3){auto& params=snapshot.tracks[0].slots[0].parameters;for(int i=params.getNumChildren()-1;i>=0;--i){const auto id=params.getChild(i)["id"].toString();if(id=="lookaheadMs"||id=="keyListen")params.removeChild(i,nullptr);}}
  for(bool json:{false,true}){const auto file=dir.getChildFile("gate-profile-"+juce::String(variant)+(json?".json":".xml"));if(json)hem::PresetManager::save(snapshot,file);else hem::SessionState::save(snapshot,file);
   MainComponentTestAccess::install(host,index); // Dirty reference destination.
   require(MainComponentTestAccess::load(host,file).wasOk(),"Profile session admission failed");until([&]{return !MainComponentTestAccess::sessionBusy(host);},"Profile session worker timeout");
   auto* restored=MainComponentTestAccess::bank(host).tracks[0].chain.activeProcessor(0);require(restored&&expected.isEquivalentTo(hem::SessionState::captureParameters(*restored)),"XML/JSON staging reinitialized stored profile/custom/legacy values");
  }
 }
 MainComponentTestAccess::install(host,-1);workspace.setVaultFilter("");MainComponentTestAccess::home(host);
 std::cout<<"PASS Gate full factory snapshots, unchanged APVTS defaults, raw/state duplicates, parameter-reset labels, menu/vault callbacks, and 8 XML/JSON worker restores (Legacy v1, Reference v2, custom, historical)\n";
}
}
