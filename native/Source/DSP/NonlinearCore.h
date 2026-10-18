#pragma once
#include "FinishingCore.h"

namespace hem {
// Fixed storage, independent per-channel positions. All paths stay warm.
template<int N> struct FixedDelay {
    std::array<std::array<float,N>,2> data{};std::array<int,2> pos{};
    float tick(float x,int ch) noexcept {auto& p=pos[size_t(ch)];auto& d=data[size_t(ch)];const float y=d[size_t(p)];d[size_t(p)]=x;p=(p+1)%N;return y;}
};
struct ParallelSettings {float drive=0,blend=0,output=0;int type=0,quality=1;bool soloWet=false,bypass=false;};
class ParallelCore {
public:
    void prepare(double sr) noexcept {shaper.prepare();dry={};direct={};dc.reset();dc.configure(Biquad::HighPass,sr,8,.7071f);controls.reset(sr,{0,0,1,0,1,0,0});peak.store(0);}
    int latency() const noexcept {return 16;}
    void process(float* l,float* r,int n,const ParallelSettings& p) noexcept {
        controls.target({p.drive,p.blend*.01f,gain(p.output),p.type==1?.3f:0.f,p.quality==1?1.f:0.f,p.soloWet?1.f:0.f,p.bypass?1.f:0.f});float maximum=0;
        for(int i=0;i<n;++i){const auto v=controls.next();const float d=gain(v[0]),amount=std::clamp(v[0]/6.f,0.f,1.f);
            for(int ch=0;ch<2;++ch){float& x=ch==0?l[i]:r[i];const float delayed=dry.tick(x,ch);
                const double zero=std::tanh(double(v[3])),slope=std::max(.1,1-zero*zero);
                const float raw=float((std::tanh(double(x)*d+v[3])-zero)/(d*slope));
                const float one=direct.tick(x+amount*(raw-x),ch),four=shaper.tick(x,ch,d,v[3],amount);
                const float selected=one+v[4]*(four-one),wet=selected+amount*(dc.tick(selected,ch)-selected),mix=v[1]+v[5]*(1-v[1]);
                const float processed=(delayed+mix*(wet-delayed))*v[2];x=processed+v[6]*(delayed-processed);maximum=std::max(maximum,std::abs(x));
            }
        }peak.store(maximum,std::memory_order_relaxed);
    }
    std::atomic<float> peak{0};
private:OversampledSaturator shaper;FixedDelay<16> dry,direct;Biquad dc;Controls<7> controls;
};

struct ExciterSettings {
    std::array<float,3> amount{},mix{};std::array<bool,3> on{},solo{};
    float lowMid=250,midHigh=4000,output=0;bool bypass=false;
};
// Bilinear first-order low pass, prewarped at the labeled digital cutoff.
// H(z)=b(1+z^-1)/(1-(1-2b)z^-1), b=tan(pi*f/fs)/(1+tan(pi*f/fs)).
// Low + (upper-low) + (input-upper) reconstructs the input exactly in algebra.
struct ExciterSplit {
    std::array<double,2> previous{},low{},upper{};
    static double coefficient(double hz,double sr) noexcept {const double g=std::tan(pi*std::min(hz,sr*.4)/sr);return g/(1+g);}
    std::array<float,3> tick(float x,int channel,double a,double b) noexcept {
        const auto ch=size_t(channel);const double sum=x+previous[ch];previous[ch]=x;
        low[ch]=a*sum+(1-2*a)*low[ch];upper[ch]=b*sum+(1-2*b)*upper[ch];
        return {float(low[ch]),float(upper[ch]-low[ch]),float(x-upper[ch])};
    }
};
class ExciterCore {
public:
    void prepare(double rate) noexcept {sr=rate;split={};delay={};bandDelay={};for(auto& s:shapers)s.prepare();for(auto& f:dc){f.reset();f.configure(Biquad::HighPass,sr,8,.7071f);}
        controls.reset(sr,{0,0,0,0,0,0,0,0,0,1,1,1,250,4000,1,0});peak.store(0);for(auto& a:activity)a.store(0);}
    int latency() const noexcept {return 16;}
    static float effectiveHz(float hz,double rate) noexcept {return std::min(hz,float(rate*.4));}
    void process(float* l,float* r,int n,const ExciterSettings& p) noexcept {
        const bool solo=p.solo[0]||p.solo[1]||p.solo[2];
        controls.target({p.amount[0]*.01f,p.amount[1]*.01f,p.amount[2]*.01f,p.mix[0]*.01f,p.mix[1]*.01f,p.mix[2]*.01f,
            p.on[0]?1.f:0.f,p.on[1]?1.f:0.f,p.on[2]?1.f:0.f,(!solo||p.solo[0])?1.f:0.f,(!solo||p.solo[1])?1.f:0.f,(!solo||p.solo[2])?1.f:0.f,
            effectiveHz(p.lowMid,sr),effectiveHz(p.midHigh,sr),gain(p.output),p.bypass?1.f:0.f});
        float maximum=0;std::array<float,3> active{};
        for(int i=0;i<n;++i){const auto v=controls.next();
            const double a=ExciterSplit::coefficient(v[12],sr),b=ExciterSplit::coefficient(v[13],sr);
            for(int ch=0;ch<2;++ch){float& x=ch==0?l[i]:r[i];const float original=delay.tick(x,ch);
                const auto bands=split.tick(x,ch,a,b);
                float selected=original,added=0;
                for(size_t j=0;j<3;++j){const float band=bandDelay[j].tick(bands[j],ch);
                    const float harmonic=dc[j].tick(shapers[j].tick(bands[j],ch,1+7*v[j],.25f,1,true),ch)*v[j]*v[j+3]*v[j+6]*v[j+9];
                    selected+=(v[j+9]-1)*band;added+=harmonic;active[j]=std::max(active[j],std::abs(harmonic)*(1-v[15]));}
                const float processed=(selected+added)*v[14];x=processed+v[15]*(original-processed);maximum=std::max(maximum,std::abs(x));
            }
        }peak.store(maximum,std::memory_order_relaxed);for(size_t j=0;j<3;++j)activity[j].store(active[j],std::memory_order_relaxed);
    }
    std::atomic<float> peak{0};std::array<std::atomic<float>,3> activity{};
private:double sr=48000;ExciterSplit split;FixedDelay<16> delay;std::array<FixedDelay<16>,3> bandDelay;
    std::array<OversampledSaturator,3> shapers;std::array<Biquad,3> dc;Controls<16> controls;
};
}
