#pragma once
#include "DspCore.h"

namespace hem {
// All filter histories and parameter ramps belong to the audio thread. Meters
// are the only data shared back to editors, through atomic stores/loads.
inline float envelopeCoefficient(float ms, double sr) noexcept {
    return float(std::exp(-1.0 / (sr * std::max(.05f, ms) * .001)));
}
inline void follow(float& envelope, float x, float attack, float release) noexcept {
    const float c = x > envelope ? attack : release;
    envelope = x + c * (envelope - x);
}
template<size_t N> struct Controls {
    std::array<Ramp,N> ramps;
    void reset(double sr, const std::array<float,N>& values) noexcept {
        for(size_t i=0;i<N;++i) ramps[i].reset(sr,values[i]);
    }
    void target(const std::array<float,N>& values) noexcept {
        for(size_t i=0;i<N;++i) ramps[i].set(values[i]);
    }
    std::array<float,N> next() noexcept {
        std::array<float,N> v{};
        for(size_t i=0;i<N;++i) v[i]=ramps[i].next();
        return v;
    }
};
struct DhtaSettings {
    float frequency=6800,q=2,threshold=0,range=0,attack=2,release=80,mix=100,output=0;
    bool wide=false,delta=false;
};
class DhtaCore {
public:
    void prepare(double rate) noexcept {
        sr=rate;band.reset();env=0;phase=0;
        controls.reset(sr,{6800,2,0,0,2,80,1,1,0,0});reduction.store(0);
    }
    void process(float* l,float* r,int n,const DhtaSettings& p) noexcept {
        controls.target({p.frequency,p.q,p.threshold,p.range,p.attack,p.release,p.mix*.01f,gain(p.output),p.wide?1.f:0.f,p.delta?1.f:0.f});
        float maximum=0;
        for(int i=0;i<n;++i){const auto v=controls.next();
            if(phase++%16==0){
                // Constant-peak RBJ band-pass. Subtracting its attenuated
                // contribution creates a dynamically controlled notch.
                const double w=2*pi*std::clamp(double(v[0]),20.,sr*.45)/sr;
                const double alpha=std::sin(w)/(2*std::max(.5f,v[1]));
                band.b0=alpha/(1+alpha);band.b1=0;band.b2=-band.b0;
                band.a1=-2*std::cos(w)/(1+alpha);band.a2=(1-alpha)/(1+alpha);
            }
            const float bl=band.tick(l[i],0),br=band.tick(r[i],1);
            const float excess=std::max(0.f,decibels(std::max(std::abs(bl),std::abs(br)))-v[2]);
            follow(env,std::min(std::abs(v[3]),excess),envelopeCoefficient(v[4],sr),envelopeCoefficient(v[5],sr));
            const float attenuation=1-gain(-env);
            const float removedL=attenuation*(bl+v[8]*(l[i]-bl));
            const float removedR=attenuation*(br+v[8]*(r[i]-br));
            const float processedL=l[i]-v[6]*removedL,processedR=r[i]-v[6]*removedR;
            l[i]=(processedL+v[9]*(v[6]*removedL-processedL))*v[7];
            r[i]=(processedR+v[9]*(v[6]*removedR-processedR))*v[7];
            maximum=std::max(maximum,env*v[6]);
        }
        reduction.store(maximum,std::memory_order_relaxed);
    }
    std::atomic<float> reduction{0};
private:
    double sr=96000;Biquad band;float env=0;unsigned phase=0;Controls<10> controls;
};
struct TransientSettings {float attack=0,sustain=0,speed=2,recovery=100,mix=100,output=0;};
class TransientCore {
public:
    void prepare(double rate) noexcept {sr=rate;fast=slow=0;controls.reset(sr,{0,0,2,100,1,1});gainChange.store(0);gainTelemetry={};}
    void process(float* l,float* r,int n,const TransientSettings& p) noexcept {
        controls.target({p.attack,p.sustain,p.speed,p.recovery,p.mix*.01f,gain(p.output)});
        float maximum=0;gainTelemetry={};
        for(int i=0;i<n;++i){const auto v=controls.next();const float x=std::max(std::abs(l[i]),std::abs(r[i]));
            follow(fast,x,envelopeCoefficient(v[2],sr),envelopeCoefficient(v[3]*.25f,sr));
            follow(slow,x,envelopeCoefficient(v[2]*8,sr),envelopeCoefficient(v[3],sr));
            // Positive envelope contrast identifies onset; negative contrast
            // identifies decay. Amounts are bounded in dB, not threshold gain.
            const float contrast=(fast-slow)/std::max(1.e-7f,std::max(fast,slow));
            const float db=v[0]*std::max(0.f,contrast)+v[1]*std::max(0.f,-contrast);
            const float g=(1+v[4]*(gain(db)-1))*v[5];l[i]*=g;r[i]*=g;
            // Observe the exact onset/decay branch and mixed multiplier; never I/O ratios.
            const float applied=decibels(1+v[4]*(gain(db)-1));
            const int branch=contrast>=0?0:1;
            if(std::abs(db)>std::abs(gainTelemetry[branch]))gainTelemetry[branch]=db;
            if(std::abs(applied)>std::abs(gainTelemetry[branch+2]))gainTelemetry[branch+2]=applied;
            if(std::abs(db)>std::abs(maximum))maximum=db;
        }
        gainChange.store(maximum,std::memory_order_relaxed);
    }
    std::atomic<float> gainChange{0};
    std::array<float,4> gainTelemetry{}; // Audio-owner: onset/decay detector dB, mixed applied dB; pre-output.
private:double sr=96000;float fast=0,slow=0;Controls<6> controls;
};
struct SubBassSettings {float crossover=100,lowGain=0,subCut=25,drive=0,mono=0,mix=100,output=0;bool cutEnabled=false;};
class SubBassCore {
public:
    void prepare(double rate) noexcept {sr=rate;low.reset();hp.reset();phase=0;controls.reset(sr,{100,1,25,0,0,1,1,0});peak.store(0);}
    void process(float* l,float* r,int n,const SubBassSettings& p) noexcept {
        controls.target({p.crossover,gain(p.lowGain),p.subCut,p.drive,p.mono*.01f,p.mix*.01f,gain(p.output),p.cutEnabled?1.f:0.f});
        float maximum=0;
        for(int i=0;i<n;++i){const auto v=controls.next();
            if(phase++%16==0){low.configure(Biquad::LowPass,sr,v[0],.7071f);hp.configure(Biquad::HighPass,sr,v[2],.7071f);}
            const float a=low.tick(l[i],0),b=low.tick(r[i],1),mid=(a+b)*.5f;
            const float d=gain(v[3]);const float nonlinear=v[3]/12.f;
            // Subtractive split guarantees exact dry reconstruction at unity.
            // Saturation is restricted to the extracted low band. No fabricated
            // subharmonic oscillator is mixed into the source.
            float bassL=a+v[4]*(mid-a),bassR=b+v[4]*(mid-b);
            bassL+=nonlinear*(std::tanh(bassL*d)/d-bassL);
            bassR+=nonlinear*(std::tanh(bassR*d)/d-bassR);
            float wetL=l[i]+bassL*v[1]-a,wetR=r[i]+bassR*v[1]-b;
            const float cutL=hp.tick(wetL,0),cutR=hp.tick(wetR,1);
            wetL+=v[7]*(cutL-wetL);wetR+=v[7]*(cutR-wetR);
            l[i]=(l[i]+v[5]*(wetL-l[i]))*v[6];r[i]=(r[i]+v[5]*(wetR-r[i]))*v[6];
            maximum=std::max({maximum,std::abs(l[i]),std::abs(r[i])});
        }
        peak.store(maximum,std::memory_order_relaxed);
    }
    std::atomic<float> peak{0};
private:double sr=96000;Biquad low,hp;unsigned phase=0;Controls<8> controls;
};
}
