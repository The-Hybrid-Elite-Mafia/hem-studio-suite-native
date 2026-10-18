#pragma once
#include "DelaySettings.h"
#include "FinishingCore.h"
#include <memory>
#include <limits>
#include <string>

namespace hem::delay {
enum class Kind { multitap, tape };
struct Tap {
    DelayTimeSetting time;
    double feedback=0, pan=0, levelDb=0, lowHz=20, highHz=20000;
    bool on=false,solo=false,mute=false,lowOn=false,highOn=false;
};
struct Settings {
    std::array<Tap,8> taps{};
    double mix=0,outputDb=0,wow=0,flutter=0,driveDb=0;
    bool sync=false,bypass=false;
};
inline Settings defaults(Kind kind) {
    Settings p;
    for(std::size_t i=0;i<8;++i)p.taps[i].time.manualMilliseconds=250.0*double(i+1);
    if(kind==Kind::tape)p.taps[0].on=true;
    return p;
}
inline double finite(double value,double low,double high,double fallback=0) noexcept {
    return std::isfinite(value)?std::clamp(value,low,high):fallback;
}
inline double linearGain(double db) noexcept {return std::pow(10.0,db/20.0);}
inline std::array<bool,8> activity(const Settings& p,Kind kind,bool externalAudible=true) noexcept {
    std::array<TapActivity,8> a{};
    for(std::size_t i=0;i<(kind==Kind::tape?1u:8u);++i)a[i]={p.taps[i].on,p.taps[i].solo,p.taps[i].mute};
    return audibleTaps(a,externalAudible);
}
struct CapturedTail {
    bool valid=true,syncConflict=false;
    double seconds=0;
    std::array<FrozenTailBound,8> taps{};
};
inline CapturedTail capturedTail(Kind kind,const Settings& p,TempoSnapshot tempo,double rate,bool nodeAudible=true) noexcept {
    CapturedTail result;
    if(!std::isfinite(rate)||rate<minimumRate||rate>maximumRate){result.valid=false;return result;}
    const auto active=activity(p,kind,nodeAudible&&!p.bypass&&p.mix>0);
    unsigned count=0;for(bool a:active)if(a)++count;
    for(std::size_t i=0;i<8;++i){
        if(!active[i]){result.taps[i].valid=true;continue;}
        auto time=p.taps[i].time;time.sync=p.sync;
        const auto resolved=resolveTime(time,tempo);
        if(!resolved.resolved()){result.valid=false;result.syncConflict=true;return result;}
        const auto& tap=p.taps[i];FrozenTailRequest request;
        request.effectiveDelaySeconds=resolved.processingMilliseconds*.001;
        if(kind==Kind::tape)request.effectiveDelaySeconds+=(p.wow*.05+p.flutter*.005)*.001;
        request.feedback=tap.feedback/100.;
        request.filtersEnabled=tap.lowOn||tap.highOn;
        request.enabledFilterStages=unsigned(tap.lowOn)+unsigned(tap.highOn);
        request.slowestEnabledFilterHz=std::min(tap.lowOn?tap.lowHz:20000.,tap.highOn?tap.highHz:20000.);
        request.postHistoryPeakGain=linearGain(p.outputDb)*p.mix*.01*count
            *(kind==Kind::multitap?linearGain(tap.levelDb):1.)*(tap.lowOn?2.:1.);
        if(kind==Kind::tape){
            request.postHistoryPeakGain*=p.driveDb>0?10.:1.;
            request.finiteOutputSupportSeconds=(p.driveDb>0?32.:16.)/rate;
        }
        result.taps[i]=frozenTail(request,rate);
        result.valid=result.valid&&result.taps[i].valid;
        result.seconds=std::max(result.seconds,result.taps[i].seconds);
    }
    return result;
}
class Linear final {
public:
    void reset(double v,unsigned n) noexcept {current=target=v;step=0;remaining=0;length=std::max(1u,n);}
    void set(double v) noexcept {if(v!=target){target=v;remaining=length;step=(target-current)/length;}}
    double next() noexcept {if(remaining&&!--remaining){current=target;}else if(remaining)current+=step;return current;}
    double value() const noexcept{return current;}
private:double current=0,target=0,step=0;unsigned remaining=0,length=1;
};
// Every read head is bounded. New requests during a fade are coalesced; the
// existing endpoints never jump mid-fade and no third head is allocated.
class CrossfadeHead final {
public:
    void reset(double d,unsigned n) noexcept {from=to=pending=d;position=0;length=std::max(1u,n);moving=false;}
    void target(double d) noexcept {pending=d;if(!moving&&d!=to){from=to;to=d;position=0;moving=true;}}
    StereoSample read(const StereoHistory& history) noexcept {
        if(!moving)return history.read(to);
        const auto a=history.read(from),b=history.read(to);const double w=double(++position)/length;
        StereoSample y{float((1-w)*a.left+w*b.left),float((1-w)*a.right+w*b.right)};
        if(position>=length){moving=false;if(pending!=to)target(pending);}return y;
    }
private:double from=1,to=1,pending=1;unsigned position=0,length=1;bool moving=false;
};
struct FilterPair {
    std::array<double,2> lowState{},highState{};
    void clear() noexcept {lowState={};highState={};}
    double tick(double x,int ch,double lowPole,double highPole,double lowWet,double highWet) noexcept {
        auto clean=[](double v) noexcept {return std::isfinite(v)&&std::abs(v)>=1e-20?v:0.;};
        auto& lo=lowState[std::size_t(ch)];auto& hi=highState[std::size_t(ch)];
        lo=clean(lowPole*lo+(1-lowPole)*x);const double hp=x-lowWet*lo;
        hi=clean(highPole*hi+(1-highPole)*hp);return clean(hp+highWet*(hi-hp));
    }
};

class Core final {
public:
    explicit Core(Kind k):kind(k){}
    // Strong exception guarantee: all allocation/initialisation in a candidate.
    // Failed prepare leaves the previous runtime/rate/output history intact.
    void prepare(double rate,const Settings& p,TempoSnapshot tempo,bool frozen=false,bool nodeAudible=true) {
        if(!std::isfinite(rate)||rate<minimumRate||rate>maximumRate)throw std::invalid_argument("Delay requires 8000..384000 Hz");
        const auto tail=capturedTail(kind,p,tempo,rate,nodeAudible);
        if(frozen&&!tail.valid)throw std::invalid_argument(tail.syncConflict?"Audible delay sync division exceeds 1..4000 ms at captured BPM":"Invalid captured delay tail");
        try {
            auto candidate=std::make_unique<Runtime>();candidate->rate=rate;candidate->frozen=frozen;
            candidate->frozenSettings=p;candidate->frozenTempo=tempo;candidate->shaper.prepare();
            const auto ramp=unsigned(std::max(1.,std::ceil(rate*.02)));
            const auto active=activity(p,kind);
            candidate->mix.reset(p.mix*.01,ramp);candidate->output.reset(linearGain(p.outputDb),ramp);
            candidate->bypass.reset(p.bypass?0.:1.,ramp);candidate->drive.reset(p.driveDb,ramp);
            candidate->wow.reset(p.wow,ramp);candidate->flutter.reset(p.flutter,ramp);
            for(std::size_t i=0;i<count();++i){auto& t=candidate->taps[i];const auto& s=p.taps[i];
                // Frozen inactive paths cannot become audible: omit their rings.
                // Live instances reserve every tap so automation never allocates.
                if(!frozen||tail.taps[i].samples>0)t.history.prepare(rate);
                auto time=s.time;time.sync=p.sync;const auto resolved=resolveTime(time,tempo);
                const double d=std::clamp(resolved.processingMilliseconds,1.,4000.)*rate*.001;
                t.head.reset(d,ramp);t.delay=d;t.delayTarget=d;t.feedback.reset(s.feedback/100.,ramp);
                t.on.reset(s.on?1.:0.,ramp);t.audible.reset(active[i]?1.:0.,ramp);
                t.level.reset(linearGain(s.levelDb),ramp);t.pan.reset(s.pan*.01,ramp);
                t.lowWet.reset(s.lowOn?1.:0.,ramp);t.highWet.reset(s.highOn?1.:0.,ramp);
                t.lowPole.reset(pole(s.lowHz,rate),ramp);t.highPole.reset(pole(s.highHz,rate),ramp);
                auto bound=tail.taps[i];
                if(!frozen){bound.valid=true;bound.needsResidualTermination=true;
                    bound.samples=std::uint64_t(std::ceil(liveSafetyTailSeconds*rate));
                    bound.residualBoundSamples=bound.samples-std::uint64_t(ramp);}
                t.schedule.configure(bound);
            }
            runtime.swap(candidate);
        }catch(const std::bad_alloc&){std::size_t taps=0;for(std::size_t i=0;i<count();++i)if(!frozen||tail.taps[i].samples>0)++taps;
            throw std::runtime_error("Delay allocation failed at "+std::to_string(int(rate))+" Hz (requested rings "+std::to_string(requiredRingBytes(rate,kind)/count()*taps)+" bytes); previous instance retained");}
    }
    static std::uint64_t requiredRingBytes(double rate,Kind kind) noexcept {
        return std::uint64_t(std::ceil(rate*maximumHistorySeconds)+2)*2*sizeof(float)*(kind==Kind::tape?1:8);
    }
    bool ready()const noexcept{return bool(runtime);}
    void swapRuntime(Core& other)noexcept{runtime.swap(other.runtime);}
    double sampleRate()const noexcept{return runtime?runtime->rate:0;}
    int latency()const noexcept{return kind==Kind::tape?16:0;}
    struct Meters {float input=0,output=0;std::uint64_t protection=0;bool syncConflict=false;};
    Meters process(float* left,float* right,int samples,const Settings& requested,TempoSnapshot incoming) noexcept {
        Meters meters;if(!runtime||samples<=0)return meters;auto& r=*runtime;
        const auto& p=r.frozen?r.frozenSettings:requested;const auto tempo=r.frozen?r.frozenTempo:incoming;
        const auto active=activity(p,kind);
        r.mix.set(finite(p.mix,0,100)*.01);r.output.set(linearGain(finite(p.outputDb,-24,12)));
        r.bypass.set(p.bypass?0.:1.);r.drive.set(finite(p.driveDb,0,24));r.wow.set(finite(p.wow,0,100));r.flutter.set(finite(p.flutter,0,100));
        for(std::size_t i=0;i<count();++i){auto& t=r.taps[i];const auto& s=p.taps[i];
            auto time=s.time;time.sync=p.sync;const auto resolved=resolveTime(time,tempo);
            meters.syncConflict=meters.syncConflict||(!resolved.resolved()&&active[i]&&p.mix>0&&!p.bypass);
            const double d=finite(resolved.processingMilliseconds,1,4000,250)*r.rate*.001;
            t.head.target(d);t.delayTarget=d;t.feedback.set(finite(s.feedback,0,95)/100.);
            t.on.set(s.on?1.:0.);t.audible.set(active[i]?1.:0.);t.level.set(linearGain(finite(s.levelDb,-60,12)));
            t.pan.set(finite(s.pan,-100,100)*.01);t.lowWet.set(s.lowOn?1.:0.);t.highWet.set(s.highOn?1.:0.);
            t.lowPole.set(pole(finite(s.lowHz,20,20000,20),r.rate));t.highPole.set(pole(finite(s.highHz,20,20000,20000),r.rate));
        }
        for(int n=0;n<samples;++n){
            const StereoSample x{std::isfinite(left[n])?left[n]:0.f,std::isfinite(right[n])?right[n]:0.f};
            meters.input=std::max({meters.input,std::abs(x.left),std::abs(x.right)});
            double wetL=0,wetR=0;const double wow=r.wow.next(),flutter=r.flutter.next(),drive=r.drive.next();
            for(std::size_t i=0;i<count();++i){auto& t=r.taps[i];
                const auto decision=t.schedule.advance(x.left!=0||x.right!=0);
                const double injection=t.on.next(),audible=t.audible.next(),feedback=t.feedback.next();
                const double level=t.level.next(),pan=t.pan.next(),lowWet=t.lowWet.next(),highWet=t.highWet.next();
                const double lowPole=t.lowPole.next(),highPole=t.highPole.next();
                StereoSample y;
                if(kind==Kind::tape){
                    // <=0.25 sample/sample read-speed change; reaches every finite target.
                    t.delay+=std::clamp(t.delayTarget-t.delay,-.25,.25);
                    const double read=tapeReadSamples(t.delay/r.rate*1000,wow,flutter,r.wowPhase,r.flutterPhase,r.rate);
                    y=t.history.read(read);
                }else y=t.head.read(t.history);
                if(decision.discardHistory){if(!t.discarded){t.history.invalidate();t.filters.clear();if(kind==Kind::tape)r.shaper.resetHistory();t.discarded=true;}y={};}
                else {t.discarded=false;
                    if(t.history.write(double(x.left)*injection+feedback*y.left,double(x.right)*injection+feedback*y.right)){
                        if(r.protection!=UINT64_MAX)++r.protection;}}
                double l=t.filters.tick(y.left,0,lowPole,highPole,lowWet,highWet);
                double rr=t.filters.tick(y.right,1,lowPole,highPole,lowWet,highWet);
                if(kind==Kind::tape){
                    // Always tick FIR/dry state, including drive=0, for fixed latency.
                    const float amount=float(std::clamp(drive/6.,0.,1.));const float g=float(linearGain(drive));
                    l=r.shaper.tick(float(l),0,g,0,amount);rr=r.shaper.tick(float(rr),1,g,0,amount);
                }
                const double audibleGain=audible*decision.wetGain*(kind==Kind::tape?1.:level);
                wetL+=l*audibleGain*(pan>0?1-pan:1);wetR+=rr*audibleGain*(pan<0?1+pan:1);
            }
            r.wowPhase+=6.283185307179586*.5/r.rate;r.flutterPhase+=6.283185307179586*6/r.rate;
            if(r.wowPhase>=6.283185307179586)r.wowPhase-=6.283185307179586;
            if(r.flutterPhase>=6.283185307179586)r.flutterPhase-=6.283185307179586;
            StereoSample dry=x;if(kind==Kind::tape){dry=r.dry[r.dryIndex];r.dry[r.dryIndex]=x;r.dryIndex=(r.dryIndex+1)%16;}
            const double mix=r.mix.next(),out=r.output.next(),enabled=r.bypass.next();
            const double mixedL=((1-mix)*dry.left+mix*wetL)*out,mixedR=((1-mix)*dry.right+mix*wetR)*out;
            left[n]=output(double(dry.left)+enabled*(mixedL-dry.left));right[n]=output(double(dry.right)+enabled*(mixedR-dry.right));
            meters.output=std::max({meters.output,std::abs(left[n]),std::abs(right[n])});
        }
        meters.protection=r.protection;return meters;
    }
private:
    static double pole(double hz,double rate)noexcept{return std::exp(-6.283185307179586*std::min(hz,rate*.45)/rate);}
    static float output(double x)noexcept {
        if(!std::isfinite(x)||std::abs(x)<1e-20)return 0;
        return float(std::clamp(x,-double(std::numeric_limits<float>::max()),double(std::numeric_limits<float>::max())));
    }
    std::size_t count()const noexcept{return kind==Kind::tape?1:8;}
    struct TapRuntime {
        StereoHistory history;CrossfadeHead head;FilterPair filters;FrozenSilenceSchedule schedule;
        Linear feedback,on,audible,level,pan,lowWet,highWet,lowPole,highPole;
        double delay=1,delayTarget=1;bool discarded=false;
    };
    struct Runtime {
        std::array<TapRuntime,8> taps;double rate=0,wowPhase=0,flutterPhase=0;
        Linear mix,output,bypass,drive,wow,flutter;OversampledSaturator shaper;
        std::array<StereoSample,16> dry{};unsigned dryIndex=0;std::uint64_t protection=0;
        bool frozen=false;Settings frozenSettings;TempoSnapshot frozenTempo;
    };
    Kind kind;std::unique_ptr<Runtime> runtime;
};
} // namespace hem::delay
