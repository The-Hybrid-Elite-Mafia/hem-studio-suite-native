#pragma once
#include "ExpandedBandParameters.h"
#include "ExpandedEq.h"
#include "DynamicEqRequest.h"
#include "BandVisualTelemetry.h"
namespace hem::bands {
template<Kind K> class ExpandedCore {
    friend struct ExpandedCoreReviewAccess; // Read-only test diagnostics; no callback work.
public:
    static constexpr size_t count=expandedSpecs<K>().size();
    static constexpr bool eq=K==Kind::Dynamic||K==Kind::MidSide;
    static constexpr size_t outputIndex=eq?1:3;
    Telemetry telemetry;VisualTelemetry visuals;
    bool prepare(double rate,const ExpandedSettings<K>& initial=expandedDefaults<K>()) noexcept {
        ready=false;sr=0;telemetry.publish({});visuals.publish({});
        if(!std::isfinite(rate)||rate<8000||rate>384000)return false;
        sr=rate;target=expandedDefaults<K>();sanitize(initial);current=target;phase=0;sampleClock=0;
        inputPeak=outputPeak=0;visualCountdown=0;scopePhase=0;scopeCursor=scopeCount=0;scopeStorage={};scopeEnergy={};visualCuts={};signedChange={};
        filters={};bandpasses={};detectors={};dynamicRequests={};bankDetectors={};outputGain={};bankGains={};
        detectorDecay=std::exp(-1/(sr*.020));visualPeriod=std::max(1,int(sr/25));scopePeriod=std::max(1,int(sr/2000));
        for(size_t i=0;i<count;++i)ramps[i].reset(sr,target[i]);
        if constexpr(K==Kind::Dynamic){for(size_t b=0;b<8;++b)filters[b].prepare(sr,int(target[66+b*3]));}
        if constexpr(K==Kind::MidSide){for(size_t b=0;b<10;++b)filters[b].prepare(sr,int(target[42+b]));
            for(size_t lane=0;lane<2;++lane)laneParticipation[lane].reset(sr,laneSelected(lane));}
        if constexpr(!eq){topology=int(target[count-1]);transitionTopology=topology;topologyChanging=false;topologyFade.reset(sr,0);
            initialiseBank(0);initialiseBank(1);}
        ready=true;return true;
    }
    void process(float* left,float* right,int length,const ExpandedSettings<K>& values) noexcept {
        if(!left||!right||length<=0)return;if(!ready){std::fill_n(left,length,0.f);std::fill_n(right,length,0.f);return;}
        sanitize(values);size_t moving=0;
        for(size_t i=0;i<count;++i){if(choiceParameter<K>(i)){current[i]=target[i];continue;}
            ramps[i].set(target[i]);if(ramps[i].remaining)activeRamps[moving++]=i;}
        if constexpr(K==Kind::MidSide)for(size_t lane=0;lane<2;++lane)laneParticipation[lane].set(laneSelected(lane));
        if constexpr(!eq){legacyBank.targets(target[1],target[2]);modernBank.targets(target[1],target[middleIndex()],target[2]);
            for(int path=0;path<2;++path)for(size_t b=0;b<(path?4u:3u);++b)participation[size_t(path)][b].set(selected(path,b));}
        const bool observe=visuals.subscribed.load(std::memory_order_relaxed);
        if(observe!=observing){observing=observe;visualCountdown=0;inputPeak=outputPeak=0;scopeEnergy={};visualCuts={};scopeStorage={};scopePhase=0;scopeCursor=scopeCount=0;}
        Snapshot meter;meter.processed=true;meter.enableMinimum=1;double ll=0,rr=0,lr=0;
        for(int n=0;n<length;++n){for(size_t j=0;j<moving;++j){const auto index=activeRamps[j];current[index]=ramps[index].next();}
            const auto& p=current;std::array<double,2> dry{left[n],right[n]};
            for(size_t c=0;c<2;++c)if(!std::isfinite(dry[c])){dry[c]=0;recover(c);}
            std::array<double,2> wet=dry;
            if constexpr(K==Kind::Dynamic){std::array<EqAccumulator,2> precise{{{dry[0],0},{dry[1],0}}};for(size_t b=0;b<8;++b){const size_t i=2+b*8,extra=66+b*3;
                    if(phase==0)bandpasses[b].detector(sr,p[i+1],p[i+3]);
                    const double a=bandpasses[b].section.tick(dry[0],0),r=bandpasses[b].section.tick(dry[1],1);
                    auto& detector=detectors[b];detector.peak=std::max({std::abs(a),std::abs(r),detectorDecay*detector.peak});
                    // Range=0 still tracks peak energy for later automation;
                    // its requested adjustment is exactly zero without log10.
                    const double requested=dynamicRequests[b].get(detector.peak,p[i+4],p[i+5]);
                    const double envelope=detector.follow(requested,p[i+6],p[i+7],sr);
                    const double change=p[i]*p[extra+1]*(2*p[extra+2]-1)*envelope;signedChange[b]=change;
                    const double gain=p[i]*(p[i+2]+p[extra+1]*(2*p[extra+2]-1)*envelope);
                    if(phase==0)filters[b].configure(p[i+1],p[i+3],gain,int(p[extra]));precise=filters[b].stereoAccurate(precise);
                    meter.cuts[b]=std::max(meter.cuts[b],float(std::abs(change)));}wet={precise[0].value(),precise[1].value()};}
            else if constexpr(K==Kind::MidSide){wet={(dry[0]+dry[1])*.5,(dry[0]-dry[1])*.5};
                std::array<EqAccumulator,2> precise{{{wet[0],0},{wet[1],0}}};
                for(size_t b=0;b<10;++b){const size_t i=midSideBase(b),lane=b/5;
                    if(phase==0)filters[b].configure(p[i+1],p[i+3],double(p[i])*p[i+2],int(p[42+b]));precise[lane]=filters[b].monoAccurate(precise[lane]);}
                wet={precise[0].value(),precise[1].value()};
                lastLaneParticipation={laneParticipation[0].next(),laneParticipation[1].next()};
                wet[0]*=lastLaneParticipation[0];wet[1]*=lastLaneParticipation[1]*(p[52]*.01);
                wet={wet[0]+wet[1],wet[0]-wet[1]};}
            else {if(!topologyChanging&&int(target[count-1])!=topology){transitionTopology=int(target[count-1]);initialiseBank(transitionTopology);
                    topologyFade.reset(sr,0);topologyFade.set(1);topologyChanging=true;
                    legacyBank.targets(target[1],target[2]);modernBank.targets(target[1],target[middleIndex()],target[2]);}
                wet=processBank(topology,dry,meter,observe);
                if(topologyChanging){const auto other=processBank(transitionTopology,dry,meter,false);const double blend=topologyFade.next();
                    for(size_t c=0;c<2;++c)wet[c]+=blend*(other[c]-wet[c]);
                    if(topologyFade.remaining==0){topology=transitionTopology;topologyChanging=false;scopeStorage={};scopeEnergy={};visualCuts={};scopePhase=0;scopeCursor=scopeCount=0;}}}
            phase=(phase+1)&15;const double output=outputGain.get(p[outputIndex]),enabled=p[0];
            for(size_t c=0;c<2;++c){double y=enabled==0?dry[c]:dry[c]+enabled*(output*wet[c]-dry[c]);
                if(!std::isfinite(wet[c])||!std::isfinite(y)||std::abs(y)>std::numeric_limits<float>::max()){recover(c);y=enabled==0?dry[c]:0;}
                wet[c]=float(y);meter.outputPeak=std::max(meter.outputPeak,float(std::abs(y)));}
            left[n]=float(wet[0]);right[n]=float(wet[1]);ll+=wet[0]*wet[0];rr+=wet[1]*wet[1];lr+=wet[0]*wet[1];
            meter.enableMinimum=std::min(meter.enableMinimum,p[0]);meter.enableMaximum=std::max(meter.enableMaximum,p[0]);++sampleClock;
            if(observe){inputPeak=std::max({inputPeak,std::abs(dry[0]),std::abs(dry[1])});outputPeak=std::max({outputPeak,std::abs(wet[0]),std::abs(wet[1])});
                if(--visualCountdown<=0){publishVisual();visualCountdown=visualPeriod;inputPeak=outputPeak=0;scopeEnergy={};visualCuts={};}}
        }
        meter.correlation=ll*rr>1e-20?float(std::clamp(lr/std::sqrt(ll*rr),-1.,1.)):0;telemetry.publish(meter);
    }
private:
    static constexpr size_t middleIndex(){return K==Kind::Dynamics?28:14;}
    static constexpr size_t midSideBase(size_t band){return band==4?34:band==9?38:band<4?2+4*band:18+4*(band-5);}
    static constexpr size_t bankBase(int path,size_t b){
        if constexpr(K==Kind::Dynamics)return path?(b==0?4:b==1?29:b==2?37:20):4+b*8;
        else return path?(b==0?5:b==1?15:b==2?18:11):5+b*3;
    }
    static constexpr size_t onIndex(int path,size_t b){const size_t start=K==Kind::Dynamics?45:21;
        return path?start+b:(b==0?start:b==1?start+4:start+3);}
    float laneSelected(size_t lane) const noexcept {const bool any=target[53]>.5f||target[54]>.5f;return !any||target[53+lane]>.5f?1.f:0.f;}
    float selected(int path,size_t b) const noexcept {const size_t soloOffset=K==Kind::Dynamics?7:2;bool any=false;
        for(size_t i=0;i<(path?4u:3u);++i)any=any||target[bankBase(path,i)+soloOffset]>.5f;
        return !any||target[bankBase(path,b)+soloOffset]>.5f?1.f:0.f;}
    void initialiseBank(int path) noexcept {
        if(path)modernBank.prepare(sr,current[1],current[middleIndex()],current[2]);else legacyBank.prepare(sr,current[1],current[2]);
        bankDetectors[size_t(path)]={};bankGains[size_t(path)]={};
        for(size_t b=0;b<(path?4u:3u);++b)participation[size_t(path)][b].reset(sr,selected(path,b));
    }
    std::array<double,2> processBank(int path,const std::array<double,2>& dry,Snapshot& meter,bool observe) noexcept {
        std::array<std::array<double,2>,4> bands{};if(path)bands=modernBank.template tick<K==Kind::Dynamics>(dry[0],dry[1]);else{const auto split=legacyBank.template tick<K==Kind::Dynamics>(dry[0],dry[1]);for(size_t b=0;b<3;++b)bands[b]=split[b];}
        const bool takeScope=observe&&scopePhase==0;std::array<double,2> result{};const auto& p=current;
        for(size_t b=0;b<(path?4u:3u);++b){const size_t i=bankBase(path,b);const auto raw=bands[b];auto processed=raw;const double on=p[onIndex(path,b)];double effectiveCut=0;
            if constexpr(K==Kind::Dynamics){auto& detector=bankDetectors[size_t(path)][b];const double level=detector.level(raw[0],raw[1],detectorDecay);
                const double cut=detector.follow(compressionRequest(level,p[i],p[i+1],p[i+4],p[i+5]),p[i+2],p[i+3],sr);
                const double gain=bankGains[size_t(path)][b].get(p[i+6]-cut);for(auto& x:processed)x*=gain;
                effectiveCut=on==1?cut:on==0?0:-20*std::log10((1-on)+on*dbGain(-cut));
            }else{const double m=(raw[0]+raw[1])*.5,s=(raw[0]-raw[1])*.5;
                const double width=p[i]*.01*(b==0?1-p[4]:1),gain=bankGains[size_t(path)][b].get(p[i+1]);processed={(m+width*s)*gain,(m-width*s)*gain};}
            if(on==0)processed=raw;else if(on!=1)for(size_t c=0;c<2;++c)processed[c]=raw[c]+on*(processed[c]-raw[c]);
            if constexpr(K==Kind::Dynamics){const double mix=p[50]*.01;if(mix==0)processed=raw;else if(mix!=1)for(size_t c=0;c<2;++c)processed[c]=raw[c]+mix*(processed[c]-raw[c]);}
            const double selection=participation[size_t(path)][b].next();for(size_t c=0;c<2;++c)result[c]+=selection*processed[c];
            meter.cuts[b]=std::max(meter.cuts[b],float(effectiveCut));
            if(observe){visualCuts[b]=std::max(visualCuts[b],effectiveCut);auto& energy=scopeEnergy[b];energy[0]+=processed[0]*processed[0];energy[1]+=processed[1]*processed[1];energy[2]+=processed[0]*processed[1];
                if(takeScope)for(size_t c=0;c<2;++c)scopeStorage[b][scopeCursor][c]=float(std::clamp(processed[c],-double(std::numeric_limits<float>::max()),double(std::numeric_limits<float>::max())));}
        }
        if(observe){if(takeScope){scopeCursor=(scopeCursor+1)%32;scopeCount=std::min(size_t(32),scopeCount+1);}scopePhase=(scopePhase+1)%scopePeriod;}
        return result;
    }
    void sanitize(const ExpandedSettings<K>& values) noexcept {for(size_t i=0;i<count;++i)if(std::isfinite(values[i])){const auto& spec=expandedSpecs<K>()[i];
        float value=std::clamp(values[i],spec.minimum,spec.maximum);if(spec.toggle)value=float(value>=.5f);else if(choiceParameter<K>(i))value=std::round(value);target[i]=value;}}
    void recover(size_t channel) noexcept {if constexpr(K==Kind::MidSide){for(auto& f:filters){f.reset(0);f.reset(1);}}else for(auto& f:filters)f.reset(channel);
        for(auto& b:bandpasses)b.section.reset(channel);for(auto& d:detectors)d.reset();
        if constexpr(!eq){legacyBank.resetChannel(channel);modernBank.resetChannel(channel);for(auto& path:bankDetectors)for(auto& d:path)d.reset();}}
    void publishVisual() noexcept {
        VisualFrame frame;frame.samples=sampleClock;frame.rate=sr;frame.processed=true;frame.inputPeak=inputPeak;frame.outputPeak=outputPeak;frame.topology=topology;
        if constexpr(eq){const size_t bands=K==Kind::Dynamic?8:10;for(size_t b=0;b<bands;++b){frame.coefficients[b]=filters[b].coefficients();frame.transition=frame.transition||filters[b].transitioning;}
            if constexpr(K==Kind::Dynamic)frame.signedDynamics=signedChange;
            else{frame.width=current[52];frame.midParticipation=lastLaneParticipation[0];frame.sideParticipation=lastLaneParticipation[1];}}
        else{frame.transition=topologyChanging;frame.crossovers=topology?modernBank.actualCutoffs():std::array<double,3>{current[1],0,current[2]};
            frame.scopeCount=unsigned(scopeCount);
            for(size_t b=0;b<4;++b){frame.bandCuts[b]=visualCuts[b];const auto& energy=scopeEnergy[b];frame.correlationValid[b]=energy[0]*energy[1]>1e-20;
                frame.correlations[b]=frame.correlationValid[b]?std::clamp(energy[2]/std::sqrt(energy[0]*energy[1]),-1.,1.):0;
                for(size_t i=0;i<scopeCount;++i)frame.scopes[b][i]=scopeStorage[b][(scopeCursor+32-scopeCount+i)%32];}}
        visuals.publish(frame);
    }
    ExpandedSettings<K> target{},current{};std::array<Ramp,count> ramps;std::array<size_t,count> activeRamps{};
    std::array<MorphingEq,10> filters;std::array<TunedSection,8> bandpasses;std::array<Detector,8> detectors;
    std::array<DynamicEqRequest,8> dynamicRequests;
    std::array<std::array<Detector,4>,2> bankDetectors;std::array<std::array<CachedGain,4>,2> bankGains;
    std::array<std::array<Ramp,4>,2> participation;std::array<Ramp,2> laneParticipation;std::array<float,2> lastLaneParticipation{1,1};
    ThreeBandBank legacyBank;FourBandBank modernBank;CachedGain outputGain;Ramp topologyFade;
    double sr=0,detectorDecay=0,inputPeak=0,outputPeak=0;unsigned phase=0;uint64_t sampleClock=0;
    int topology=1,transitionTopology=1,visualCountdown=0,visualPeriod=1,scopePhase=0,scopePeriod=1;size_t scopeCursor=0,scopeCount=0;
    bool ready=false,topologyChanging=false,observing=false;
    std::array<double,8> signedChange{};std::array<double,4> visualCuts{};std::array<std::array<double,3>,4> scopeEnergy{};
    std::array<std::array<std::array<float,2>,32>,4> scopeStorage{};
};
}
