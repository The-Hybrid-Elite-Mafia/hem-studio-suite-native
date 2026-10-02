#pragma once
#include "SpecialtyCore.h"
namespace hem {
struct RestorerSettings {
    float amount=0,floor=-60,release=100,protect=100,mix=100,output=0;
    bool learn=false;
};
// Eight complementary bands reconstructed by subtracting adjacent low-pass
// outputs. Explicit noise-only learning; no neural inference or AI detection.
class RestorerCore {
public:
    void prepare(double rate) noexcept {
        sr=rate;env={};slow={};noise.fill(1.e-3f);learned=false;
        constexpr std::array<float,7> edges{125,250,500,1000,2000,4000,8000};
        for(size_t b=0;b<7;++b){filters[b].reset();filters[b].configure(Biquad::LowPass,sr,edges[b],.7071f);}
        controls.reset(sr,{0,-60,100,1,1,1,1});reduction.store(0);
    }
    void process(float* l,float* r,int n,const RestorerSettings& p) noexcept {
        controls.target({p.amount,p.floor,p.release,p.protect*.01f,p.mix*.01f,gain(p.output),p.learn?0.f:1.f});
        float maxReduction=0;
        for(int i=0;i<n;++i){auto v=controls.next();std::array<std::array<float,8>,2> bands{};
            for(int ch=0;ch<2;++ch){const float x=ch==0?l[i]:r[i];float previous=0;
                for(size_t b=0;b<7;++b){const float filtered=filters[b].tick(x,ch);bands[size_t(ch)][b]=filtered-previous;previous=filtered;}
                bands[size_t(ch)][7]=x-previous;
            }
            float outL=l[i],outR=r[i];
            for(size_t b=0;b<8;++b){const float x=std::max(std::abs(bands[0][b]),std::abs(bands[1][b]));
                follow(env[b],x,envelopeCoefficient(1,sr),envelopeCoefficient(v[2],sr));
                follow(slow[b],x,envelopeCoefficient(30,sr),envelopeCoefficient(250,sr));
                if(p.learn){noise[b]+=float(1-std::exp(-1/(sr*.2)))*(x-noise[b]);learned=true;}
                const float reference=learned?std::max(1.e-8f,noise[b]*2):gain(v[1]);
                const float lowLevel=std::clamp(1-env[b]/(reference*2),0.f,1.f);
                const float transient=std::clamp((env[b]-slow[b])/std::max(env[b],1.e-8f),0.f,1.f);
                const float cut=v[0]*lowLevel*(1-v[3]*transient)*v[6];
                const float removed=(1-gain(-cut))*v[4];
                outL-=removed*bands[0][b];outR-=removed*bands[1][b];
                maxReduction=std::max(maxReduction,cut*v[4]);
            }
            l[i]=outL*v[5];r[i]=outR*v[5];
        }
        reduction.store(maxReduction,std::memory_order_relaxed);
    }
    std::atomic<float> reduction{0};
private:
    double sr=96000;bool learned=false;
    std::array<Biquad,7> filters;std::array<float,8> env{},slow{},noise{};Controls<7> controls;
};
}
