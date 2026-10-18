#pragma once
// Prepared for the next authorized native test run; not executed during repair.
#if JUCE_WINDOWS
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
static juce::AudioProcessorEditor* navigationEditor(juce::Component& parent){
 if(auto* editor=dynamic_cast<juce::AudioProcessorEditor*>(&parent))return editor;
 for(auto* child:parent.getChildren())if(auto* editor=navigationEditor(*child))return editor;
 return nullptr;
}
#if JUCE_WINDOWS
static void pumpNavigationMessages(){
 const auto end=juce::Time::getMillisecondCounter()+100;
 do {MSG message{};while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){
  if(message.message==WM_QUIT){PostQuitMessage(int(message.wParam));throw std::runtime_error("Unexpected quit during navigation fixture");}
  TranslateMessage(&message);DispatchMessageW(&message);
  if(juce::Time::getMillisecondCounter()>=end)return;
 }juce::Thread::sleep(1);}while(juce::Time::getMillisecondCounter()<end);
}
#endif
static void editorNavigationChecks(MainComponent& app,int eq){
 auto require=[](bool ok,const char* message){if(!ok)throw std::runtime_error(message);};
 auto& workspace=MainComponentTestAccess::workspace(app);
 auto back=[&]{auto* button=dynamic_cast<juce::Button*>(find(app,"plugin-expand"));require(button!=nullptr,"Host Back missing");auto action=button->onClick;action();};
 auto checkPage=[&](int expected){require(MainComponentTestAccess::page(app)==expected&&!workspace.hasEditor(),"Back did not restore its actual origin");};
 // Ordinary entry, then internal replacement, must both return to the entry page.
 for(bool fromVault:{false,true}){
  if(fromVault)MainComponentTestAccess::vault(app);else MainComponentTestAccess::home(app);
  MainComponentTestAccess::open(app);back();checkPage(fromVault?3:0);
  MainComponentTestAccess::open(app);MainComponentTestAccess::replaceEditor(app);back();checkPage(fromVault?3:0);
 }
 // A stale Vault origin must not leak into a new rack visit for any editor kind.
 MainComponentTestAccess::vault(app);MainComponentTestAccess::open(app);back();checkPage(3);
 MainComponentTestAccess::rack(app);MainComponentTestAccess::open(app);back();checkPage(2);
 MainComponentTestAccess::monitor(app);back();checkPage(2);
 MainComponentTestAccess::clip(app);require(MainComponentTestAccess::clipOwned(app),"Clip owner fixture missing");
 back();checkPage(2);require(!MainComponentTestAccess::clipOwned(app),"Host Back retained clip owner or audio-preview pointer");
#if JUCE_WINDOWS
 auto escape=[&]{auto* editor=navigationEditor(workspace);require(editor!=nullptr,"Escape fixture editor missing");require(editor->keyPressed(juce::KeyPress(juce::KeyPress::escapeKey)),"Editor rejected Escape");};
 MainComponentTestAccess::vault(app);MainComponentTestAccess::clip(app);escape();escape();
 require(workspace.hasEditor()&&MainComponentTestAccess::clipOwned(app),"Clip Escape retired its callback synchronously");
 pumpNavigationMessages();checkPage(3);require(!MainComponentTestAccess::clipOwned(app),"Clip Escape failed centralized teardown");
 MainComponentTestAccess::clip(app);escape();MainComponentTestAccess::open(app);pumpNavigationMessages();
 require(workspace.hasEditor(),"Old clip Escape closed a replacement editor");back();checkPage(3);
 int reference=-1;for(size_t i=0;i<hem::PluginRegistry::entries().size();++i)if(juce::String(hem::PluginRegistry::entries()[i].id)=="reference-match")reference=int(i);
 require(reference>=0,"Reference Match registry entry missing");MainComponentTestAccess::install(app,reference);
 MainComponentTestAccess::vault(app);MainComponentTestAccess::open(app);escape();require(workspace.hasEditor(),"Reference Escape retired its callback synchronously");
 pumpNavigationMessages();checkPage(3);
 MainComponentTestAccess::open(app);escape();MainComponentTestAccess::rack(app);MainComponentTestAccess::open(app);pumpNavigationMessages();
 require(workspace.hasEditor(),"Old Reference Escape closed a replacement editor");back();checkPage(2);
 MainComponentTestAccess::install(app,eq);
#else
 (void)eq;std::cout<<"SKIP deferred Escape checks: native Windows message pump required\n";
#endif
 MainComponentTestAccess::home(app);
 std::cout<<"PASS editor origin, replacement origin and host Back clip teardown\n";
}
