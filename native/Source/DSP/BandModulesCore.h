#pragma once
#include "BandBank.h"
#include "BandParameters.h"
#include <cstdint>
#include <limits>

namespace hem::bands {
inline double dbGain(double db) noexcept {return std::pow(10.,db/20.);}
struct CachedGain {
    double previous=999,value=1;
    double get(double db) noexcept {if(db!=previous){previous=db;value=dbGain(db);}return value;}
};
inline double dynamicRequest(double level,double threshold,double range) noexcept {return std::min(range,std::max(0.,level-threshold));}
inline double compressionRequest(double level,double threshold,double ratio,double knee,double range) noexcept {
    const double over=level-threshold,slope=1-1/ratio;
    const double cut=knee<=0?slope*std::max(0.,over):over<=-knee/2?0:over>=knee/2?slope*over:slope*(over+knee/2)*(over+knee/2)/(2*knee);
    return std::min(range,std::max(0.,cut));
}
struct TimeCoefficient {
    double time=-1,coefficient=0;
    double get(double ms,double rate) noexcept {if(ms!=time){time=ms;coefficient=std::exp(-1/(rate*ms*.001));}return coefficient;}
};
struct Detector {
    double peak=0,cut=0;
    TimeCoefficient attack,release;
    double level(double left,double right,double decay) noexcept {
        peak=std::max({std::abs(left),std::abs(right),decay*peak});
        return 20*std::log10(std::max(peak,1e-12));
    }
    double follow(double target,double attackMs,double releaseMs,double rate) noexcept {
        const double coefficient=target>cut?attack.get(attackMs,rate):release.get(releaseMs,rate);
        cut=target+(cut-target)*coefficient;return cut;
    }
    void reset() noexcept {peak=cut=0;}
};
inline Coefficients designBell(double rate,double hz,double q,double db) noexcept {
    const double w=2*pi*std::clamp(hz,1.,rate*.45)/rate;
    const double a=std::pow(10.,db/40.),alpha=std::sin(w)/(2*q),u=alpha/a,d=1+u;
    // Algebraically identical RBJ bell, evaluated as small corrections to
    // 1/-2. This avoids separately rounding near-unity numerator/denominator
    // terms, which magnifies phase error for stacked low-frequency high-Q bells.
    const double halfSin=std::sin(w*.5),a1=-2+2*(2*halfSin*halfSin+u)/d;
    return {1+alpha*(a-1/a)/d,a1,1-alpha*(a+1/a)/d,a1,1-2*u/d};
}
inline Coefficients designDetector(double rate,double hz,double q) noexcept {
    const double w=2*pi*std::clamp(hz,1.,rate*.45)/rate,alpha=std::sin(w)/(2*q),d=1+alpha;
    return {alpha/d,0,-alpha/d,-2*std::cos(w)/d,(1-alpha)/d};
}
struct TunedSection {
    Section section;double frequency=-1,q=-1,gain=999;
    void bell(double rate,double hz,double width,double db) noexcept {
        if(hz!=frequency||width!=q||db!=gain){section.c=designBell(rate,hz,width,db);frequency=hz;q=width;gain=db;}
    }
    void detector(double rate,double hz,double width) noexcept {
        if(hz!=frequency||width!=q){section.c=designDetector(rate,hz,width);frequency=hz;q=width;}
    }
};
struct Snapshot {
    bool available=false,processed=false;
    std::array<float,8> cuts{};
    float outputPeak=0,correlation=0,enableMinimum=0,enableMaximum=0;
};
// One serialized audio writer; GUI uses bounded coherent reads of atomics only.
class Telemetry {
public:
    void publish(const Snapshot& s) noexcept {
        sequence.fetch_add(1);for(size_t i=0;i<8;++i)cuts[i].store(s.cuts[i]);
        peak.store(s.outputPeak);correlation.store(s.correlation);minimum.store(s.enableMinimum);maximum.store(s.enableMaximum);
        processed.store(s.processed);sequence.fetch_add(1);
    }
    Snapshot read() const noexcept {
        for(int attempt=0;attempt<3;++attempt){const auto before=sequence.load();if(before&1u)continue;
            Snapshot s;for(size_t i=0;i<8;++i)s.cuts[i]=cuts[i].load();s.outputPeak=peak.load();s.correlation=correlation.load();
            s.enableMinimum=minimum.load();s.enableMaximum=maximum.load();s.processed=processed.load();
            if(before==sequence.load()){s.available=true;return s;}}
        return {};
    }
private:
    std::atomic<uint32_t> sequence{0};std::array<std::atomic<float>,8> cuts{};
    std::atomic<float> peak{0},correlation{0},minimum{0},maximum{0};std::atomic<bool> processed{false};
    static_assert(std::atomic<float>::is_always_lock_free&&std::atomic<uint32_t>::is_always_lock_free&&std::atomic<bool>::is_always_lock_free);
};
template<Kind K> class Core {
public:
    static constexpr size_t count=specs<K>().size();
    static constexpr bool eq=K==Kind::Dynamic||K==Kind::MidSide;
    static constexpr size_t outputIndex=eq?1:3;
    Telemetry telemetry;
    bool prepare(double rate,const Settings<K>& initial=defaults<K>()) noexcept {
        ready=false;sr=0;telemetry.publish({});
        if(!std::isfinite(rate)||rate<8000||rate>384000)return false;
        sr=rate;target=defaults<K>();sanitize(initial);phase=0;bells={};bandpasses={};detectors={};outputGain={};bandGains={};
        detectorDecay=std::exp(-1/(sr*.020));
        for(size_t i=0;i<count;++i)ramps[i].reset(sr,target[i]);
        if constexpr(!eq){bank.prepare(sr,target[1],target[2]);for(size_t b=0;b<3;++b)participation[b].reset(sr,selected(b));}
        ready=true;return true;
    }
    void process(float* left,float* right,int length,const Settings<K>& values) noexcept {
        if(!left||!right||length<=0)return;
        if(!ready){std::fill_n(left,length,0.f);std::fill_n(right,length,0.f);return;}
        sanitize(values);for(size_t i=0;i<count;++i)ramps[i].set(target[i]);
        if constexpr(!eq){bank.targets(target[1],target[2]);for(size_t b=0;b<3;++b)participation[b].set(selected(b));}
        Snapshot meter;meter.processed=true;meter.enableMinimum=1;double ll=0,rr=0,lr=0;
        for(int n=0;n<length;++n){
            Settings<K> p{};for(size_t i=0;i<count;++i)p[i]=ramps[i].next();
            std::array<double,2> dry{left[n],right[n]};
            for(size_t c=0;c<2;++c)if(!std::isfinite(dry[c])){dry[c]=0;recover(c);}
            auto wet=dry;
            if constexpr(K==Kind::MidSide){
                wet={(dry[0]+dry[1])*.5,(dry[0]-dry[1])*.5};
                for(size_t b=0;b<8;++b){const size_t i=2+b*4,lane=b/4;
                    if(phase==0)bells[b].bell(sr,p[i+1],p[i+3],double(p[i])*p[i+2]);
                    wet[lane]=bells[b].section.tick(wet[lane],0);}
                wet={wet[0]+wet[1],wet[0]-wet[1]};
            }else if constexpr(K==Kind::Dynamic){
                // Every feed-forward detector sees the same sanitized dry input.
                for(size_t b=0;b<8;++b){const size_t i=2+b*8;
                    if(phase==0)bandpasses[b].detector(sr,p[i+1],p[i+3]);
                    const double l=bandpasses[b].section.tick(dry[0],0),r=bandpasses[b].section.tick(dry[1],1);
                    const double level=detectors[b].level(l,r,detectorDecay);
                    const double cut=detectors[b].follow(dynamicRequest(level,p[i+4],p[i+5]),p[i+6],p[i+7],sr);
                    if(phase==0)bells[b].bell(sr,p[i+1],p[i+3],p[i]*(p[i+2]-cut));
                    for(size_t c=0;c<2;++c)wet[c]=bells[b].section.tick(wet[c],c);
                    meter.cuts[b]=std::max(meter.cuts[b],float(cut));
                }
            }else{
                const auto split=bank.tick(dry[0],dry[1]);wet={0,0};
                for(size_t b=0;b<3;++b){const double selectedGain=participation[b].next();auto band=split[b];
                    if constexpr(K==Kind::Dynamics){const size_t i=4+b*8;
                        const double level=detectors[b].level(band[0],band[1],detectorDecay);
                        const double cut=detectors[b].follow(compressionRequest(level,p[i],p[i+1],p[i+4],p[i+5]),p[i+2],p[i+3],sr);
                        const double gain=bandGains[b].get(p[i+6]-cut);for(auto& x:band)x*=gain;
                        meter.cuts[b]=std::max(meter.cuts[b],float(cut));
                    }else{const size_t i=5+b*3;const double m=(band[0]+band[1])*.5,s=(band[0]-band[1])*.5;
                        const double width=p[i]*.01*(b==0?1-p[4]:1),gain=bandGains[b].get(p[i+1]);band={(m+width*s)*gain,(m-width*s)*gain};}
                    for(size_t c=0;c<2;++c)wet[c]+=selectedGain*band[c];
                }
            }
            phase=(phase+1)&15;
            const double gain=outputGain.get(p[outputIndex]),enable=p[0];
            for(size_t c=0;c<2;++c){
                double y=enable==0?dry[c]:dry[c]+enable*(gain*wet[c]-dry[c]);
                if(!std::isfinite(wet[c])||!std::isfinite(y)||std::abs(y)>std::numeric_limits<float>::max()){
                    recover(c);y=enable==0?dry[c]:0;
                }
                wet[c]=float(y);meter.outputPeak=std::max(meter.outputPeak,float(std::abs(y)));
            }
            left[n]=float(wet[0]);right[n]=float(wet[1]);
            ll+=wet[0]*wet[0];rr+=wet[1]*wet[1];lr+=wet[0]*wet[1];
            meter.enableMinimum=std::min(meter.enableMinimum,p[0]);meter.enableMaximum=std::max(meter.enableMaximum,p[0]);
        }
        meter.correlation=ll*rr>1e-20?float(std::clamp(lr/std::sqrt(ll*rr),-1.,1.)):0;
        telemetry.publish(meter);
    }
private:
    void sanitize(const Settings<K>& values) noexcept {
        for(size_t i=0;i<count;++i)if(std::isfinite(values[i])){const auto& s=specs<K>()[i];
            const auto v=std::clamp(values[i],s.minimum,s.maximum);target[i]=s.toggle?float(v>=.5f):v;}
    }
    float selected(size_t band) const noexcept {
        constexpr size_t start=K==Kind::Dynamics?11:7,stride=K==Kind::Dynamics?8:3;
        const bool any=target[start]>.5f||target[start+stride]>.5f||target[start+2*stride]>.5f;
        return !any||target[start+band*stride]>.5f?1.f:0.f;
    }
    void recover(size_t channel) noexcept {
        if constexpr(K==Kind::MidSide){for(auto& b:bells){b.section.reset(0);b.section.reset(1);}}
        else {for(auto& b:bells)b.section.reset(channel);for(auto& b:bandpasses)b.section.reset(channel);}
        if constexpr(!eq)bank.resetChannel(channel);
        for(auto& d:detectors)d.reset(); // Linked envelopes must not retain invalid energy.
    }
    Settings<K> target{};std::array<Ramp,count> ramps;
    std::array<TunedSection,8> bells,bandpasses;std::array<Detector,8> detectors;
    ThreeBandBank bank;std::array<Ramp,3> participation;
    CachedGain outputGain;std::array<CachedGain,3> bandGains;
    double sr=0,detectorDecay=0;unsigned phase=0;bool ready=false;
};
}
