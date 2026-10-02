#include "../Source/Plugins/PluginRegistry.h"
#include <iostream>
#include <stdexcept>

namespace {
void require(bool condition,const juce::String& message){if(!condition)throw std::runtime_error(message.toStdString());}
juce::Component* find(juce::Component& parent,const juce::String& id){
 if(parent.getComponentID()==id)return &parent;
 for(int i=0;i<parent.getNumChildComponents();++i)if(auto* c=find(*parent.getChildComponent(i),id))return c;
 return nullptr;
}
}
int main(){juce::ScopedJuceInitialiser_GUI gui;try{
 int parameters=0,toggles=0,knobs=0;
 for(const auto& entry:hem::PluginRegistry::entries()){
  auto processor=entry.create();processor->setPlayConfigDetails(2,2,48000,256);processor->prepareToPlay(48000,256);
  std::unique_ptr<juce::AudioProcessorEditor> editor(processor->createEditor());require(editor!=nullptr,"Missing native editor");
  for(const auto& spec:processor->parameterSpecs()){
   ++parameters;auto* raw=processor->state.getRawParameterValue(spec.id);require(raw!=nullptr,"Missing APVTS parameter");
   require(std::abs(raw->load()-spec.initial)<1.e-4f,juce::String(entry.id)+" startup parameter mismatch: "+spec.id);
   auto* component=find(*editor,spec.id);require(component!=nullptr,juce::String(entry.id)+" missing actual control: "+spec.id);
   require(component->getName()==spec.name,"Accessible control identity differs from parameter contract");
   if(spec.toggle){
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
  }
  // Real GUI composition, with all native parameter controls inside the editor.
  juce::Image pixels(juce::Image::ARGB,editor->getWidth(),editor->getHeight(),true);juce::Graphics g(pixels);editor->paintEntireComponent(g,true);
  for(const auto& spec:processor->parameterSpecs()){auto* c=find(*editor,spec.id);require(c&&c->getWidth()>0&&c->getHeight()>0&&editor->getLocalBounds().contains(c->getBounds()),"Parameter control lies outside its native editor");}
  editor.reset();processor->releaseResources();
 }
 std::cout<<"PASS "<<hem::PluginRegistry::entries().size()<<" real native editors, "<<parameters<<" parameter bindings ("<<knobs<<" numeric, "<<toggles<<" toggles), startup defaults, min/max/reset, accessible identities and raster bounds\n";return 0;
}catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}}
