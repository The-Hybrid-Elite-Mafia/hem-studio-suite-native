#pragma once
#include "SpecialtyProcessors.h"
#include "FinishingProcessors.h"
namespace hem {
// Stable string IDs are the persistence contract; menu indices are UI-only.
// Registration and construction are message-thread operations.
class PluginRegistry {
public:
    struct Entry {
        const char* id;
        const char* name;
        const char* category;
        std::unique_ptr<ModuleProcessor> (*create)();
        bool monitoringOnly=false;
    };
    static const std::array<Entry,12>& entries() {
        static const std::array<Entry,12> list{{
            {"sipa","S.I.P.A. / Stereo Image & Phase","Spatial",&make<SipaProcessor>},
            {"surgical-eq","Surgical Master EQ","EQ",&make<SurgicalEqProcessor>},
            {"vca","VCA Compressor","Dynamics",&make<VcaProcessor>},
            {"true-peak","True-Peak Limiter","Master",&make<TruePeakProcessor>},
            {"dhta","D.H.T.A. / De-Esser & Harshness","Dynamics",&make<DhtaProcessor>},
            {"transient","Transient Shaper & Attack Enhancer","Dynamics",&make<TransientProcessor>},
            {"sub-bass","808 Sub-Bass Manager","Bass",&make<SubBassProcessor>},
            {"spectral-restorer","Artifact & Spectral Restorer","Restoration",&make<RestorerProcessor>},
            {"vocal-presence","Vocal Presence & Resonance Architect","Vocal",&make<VocalProcessor>},
            {"triode-tube","Triode Tube Saturator","Saturation",&make<TubeProcessor>},
            {"tape","Tape Saturator","Saturation",&make<TapeProcessor>},
            {"monitor-eq","Optional Monitor EQ","Monitoring",&make<MonitorProcessor>,true}
        }};
        return list;
    }
    static std::unique_ptr<ModuleProcessor> create(const juce::String& id) {
        for(const auto& entry:entries()) if(id==entry.id) return entry.create();
        return {};
    }
    static juce::String idFor(const ModuleProcessor& processor) {
        if(processor.isExternal())return "external-vst3";
        static const std::array<const char*,12> types{{"SIPA","SURGICAL_EQ","VCA","TRUE_PEAK","DHTA","TRANSIENT","SUB_BASS","RESTORER","VPR_ARCHITECT","TRIODE_TUBE","TAPE","MONITOR_EQ"}};
        for(size_t i=0;i<types.size();++i) if(processor.state.state.hasType(types[i])) return entries()[i].id;
        return {};
    }
private:
    template<class T> static std::unique_ptr<ModuleProcessor> make() {return std::make_unique<T>();}
};
}
