#pragma once
#include "../SessionState.h"
#include "../Plugins/DelayProcessors.h"
#include "../Plugins/ReferenceMatchProcessor.h"
#include <functional>

namespace hem {

struct OfflineMixOptions {
    double sampleRate = 48000;
    int blockSize = 256;
    double tailSeconds = 2;
    bool allowTailTruncation = false;
};

// Created on the message thread; adapters may retire on workers because the
// ExternalPluginHost retains the actual VST3 owners until main-thread collection.
struct OfflinePreparedPlugins {
    std::array<std::array<std::unique_ptr<ModuleProcessor>,8>,13> slots;
};

struct OfflineMixResult {
    juce::Result status = juce::Result::ok();
    // Set only after the complete file has been atomically published.
    juce::int64 frames = 0;
    int latencyRemoved = 0;
};

// Synchronous worker-thread operation. No audio device or read-ahead thread is
// used. The caller must pass an independently captured, immutable snapshot.
// Callbacks run on the calling worker, never on the message/audio thread.
//
// The export starts at zero, ignores transport position/looping and all monitor
// state, and ends at ceil(longest source duration * rate) + ceil(tail * rate).
// Input trim -> 13 stereo 8-slot chains -> track gain/mute/solo -> sum -> output
// gain -> unchanged CoreMasterStage. Fixed insert + actual master latency is
// removed; processing continues through that latency and the requested tail.
//
// A same-directory temporary WAV is flushed and checked before an atomic,
// no-replace hard-link publication. Existing destinations are never replaced.
// Cancellation/failure removes the temporary and leaves no final output.
// A filesystem without hard-link support is rejected safely.
class OfflineMixRenderer final {
public:
    // Optional worker-thread observation seam for deterministic live/offline
    // parity checks; no encoding, GUI or device-thread callback is involved.
    using Observer = std::function<void(const juce::AudioBuffer<float>&,juce::int64,int,bool)>;
    // Conservative sum of the new filter decay bounds along each audible chain.
    // Existing processors' legacy generic .02 s metadata is not a decay contract.
    static double requiredFoundationTail(const SessionSnapshot& snapshot) {
        double longest=0;
        const bool solo=std::any_of(snapshot.tracks.begin(),snapshot.tracks.end(),[](const auto& t){return t.solo;});
        for(const auto& track:snapshot.tracks){
            if(track.muted||(solo&&!track.solo))continue;
            double seconds=0;
            for(const auto& slot:track.slots){
                if(slot.bypassed)continue;
                if(slot.pluginId=="dc-subsonic")seconds+=1;
                if(slot.pluginId=="plosive-control")seconds+=.5;
                if(slot.pluginId=="hum-buzz")seconds+=8;
                if(slot.pluginId=="dynamic-eq-8"||slot.pluginId=="mid-side-eq"||slot.pluginId=="multiband-dynamics"||slot.pluginId=="multiband-imager"){
                    try{auto processor=PluginRegistry::create(slot.pluginId);SessionState::restoreParameters(*processor,slot.parameters);seconds+=processor->getTailLengthSeconds();}
                    catch(...){seconds+=slot.pluginId=="dynamic-eq-8"?60.:slot.pluginId=="mid-side-eq"?24.:1.;}
                }
            }
            longest=std::max(longest,seconds);
        }return longest;
    }
    static juce::Result preflightTail(const SessionSnapshot& snapshot,const OfflineMixOptions& options) {
        try {
            if(!std::isfinite(snapshot.tempoBpm)||snapshot.tempoBpm<20||snapshot.tempoBpm>400)return juce::Result::fail("Project BPM must be 20..400");
            double required=0;const bool solo=std::any_of(snapshot.tracks.begin(),snapshot.tracks.end(),[](const auto& t){return t.solo;});
            for(const auto& track:snapshot.tracks){if(track.muted||(solo&&!track.solo))continue;double seconds=0;bool signalCanReach=track.source!=juce::File();
                for(const auto& slot:track.slots){if(slot.bypassed)continue;
                    if(slot.pluginId=="dc-subsonic")seconds+=1;
                    else if(slot.pluginId=="plosive-control")seconds+=.5;
                    else if(slot.pluginId=="hum-buzz")seconds+=8;
                    else if(slot.pluginId=="drum-replacer"&&slot.native){
                        double longest=0;for(const auto& sample:slot.native->resources)
                            if(sample.sourceRate>0)longest=std::max(longest,double(sample.frames)/sample.sourceRate);
                        seconds+=longest+.003;
                    }
                    else if(slot.pluginId=="amp-cabinet")seconds+=1.;
                    else if(slot.pluginId=="reference-match"){
                        // Preflight precedes construction of private render nodes;
                        // fit only captured data at the requested export rate.
                        ReferenceMatchProcessor validator;
                        auto checked=NativeProcessorState::validate(validator,slot);
                        if(checked.failed())return checked;
                        ReferenceMatchProcessor::Snapshot captured;
                        checked=ReferenceMatchProcessor::decodeSnapshot(*slot.matchProfile,captured);
                        if(checked.failed())return checked;
                        if(captured.committed&&double(captured.parameters.getChildWithProperty("id","bypass")["value"])<.5){
                            const auto amount=double(captured.parameters.getChildWithProperty("id","matchAmount")["value"]);
                            const auto smoothing=double(captured.parameters.getChildWithProperty("id","smoothing")["value"]);
                            const auto fitted=reference_match::fit(captured.source,captured.reference,options.sampleRate,amount,smoothing);
                            if(!fitted.valid||!std::isfinite(fitted.tailSeconds))return juce::Result::fail("Invalid captured Reference Match tail");
                            seconds+=fitted.tailSeconds;
                        }
                    }
                    else if(slot.pluginId=="dynamic-eq-8"||slot.pluginId=="mid-side-eq"||slot.pluginId=="multiband-dynamics"||slot.pluginId=="multiband-imager"){
                        auto processor=PluginRegistry::create(slot.pluginId);SessionState::restoreParameters(*processor,slot.parameters);seconds+=processor->getTailLengthSeconds();
                    }
                    else if(slot.pluginId=="multitap-delay"||slot.pluginId=="tape-delay"){
                        auto p=PluginRegistry::create(slot.pluginId);SessionState::restoreParameters(*p,slot.parameters);
                        const auto tail=static_cast<DelayProcessor&>(*p).capturedTail(options.sampleRate,snapshot.tempoBpm,signalCanReach);
                        if(!tail.valid)return juce::Result::fail(tail.syncConflict?"Audible delay has an unavailable sync division at captured project BPM. Choose a shorter division or manual delay.":"Invalid captured delay tail");
                        seconds+=tail.seconds;
                    }
                    // Other plug-ins may generate audio from silence. A chain of
                    // only fresh delays before any source/generator cannot do so.
                    if(slot.pluginId.isNotEmpty()&&slot.pluginId!="multitap-delay"&&slot.pluginId!="tape-delay")signalCanReach=true;
                }required=std::max(required,seconds);
            }
            if(options.tailSeconds<required&&!options.allowTailTruncation)
                return juce::Result::fail("Selected tail is shorter than the captured effect bound ("+juce::String(required,3)+" s"+(required>3600?"; exceeds 3600 s export limit":"")+"). Increase tail or explicitly enable ALLOW TAIL TRUNCATION.");
            return juce::Result::ok();
        }catch(const std::exception& e){return juce::Result::fail(e.what());}
    }
    static OfflineMixResult render(
        const SessionSnapshot&, const juce::File&, const OfflineMixOptions&,
        const std::function<bool()>& cancelled = {},
        const std::function<void(double)>& progress = {},
        OfflinePreparedPlugins* preparedExternal = nullptr,
        const Observer& observer = {});
};

} // namespace hem
