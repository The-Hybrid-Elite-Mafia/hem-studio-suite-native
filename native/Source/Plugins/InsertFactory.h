#pragma once
#include "PluginRegistry.h"

namespace hem {
// New user inserts only. Persistence/duplication MUST use PluginRegistry::create
// and restore the stored values. This factory never receives an existing object.
// Call on the control thread before preparation/publication, never in audio.
class InsertFactory final {
public:
    static int legacyGateChoice() { return int(PluginRegistry::entries().size()); }
    static juce::String name(int choice) {
        if(choice==legacyGateChoice())return "Smart Gate / Expander - Legacy factory v1";
        const auto& entries=PluginRegistry::entries();
        if(choice<0||choice>=int(entries.size()))return "EMPTY";
        return juce::String(entries[size_t(choice)].name)
            +(juce::String(entries[size_t(choice)].id)=="smart-gate"?" - Reference v2":"");
    }
    static std::unique_ptr<ModuleProcessor> create(int choice) {
        if(choice==legacyGateChoice())return PluginRegistry::create("smart-gate");
        const auto& entries=PluginRegistry::entries();
        if(choice<0||choice>=int(entries.size()))return {};
        if(!entries[size_t(choice)].realtimeInsert)return {};
        auto processor=entries[size_t(choice)].create();
        if(juce::String(entries[size_t(choice)].id)=="smart-gate"){
            // Full snapshot = unchanged legacy constructor values, except these
            // two approved reference-v2 overrides. APVTS defaults remain legacy.
            for(const auto& setting:std::array<std::pair<const char*,float>,2>{{{"ratio",1.f},{"hysteresis",0.f}}}){
                auto* parameter=processor->state.getParameter(setting.first);
                parameter->setValueNotifyingHost(parameter->convertTo0to1(setting.second));
            }
        }
        return processor;
    }
};
}
