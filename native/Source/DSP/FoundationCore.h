#pragma once
#include "DspCore.h"
#include <cstdint>
#include <limits>

namespace hem {
enum class FoundationKind { cleaner, gate, rider, plosive, hum };
// Order matches the published APVTS contract. Unused entries are zero.
using FoundationSettings = std::array<float,10>;
inline FoundationSettings foundationDefaults(FoundationKind k) noexcept {
    switch(k) {
    case FoundationKind::cleaner:return {0,0,25,0,100,0};
    case FoundationKind::gate:return {-40,6,2,0,5,50,100,0,100,0};
    case FoundationKind::rider:return {-18,0,0,400,150,-60,100,0};
    case FoundationKind::plosive:return {100,-24,6,0,2,120,100,0};
    case FoundationKind::hum:return {50,30,8,0,100,0};
    } return {};
}
namespace foundation {
// Inclusive audio-rate contract. Validate before Ramp/sample-count conversions.
inline bool supportedRate(double rate) noexcept {return std::isfinite(rate)&&rate>=8000&&rate<=384000;}
inline double dbGain(double db) noexcept { return std::pow(10.,db/20.); }
inline double powerDb(double p) noexcept { return 10*std::log10(std::max(1.e-24,p)); }
inline double approach(double current,double target,double ms,double rate) noexcept {
    return target+(current-target)*std::exp(-1/(rate*ms*.001));
}
inline double plosiveRequest(double fastDb,double slowDb,double threshold,double contrast,double range) noexcept {
    return std::min(range,std::max(0.,fastDb-threshold))*std::clamp((fastDb-slowDb-contrast)/6.,0.,1.);
}
struct Section {
    double b0=1,b1=0,b2=0,a1=0,a2=0;
    std::array<double,2> z1{},z2{};
    void reset(int c) noexcept { z1[size_t(c)]=z2[size_t(c)]=0; }
    // Prepared rates only; frequencies clamp to [1, .45 Fs].
    void configure(int type,double fs,double hz,double q) noexcept {
        hz=std::clamp(hz,1.,fs*.45);
        const double w=2*pi*hz/fs,c=std::cos(w),alpha=std::sin(w)/(2*q),d=1+alpha;
        a1=-2*c/d;a2=(1-alpha)/d;
        if(type==0){b0=(1+c)/(2*d);b1=-(1+c)/d;b2=b0;}
        else if(type==1){b0=(1-c)/(2*d);b1=(1-c)/d;b2=b0;}
        else {b0=1/d;b1=a1;b2=b0;}
    }
    double tick(double x,int c) noexcept {
        const auto i=size_t(c);const double y=b0*x+z1[i];
        z1[i]=b1*x-a1*y+z2[i];z2[i]=b2*x-a2*y;return y;
    }
};
}
// Audio-thread-owned fixed storage. prepare is serialized with process by host.
// Parameter targets change only at block boundaries; coefficient cadence uses
// the absolute sample stream, never the callback size. No allocation or I/O.
template<FoundationKind Kind> class FoundationCore {
public:
    static constexpr int count=Kind==FoundationKind::gate?10:(Kind==FoundationKind::rider||Kind==FoundationKind::plosive?8:6);
    std::atomic<float> measurement{0};
    bool prepare(double sampleRate,const FoundationSettings& settings=foundationDefaults(Kind)) noexcept {
        prepared=false;sr=0;samples=0;measurement.store(0,std::memory_order_relaxed);
        if(!foundation::supportedRate(sampleRate))return false;
        sr=sampleRate;
        filters={};previous={};dcHistory={};energies={};envelope=fast=slow=detector=0;
        openState=false;below=0;samples=0;phase=0;measurement.store(0,std::memory_order_relaxed);
        dcR=std::exp(-2*pi*5/sr);peakRelease=std::exp(-1/(sr*.05));
        rmsRelease=std::exp(-1/(sr*.1));fastRelease=std::exp(-1/(sr*.005));slowRelease=std::exp(-1/(sr*.25));
        warmup=uint64_t(std::ceil(sr*.1));
        holdTargetMs=50;
        if constexpr(Kind==FoundationKind::gate)
            if(std::isfinite(settings[5]))holdTargetMs=std::clamp(double(settings[5]),0.,500.);
        for(int i=0;i<count;++i) ramps[size_t(i)].reset(sr,
            Kind==FoundationKind::gate&&i==5?float(holdTargetMs):settings[size_t(i)]);
        const int harmonics=int(std::lround(std::isfinite(settings[2])
            ?std::clamp(double(settings[2]),1.,16.):8.));
        for(int i=0;i<16;++i) participation[size_t(i)].reset(sr,i<harmonics?1.f:0.f);
        updateFilters(settings);
        prepared=true;return true;
    }
    void process(float* left,float* right,int n,const FoundationSettings& settings) noexcept {
        processObserved(left,right,n,settings,[](const auto&,const auto&,float,bool) noexcept {});
    }
    template<class Observer> void processObserved(float* left,float* right,int n,const FoundationSettings& settings,Observer&& observer) noexcept {
        processTransformed(left,right,n,settings,observer,[](const auto&,const auto& normal,const auto&,double) noexcept {return normal;});
    }
    template<class Observer,class Transform> void processTransformed(float* left,float* right,int n,const FoundationSettings& settings,Observer&& observer,Transform&& transform) noexcept {
        if(n<=0||!left||!right)return;
        if(!prepared){std::fill_n(left,n,0.f);std::fill_n(right,n,0.f);return;}
        // Hold is snapped, not ramped. Ignore malformed targets and retain the
        // last finite clamped value before any floating-to-integer conversion.
        if constexpr(Kind==FoundationKind::gate)
            if(std::isfinite(settings[5]))holdTargetMs=std::clamp(double(settings[5]),0.,500.);
        for(int i=0;i<count;++i) {
            if constexpr(Kind==FoundationKind::gate) {if(i==5)continue;}
            if constexpr(Kind==FoundationKind::hum) {if(i==2)continue;}
            if(std::isfinite(settings[size_t(i)]))ramps[size_t(i)].set(settings[size_t(i)]);
        }
        if constexpr(Kind==FoundationKind::hum) {
            const int h=int(std::lround(std::isfinite(settings[2])
                ?std::clamp(double(settings[2]),1.,16.):8.));
            for(int i=0;i<16;++i)participation[size_t(i)].set(i<h?1.f:0.f);
        }
        float blockMeter=0;
        for(int s=0;s<n;++s) {
            FoundationSettings p{};for(int i=0;i<count;++i)p[size_t(i)]=ramps[size_t(i)].next();
            std::array<double,2> dry{left[s],right[s]},wet{};
            bool displayActivity=false;
            for(int c=0;c<2;++c)if(!std::isfinite(dry[size_t(c)])) {
                dry[size_t(c)]=0;resetChannel(c);
            }
            if(phase==0)updateFilters(p);phase=(phase+1)&15;
            if constexpr(Kind==FoundationKind::cleaner) {
                for(int c=0;c<2;++c){const auto i=size_t(c);
                    const double dc=(1+dcR)*.5*(dry[i]-previous[i])+dcR*dcHistory[i];
                    previous[i]=dry[i];dcHistory[i]=dc;
                    const double x=dry[i]+p[0]*(dc-dry[i]);
                    const double one=filters[0].tick(x,c),two=filters[1].tick(one,c);
                    wet[i]=x+p[1]*(one+p[3]*(two-one)-x);
                }
            } else if constexpr(Kind==FoundationKind::gate) {
                detector=std::max(std::max(std::abs(dry[0]),std::abs(dry[1])),peakRelease*detector);
                const double level=20*std::log10(std::max(detector,1.e-12));
                const double boundary=p[0]-p[1];
                const auto hold=uint64_t(std::ceil(sr*holdTargetMs*.001));
                if(level>=p[0]){openState=true;below=0;}
                else if(level<=boundary){if(below<hold+1)++below;if(below>=std::max(uint64_t(1),hold))openState=false;}
                else below=0;
                const double expanded=std::min(double(p[3]),(p[2]-1)*std::max(0.,boundary-level));
                displayActivity=openState;
                const double request=openState?0:expanded+p[7]*(p[3]-expanded);
                envelope=foundation::approach(envelope,request,request<envelope?p[4]:p[6],sr);
                const double g=foundation::dbGain(-envelope);
                wet={dry[0]*g,dry[1]*g};
                const float m=float(-20*std::log10(1-p[8]*.01+p[8]*.01*g));blockMeter=std::max(blockMeter,m);
            } else if constexpr(Kind==FoundationKind::rider) {
                for(size_t c=0;c<2;++c)energies[c]=rmsRelease*energies[c]+(1-rmsRelease)*dry[c]*dry[c];
                const double level=foundation::powerDb(std::max(energies[0],energies[1]));
                displayActivity=samples>=warmup&&level>p[5];
                const double request=samples<warmup||level<=p[5]?0:std::clamp(p[0]-level,-double(p[2]),double(p[1]));
                envelope=foundation::approach(envelope,request,request>envelope?p[3]:p[4],sr);
                const double g=foundation::dbGain(envelope);wet={dry[0]*g,dry[1]*g};
                if(std::abs(envelope)>std::abs(blockMeter))blockMeter=float(envelope);
            } else if constexpr(Kind==FoundationKind::plosive) {
                std::array<double,2> low{filters[0].tick(dry[0],0),filters[0].tick(dry[1],1)};
                const double power=std::max(low[0]*low[0],low[1]*low[1]);
                fast=fastRelease*fast+(1-fastRelease)*power;slow=slowRelease*slow+(1-slowRelease)*power;
                displayActivity=foundation::powerDb(fast)>p[1]&&foundation::powerDb(fast)-foundation::powerDb(slow)>p[2];
                const double request=foundation::plosiveRequest(foundation::powerDb(fast),foundation::powerDb(slow),p[1],p[2],p[3]);
                envelope=foundation::approach(envelope,request,request>envelope?p[4]:p[5],sr);
                const double cut=1-foundation::dbGain(-envelope);
                wet={dry[0]-cut*low[0],dry[1]-cut*low[1]};blockMeter=std::max(blockMeter,float(p[6]*.01*envelope));
            } else {
                wet=dry;
                for(size_t k=0;k<16;++k){const double amount=participation[k].next();
                    for(int c=0;c<2;++c){const auto i=size_t(c);const double filtered=filters[k].tick(wet[i],c);wet[i]+=amount*(filtered-wet[i]);}}
                const double amount=1-foundation::dbGain(-p[3]);
                for(size_t c=0;c<2;++c)wet[c]=dry[c]+amount*(wet[c]-dry[c]);
            }
            const double mix=p[size_t(count-2)]*.01,output=foundation::dbGain(p[size_t(count-1)]);
            std::array<double,2> normal{};
            for(size_t c=0;c<2;++c)normal[c]=output*(dry[c]+mix*(wet[c]-dry[c]));
            const double envelopeForOutput=envelope;
            // Keep the legacy exceptional-state reset on the original input
            // sample, before any delay or subtraction. Removed signal must
            // subtract the actual guarded normal output, not an overflow.
            for(size_t c=0;c<2;++c)if(!std::isfinite(normal[c])||std::abs(normal[c])>std::numeric_limits<float>::max()){
                normal[c]=0;resetChannel(int(c));resetLinkedDetector();
            }
            const auto rendered=transform(dry,normal,p,envelopeForOutput);
            for(size_t c=0;c<2;++c){
                const double emitted=rendered[c];
                // Check representability before narrowing, then guard the actual
                // float. Ordinary finite audio is never clipped or reset.
                float y=0;
                if(std::isfinite(emitted)&&std::abs(emitted)<=std::numeric_limits<float>::max())y=float(emitted);
                if(!std::isfinite(emitted)||std::abs(emitted)>std::numeric_limits<float>::max()||!std::isfinite(y)){
                    y=0;resetChannel(int(c));resetLinkedDetector();
                }
                (c==0?left:right)[s]=y;
                if constexpr(Kind==FoundationKind::cleaner||Kind==FoundationKind::hum)blockMeter=std::max(blockMeter,std::abs(y));}
            float displayGain=float(envelope);
            if constexpr(Kind==FoundationKind::gate)displayGain=float(-20*std::log10(1-p[8]*.01+p[8]*.01*foundation::dbGain(-envelope)));
            if constexpr(Kind==FoundationKind::plosive)displayGain*=p[6]*.01f;
            observer(dry,std::array<float,2>{left[s],right[s]},displayGain,displayActivity);
            ++samples;
        }
        measurement.store(blockMeter,std::memory_order_relaxed);
    }
    double notchFrequency() const noexcept {return configuredFundamental;} // audio-owner telemetry only
    double envelopeDb() const noexcept {return envelope;} // deterministic timing test seams
    bool isOpen() const noexcept {return openState;}
    uint64_t processedSamples() const noexcept {return samples;}
    bool isPrepared() const noexcept {return prepared;}
    double sampleRate() const noexcept {return sr;}
private:
    void resetChannel(int c) noexcept {
        previous[size_t(c)]=dcHistory[size_t(c)]=energies[size_t(c)]=0;
        for(auto& filter:filters)filter.reset(c);
    }
    // Detectors are stereo-linked: discard their exceptional excursion while
    // retaining the other channel's independent filter/energy histories.
    void resetLinkedDetector() noexcept {envelope=fast=slow=detector=0;openState=false;below=0;}
    void updateFilters(const FoundationSettings& p) noexcept {
        if constexpr(Kind==FoundationKind::cleaner){filters[0].configure(0,sr,p[2],std::sqrt(.5));filters[1].configure(0,sr,p[2],std::sqrt(.5));}
        if constexpr(Kind==FoundationKind::plosive)filters[0].configure(1,sr,p[0],std::sqrt(.5));
        if constexpr(Kind==FoundationKind::hum){configuredFundamental=p[0];for(size_t k=0;k<16;++k)filters[k].configure(2,sr,(k+1)*double(p[0]),p[1]);}
    }
    double configuredFundamental=0;
    double sr=0,dcR=0,peakRelease=0,rmsRelease=0,fastRelease=0,slowRelease=0;
    std::array<Ramp,10> ramps{};std::array<Ramp,16> participation{};
    std::array<foundation::Section,16> filters{};
    std::array<double,2> previous{},dcHistory{},energies{};
    double envelope=0,fast=0,slow=0,detector=0,holdTargetMs=50;
    uint64_t below=0,samples=0,warmup=0;int phase=0;bool openState=false,prepared=false;
};
using DcSubsonicCore=FoundationCore<FoundationKind::cleaner>;
using SmartGateCore=FoundationCore<FoundationKind::gate>;
using VocalRiderCore=FoundationCore<FoundationKind::rider>;
using PlosiveCore=FoundationCore<FoundationKind::plosive>;
using HumBuzzCore=FoundationCore<FoundationKind::hum>;
}
