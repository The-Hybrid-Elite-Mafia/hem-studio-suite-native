#pragma once
#include "DspCore.h"
#include "RestorationTelemetry.h"
#include <limits>
namespace hem {
namespace restoration {
inline float safe(float x,float fallback=0)noexcept{return std::isfinite(x)?x:fallback;}
inline float clamp(float x,float lo,float hi,float fallback=0)noexcept{return std::clamp(safe(x,fallback),lo,hi);}
inline bool rateValid(double sr)noexcept{return std::isfinite(sr)&&sr>=8000&&sr<=384000;}
inline float outputSample(double x)noexcept{return std::isfinite(x)&&std::abs(x)<=std::numeric_limits<float>::max()?float(x):0.f;}
inline float powerDb(double x)noexcept{return float(10*std::log10(std::max(1.e-24,x)));}
struct Bandpass {
    double b=0,a1=0,a2=0;std::array<double,2>z1{},z2{};
    void prepare(double sr,double hz,double q)noexcept{const double w=2*pi*std::clamp(hz,1.,sr*.45)/sr,a=std::sin(w)/(2*q),d=1+a;b=a/d;a1=-2*std::cos(w)/d;a2=(1-a)/d;}
    double tick(double x,size_t c)noexcept{const double y=b*x+z1[c];z1[c]=-a1*y+z2[c];z2[c]=-b*x-a2*y;return y;}
    void reset()noexcept{z1={};z2={};}
};
}
struct DynamicResonanceSettings {
    float sensitivity=50,low=120,high=14000,selectivity=50,maxReduction=0,attack=20,release=150,mix=100,output=0;
    int profile=0;bool soloDelta=false,bypass=false;
};
// Original causal adaptive-filter implementation. The 32 constant-Q detectors
// measure local spectral prominence. A shared sum-of-cuts budget bounds the
// frozen-coefficient response attenuation; it is not an instantaneous limiter.
class DynamicResonanceCore {
public:
    bool prepare(double sampleRate,const DynamicResonanceSettings&p={})noexcept{
        ready=false;sr=sampleRate;if(!restoration::rateValid(sr)){sr=0;telemetry.prepare(0,4);peak.store(0);return false;}
        settings=sanitize(p);for(size_t i=0;i<bands;++i){hz[i]=float(80*std::pow(std::min(18000.,sr*.42)/80,double(i)/(bands-1)));pre[i].prepare(sr,hz[i],5);post[i].prepare(sr,hz[i],5);delta[i].prepare(sr,hz[i],5);}
        telemetry.prepare(sr,4);reset();ready=true;return true;
    }
    void reset()noexcept{
        for(auto&f:pre)f.reset();for(auto&f:post)f.reset();for(auto&f:delta)f.reset();for(auto&f:cuts)f.reset();energy={};outputEnergy={};deltaEnergy={};cut={};phase=protect=0;fast=slow=0;frames=0;
        peak.store(0);telemetry.reset();if(!restoration::rateValid(sr))return;
        mix.reset(sr,settings.mix*.01f);output.reset(sr,gain(settings.output));active.reset(sr,settings.bypass?0:1);solo.reset(sr,settings.soloDelta?1:0);selectivity.reset(sr,settings.selectivity);
        detectorDecay=std::exp(-1/(sr*.012));fastDecay=std::exp(-1/(sr*.001));slowDecay=std::exp(-1/(sr*.030));updateFilters();
    }
    int latency()const noexcept{return 0;}
    restoration::Display display()const noexcept{return telemetry.snapshot();}
    void process(float*l,float*r,int n,const DynamicResonanceSettings&target)noexcept{
        if(!l||!r||n<=0)return;if(!ready){std::fill_n(l,n,0);std::fill_n(r,n,0);return;}settings=sanitize(target);mix.set(settings.mix*.01f);output.set(gain(settings.output));active.set(settings.bypass?0:1);solo.set(settings.soloDelta?1:0);selectivity.set(settings.selectivity);float maximum=0;
        for(int j=0;j<n;++j){std::array<double,2>x{restoration::safe(l[j]),restoration::safe(r[j])};const double level=std::max(x[0]*x[0],x[1]*x[1]);fast=fastDecay*fast+(1-fastDecay)*level;slow=slowDecay*slow+(1-slowDecay)*level;if(fast>4*slow&&fast>1.e-10)protect=int(sr*.008);else if(protect>0)--protect;
            for(size_t k=0;k<bands;++k){const double a=pre[k].tick(x[0],0),b=pre[k].tick(x[1],1);energy[k]=detectorDecay*energy[k]+(1-detectorDecay)*std::max(a*a,b*b);}
            selectivity.next();if(phase==0)updateFilters();phase=(phase+1)&31;++frames;
            auto wet=x;for(auto&f:cuts){wet[0]=f.tick(float(wet[0]),0);wet[1]=f.tick(float(wet[1]),1);}
            const float amount=mix.next(),gainOut=output.next(),enabled=active.next(),listen=solo.next();std::array<float,2>y{},removed{};
            for(size_t c=0;c<2;++c){removed[c]=restoration::outputSample(amount*(x[c]-wet[c]));const double normal=(x[c]-removed[c])*gainOut,monitor=removed[c]*gainOut;const double chosen=normal+listen*(monitor-normal);y[c]=restoration::outputSample(x[c]+enabled*(chosen-x[c]));}
            for(size_t k=0;k<bands;++k){const double a=post[k].tick(y[0],0),b=post[k].tick(y[1],1),d0=delta[k].tick(removed[0]*enabled,0),d1=delta[k].tick(removed[1]*enabled,1);outputEnergy[k]=detectorDecay*outputEnergy[k]+(1-detectorDecay)*std::max(a*a,b*b);deltaEnergy[k]=detectorDecay*deltaEnergy[k]+(1-detectorDecay)*std::max(d0*d0,d1*d1);}
            l[j]=y[0];r[j]=y[1];maximum=std::max({maximum,std::abs(y[0]),std::abs(y[1])});float sum=0;for(float v:cut)sum+=v;telemetry.sample(sum/24,0,0,true);
            if(phase==0){std::array<float,bands>a{},b{},d{},c{};for(size_t k=0;k<bands;++k){a[k]=restoration::powerDb(energy[k]);b[k]=restoration::powerDb(outputEnergy[k]);d[k]=restoration::powerDb(deltaEnergy[k]);c[k]=cut[k]*enabled;}telemetry.spectrum(hz,a,b,d,c);}
        }peak.store(maximum,std::memory_order_relaxed);
    }
    static DynamicResonanceSettings sanitize(DynamicResonanceSettings p)noexcept{p.sensitivity=restoration::clamp(p.sensitivity,0,100,50);p.low=restoration::clamp(p.low,80,16000,120);p.high=restoration::clamp(p.high,200,20000,14000);p.selectivity=restoration::clamp(p.selectivity,0,100,50);p.maxReduction=restoration::clamp(p.maxReduction,0,24);p.attack=restoration::clamp(p.attack,.5f,200,20);p.release=restoration::clamp(p.release,10,2000,150);p.mix=restoration::clamp(p.mix,0,100,100);p.output=restoration::clamp(p.output,-24,6);p.profile=std::clamp(p.profile,0,2);return p;}
    std::atomic<float>peak{0};
private:
    static constexpr size_t bands=restoration::Display::bands;
    void updateFilters()noexcept{
        std::array<float,bands>request{};float total=0;const float lo=std::min(settings.low,settings.high),hi=std::max(settings.low,settings.high);const double attack=std::exp(-32/(sr*settings.attack*.001)),release=std::exp(-32/(sr*settings.release*.001));
        for(size_t k=0;k<bands;++k){const size_t low=k>=2?k-2:0,high=std::min(bands-1,k+2);const double neighbourhood=std::max(1.e-24,.5*(energy[low]+energy[high]));const float prominence=restoration::powerDb(energy[k]/neighbourhood);bool focus=hz[k]>=lo&&hz[k]<=hi;
            if(settings.profile==1)focus=focus&&hz[k]>=200&&hz[k]<=8000;if(settings.profile==2)focus=focus&&hz[k]>=3000;
            request[k]=focus&&protect==0&&frames>uint64_t(sr*.02)&&energy[k]>1.e-8?std::max(0.f,prominence-(10-settings.sensitivity*.08f))*.75f:0.f;total+=request[k];}
        const float scale=total>settings.maxReduction&&total>0?settings.maxReduction/total:1;
        float sum=0;for(size_t k=0;k<bands;++k){const float wanted=request[k]*scale;const double coefficient=wanted>cut[k]?attack:release;cut[k]=float(wanted+(cut[k]-wanted)*coefficient);sum+=cut[k];}
        // A reduced maximum takes effect at the next 32-sample control tick.
        const float correction=sum>settings.maxReduction&&sum>0?settings.maxReduction/sum:1;
        for(size_t k=0;k<bands;++k){cut[k]*=correction;cuts[k].configure(Biquad::Bell,sr,hz[k],2+selectivity.value*.10f,-cut[k]);}
    }
    double sr=0,detectorDecay=0,fastDecay=0,slowDecay=0,fast=0,slow=0;bool ready=false;int phase=0,protect=0;uint64_t frames=0;DynamicResonanceSettings settings;
    std::array<restoration::Bandpass,bands>pre{},post{},delta{};std::array<Biquad,bands>cuts{};std::array<double,bands>energy{},outputEnergy{},deltaEnergy{};std::array<float,bands>hz{},cut{};Ramp mix,output,active,solo,selectivity;restoration::Telemetry telemetry;
};

struct BreathMouthSettings {
    float breathDetection=50,breathReduction=0,breathLength=100,mouthDetection=50,mouthReduction=0,mouthLength=.5f,low=1500,high=12000;
    bool listenBreath=false,listenMouth=false,bypass=false;
};
// Conservative manual-tunable heuristics, not a speech/breath classifier.
// Noise likelihood requires distributed focus-band energy and low level.
// A three-point impulse residual with quiet shoulders drives mouth reduction.
class BreathMouthCore {
public:
    bool prepare(double sampleRate,const BreathMouthSettings&p={})noexcept{
        ready=false;sr=sampleRate;if(!restoration::rateValid(sr)){sr=0;delay=0;peak.store(0);telemetry.prepare(0,4);return false;}settings=sanitize(p);delay=std::max(2,int(std::ceil(sr*.0025)));telemetry.prepare(sr,4);reset();ready=true;return true;
    }
    void reset()noexcept{audio={};gains={};detectorBands={};energies={};previous={};beforePrevious={};noisePower=0;lastLow=lastHigh=-1;breathScore=mouthScore=breathEnvelope=mouthEnvelope=0;position=phase=hold=0;frames=events=0;peak.store(0);telemetry.reset();if(!restoration::rateValid(sr))return;active.reset(sr,settings.bypass?0:1);listenB.reset(sr,settings.listenBreath?1:0);listenM.reset(sr,settings.listenMouth?1:0);reduceB.reset(sr,settings.breathReduction);reduceM.reset(sr,settings.mouthReduction);powerDecay=std::exp(-1/(sr*.015));backgroundDecay=std::exp(-1/(sr*.02));updateBands();}
    int latency()const noexcept{return delay;}
    restoration::Display display()const noexcept{return telemetry.snapshot();}
    void process(float*l,float*r,int n,const BreathMouthSettings&target)noexcept{
        if(!l||!r||n<=0)return;if(!ready){std::fill_n(l,n,0);std::fill_n(r,n,0);return;}settings=sanitize(target);active.set(settings.bypass?0:1);listenB.set(settings.listenBreath?1:0);listenM.set(settings.listenMouth?1:0);reduceB.set(settings.breathReduction);reduceM.set(settings.mouthReduction);float maximum=0,maximumBreath=0,maximumMouth=0;
        for(int i=0;i<n;++i){const std::array<float,2>x{restoration::safe(l[i]),restoration::safe(r[i])};double power=0;for(size_t c=0;c<2;++c)power=std::max(power,double(x[c])*x[c]);
            for(size_t k=0;k<8;++k){const double a=detectorBands[k].tick(x[0],0),b=detectorBands[k].tick(x[1],1);energies[k]=powerDecay*energies[k]+(1-powerDecay)*std::max(a*a,b*b);}
            if(phase==0){updateBands();double arithmetic=0,logs=0;for(size_t k=0;k<8;++k){const double v=std::max(1.e-24,energies[k]/frequency[k]);arithmetic+=v;logs+=std::log(v);}arithmetic/=8;const double flatness=std::exp(logs/8)/std::max(1.e-24,arithmetic);const double threshold=.94-settings.breathDetection*.0064;const double level=restoration::powerDb(noisePower);const double levelGate=level>-70&&level<-12?1:0;breathScore=float(std::clamp((flatness-threshold)/std::max(.05,1-threshold),0.,1.)*levelGate);}
            phase=(phase+1)&63;noisePower=backgroundDecay*noisePower+(1-backgroundDecay)*power;
            // Assess the previous input sample with one sample of future context.
            float impulseScore=0;for(size_t c=0;c<2;++c){const double estimate=.5*(beforePrevious[c]+x[c]),residual=std::abs(previous[c]-estimate),shoulders=std::max(std::abs(double(beforePrevious[c])),std::abs(double(x[c]))),centre=std::abs(double(previous[c]));const double ratio=2.5+(100-settings.mouthDetection)*.06,minimum=.003+(100-settings.mouthDetection)*.0003;const double local=std::max(minimum,shoulders*ratio);const double focus=std::clamp((settings.high-settings.low)/6000.,0.,1.);if(frames>uint64_t(sr*.01)&&centre>local&&residual>minimum&&centre>4*std::sqrt(std::max(1.e-12,noisePower)))impulseScore=std::max(impulseScore,float(std::clamp((centre-local)/std::max(.001,centre),0.,1.)*focus));}
            if(impulseScore>.1f){++events;hold=std::max(1,int(sr*settings.mouthLength*.001));mouthScore=impulseScore;}
            else mouthScore*=float(std::exp(-1/(sr*.01)));
            const double attack=std::exp(-1/(sr*.01)),release=std::exp(-1/(sr*settings.breathLength*.001));breathEnvelope=breathScore+(breathEnvelope-breathScore)*(breathScore>breathEnvelope?attack:release);
            if(hold>0){mouthEnvelope=std::max(mouthEnvelope,impulseScore);--hold;}else mouthEnvelope*=float(std::exp(-1/(sr*settings.mouthLength*.001)));
            const float rb=reduceB.next()*breathEnvelope,rm=reduceM.next()*mouthEnvelope;const auto previousPosition=(position+capacity-1)%capacity;
            // Backdate the isolated-sample decision, never move host latency.
            if(impulseScore>.1f)gains[1][previousPosition]=std::max(gains[1][previousPosition],reduceM.value*impulseScore);
            for(size_t c=0;c<2;++c)audio[c][position]=x[c];gains[0][position]=rb;gains[1][position]=rm;
            const auto read=(position+capacity-size_t(delay))%capacity;const float bGain=gain(-gains[0][read]),mGain=gain(-gains[1][read]),enabled=active.next(),lb=listenB.next(),lm=listenM.next();
            float db=0,dm=0;for(size_t c=0;c<2;++c){const float dry=audio[c][read],breath=dry*(1-bGain),mouth=dry*bGain*(1-mGain),normal=dry-breath-mouth;const float totalListen=std::max(lb,lm);const float audition=breath*lb+mouth*lm;const float chosen=normal*(1-totalListen)+audition;const float y=restoration::outputSample(dry+enabled*(chosen-dry));(c? r:l)[i]=y;maximum=std::max(maximum,std::abs(y));db=std::max(db,std::abs(breath));dm=std::max(dm,std::abs(mouth));}
            maximumBreath=std::max(maximumBreath,gains[0][read]*enabled);maximumMouth=std::max(maximumMouth,gains[1][read]*enabled);telemetry.sample(breathScore,mouthScore,std::max(gains[0][read],gains[1][read])*enabled/24,true);previous= x;beforePrevious={audio[0][previousPosition],audio[1][previousPosition]};position=(position+1)%capacity;++frames;
        }
        peak.store(maximum,std::memory_order_relaxed);telemetry.status(breathScore,mouthScore,maximumBreath,maximumMouth,events);
    }
    static BreathMouthSettings sanitize(BreathMouthSettings p)noexcept{p.breathDetection=restoration::clamp(p.breathDetection,0,100,50);p.breathReduction=restoration::clamp(p.breathReduction,0,24);p.breathLength=restoration::clamp(p.breathLength,10,500,100);p.mouthDetection=restoration::clamp(p.mouthDetection,0,100,50);p.mouthReduction=restoration::clamp(p.mouthReduction,0,24);p.mouthLength=restoration::clamp(p.mouthLength,.05f,2,.5f);p.low=restoration::clamp(p.low,100,8000,1500);p.high=restoration::clamp(p.high,2000,20000,12000);return p;}
    std::atomic<float>peak{0};
private:
    void updateBands()noexcept{const float lo=std::clamp(std::min(settings.low,settings.high),50.f,float(sr*.35)),hi=std::clamp(std::max(settings.low,settings.high),lo+1,float(sr*.45));if(lo==lastLow&&hi==lastHigh)return;lastLow=lo;lastHigh=hi;for(size_t k=0;k<8;++k){frequency[k]=float(lo*std::pow(hi/lo,double(k)/7));detectorBands[k].prepare(sr,frequency[k],3);}}
    static constexpr size_t capacity=2048;double sr=0,powerDecay=0,backgroundDecay=0,noisePower=0;bool ready=false;int delay=0,phase=0,hold=0;size_t position=0;uint64_t frames=0,events=0;float breathScore=0,mouthScore=0,breathEnvelope=0,mouthEnvelope=0,lastLow=-1,lastHigh=-1;BreathMouthSettings settings;std::array<std::array<float,capacity>,2>audio{},gains{};std::array<float,2>previous{},beforePrevious{};std::array<restoration::Bandpass,8>detectorBands{};std::array<double,8>energies{};std::array<float,8>frequency{};Ramp active,listenB,listenM,reduceB,reduceM;restoration::Telemetry telemetry;
};
}
