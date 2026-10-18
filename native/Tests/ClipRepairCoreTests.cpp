#include "../Source/DSP/ClipRepairCore.h"
#include "FoundationAllocationProbe.h"
#include <bit>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <string>
#include <vector>

namespace {
using Core=hem::ClipRepairCore;
using Settings=hem::ClipRepairSettings;
using Pair=Core::Pair;
int checks=0;
void require(bool condition,const char* message) {
    ++checks;
    if(!condition) { std::fprintf(stderr,"FAIL: %s\n",message);std::exit(1); }
}
bool same(float a,float b) { return std::bit_cast<std::uint32_t>(a)==std::bit_cast<std::uint32_t>(b); }
struct Result {
    std::vector<float> left,right;
    std::uint64_t events=0,repairs=0,skips=0;
    int delay=0;
};
Result run(const std::vector<float>& input,const std::vector<float>& second,
           double rate,Settings settings,int block=0) {
    Core core;core.prepare(rate,settings);
    Result result;result.delay=core.latency();
    result.left=input;result.right=second;
    result.left.resize(input.size()+static_cast<std::size_t>(result.delay),0);
    result.right.resize(result.left.size(),0);
    const int total=static_cast<int>(result.left.size());
    if(block<=0) block=total;
    for(int pos=0;pos<total;pos+=block)
        core.process(result.left.data()+pos,result.right.data()+pos,std::min(block,total-pos),settings);
    result.events=core.clipEvents.load();result.repairs=core.repairedSamples.load();result.skips=core.skippedEvents.load();
    return result;
}
std::vector<float> sine(double rate,double frequency,double amplitude,int n,double phase=0.17) {
    std::vector<float> data(static_cast<std::size_t>(n));
    for(int i=0;i<n;++i) data[static_cast<std::size_t>(i)]=
        static_cast<float>(amplitude*std::sin(2*hem::pi*frequency*i/rate+phase));
    return data;
}
std::vector<float> clipped(std::vector<float> data,float ceiling=0.8f) {
    for(auto& x:data) x=std::clamp(x,-ceiling,ceiling);
    return data;
}
void checkDelayed(const Result& result,const std::vector<float>& left,const std::vector<float>& right) {
    for(int i=0;i<result.delay;++i)
        require(result.left[static_cast<std::size_t>(i)]==0&&result.right[static_cast<std::size_t>(i)]==0,
                "fixed delay begins with silence");
    for(std::size_t i=0;i<left.size();++i)
        require(same(result.left[i+static_cast<std::size_t>(result.delay)],left[i])&&
                same(result.right[i+static_cast<std::size_t>(result.delay)],right[i]),
                "neutral/bypass preserves delayed finite samples bit-for-bit");
}

void neutralityAndLatency() {
    for(double rate:{8000.,11025.,22050.,44100.,47999.5,48000.,88200.,96000.,192000.,384000.}) {
        auto a=clipped(sine(rate,257,1.5,3001));
        auto b=sine(rate,137,0.7,3001);
        a[17]=-0.f;b[29]=-0.f;a[37]=2.5f;
        auto result=run(a,b,rate,{});
        require(result.delay==static_cast<int>(std::floor(rate*0.006)),"reported fixed sample delay");
        require(result.delay/rate<=0.006,"delay never exceeds six milliseconds");
        require(result.repairs==0,"amount zero never counts repair");
        checkDelayed(result,a,b);
        Settings bypass;bypass.amount=100;bypass.output=6;bypass.listenRepair=true;bypass.bypass=true;
        result=run(a,b,rate,bypass,31);
        checkDelayed(result,a,b);
        require(result.repairs==0,"static bypass never counts repair");
    }
    Core unprepared;
    float left=0.25f,right=-0.5f;
    unprepared.process(&left,&right,1,{});
    require(left==0.25f&&right==-0.5f,"unprepared core safely leaves input alone");
    unprepared.process(nullptr,nullptr,999,{});
}

void knownCleanMse() {
    double worstFast=0,worstPrecise=0;
    int improvedCases=0,conservativeCases=0;
    for(double rate:{8000.,44100.,48000.,96000.,192000.,384000.})
        for(int algorithm:{0,1})
            for(double frequency:{125.,257.,997.,2003.})
                for(double amplitude:{1.05,1.25,1.65}) {
                    const auto clean=sine(rate,frequency,amplitude,static_cast<int>(rate*0.13));
                    const auto damaged=clipped(clean);
                    Settings s;s.amount=100;s.threshold=-2;s.interpolationLength=4;s.algorithm=algorithm;
                    const auto result=run(damaged,damaged,rate,s,127);
                    double before=0,after=0;
                    for(std::size_t i=static_cast<std::size_t>(rate*0.01);
                        i<clean.size()-static_cast<std::size_t>(rate*0.01);++i) {
                        before+=std::pow(static_cast<double>(damaged[i])-clean[i],2);
                        after+=std::pow(static_cast<double>(result.left[i+static_cast<std::size_t>(result.delay)])-clean[i],2);
                    }
                    if(rate==8000&&frequency>500) {
                        require(after==before&&result.repairs==0,"undersampled/ambiguous high peaks conservatively unchanged");
                        ++conservativeCases;
                    } else {
                        const double ratio=after/before;
                        require(ratio<(algorithm==0?0.02:0.00001),"repair improves reference sine MSE, across rates/frequencies/levels");
                        require(result.repairs>0&&result.events>0,"MSE improvement has measured repair and event counts");
                        (algorithm==0?worstFast:worstPrecise)=std::max(algorithm==0?worstFast:worstPrecise,ratio);
                        ++improvedCases;
                    }
                    require(result.skips<=result.events,"skipped events are a subset of detected candidate events");
                }
    std::printf("Reference sine matrix: %d improved, %d conservative; worst MSE ratios Fast %.8g, Precise %.8g\n",
                improvedCases,conservativeCases,worstFast,worstPrecise);
}

void preserveUnclippedAndTransients() {
    for(double rate:{8000.,48000.,384000.}) for(int algorithm:{0,1}) {
        const int n=static_cast<int>(rate*0.1);
        auto clean=sine(rate,37,1.2,n);
        auto mixed=sine(rate,731,0.74,n,0.22);
        for(int i=0;i<n;++i) {
            mixed[static_cast<std::size_t>(i)]+=static_cast<float>(0.41*std::sin(2*hem::pi*1703*i/rate));
        }
        Settings s;s.threshold=-12;s.amount=100;s.algorithm=algorithm;s.interpolationLength=4;
        const auto result=run(clean,mixed,rate,s,53);
        checkDelayed(result,clean,mixed);
        require(result.repairs==0,"unclipped high peaks and multitones are not modified");

        std::vector<float> transient(1200,0),other(1200,0);
        transient[100]=1.5f;transient[250]=-1.3f;
        for(int i=0;i<1200;++i) {
            const double distance=(i-700)/13.;
            transient[static_cast<std::size_t>(i)]+=static_cast<float>(1.7*std::exp(-distance*distance));
        }
        // Legitimate square pulse / hard step: no smooth shoulder evidence.
        std::fill(other.begin()+150,other.begin()+170,1.1f);
        std::fill(other.begin()+600,other.begin()+1000,-0.9f);
        const auto transients=run(transient,other,rate,s);
        checkDelayed(transients,transient,other);
        require(transients.repairs==0,"clean impulses, Gaussian transients and square pulses preserved");
        require(transients.skips>0,"ambiguous flat pulses are visibly skipped");
    }
}

void parametersAndConservativeSkips() {
    const auto clean=sine(48000,257,1.25,12000);
    const auto damaged=clipped(clean);
    Settings s;s.threshold=-2;s.amount=100;s.interpolationLength=4;
    const auto full=run(damaged,damaged,48000,s);
    s.amount=50;const auto half=run(damaged,damaged,48000,s);
    double fullEnergy=0,halfEnergy=0;
    for(std::size_t i=0;i<damaged.size();++i) {
        const auto k=i+static_cast<std::size_t>(full.delay);
        const double fd=static_cast<double>(full.left[k])-damaged[i];
        const double hd=static_cast<double>(half.left[k])-damaged[i];
        require(std::abs(hd-0.5*fd)<2.e-7,"amount scales the estimated correction");
        fullEnergy+=fd*fd;halfEnergy+=hd*hd;
    }
    require(halfEnergy>0.249*fullEnergy&&halfEnergy<0.251*fullEnergy,"amount materially changes energy");
    s.amount=100;s.threshold=0;
    const auto threshold=run(damaged,damaged,48000,s);
    checkDelayed(threshold,damaged,damaged);
    require(threshold.events==0,"threshold excludes lower-level clipping");
    s.threshold=-2;s.interpolationLength=0.1f;
    const auto shortWindow=run(damaged,damaged,48000,s);
    checkDelayed(shortWindow,damaged,damaged);
    require(shortWindow.events>0&&shortWindow.skips==shortWindow.events,"too-long plateaus counted once and skipped");
    s.interpolationLength=4;s.algorithm=0;
    const auto fast=run(damaged,damaged,48000,s);
    bool algorithmChanges=false;
    for(std::size_t i=0;i<fast.left.size();++i)
        algorithmChanges=algorithmChanges||std::abs(fast.left[i]-full.left[i])>1.e-4;
    require(algorithmChanges,"Fast and Precise are materially different estimators");

    s.algorithm=1;s.listenRepair=true;
    const auto audition=run(damaged,damaged,48000,s);
    for(std::size_t i=0;i<damaged.size();++i) {
        const auto k=i+static_cast<std::size_t>(full.delay);
        require(std::abs(audition.left[k]-(full.left[k]-damaged[i]))<2.e-7,
                "Listen Repair outputs aligned difference only");
    }
    s.output=6;
    const auto loudRepair=run(damaged,damaged,48000,s);
    for(std::size_t i=0;i<loudRepair.left.size();++i)
        require(std::abs(loudRepair.left[i]-audition.left[i]*hem::gain(6))<5.e-7,
                "output gain works on audition contribution");

    auto nearFlat=damaged;
    for(std::size_t i=0;i<nearFlat.size();++i)
        if(std::abs(nearFlat[i])==0.8f) nearFlat[i]+=i%2?2.e-7f:-2.e-7f;
    s.output=0;s.listenRepair=false;
    const auto near=run(nearFlat,nearFlat,48000,s);
    require(near.repairs>0,"near-flat clipping with tiny quantization error is detected");

    // Zero-centred sinusoid hypothesis is deliberately not asserted for arbitrary
    // flat material: a mismatching shoulder must skip, never silently repair.
    std::vector<float> ambiguous(1000,0);
    ambiguous[396]=0.1f;ambiguous[397]=0.2f;ambiguous[398]=0.3f;ambiguous[399]=0.4f;
    std::fill(ambiguous.begin()+400,ambiguous.begin()+408,0.8f);
    ambiguous[408]=0.6f;ambiguous[409]=0.3f;ambiguous[410]=0.1f;
    const auto skip=run(ambiguous,ambiguous,48000,s);
    checkDelayed(skip,ambiguous,ambiguous);
    require(skip.events==2&&skip.skips==2&&skip.repairs==0,"ambiguous model emits truthful skipped telemetry");
}

void observerAndStereoIsolation() {
    Core core;Settings s;s.threshold=-2;s.amount=100;s.interpolationLength=4;
    core.prepare(48000,s);
    auto a=clipped(sine(48000,997,1.5,12000));
    auto b=sine(48000,191,0.32,12000);
    const auto dryA=a,dryB=b;
    a.resize(a.size()+static_cast<std::size_t>(core.latency()),0);b.resize(a.size(),0);
    std::size_t index=0;std::uint64_t corrected=0;
    core.processObserved(a.data(),b.data(),static_cast<int>(a.size()),s,
        [&](const Pair& dry,const Pair& wet,const Pair& delta,float activity) noexcept {
            if(index>=static_cast<std::size_t>(core.latency())) {
                const auto source=index-static_cast<std::size_t>(core.latency());
                if(source<dryA.size()) {
                    require(same(dry[0],dryA[source])&&same(dry[1],dryB[source]),"observer dry is output-time aligned");
                    require(same(wet[1],dryB[source])&&delta[1]==0,"repair does not cross stereo channels");
                }
            }
            require(wet[0]==a[index]&&wet[1]==b[index],"normal wet observer equals audible output");
            require(delta[0]==wet[0]-dry[0]&&delta[1]==wet[1]-dry[1],"observer delta matches actual float correction");
            require(activity==((delta[0]!=0||delta[1]!=0)?1.f:0.f),"per-frame observer activity is measured");
            corrected+=static_cast<unsigned>(delta[0]!=0)+static_cast<unsigned>(delta[1]!=0);
            ++index;
        });
    require(index==a.size(),"one observer callback per output frame");
    require(corrected==core.repairedSamples.load()&&corrected>0,"repair counter counts altered channel samples exactly");
    require(core.repairPeak.load()>0&&core.peak.load()>0&&core.activity.load()==1,"live block telemetry populated");

    // Output gain alone must not inflate repair counts or delta display.
    s.amount=0;s.output=-6;core.prepare(48000,s);
    auto neutral=sine(48000,333,0.4,2000),right=neutral;
    core.processObserved(neutral.data(),right.data(),static_cast<int>(neutral.size()),s,
        [](const Pair&,const Pair&,const Pair& delta,float active) noexcept {
            require(delta[0]==0&&delta[1]==0&&active==0,"output gain alone is not a repair");
        });
    require(core.repairedSamples.load()==0,"gain-only sample count remains zero");
}

void rampsPartitionAndReset() {
    const auto input=clipped(sine(48000,257,1.25,12000));
    Settings s;s.threshold=-2;s.amount=100;s.interpolationLength=4;
    const auto one=run(input,input,48000,s);
    const auto single=run(input,input,48000,s,1);
    const auto blocks=run(input,input,48000,s,113);
    require(one.left==single.left&&one.left==blocks.left,"streaming repair is block partition invariant");
    require(one.events==single.events&&one.skips==blocks.skips&&one.repairs==blocks.repairs,
            "counters are block partition invariant");

    Core core;Settings zero;core.prepare(48000,zero);
    std::vector<float> left(1400,0.25f),right=left;
    core.process(left.data(),right.data(),900,zero);
    Settings boosted;boosted.output=6;
    std::fill(left.begin(),left.end(),0.25f);right=left;
    core.process(left.data(),right.data(),500,boosted);
    require(left[0]>0.25f&&left[0]<0.251f,"output gain begins a smooth 10 ms ramp");
    for(int i=1;i<480;++i) require(left[static_cast<std::size_t>(i)]>=left[static_cast<std::size_t>(i-1)]&&
        left[static_cast<std::size_t>(i)]-left[static_cast<std::size_t>(i-1)]<0.001f,"output ramp bounded and monotonic");
    require(std::abs(left[499]-0.25f*hem::gain(6))<1.e-7,"output ramp reaches target");
    boosted.bypass=true;
    std::fill(left.begin(),left.end(),0.25f);right=left;
    core.process(left.data(),right.data(),500,boosted);
    require(left[0]>0.49f&&left[0]<0.25f*hem::gain(6),"bypass starts a smooth ramp");
    require(left[499]==0.25f,"bypass settles to exact delayed dry");
    for(int i=1;i<480;++i) require(std::abs(left[static_cast<std::size_t>(i)]-left[static_cast<std::size_t>(i-1)])<0.001f,
                                  "bypass ramp has no hard jump");

    core.prepare(48000,s);
    auto a=input,b=input;core.process(a.data(),b.data(),static_cast<int>(a.size()),s);
    const auto events=core.clipEvents.load(),repairs=core.repairedSamples.load();
    core.reset();require(core.clipEvents.load()==0&&core.repairedSamples.load()==0,"reset clears telemetry");
    auto c=input,d=input;core.process(c.data(),d.data(),static_cast<int>(c.size()),s);
    require(a==c&&b==d&&events==core.clipEvents.load()&&repairs==core.repairedSamples.load(),"reset is deterministic");
}

void invalidInputsAndAllocationSafety() {
    for(double rate:{0.,-1.,7999.,384001.,1.e300,std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN()}) {
        Core core;core.prepare(rate,{});
        require(!core.preparedRateWasValid()&&core.sampleRate()==48000&&core.latency()==288,"invalid sample rate has bounded 48 kHz fallback");
        Settings s;s.amount=std::numeric_limits<float>::infinity();s.threshold=std::numeric_limits<float>::quiet_NaN();
        s.output=std::numeric_limits<float>::quiet_NaN();s.interpolationLength=-1;s.algorithm=999;
        std::vector<float> a(1000,0),b(1000,0);
        a[0]=std::numeric_limits<float>::infinity();a[7]=std::numeric_limits<float>::quiet_NaN();
        b[12]=-std::numeric_limits<float>::infinity();
        core.process(a.data(),b.data(),1000,s);
        for(std::size_t i=0;i<a.size();++i) require(std::isfinite(a[i])&&std::isfinite(b[i]),"non-finite input/settings do not contaminate output");
        core.process(nullptr,b.data(),20,s);core.process(a.data(),nullptr,20,s);core.process(a.data(),b.data(),-1,s);
    }

    Core core;Settings s;s.amount=100;s.threshold=-2;s.interpolationLength=4;
    core.prepare(384000,s);
    std::array<float,512> a{},b{};
    std::uint64_t observed=0;
    allocationProbe::testThread=true;allocationProbe::count.store(0);allocationProbe::enabled.store(true);
    for(int block=0;block<400;++block) {
        for(std::size_t i=0;i<a.size();++i) {
            a[i]=std::clamp(static_cast<float>(1.5*std::sin(2*hem::pi*997*(block*512+static_cast<int>(i))/384000.)),-0.8f,0.8f);
            b[i]=a[i];
        }
        s.algorithm=block%2;s.amount=static_cast<float>(block%101);s.bypass=block%31==0;
        s.output=static_cast<float>(block%31)-24;s.listenRepair=block%19==0;
        core.processObserved(a.data(),b.data(),512,s,[&](const Pair&,const Pair&,const Pair&,float) noexcept {++observed;});
    }
    core.reset();
    allocationProbe::enabled.store(false);allocationProbe::testThread=false;
    require(allocationProbe::count.load()==0,"process, observer integration, parameter changes and reset allocate nothing");
    require(observed==400*512,"allocation stress processed every frame");
}
} // namespace

int main() {
    const auto began=std::chrono::steady_clock::now();
    neutralityAndLatency();knownCleanMse();preserveUnclippedAndTransients();
    parametersAndConservativeSkips();observerAndStereoIsolation();rampsPartitionAndReset();invalidInputsAndAllocationSafety();
    const auto elapsed=std::chrono::duration<double>(std::chrono::steady_clock::now()-began).count();
    std::printf("ClipRepairCore: %d checks passed in %.3f seconds\n",checks,elapsed);
}
