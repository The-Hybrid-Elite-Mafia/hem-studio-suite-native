#pragma once
#include "SpecialtyCore.h"

namespace hem {
// 4x oversampling with 65-tap Blackman-windowed low-pass filters on both sides
// of the nonlinearity. Fixed round-trip group delay: 16 base-rate samples.
// No allocation, trigonometry or coefficient design occurs in tick().
class OversampledSaturator {
public:
    static constexpr int taps=65,latencySamples=16;
    void prepare() noexcept {
        constexpr double cutoff=.1125;double sum=0;
        for(int i=0;i<taps;++i){const double x=i-32.;
            const double sinc=std::abs(x)<1.e-12?2*cutoff:std::sin(2*pi*cutoff*x)/(pi*x);
            const double window=.42-.5*std::cos(2*pi*i/64)+.08*std::cos(4*pi*i/64);
            coefficients[size_t(i)]=sinc*window;sum+=coefficients[size_t(i)];}
        for(auto& v:coefficients)v/=sum;
        up={};down={};dry={};position={};dryPosition={};
    }
    float tick(float x,int channel,float drive,float bias,float amount) noexcept {
        const auto ch=size_t(channel);auto& dp=dryPosition[ch];
        const float delayed=dry[ch][size_t(dp)];dry[ch][size_t(dp)]=x;dp=(dp+1)%latencySamples;
        auto& pos=position[ch];float result=0;
        const double zero=std::tanh(double(bias));
        const double slope=std::max(.1,1-zero*zero);
        for(int phase=0;phase<4;++phase){
            up[ch][size_t(pos)]=phase==0?x*4:0;
            double interpolated=0;
            for(int k=0;k<taps;++k)interpolated+=coefficients[size_t(k)]*up[ch][size_t((pos-k+taps)%taps)];
            const double shaped=(std::tanh(interpolated*drive+bias)-zero)/(drive*slope);
            down[ch][size_t(pos)]=shaped;
            double filtered=0;
            for(int k=0;k<taps;++k)filtered+=coefficients[size_t(k)]*down[ch][size_t((pos-k+taps)%taps)];
            if(phase==0)result=float(filtered);
            pos=(pos+1)%taps;
        }
        return delayed+amount*(result-delayed);
    }
private:
    std::array<double,taps> coefficients{};
    std::array<std::array<double,taps>,2> up{},down{};
    std::array<std::array<float,latencySamples>,2> dry{};
    std::array<int,2> position{},dryPosition{};
};
struct TubeSettings {float drive=0,bias=0,tone=0,mix=100,output=0;};
class TubeCore {
public:
    void prepare(double rate) noexcept {
        sr=rate;shaper.prepare();shelf.reset();dc.reset();phase=0;
        dc.configure(Biquad::HighPass,sr,8,.7071f);
        controls.reset(sr,{0,0,0,1,1});peak.store(0);
    }
    int latency() const noexcept {return OversampledSaturator::latencySamples;}
    void process(float* l,float* r,int n,const TubeSettings& p) noexcept {
        controls.target({p.drive,p.bias*.01f,p.tone,p.mix*.01f,gain(p.output)});
        float maximum=0;
        for(int i=0;i<n;++i){const auto v=controls.next();
            if(phase++%16==0)shelf.configure(Biquad::HighShelf,sr,6000,.7071f,v[2]);
            const float amount=std::clamp(v[0]/6.f,0.f,1.f),d=gain(v[0]);
            for(int ch=0;ch<2;++ch){float& sample=ch==0?l[i]:r[i];
                const float shaped=shaper.tick(sample,ch,d,v[1],amount*v[3]);
                const float filtered=dc.tick(shaped,ch);
                const float dcRemoved=shaped+amount*v[3]*(filtered-shaped);
                const float toned=shelf.tick(dcRemoved,ch);
                sample=(dcRemoved+v[3]*(toned-dcRemoved))*v[4];
                maximum=std::max(maximum,std::abs(sample));
            }
        }
        peak.store(maximum,std::memory_order_relaxed);
    }
    std::atomic<float> peak{0};
private:double sr=96000;OversampledSaturator shaper;Biquad shelf,dc;unsigned phase=0;Controls<5> controls;
};
struct TapeSettings {float drive=0,warmth=0,rolloff=0,mix=100,output=0;};
class TapeCore {
public:
    void prepare(double rate) noexcept {
        sr=rate;shaper.prepare();headBump.reset();shelf.reset();phase=0;
        controls.reset(sr,{0,0,0,1,1});peak.store(0);
    }
    int latency() const noexcept {return OversampledSaturator::latencySamples;}
    void process(float* l,float* r,int n,const TapeSettings& p) noexcept {
        controls.target({p.drive,p.warmth,p.rolloff,p.mix*.01f,gain(p.output)});float maximum=0;
        for(int i=0;i<n;++i){const auto v=controls.next();
            if(phase++%16==0){headBump.configure(Biquad::Bell,sr,80,.7f,v[1]);shelf.configure(Biquad::HighShelf,sr,10000,.7071f,v[2]);}
            const float d=gain(v[0]),amount=std::clamp(v[0]/6.f,0.f,1.f);
            for(int ch=0;ch<2;++ch){float& sample=ch==0?l[i]:r[i];
                // Symmetric soft saturation with independent low-frequency bump
                // and high-frequency loss. This is tape-inspired DSP, not a
                // measured magnetic hysteresis model of a specific machine.
                const float shaped=shaper.tick(sample,ch,d,0,amount*v[3]);
                const float toned=shelf.tick(headBump.tick(shaped,ch),ch);
                sample=(shaped+v[3]*(toned-shaped))*v[4];maximum=std::max(maximum,std::abs(sample));
            }
        }
        peak.store(maximum,std::memory_order_relaxed);
    }
    std::atomic<float> peak{0};
private:double sr=96000;OversampledSaturator shaper;Biquad headBump,shelf;unsigned phase=0;Controls<5> controls;
};
struct VocalSettings {
    float cutoff=80,body=0,mud=0,presence=0,brightness=0,air=0;
    float resonance=2800,q=2,threshold=0,range=0,attack=5,release=120,output=0;
    bool cutEnabled=false;
};
class VocalCore {
public:
    void prepare(double rate) noexcept {
        sr=rate;phase=0;hp.reset();for(auto& f:tone)f.reset();dynamic.prepare(sr);
        controls.reset(sr,{80,0,0,0,0,0,0,1});peak.store(0);
    }
    void process(float* l,float* r,int n,const VocalSettings& p) noexcept {
        controls.target({p.cutoff,p.body,p.mud,p.presence,p.brightness,p.air,p.cutEnabled?1.f:0.f,gain(p.output)});
        for(int i=0;i<n;++i){const auto v=controls.next();
            if(phase++%16==0){hp.configure(Biquad::HighPass,sr,v[0],.7071f);
                constexpr std::array<float,5> frequencies{150,350,2500,5000,12000};
                constexpr std::array<float,5> qs{.7f,1.2f,.8f,.8f,.7071f};
                for(size_t b=0;b<5;++b)tone[b].configure(b==4?Biquad::HighShelf:Biquad::Bell,sr,frequencies[b],qs[b],v[b+1]);}
            for(int ch=0;ch<2;++ch){float& x=ch==0?l[i]:r[i];const float filtered=hp.tick(x,ch);x+=v[6]*(filtered-x);
                for(auto& f:tone)x=f.tick(x,ch);x*=v[7];}
        }
        DhtaSettings d;d.frequency=p.resonance;d.q=p.q;d.threshold=p.threshold;d.range=p.range;d.attack=p.attack;d.release=p.release;
        dynamic.process(l,r,n,d);float maximum=0;for(int i=0;i<n;++i)maximum=std::max({maximum,std::abs(l[i]),std::abs(r[i])});peak.store(maximum,std::memory_order_relaxed);
    }
    std::atomic<float> peak{0};
    float reduction() const noexcept {return dynamic.reduction.load(std::memory_order_relaxed);}
private:double sr=96000;unsigned phase=0;Biquad hp;std::array<Biquad,5> tone;DhtaCore dynamic;Controls<8> controls;
};
struct MonitorSettings {float bass=0,body=0,presence=0,air=0,preamp=0;bool enabled=false;};
class MonitorCore {
public:
    void prepare(double rate) noexcept {sr=rate;phase=0;for(auto& f:filters)f.reset();controls.reset(sr,{0,0,0,0,1,0});peak.store(0);}
    void process(float* l,float* r,int n,const MonitorSettings& p) noexcept {
        controls.target({p.bass,p.body,p.presence,p.air,gain(p.preamp),p.enabled?1.f:0.f});float maximum=0;
        for(int i=0;i<n;++i){const auto v=controls.next();
            if(phase++%16==0){filters[0].configure(Biquad::Bell,sr,80,.7f,v[0]);filters[1].configure(Biquad::Bell,sr,250,1.f,v[1]);filters[2].configure(Biquad::Bell,sr,3000,1.f,v[2]);filters[3].configure(Biquad::HighShelf,sr,10000,.7071f,v[3]);}
            for(int ch=0;ch<2;++ch){float& sample=ch==0?l[i]:r[i];float wet=sample;for(auto& f:filters)wet=f.tick(wet,ch);
                sample+=v[5]*(wet*v[4]-sample);maximum=std::max(maximum,std::abs(sample));}
        }
        peak.store(maximum,std::memory_order_relaxed);
    }
    std::atomic<float> peak{0};
private:double sr=96000;unsigned phase=0;std::array<Biquad,4> filters;Controls<6> controls;
};
}
