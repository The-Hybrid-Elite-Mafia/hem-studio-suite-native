#include "../Source/UI/NativeWorkspace.h"
#include "../Source/Plugins/PluginRegistry.h"
#include "../Source/Plugins/RestorationEditors.h"
#include <iostream>
#include <stdexcept>
namespace {
void check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
juce::Component* find(juce::Component& c,const juce::String& id){if(c.getComponentID()==id)return &c;for(auto* child:c.getChildren())if(auto* hit=find(*child,id))return hit;return nullptr;}
void dispatch(){juce::MessageManager::getInstance()->runDispatchLoopUntil(25);}
void close(hem::NativeWorkspace& workspace){auto* b=dynamic_cast<juce::Button*>(find(workspace,"close"));if(!b||!b->isEnabled()||!b->onClick)std::cerr<<"Close diagnostic: exists="<<(b!=nullptr)<<" enabled="<<(b&&b->isEnabled())<<" callback="<<(b&&bool(b->onClick))<<" workspace="<<workspace.getWidth()<<"x"<<workspace.getHeight()<<"\n";check(b&&b->isEnabled()&&bool(b->onClick),"Original Close is not bound by real host");b->onClick();}
std::unique_ptr<hem::NativeWorkspace> makeWorkspace(hem::TrackBank& bank,int& track,int& slot,int& back){
 hem::NativeWorkspace::Callbacks callbacks;callbacks.bank=[&]() -> hem::TrackBank& {return bank;};callbacks.getSelectedTrack=[&]{return track;};callbacks.getSelectedSlot=[&]{return slot;};callbacks.setSelectedTrack=[&](int value){track=value;};callbacks.setSelectedSlot=[&](int value){slot=value;};callbacks.onEditorBack=[&]{++back;};
 auto w=std::make_unique<hem::NativeWorkspace>(std::move(callbacks));w->setSize(1920,1200);w->setVisible(true);return w;
}
}
int main(){juce::ScopedJuceInitialiser_GUI gui;try{
 auto bank=std::make_unique<hem::TrackBank>();int track=0,slot=0,backs=0;
 auto workspace=makeWorkspace(*bank,track,slot,backs);
 int cycles=0;
 for(const char* id:{"dynamic-resonance-suppressor","breath-mouth-control"}){
  auto p=hem::PluginRegistry::create(id);check(bool(p),"Registry processor missing");p->prepareToPlay(48000,257);
  for(int repeat=0;repeat<3;++repeat){
   workspace->setEditor(std::unique_ptr<juce::AudioProcessorEditor>(p->createEditor()));const int before=backs;
   close(*workspace);close(*workspace);check(workspace->hasEditor(),"Close deleted its own control synchronously");dispatch();
   check(!workspace->hasEditor()&&backs==before+1,"Repeated Close was not exactly one lifetime-safe host Back");++cycles;
  }
  // An older queued close must not close a newer editor, even for same processor.
  workspace->setEditor(std::unique_ptr<juce::AudioProcessorEditor>(p->createEditor()));close(*workspace);
  workspace->setEditor(std::unique_ptr<juce::AudioProcessorEditor>(p->createEditor()));const int before=backs;dispatch();
  check(workspace->hasEditor()&&backs==before,"Stale Close affected replacement editor");
  close(*workspace);dispatch();check(!workspace->hasEditor()&&backs==before+1,"Replacement editor Close disconnected");
  // Match host retirement contract: clear editor before removing processor.
  workspace->setEditor(std::unique_ptr<juce::AudioProcessorEditor>(p->createEditor()));close(*workspace);
  workspace->clearEditor();p.reset();const int retired=backs;track=12;slot=7;dispatch();
  check(!workspace->hasEditor()&&backs==retired,"Retired Close reached new selection or destroyed processor");track=slot=0;
  std::cout<<"PASS original host Close, repeated clicks, replaced editor, processor retirement and selection isolation "<<id<<'\n';
 }
 // Artwork09 has no local Close; the existing host Back remains the real route.
 auto clip=hem::PluginRegistry::create("clip-repair");clip->prepareToPlay(48000,257);workspace->setEditor(std::unique_ptr<juce::AudioProcessorEditor>(clip->createEditor()));check(find(*workspace,"close")==nullptr,"Invented Clip Repair Close control");check(workspace->keyPressed(juce::KeyPress(juce::KeyPress::escapeKey))&&!workspace->hasEditor(),"Clip Repair host Escape/Back failed");
 // Pending callbacks must safely expire if the whole workspace owner is gone.
 auto p=hem::PluginRegistry::create("dynamic-resonance-suppressor");p->prepareToPlay(48000,257);workspace->setEditor(std::unique_ptr<juce::AudioProcessorEditor>(p->createEditor()));close(*workspace);const int before=backs;workspace.reset();dispatch();check(backs==before,"Destroyed owner callback escaped lifetime guard");
 std::cout<<"PASS "<<cycles<<" repeated close cycles, real deferred owner binding, stale callback suppression,09 host Back and destroyed-owner safety; no native popup/window claim\n";return 0;
}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
