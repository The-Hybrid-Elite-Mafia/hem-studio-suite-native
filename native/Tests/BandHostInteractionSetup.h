#pragma once
#include "../Source/Plugins/OriginalDynamicEqEditor.h"
#include "../Source/Plugins/OriginalMidSideEqEditor.h"
#include "../Source/Plugins/OriginalBankEditorBase.h"
namespace bandHostFixture {
inline juce::Component* find(juce::Component& parent,const juce::String& id){if(parent.getComponentID()==id)return &parent;for(auto* c:parent.getChildren())if(auto* p=find(*c,id))return p;return nullptr;}
// Fixture navigation/activation only. The original lifecycle test still performs
// every root hit-test, gesture-count, parameter, session and lifetime assertion.
inline void prepare(hem::ModuleProcessor& processor,juce::AudioProcessorEditor& editor,const juce::String& id){
    auto* band=dynamic_cast<hem::BandModuleProcessor*>(&processor);if(!band)return;
    auto set=[&](const juce::String& key,float value){auto* p=processor.state.getParameter(key);if(p)p->setValueNotifyingHost(p->convertTo0to1(value));};
    if(auto* dynamic=dynamic_cast<hem::OriginalDynamicEqEditor*>(&editor)){
        if(id.startsWith("b")){const auto prefix=id.substring(0,2);set(prefix+"On",1);set(prefix+"DynamicOn",1);set(prefix+"Range",6);
            if(auto* page=dynamic_cast<juce::ComboBox*>(find(editor,"uiBandPage")))page->setSelectedId(id.substring(1,2).getIntValue(),juce::sendNotificationSync);}
        dynamic->refresh();
    }else if(auto* ms=dynamic_cast<hem::OriginalMidSideEqEditor*>(&editor)){
        if(id.length()>2&&juce::CharacterFunctions::isDigit(id[1]))set(id.substring(0,2)+"On",1);ms->refresh();
    }else if(auto* bank=dynamic_cast<hem::OriginalBankEditorBase*>(&editor)){
        set("topology",id.startsWith("mid")||id=="legacyMidOn"?0.f:1.f);
        for(const auto& spec:processor.parameterSpecs()){const juce::String key(spec.id);if(key.endsWith("On"))set(key,1);if(key.endsWith("Ratio"))set(key,4);if(key.endsWith("Range"))set(key,12);}set("lowMono",0);bank->refresh();
        if(auto* c=find(editor,id);c&&!c->isVisible())if(auto* button=dynamic_cast<juce::Button*>(find(editor,"uiAdvanced")))if(button->onClick)button->onClick();bank->refresh();
    }
}
}
