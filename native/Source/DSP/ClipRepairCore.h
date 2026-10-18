#pragma once
// Original, dependency-free bounded-lookahead waveform estimation. This cannot
// recover the original samples erased by clipping. See RestorationModules docs.
#include "DspCore.h"
#include <cstdint>
#include <limits>

namespace hem {
struct ClipRepairSettings {
    float threshold=-0.3f;          // dBFS, [-24, 0]
    float interpolationLength=2;   // maximum plateau length, ms, [0.1, 4]
    float amount=0;                // percent, [0, 100]; neutral by default
    float output=0;                // dB, [-24, 6]
    int algorithm=1;               // 0: cubic Hermite; 1: local sinusoidal model
    bool listenRepair=false;
    bool bypass=false;
};

class ClipRepairCore {
public:
    using Settings=ClipRepairSettings;
    using Pair=std::array<float,2>;
    void prepare(double sampleRate,const Settings& initial={}) {
        validRate_=std::isfinite(sampleRate)&&sampleRate>=8000&&sampleRate<=384000;
        rate_=validRate_?sampleRate:48000.0;
        // floor, not ceil: the reported delay never exceeds six milliseconds.
        delay_=static_cast<int>(std::floor(rate_*0.006));
        context_=std::max(3,static_cast<int>(std::floor(rate_*0.00075)));
        ringSize_=static_cast<std::size_t>(2*delay_+16);
        dry_.assign(ringSize_,{});
        estimate_.assign(ringSize_,{});
        settings_=sanitise(initial);
        reset();
    }
    // Preserve the latest settings, reset history/counters and finish all ramps.
    void reset() noexcept {
        std::fill(dry_.begin(),dry_.end(),Pair{});
        std::fill(estimate_.begin(),estimate_.end(),Pair{});
        position_=0;suppress_={};suppressLevel_={};skipUntil_={};
        clips_=repairs_=skips_=0;
        amount_.reset(rate_,settings_.amount*0.01f);
        output_.reset(rate_,gain(settings_.output));
        enabled_.reset(rate_,settings_.bypass?0.f:1.f);
        listen_.reset(rate_,settings_.listenRepair?1.f:0.f);
        publish(0,0,0);
    }
    int latency() const noexcept { return delay_; }
    double sampleRate() const noexcept { return rate_; }
    bool preparedRateWasValid() const noexcept { return validRate_; }
    void process(float* left,float* right,int n,const Settings& settings) noexcept {
        processObserved(left,right,n,settings,[](const Pair&,const Pair&,const Pair&,float) noexcept {});
    }
    // The observer is synchronous and must itself be realtime-safe/noexcept.
    // dry: delayed original; wet: normal listening output, including gain/bypass;
    // delta: ONLY the applied waveform repair, after gain/bypass, excluding gain
    // changes to the original. Listen Repair emits delta, with a 10 ms crossfade.
    // activity is 1 exactly when either channel has an applied repair this frame.
    template<class Observer>
    void processObserved(float* left,float* right,int n,const Settings& settings,
                         Observer&& observer) noexcept {
        if(n<=0||left==nullptr||right==nullptr||dry_.empty()) return;
        settings_=sanitise(settings);
        amount_.set(settings_.amount*0.01f);
        output_.set(gain(settings_.output));
        enabled_.set(settings_.bypass?0.f:1.f);
        listen_.set(settings_.listenRepair?1.f:0.f);
        const double threshold=static_cast<double>(gain(settings_.threshold));
        const int maximum=std::max(1,static_cast<int>(std::floor(rate_*settings_.interpolationLength*0.001)));
        float blockPeak=0,blockRepair=0,blockActivity=0;
        for(int i=0;i<n;++i,++position_) {
            const auto inputSlot=slot(position_);
            dry_[inputSlot]={finiteInput(left[i]),finiteInput(right[i])};
            estimate_[inputSlot]=dry_[inputSlot];
            const auto when=position_-delay_;
            Pair dry{},wet{},delta{};
            if(when>=0) {
                for(std::size_t channel=0;channel<2;++channel)
                    inspect(when,channel,threshold,maximum,settings_.algorithm);
                dry=dry_[slot(when)];
            }
            const double amount=amount_.next(),out=output_.next(),enabled=enabled_.next();
            const double audition=listen_.next();
            float frameActivity=0;
            for(std::size_t channel=0;channel<2;++channel) {
                const double original=dry[channel];
                const double estimate=when>=0?estimate_[slot(when)][channel]:0;
                const double change=amount*(estimate-original);
                // Explicit identity branches preserve signed zeros and all
                // finite dry samples bit-for-bit in neutral and bypass modes.
                if(enabled==0||(out==1&&change==0)) wet[channel]=dry[channel];
                else wet[channel]=boundedFloat(original+enabled*(out*(original+change)-original));
                const float noRepair=(enabled==0||out==1)?dry[channel]:
                    boundedFloat(original+enabled*(out*original-original));
                delta[channel]=boundedFloat(static_cast<double>(wet[channel])-noRepair);
                const float audible=(audition==0||enabled==0)?wet[channel]:
                    boundedFloat(static_cast<double>(wet[channel])+audition*
                        (static_cast<double>(delta[channel])-wet[channel])*enabled);
                if(channel==0) left[i]=audible; else right[i]=audible;
                // Count applied corrections only: detection, output gain,
                // amount=0, fully bypassed events do not count as repairs.
                if(delta[channel]!=0) { ++repairs_;frameActivity=1; }
                blockPeak=std::max(blockPeak,std::abs(audible));
                blockRepair=std::max(blockRepair,std::abs(delta[channel]));
            }
            blockActivity=std::max(blockActivity,frameActivity);
            observer(dry,wet,delta,frameActivity);
        }
        publish(blockPeak,blockRepair,blockActivity);
    }
    // Cumulative per-channel plateau events / delivered repaired samples.
    // Publish once per process call; independent relaxed atomic snapshots.
    std::atomic<std::uint64_t> clipEvents{0},repairedSamples{0},skippedEvents{0};
    std::atomic<float> peak{0},repairPeak{0},activity{0};

private:
    static_assert(std::atomic<std::uint64_t>::is_always_lock_free,
                  "ClipRepair requires lock-free 64-bit telemetry atomics");
    static_assert(std::atomic<float>::is_always_lock_free,
                  "ClipRepair requires lock-free float telemetry atomics");
    static float finiteInput(float x) noexcept { return std::isfinite(x)?x:0.f; }
    static float boundedFloat(double x) noexcept {
        const double limit=std::numeric_limits<float>::max();
        return static_cast<float>(std::clamp(x,-limit,limit));
    }
    static float checked(float x,float fallback,float lo,float hi) noexcept {
        return std::isfinite(x)?std::clamp(x,lo,hi):fallback;
    }
    static Settings sanitise(Settings s) noexcept {
        s.threshold=checked(s.threshold,-0.3f,-24,0);
        s.interpolationLength=checked(s.interpolationLength,2,0.1f,4);
        s.amount=checked(s.amount,0,0,100);
        s.output=checked(s.output,0,-24,6);
        s.algorithm=s.algorithm==0?0:1;
        return s;
    }
    std::size_t slot(std::int64_t t) const noexcept { return static_cast<std::size_t>(t)%ringSize_; }
    double sample(std::int64_t t,std::size_t c) const noexcept {
        return t<0?0:static_cast<double>(dry_[slot(t)][c]);
    }
    static double tolerance(double level) noexcept { return std::max(2.e-7,std::abs(level)*2.e-6); }
    static bool flat(double x,double level) noexcept {
        return x*level>0&&std::abs(x-level)<=tolerance(level);
    }
    void inspect(std::int64_t start,std::size_t c,double threshold,int maximum,int algorithm) noexcept {
        const double level=sample(start,c);
        if(suppress_[c]) {
            if(flat(level,suppressLevel_[c])) return;
            suppress_[c]=false;
        }
        if(start<skipUntil_[c]||std::abs(level)<threshold) return;
        // Three samples plus an actual start: an isolated high sample/rounded
        // peak is never enough to infer clipping.
        if(flat(sample(start-1,c),level)||!flat(sample(start+1,c),level)||
           !flat(sample(start+2,c),level)) return;
        ++clips_;
        int length=3;
        while(length<=maximum&&flat(sample(start+length,c),level)) ++length;
        if(length>maximum) {
            ++skips_;suppress_[c]=true;suppressLevel_[c]=level;return;
        }
        skipUntil_[c]=start+length;
        const auto end=start+length-1;
        if(start<3) { ++skips_;return; }
        const double sign=level>0?1:-1;
        const double y0=sample(start-1,c)*sign,y1=sample(end+1,c)*sign;
        const double l2=sample(start-2,c)*sign,l3=sample(start-3,c)*sign;
        const double r2=sample(end+2,c)*sign,r3=sample(end+3,c)*sign;
        const double p=std::abs(level),tol=tolerance(level);
        const double slopeL=y0-l2,slopeR=y1-r2;
        // Real shoulders must approach/recede, with finite differences much
        // larger than the near-flat tolerance. Reject square pulses, DC and
        // the naturally flat summit of a smooth, very low-frequency peak.
        if(y0>=p-tol||y1>=p-tol||slopeL<=8*tol||slopeR<=8*tol||
           l2<=l3||r2<=r3||y0-l2>4*(l2-l3)||y1-r2>4*(r2-r3)) {
            ++skips_;return;
        }
        const double distance=length+1;
        const double m0=1.5*y0-2*l2+0.5*l3;
        const double m1=-1.5*y1+2*r2-0.5*r3;
        double omega=0,sineCoefficient=0;
        if(algorithm==1&&!sinusoid(start,end,c,sign,p,y0,y1,distance,omega,sineCoefficient)) {
            ++skips_;return;
        }
        // Validate the complete proposed patch before changing any ring slot.
        // The estimate_ ring is only written after this pass succeeds. Cap
        // inferred peaks at 4x the plateau; dry audio is never hard-limited.
        bool changes=false;
        for(int k=1;k<=length;++k) {
            const double value=prediction(k,distance,y0,y1,m0,m1,algorithm,omega,sineCoefficient);
            if(!std::isfinite(value)||value<0||value>4*p) { ++skips_;return; }
            if(value>std::abs(sample(start+k-1,c))+tol) changes=true;
        }
        if(!changes) { ++skips_;return; }
        for(int k=1;k<=length;++k) {
            const auto t=start+k-1;
            const double value=prediction(k,distance,y0,y1,m0,m1,algorithm,omega,sineCoefficient);
            // Do not lower an observed hard-clip lower bound. No shoulder,
            // neighbouring transient or opposite channel is rewritten.
            estimate_[slot(t)][c]=boundedFloat(sign*std::max(std::abs(sample(t,c)),value));
        }
    }
    bool sinusoid(std::int64_t start,std::int64_t end,std::size_t c,double sign,
                  double p,double y0,double y1,double distance,
                  double& omega,double& sineCoefficient) const noexcept {
        // Local constant-frequency, zero-centred sinusoid hypothesis. Estimate
        // cos(h*w) from x[n-h]+x[n+h]=2*cos(h*w)*x[n] on each shoulder.
        int left=0,right=0;
        const double margin=tolerance(p);
        for(int j=1;j<=context_&&start-j>=0;++j) {
            if(std::abs(sample(start-j,c))>=p-margin) break;
            ++left;
        }
        for(int j=1;j<=context_;++j) {
            if(std::abs(sample(end+j,c))>=p-margin) break;
            ++right;
        }
        const int h=(std::min(left,right)-1)/2;
        if(h<1) return false;
        const double a=sample(start-1-h,c)*sign;
        const double b=sample(end+1+h,c)*sign;
        const double den=2*(a*a+b*b);
        if(den<1.e-16) return false;
        const double cosine=(a*(y0+sample(start-1-2*h,c)*sign)+
                             b*(y1+sample(end+1+2*h,c)*sign))/den;
        if(cosine<=0||cosine>=1) return false;
        omega=std::acos(cosine)/h;
        const double span=omega*distance;
        if(span<1.e-5||span>0.95*pi) return false;
        const double denominator=std::sin(span);
        sineCoefficient=(y1-y0*std::cos(span))/denominator;
        // Independent intermediate points on BOTH shoulders must agree with
        // the proposed model. Complex/ambiguous material is left untouched.
        double error=0,energy=0;
        for(int side=0;side<2;++side) {
            const int available=side==0?left:right;
            for(int j=1;j<=available;++j) {
                const double k=side==0?1.0-j:distance+j-1.0;
                const double observed=sample(side==0?start-j:end+j,c)*sign;
                const double predicted=y0*std::cos(omega*k)+sineCoefficient*std::sin(omega*k);
                const double e=predicted-observed;
                if(std::abs(e)>0.025*p) return false;
                error+=e*e;energy+=observed*observed;
            }
        }
        return error<=std::max(1.e-18,energy*0.0001);
    }
    static double prediction(int k,double d,double y0,double y1,double m0,double m1,
                             int algorithm,double omega,double sineCoefficient) noexcept {
        if(algorithm==1) return y0*std::cos(omega*k)+sineCoefficient*std::sin(omega*k);
        const double t=k/d,t2=t*t,t3=t2*t;
        return (2*t3-3*t2+1)*y0+(t3-2*t2+t)*d*m0+
               (-2*t3+3*t2)*y1+(t3-t2)*d*m1;
    }
    void publish(float outputPeak,float correctionPeak,float active) noexcept {
        clipEvents.store(clips_,std::memory_order_relaxed);
        repairedSamples.store(repairs_,std::memory_order_relaxed);
        skippedEvents.store(skips_,std::memory_order_relaxed);
        peak.store(outputPeak,std::memory_order_relaxed);
        repairPeak.store(correctionPeak,std::memory_order_relaxed);
        activity.store(active,std::memory_order_relaxed);
    }
    double rate_=48000;
    int delay_=288,context_=36;
    bool validRate_=false;
    std::size_t ringSize_=0;
    std::int64_t position_=0;
    std::array<std::int64_t,2> skipUntil_{};
    std::array<bool,2> suppress_{};
    std::array<double,2> suppressLevel_{};
    std::vector<Pair> dry_,estimate_;
    Settings settings_{};
    Ramp amount_,output_,enabled_,listen_;
    std::uint64_t clips_=0,repairs_=0,skips_=0;
};
} // namespace hem
