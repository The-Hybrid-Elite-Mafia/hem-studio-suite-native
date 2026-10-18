#pragma once
#include <stdexcept>

namespace hem::controlChecks {
inline juce::String choicePrefix(const juce::Component& c) {
    // Metadata identifies the visible buttons themselves, never a hidden
    // surrogate slider. Keep earlier module contracts backward compatible.
    for (const auto* prefix : {"hem", "analysis", "reverb", "modulation"})
        if (c.getProperties().contains(juce::String(prefix) + "ChoiceParameter"))
            return prefix;
    return {};
}
inline bool choice(const juce::Component& c) {
    return choicePrefix(c).isNotEmpty();
}
inline juce::Component* find(juce::Component& root,const juce::String& id) {
    if(root.getComponentID()==id)return &root;
    for(auto* child:root.getChildren())if(auto* found=find(*child,id))return found;
    return nullptr;
}
inline void require(bool pass,const char* message) {if(!pass)throw std::runtime_error(message);}
inline bool exerciseDiscrete(ModuleProcessor& processor,juce::AudioProcessorEditor& editor,
                             const ParameterSpec& spec,juce::Component& component) {
    auto* value=processor.state.getRawParameterValue(spec.id);
    auto* parameter=processor.state.getParameter(spec.id);
    if(auto* combo=dynamic_cast<juce::ComboBox*>(&component)){
        require(spec.step==1&&spec.minimum==0&&combo->getNumItems()==int(spec.maximum)+1,
                "ComboBox choices differ from the parameter contract");
        for(int option=0;option<combo->getNumItems();++option){
            combo->setSelectedItemIndex(option,juce::sendNotificationSync);
            require(value->load()==float(option),"Actual ComboBox option did not reach APVTS");
        }
    }else if(choice(component)){
        const auto prefix=choicePrefix(component);
        require(spec.step==1&&spec.minimum==0,"Choice bar does not describe a discrete parameter");
        for(int option=0;option<=int(spec.maximum);++option){
            auto* button=dynamic_cast<juce::Button*>(find(editor,option==0?juce::String(spec.id):juce::String(spec.id)+"_"+juce::String(option)));
            require(button&&button->getProperties()[prefix+"ChoiceParameter"].toString()==spec.id
                    &&int(button->getProperties()[prefix+"ChoiceValue"])==option,"Actual choice button missing/misidentified");
            require(button->isVisible()&&!button->getBounds().isEmpty(),"Choice button inaccessible");
            button->onClick();
            require(value->load()==float(option)&&button->getToggleState(),"Actual choice button did not select its APVTS value");
        }
    }else return false;
    parameter->setValueNotifyingHost(parameter->convertTo0to1(spec.initial));
    if(auto* module=dynamic_cast<ModuleEditor*>(&editor))module->refreshParameterActivity();
    return true;
}
} // namespace hem::controlChecks
