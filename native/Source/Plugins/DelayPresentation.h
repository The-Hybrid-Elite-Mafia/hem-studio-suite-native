#pragma once
#include "../DSP/DelayCore.h"
#include <cstdint>
namespace hem::delayUi {
// PhysicalValueSlider maps bipolar zero to 0.5, irrespective of asymmetry.
// The displayed sweep runs from left through noon to right; default values
// never offset that physical mapping (time/frequency retain their real range).
inline double pointerRadians(double physicalProportion) noexcept {
    return hem::pi*(1.+std::clamp(physicalProportion,0.,1.));
}
struct SyncStatus {bool unavailable=false,blocksAudibleExport=false;};
inline SyncStatus syncStatus(const delay::Settings& p,delay::Kind kind,double bpm) noexcept {
    SyncStatus result;if(!p.sync)return result;
    const auto active=delay::activity(p,kind,!p.bypass&&p.mix>0);
    for(std::size_t i=0;i<(kind==delay::Kind::tape?1u:8u);++i){
        auto time=p.taps[i].time;time.sync=true;
        if(!delay::resolveTime(time,{bpm}).resolved()){
            result.unavailable=true;result.blocksAudibleExport|=active[i];
        }
    }return result;
}
class PublicationFreshness {
public:
    explicit PublicationFreshness(std::uint64_t initial=0):lastToken(initial){}
    bool observe(std::uint64_t token,bool eligible,double messageTimeMs) noexcept {
        if(token!=lastToken){lastToken=token;lastSeen=eligible?messageTimeMs:-1.;}
        if(!eligible)lastSeen=-1.;
        return eligible&&lastSeen>=0&&messageTimeMs>=lastSeen&&messageTimeMs-lastSeen<=250.;
    }
private:std::uint64_t lastToken=0;double lastSeen=-1;
};
}
