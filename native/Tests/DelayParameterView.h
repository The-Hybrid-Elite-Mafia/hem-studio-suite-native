#pragma once
#include "../Source/Plugins/DelayEditors.h"
namespace hem::test {
// Test-only view selection, matching the original shared filter/time controls.
// Existing parameter/raster/gesture assertions remain applicable to every field.
class DelayParameterView {
public:
    DelayParameterView(ModuleProcessor& p,juce::AudioProcessorEditor& e,const juce::String& id):processor(p),editor(dynamic_cast<DelayEditor*>(&e)){
        if(!editor)return;
        if(id.startsWith("tap"))editor->selectTap(id.substring(3,id.indexOfChar('.')).getIntValue()-1);
        if(id.endsWith("time_ms")||id.endsWith("division")){
            parameter=processor.state.getParameter("sync");old=parameter->getValue();parameter->setValueNotifyingHost(id.endsWith("division")?1.f:0.f);
        }
        editor->refreshBindings();
    }
    ~DelayParameterView(){if(parameter){parameter->setValueNotifyingHost(old);editor->refreshBindings();}}
private:ModuleProcessor& processor;DelayEditor* editor=nullptr;juce::RangedAudioParameter* parameter=nullptr;float old=0;
};
}
