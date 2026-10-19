#pragma once
#include "SpecialtyProcessors.h"
#include "FinishingProcessors.h"
#include "FoundationProcessors.h"
#include "AnalysisProcessors.h"
#include "ReverbProcessors.h"
#include "ModulationProcessors.h"
#include "DelayProcessors.h"
#include "BandProcessors.h"
#include "NonlinearProcessors.h"
#include "RestorationProcessors.h"
#include "DrumReplacerProcessor.h"
#include "AmpCabinetProcessor.h"
#include "ReferenceMatchProcessor.h"
#include "PitchTimeProcessors.h"
#include "StudioFaceplateProcessors.h"
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
        bool realtimeInsert=true;
        bool clipOperation=false;
        bool trackingAllowed=true;
    };
    static const std::array<Entry,41>& entries() {
        static const std::array<Entry,41> list{{
            {"sipa","S.I.P.A. / Stereo Image & Phase Architect","Spatial",&make<SipaArchitectProcessor>},
            {"surgical-eq","Surgical Master EQ","EQ",&make<SurgicalEqProcessor>},
            {"vca","VCA Compressor","Dynamics",&make<VcaProcessor>},
            {"true-peak","True-Peak Limiter","Master",&make<TruePeakProcessor>},
            {"dhta","D.H.T.A. / De-Esser & Harshness","Dynamics",&make<DhtaProcessor>},
            {"transient","Transient Shaper","Dynamics",&make<TransientShaperProcessor>},
            {"sub-bass","808 Sub-Bass Manager","Bass",&make<SubBassProcessor>},
            {"spectral-restorer","Artifact & Spectral Restorer","Restoration",&make<RestorerProcessor>},
            {"vocal-presence","Vocal Presence & Resonance Architect","Vocal",&make<VocalProcessor>},
            {"triode-tube","Triode Tube Saturator","Saturation",&make<TubeProcessor>},
            {"tape","Tape Saturator","Saturation",&make<TapeProcessor>},
            {"monitor-eq","Optional Monitor EQ","Monitoring",&make<MonitorProcessor>,true},
            {"dc-subsonic","DC / Subsonic Cleaner","Restoration",&make<DcSubsonicProcessor>},
            {"smart-gate","Smart Gate / Expander","Dynamics",&make<SmartGateProcessor>},
            {"vocal-rider","Vocal Rider","Vocal",&make<VocalRiderProcessor>},
            {"plosive-control","Plosive Control","Vocal",&make<PlosiveProcessor>},
            {"hum-buzz","Hum & Buzz Remover","Restoration",&make<HumBuzzProcessor>},
            {"loudness-lab","Loudness / Metering Lab","Metering",&make<LoudnessLabProcessor>},
            {"spectrum-spectrogram","Spectrum / Spectrogram","Analysis",&make<SpectrumSpectrogramProcessor>},
            {"delta-monitor","Delta Monitor","Monitoring",&make<DeltaMonitorProcessor>},
            {"convolution-reverb","Convolution Reverb","Reverb",&make<ConvolutionReverbProcessor>},
            {"algorithmic-reverb","Algorithmic Reverb","Reverb",&make<AlgorithmicReverbProcessor>},
            {"chorus-flanger-phaser","Chorus / Flanger / Phaser","Modulation",&make<ChorusFlangerPhaserProcessor>},
            {"rotary-tremolo-autopan","Rotary / Tremolo / Autopan","Modulation",&make<RotaryTremoloAutopanProcessor>},
            {"multitap-delay","Multitap Delay","Delay",&make<MultitapDelayProcessor>},
            {"tape-delay","Tape Delay","Delay",&make<TapeDelayProcessor>},
            {"dynamic-eq-8","Dynamic EQ 8","EQ",&make<DynamicEq8Processor>},
            {"mid-side-eq","Mid/Side EQ","EQ",&make<MidSideEqProcessor>},
            {"multiband-dynamics","Multiband Dynamics","Dynamics",&make<MultibandDynamicsProcessor>},
            {"multiband-imager","Multiband Imager","Spatial",&make<MultibandImagerProcessor>},
            {"parallel-distortion","Parallel Distortion","Saturation",&make<ParallelProcessor>},
            {"exciter","Exciter","Saturation",&make<ExciterProcessor>},
            {"dynamic-resonance-suppressor","Dynamic Resonance Suppressor","Restoration",&make<DynamicResonanceSuppressorProcessor>},
            {"breath-mouth-control","Breath / Mouth Control","Vocal",&make<BreathMouthStudioProcessor>},
            {"clip-repair","Clip Repair","Restoration",&make<ClipRepairProcessor>},
            {"drum-replacer","Drum Replacer","Drums",&make<DrumReplacerProcessor>},
            {"amp-cabinet","Amp & Cabinet Lab","Guitar",&make<AmpCabinetProcessor>},
            {"reference-match","Reference Match","Master",&make<ReferenceMatchProcessor>},
            {"vocal-de-reverb","Vocal De-Reverb","Vocal",&make<VocalDeReverbProcessor>},
            {"pitch-formant","Pitch / Formant","Clip Operations",&makePitch,false,false,true,false},
            {"time-pitch-stretch","Time / Pitch Stretch","Clip Operations",&makeStretch,false,false,true,false}
        }};
        return list;
    }
    static std::unique_ptr<ModuleProcessor> create(const juce::String& id) {
        for(const auto& entry:entries()) if(id==entry.id) return entry.create();
        return {};
    }
    static juce::String idFor(const ModuleProcessor& processor) {
        if(processor.isExternal())return "external-vst3";
        static const std::array<const char*,41> types{{"SIPA","SURGICAL_EQ","VCA","TRUE_PEAK","DHTA","TRANSIENT","SUB_BASS","RESTORER","VPR_ARCHITECT","TRIODE_TUBE","TAPE","MONITOR_EQ","DC_SUBSONIC","SMART_GATE","VOCAL_RIDER","PLOSIVE_CONTROL","HUM_BUZZ","LOUDNESS_LAB","SPECTRUM_SPECTROGRAM","DELTA_MONITOR","CONVOLUTION_REVERB","ALGORITHMIC_REVERB","CHORUS_FLANGER_PHASER","ROTARY_TREMOLO_AUTOPAN","MULTITAP_DELAY","TAPE_DELAY","DYNAMIC_EQ_8","MID_SIDE_EQ","MULTIBAND_DYNAMICS","MULTIBAND_IMAGER","PARALLEL_DISTORTION","EXCITER","DYNAMIC_RESONANCE_SUPPRESSOR","BREATH_MOUTH_CONTROL","CLIP_REPAIR","DRUM_REPLACER","AMP_CABINET","REFERENCE_MATCH","VOCAL_DE_REVERB","PITCH_FORMANT","TIME_PITCH_STRETCH"}};
        for(size_t i=0;i<types.size();++i) if(processor.state.state.hasType(types[i])) return entries()[i].id;
        return {};
    }
private:
    static std::unique_ptr<ModuleProcessor> makePitch(){return std::make_unique<PitchTimeProcessor>(false);}
    static std::unique_ptr<ModuleProcessor> makeStretch(){return std::make_unique<PitchTimeProcessor>(true);}
    template<class T> static std::unique_ptr<ModuleProcessor> make() {return std::make_unique<T>();}
};
}
