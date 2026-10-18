#include "../Source/Plugins/PluginRegistry.h"
#include "../Source/Plugins/Editors.h"
#include "DiscreteControlChecks.h"
#include "../Source/Plugins/DelayEditors.h"
#include "BandHostInteractionSetup.h"
#include <iostream>
#include <stdexcept>

namespace {
void require(bool condition,const juce::String& message){if(!condition)throw std::runtime_error(message.toStdString());}
juce::Component* find(juce::Component& parent,const juce::String& id){
 if(parent.getComponentID()==id)return &parent;
 for(int i=0;i<parent.getNumChildComponents();++i)if(auto* c=find(*parent.getChildComponent(i),id))return c;
 return nullptr;
}
void reveal(hem::ModuleProcessor& processor,juce::AudioProcessorEditor& editor,const juce::String& id){
 bandHostFixture::prepare(processor,editor,id);
 if(auto* delay=dynamic_cast<hem::DelayEditor*>(&editor)){
  if(id.startsWith("tap"))delay->selectTap(id.substring(3,4).getIntValue()-1);
  if(id.endsWith("division")||id.endsWith("time_ms"))processor.state.getParameter("sync")->setValueNotifyingHost(id.endsWith("division")?1.f:0.f);
  delay->refreshBindings();
 }
}
}
int main(){juce::ScopedJuceInitialiser_GUI gui;try{
 // Independently enumerated Surgical EQ factory contract, including units.
 {
  hem::SurgicalEqProcessor eq;std::unique_ptr<juce::AudioProcessorEditor> editor(eq.createEditor());
  struct Expected {const char* id;float minimum,maximum,initial;const char* unit;};
  for(const auto& e:std::array<Expected,14>{{
   {"hp",20,80,30,"Hz"},{"slope",1,4,1,""},{"mudFreq",150,600,300,"Hz"},{"mudGain",-12,6,0,"dB"},{"mudQ",.5f,10,1,"Q"},
   {"midFreq",600,3500,1500,"Hz"},{"midGain",-9,9,0,"dB"},{"midQ",.5f,6,1,"Q"},
   {"harshFreq",4000,8500,6500,"Hz"},{"harshGain",-12,3,0,"dB"},{"harshQ",1,8,2,"Q"},
   {"airFreq",10000,20000,12000,"Hz"},{"airGain",-6,8,0,"dB"},{"output",-60,6,0,"dB"}}}){
    auto* slider=dynamic_cast<juce::Slider*>(find(*editor,e.id));require(slider!=nullptr,"EQ control missing");
    require(std::abs(slider->getValue()-e.initial)<.001&&std::abs(slider->getMinimum()-e.minimum)<.001&&std::abs(slider->getMaximum()-e.maximum)<.001,"EQ independent factory/range contract mismatch");
    require(slider->getTextValueSuffix().trim()==e.unit,"EQ displayed unit mismatch");
    require(std::abs(slider->getDoubleClickReturnValue()-e.initial)<.001,"EQ double-click reset contract mismatch");
    if(e.minimum<0&&e.maximum>0)require(std::abs(slider->valueToProportionOfLength(0)-.5)<.0001,"EQ neutral gain pointer is not centered");
    const double position=slider->valueToProportionOfLength(e.initial);require(position>=0&&position<=1&&std::abs(slider->proportionOfLengthToValue(position)-e.initial)<.01,"EQ default pointer position does not map back to value");
  }
  auto settings=eq.settings();require(!settings.highPass&&settings.cutoff==30&&settings.slope==0&&settings.output==0,"EQ neutral filter enable/cascade/output defaults mismatch");
  for(int i=0;i<4;++i)require(settings.db[i]==0,"EQ default gain is not unity");
 }
 int parameters=0,toggles=0,knobs=0,choices=0;
 for(const auto& entry:hem::PluginRegistry::entries()){
  auto processor=entry.create();processor->setPlayConfigDetails(2,2,48000,256);processor->prepareToPlay(48000,256);
  std::unique_ptr<juce::AudioProcessorEditor> editor(processor->createEditor());require(editor!=nullptr,"Missing native editor");
  for(const auto& spec:processor->parameterSpecs()){
   auto* raw=processor->state.getRawParameterValue(spec.id);require(raw!=nullptr,"Missing APVTS parameter");
   require(std::abs(raw->load()-spec.initial)<1.e-4f,juce::String(entry.id)+" startup parameter mismatch: "+spec.id);
  }
  for(const auto& spec:processor->parameterSpecs()){
   ++parameters;auto* raw=processor->state.getRawParameterValue(spec.id);
   reveal(*processor,*editor,spec.id);
   auto* component=find(*editor,spec.id);require(component!=nullptr,juce::String(entry.id)+" missing actual control: "+spec.id);
   const bool slopeChoice=juce::String(entry.id)=="dc-subsonic"&&juce::String(spec.id)=="steep";
   float savedSubOn=0;
   if(slopeChoice){
    auto* combo=dynamic_cast<juce::ComboBox*>(component);
    auto* parameter=dynamic_cast<juce::AudioParameterBool*>(processor->state.getParameter(spec.id));
    require(combo&&parameter&&spec.toggle&&juce::String(spec.name)=="24 dB/OCT"
            &&spec.minimum==0&&spec.maximum==1&&spec.initial==0&&spec.step==1,
            "Cleaner slope changed its stable bool parameter contract");
    require(component->getName()=="Subsonic slope"&&combo->getNumItems()==2
            &&combo->getItemId(0)==1&&combo->getItemId(1)==2
            &&combo->getItemText(0)=="12 dB/oct"&&combo->getItemText(1)=="24 dB/oct",
            "Cleaner slope accessible identity or physical choices differ");
    savedSubOn=processor->state.getRawParameterValue("subOn")->load();
    processor->state.getParameter("subOn")->setValueNotifyingHost(1.f);
    auto* module=dynamic_cast<hem::ModuleEditor*>(editor.get());require(module!=nullptr,"Cleaner editor missing refresh hook");
    module->refreshParameterActivity();require(combo->isEnabled(),"Enabled HPF leaves slope unavailable");
   }else require(component->getName()==spec.name,juce::String(entry.id)+" accessible name differs for "+spec.id+": "+component->getName());
   if(hem::controlChecks::exerciseDiscrete(*processor,*editor,spec,*component)){++choices;}else if(spec.toggle){
    ++toggles;auto* button=dynamic_cast<juce::ToggleButton*>(component);require(button!=nullptr,"Toggle mapped to wrong control");
    button->setToggleState(true,juce::sendNotificationSync);require(raw->load()==1.f,"Native toggle ON did not reach DSP parameter");
    button->setToggleState(false,juce::sendNotificationSync);require(raw->load()==0.f,"Native toggle OFF did not reach DSP parameter");
   }else{
    ++knobs;auto* slider=dynamic_cast<juce::Slider*>(component);require(slider!=nullptr,"Numeric parameter mapped to wrong control");
    require(std::abs(slider->getMinimum()-spec.minimum)<1.e-4&&std::abs(slider->getMaximum()-spec.maximum)<1.e-4,"Native knob range differs from DSP parameter range");
    for(const auto value:{spec.minimum,spec.maximum,spec.initial}){
     slider->setValue(value,juce::sendNotificationSync);require(std::abs(raw->load()-value)<=std::max(1.e-4f,spec.step*.51f),juce::String(entry.id)+" knob failed to update "+spec.id);
    }
   }
   auto* parameter=processor->state.getParameter(spec.id);parameter->setValueNotifyingHost(parameter->convertTo0to1(spec.initial));
   if(slopeChoice){
    auto* combo=dynamic_cast<juce::ComboBox*>(component);auto* module=dynamic_cast<hem::ModuleEditor*>(editor.get());
    processor->state.getParameter("subOn")->setValueNotifyingHost(0.f);module->refreshParameterActivity();
    require(!combo->isEnabled(),"Disabled HPF leaves slope editable");
    combo->setSelectedItemIndex(1,juce::sendNotificationSync);
    require(raw->load()==spec.initial,"Disabled slope callback changed the stable bool parameter");
    processor->state.getParameter("subOn")->setValueNotifyingHost(savedSubOn);module->refreshParameterActivity();
   }
  }
  // Real GUI composition, with all native parameter controls inside the editor.
  juce::Image pixels(juce::Image::ARGB,editor->getWidth(),editor->getHeight(),true);juce::Graphics g(pixels);editor->paintEntireComponent(g,true);
  for(const auto& spec:processor->parameterSpecs()){reveal(*processor,*editor,spec.id);auto* c=find(*editor,spec.id);require(c&&c->getWidth()>0&&c->getHeight()>0&&editor->getLocalBounds().contains(c->getBounds()),"Parameter control lies outside its native editor");}
  editor.reset();processor->releaseResources();
 }
 std::cout<<"PASS "<<hem::PluginRegistry::entries().size()<<" real native editors, "<<parameters<<" parameter bindings ("<<knobs<<" numeric, "<<toggles<<" toggles, "<<choices<<" discrete selectors), startup defaults, min/max/reset, accessible identities and raster bounds\n";return 0;
}catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}}
