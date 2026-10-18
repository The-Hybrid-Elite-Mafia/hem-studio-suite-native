#pragma once
#include "../Source/DSP/SpecialtyCore.h"
namespace hem {
class BaselineTransientCore {
public:
    void prepare(double rate) noexcept {sr=rate;fast=slow=0;controls.reset(sr,{0,0,2,100,1,1});gainChange.store(0);}
    void process(float* l,float* r,int n,const TransientSettings& p) noexcept {
        controls.target({p.attack,p.sustain,p.speed,p.recovery,p.mix*.01f,gain(p.output)});
        float maximum=0;
        for(int i=0;i<n;++i){const auto v=controls.next();const float x=std::max(std::abs(l[i]),std::abs(r[i]));
            follow(fast,x,envelopeCoefficient(v[2],sr),envelopeCoefficient(v[3]*.25f,sr));
            follow(slow,x,envelopeCoefficient(v[2]*8,sr),envelopeCoefficient(v[3],sr));
            // Positive envelope contrast identifies onset; negative contrast
            // identifies decay. Amounts are bounded in dB, not threshold gain.
            const float contrast=(fast-slow)/std::max(1.e-7f,std::max(fast,slow));
            const float db=v[0]*std::max(0.f,contrast)+v[1]*std::max(0.f,-contrast);
            const float g=(1+v[4]*(gain(db)-1))*v[5];l[i]*=g;r[i]*=g;
            if(std::abs(db)>std::abs(maximum))maximum=db;
        }
        gainChange.store(maximum,std::memory_order_relaxed);
    }
    std::atomic<float> gainChange{0};
private:double sr=96000;float fast=0,slow=0;Controls<6> controls;
};
}
