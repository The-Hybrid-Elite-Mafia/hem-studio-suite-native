#include "../Source/Plugins/InsertFactory.h"
#include "../Source/InsertChain.h"
#include "../Source/Systems/LiveVocalManager.h"
#include "../Source/SessionState.h"
#include <iostream>
#include <stdexcept>
static void check(bool v,const char* m){if(!v)throw std::runtime_error(m);}
template<class F> static bool rejects(F&& f){try{f();return false;}catch(const std::exception&){return true;}}
int main(){juce::ScopedJuceInitialiser_GUI gui;try{
 const auto& entries=hem::PluginRegistry::entries();check(entries.size()==40,"40 approved+legacy identities");
 for(const char* id:{"pitch-formant","time-pitch-stretch"}){
  const auto it=std::find_if(entries.begin(),entries.end(),[&](const auto& e){return juce::String(e.id)==id;});
  check(it!=entries.end()&&!it->realtimeInsert&&it->clipOperation&&!it->trackingAllowed,"explicit offline capabilities");
  auto p=hem::PluginRegistry::create(id);check(p&&hem::PluginRegistry::idFor(*p)==id&&!p->realtimeInsertAllowed(),"offline adapter identity");
  check(!hem::InsertFactory::create(int(it-entries.begin())),"insert menu factory refuses offline");
  hem::InsertChain rack;rack.prepare(48000,256);check(rack.slotLatency()==288,"6ms unchanged");
  check(rejects([&]{rack.stageNode(hem::PluginRegistry::create(id),48000,256,rack.slotLatency());}),"direct rack refuses offline");
  hem::TrackingInsertChain tracking;check(rejects([&]{tracking.makeNode(id,48000,256);}),"tracking refuses offline");
  hem::SessionSnapshot snapshot;hem::MonitorProcessor monitor;snapshot.monitorParameters=hem::SessionState::captureParameters(monitor);
  auto& slot=snapshot.tracks[0].slots[0];slot.pluginId=id;slot.parameters=hem::SessionState::captureParameters(*p);
  check(rejects([&]{hem::SessionState::encode(snapshot,juce::File::getCurrentWorkingDirectory().getChildFile("not-written.xml"));}),"session slot rejects clip operation");
  auto parameters=slot.parameters.createCopy();slot.pluginId={};slot.parameters=juce::ValueTree("PARAMETERS");
  const auto path=juce::File::getCurrentWorkingDirectory().getChildFile("not-written.xml");
  auto hostile=hem::SessionState::encode(snapshot,path);auto stored=hostile.getChild(1).getChild(0);
  stored.setProperty("pluginId",id,nullptr);stored.removeChild(0,nullptr);stored.addChild(parameters,0,nullptr);
  check(rejects([&]{hem::SessionState::decode(hostile,path);}),"handcrafted session refuses offline insert");
 }
 std::cout<<"PASS offline catalog routes cannot enter factory, main rack, tracking rack or serialized insert slots; 6ms unchanged\n";return 0;
 }catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<"\n";return 1;}}
