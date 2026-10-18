#pragma once
#include "DspCore.h"
#include <stdexcept>
namespace hem {
// Fixed-size prepared coefficients are built away from the audio thread.
// Device routing itself belongs to AudioDeviceManager; this class selects the
// listening signal for that device, never a Bluetooth codec or OS endpoint.
class MonitoringRouter final {
public:
    static constexpr int maxBands=12;
    struct Curve {std::array<Biquad,maxBands> filters;int count=0;float preamp=1;};
    void prepare(double rate) {
        active={};incoming={};transitioning=false;
        profileMix.reset(rate,0);profileMix.length=std::max(1,int(rate*.02));
        routeMix.reset(rate,0);routeMix.length=std::max(1,int(rate*.02));headphones.store(false);
    }
    // Host excludes the audio callback for this bounded struct copy. Reject
    // replacement during a fade, rather than resetting a live IIR mid-signal.
    bool installPrepared(const Curve& curve) noexcept {
        if(transitioning||curve.count<0||curve.count>maxBands)return false;
        incoming=curve;for(auto& f:incoming.filters)f.reset();transitioning=true;
        profileMix.value=profileMix.target=0;profileMix.remaining=0;profileMix.set(1);return true;
    }
    void setHeadphoneMode(bool enabled) noexcept {headphones.store(enabled,std::memory_order_relaxed);}
    bool headphoneMode() const noexcept {return headphones.load(std::memory_order_relaxed);}
    // Step 1: create the listening branch without modifying the master input.
    void renderCalibration(const float* left,const float* right,float* outL,float* outR,int n) noexcept {
        for(int i=0;i<n;++i){
            const float cleanL=std::isfinite(left[i])?std::clamp(left[i],-1000000.f,1000000.f):0.f;
            const float cleanR=std::isfinite(right[i])?std::clamp(right[i],-1000000.f,1000000.f):0.f;
            float aL=cleanL*active.preamp,aR=cleanR*active.preamp;
            for(int b=0;b<active.count;++b){aL=active.filters[size_t(b)].tick(aL,0);aR=active.filters[size_t(b)].tick(aR,1);}
            if(transitioning){float bL=cleanL*incoming.preamp,bR=cleanR*incoming.preamp;
                for(int b=0;b<incoming.count;++b){bL=incoming.filters[size_t(b)].tick(bL,0);bR=incoming.filters[size_t(b)].tick(bR,1);}
                const float mix=profileMix.next();aL+=mix*(bL-aL);aR+=mix*(bR-aR);
                if(profileMix.remaining==0){active=incoming;transitioning=false;}}
            outL[i]=aL;outR[i]=aR;
        }
    }
    // Optional manual listening EQ may process outL/outR between these stages.
    // Step 2: direct mode really bypasses ALL listening correction. Final
    // MasterLimiter must run after this mix; exports consume the master branch.
    void mixRoute(const float* masterL,const float* masterR,float* outL,float* outR,int n) noexcept {
        routeMix.set(headphoneMode()?1.f:0.f);
        for(int i=0;i<n;++i){const float mix=routeMix.next();
            if(mix==0){outL[i]=masterL[i];outR[i]=masterR[i];}
            else {outL[i]=masterL[i]+mix*(outL[i]-masterL[i]);outR[i]=masterR[i]+mix*(outR[i]-masterR[i]);}}
    }
private:
    Curve active,incoming;
    Ramp profileMix,routeMix;
    bool transitioning=false;
    std::atomic<bool> headphones{false};
};
}
