#include "../Source/DSP/AlgorithmicReverbCore.h"
#if defined(__GNUC__) && !defined(__clang__)
// The shared allocation probe intentionally implements new/delete with
// malloc/free; GCC 14 diagnoses that valid replacement when it inlines it.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmismatched-new-delete"
#endif
#include "FoundationAllocationProbe.h"
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif
#include <bit>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

namespace {
using Core = hem::AlgorithmicReverbCore;
using Settings = hem::AlgorithmicReverbSettings;
constexpr double pi = 3.14159265358979323846;
int assertions = 0;

void require(bool condition, const char* message) {
    ++assertions;
    if (!condition) throw std::runtime_error(message);
}

struct Counting {
    Counting() { allocationProbe::testThread=true; allocationProbe::enabled.store(true); }
    ~Counting() { allocationProbe::enabled.store(false); allocationProbe::testThread=false; }
};

struct Signal {
    std::vector<float> left, right;
    explicit Signal(int n) : left(std::size_t(n)), right(std::size_t(n)) {}
};

void process(Core& core, Signal& signal, const Settings& settings, int block = 127) {
    for (int start=0; start<int(signal.left.size()); start+=block) {
        const int count=std::min(block,int(signal.left.size())-start);
        Counting countAllocations;
        core.process(signal.left.data()+start,signal.right.data()+start,count,settings);
    }
}

Signal impulse(const Settings& settings, double rate=24000, double seconds=2, bool stereoImpulse=false, int block=127) {
    Core core;
    core.prepare(rate);
    Signal signal(int(rate*seconds));
    signal.left[0]=1;
    if (stereoImpulse) signal.right[0]=1;
    process(core,signal,settings,block);
    return signal;
}

double energy(const Signal& signal, int begin=0, int end=-1, bool right=false) {
    const auto& data=right?signal.right:signal.left;
    if (end<0) end=int(data.size());
    double sum=0;
    for (int i=begin;i<end;++i) sum+=double(data[std::size_t(i)])*data[std::size_t(i)];
    return sum;
}

double difference(const Signal& a, const Signal& b) {
    double sum=0;
    for(std::size_t i=0;i<a.left.size();++i) {
        const double l=double(a.left[i])-b.left[i],r=double(a.right[i])-b.right[i];
        sum+=l*l+r*r;
    }
    return sum;
}

int onset(const Signal& signal) {
    for(std::size_t i=0;i<signal.left.size();++i)
        if(std::abs(signal.left[i])+std::abs(signal.right[i])>1e-8f) return int(i);
    return -1;
}

void finiteBounded(const Signal& signal,float maximum=8) {
    for(std::size_t i=0;i<signal.left.size();++i)
        require(std::isfinite(signal.left[i])&&std::isfinite(signal.right[i])&&
                std::abs(signal.left[i])<maximum&&std::abs(signal.right[i])<maximum,
                "Output must be finite and bounded");
}

void testNeutralAndRates() {
    for(double rate:{8000.,11025.,22050.,44100.,48000.,96000.,192000.}) {
        Core core;
        core.prepare(rate);
        Settings p;
        Signal dry(2048);
        for(std::size_t i=0;i<dry.left.size();++i) {
            dry.left[i]=i%17==0?-0.0f:float(0.5*std::sin(double(i)*0.023));
            dry.right[i]=float(0.3*std::cos(double(i)*0.017));
        }
        auto original=dry;
        process(core,dry,p);
        for(std::size_t i=0;i<dry.left.size();++i) {
            require(std::bit_cast<std::uint32_t>(dry.left[i])==std::bit_cast<std::uint32_t>(original.left[i]),"Neutral left is not bit-exact");
            require(std::bit_cast<std::uint32_t>(dry.right[i])==std::bit_cast<std::uint32_t>(original.right[i]),"Neutral right is not bit-exact");
        }
        require(core.latency()==0,"Dry path must report zero latency");
        p.mix=100;
        auto wet=impulse(p,rate,1.5);
        finiteBounded(wet);
        require(onset(wet)>0,"Reverb onset must follow the source impulse");
        require(energy(wet,int(rate*0.1),int(rate*0.6))>0.00001,"Actual reverberant tail is missing");
        require(energy(wet,int(rate*1.2))<energy(wet,int(rate*0.1),int(rate*0.4)),"Impulse tail did not decay");
        require(energy(wet,0,-1,true)>0.00001,"Left impulse did not spread to stereo right");
        std::cout<<"PASS rate "<<rate<<" Hz, onset "<<onset(wet)<<" samples, energy "<<energy(wet)<<'\n';
    }
}

void testControls() {
    Settings base;
    base.mix=100;
    auto reference=impulse(base);
    auto changed=base;
    changed.size=35;
    auto small=impulse(changed);
    changed.size=200;
    auto large=impulse(changed);
    require(onset(small)<onset(reference)&&onset(large)>onset(reference),"Size does not scale room travel time");
    require(difference(small,large)>0.01,"Size has no audible effect");
    changed=base;
    changed.decay=0.3f;
    auto shortTail=impulse(changed);
    changed.decay=4;
    auto longTail=impulse(changed);
    require(energy(longTail,24000)>energy(shortTail,24000)*1000,"Decay control does not extend actual tail energy");
    changed=base;
    changed.predelay=125;
    auto delayed=impulse(changed);
    require(onset(delayed)-onset(reference)==3000,"Predelay is not the requested 125 ms");
    double shiftError=0;
    for(std::size_t i=3000;i<delayed.left.size();++i) {
        shiftError+=std::abs(double(delayed.left[i])-reference.left[i-3000]);
        shiftError+=std::abs(double(delayed.right[i])-reference.right[i-3000]);
    }
    require(shiftError<0.001,"Predelay must shift the complete stationary wet response");
    changed.predelay=250;
    require(onset(impulse(changed))-onset(reference)==6000,"Maximum predelay truncated");
    changed=base;
    changed.diffusion=0;
    auto sparse=impulse(changed);
    changed.diffusion=100;
    auto dense=impulse(changed);
    require(difference(sparse,dense)>0.01,"Diffusion control is inert");
    int sparseCount=0,denseCount=0;
    for(int i=0;i<6000;++i) {
        sparseCount+=std::abs(sparse.left[std::size_t(i)])>1e-6f;
        denseCount+=std::abs(dense.left[std::size_t(i)])>1e-6f;
    }
    require(denseCount>sparseCount,"Diffusion did not increase early echo density");
    changed=base;
    changed.modDepth=5;
    auto modulated=impulse(changed);
    require(difference(reference,modulated)>0.01,"Modulation depth is inert");
    changed.modRate=4;
    require(difference(modulated,impulse(changed))>0.01,"Modulation rate is inert with nonzero depth");
    changed=base;
    changed.modRate=4;
    require(difference(reference,impulse(changed))==0,"Rate affected audio at zero modulation depth");
    changed=base;
    changed.damping=500;
    auto dark=impulse(changed);
    changed.damping=20000;
    auto bright=impulse(changed);
    auto roughness=[](const Signal& s) {
        double value=0;
        for(std::size_t i=2401;i<s.left.size();++i) {
            const double d=double(s.left[i])-s.left[i-1]; value+=d*d;
        }
        return value/energy(s,2400);
    };
    require(roughness(bright)>roughness(dark)*2,"Damping did not selectively shorten high-frequency energy");
    changed=base;
    changed.mix=25;
    auto quarter=impulse(changed);
    require(quarter.left[0]==0.75f,"Mix dry gain is not linear and immediate on initialization");
    for(std::size_t i=1;i<quarter.left.size();++i)
        require(std::abs(quarter.left[i]-reference.left[i]*0.25f)<1e-7f,"Mix wet gain does not match 25 percent");
    changed.soloWet=true;
    require(difference(reference,impulse(changed))==0,"Solo Wet must override Mix");
    changed=base;
    changed.output=-6;
    const auto attenuated=impulse(changed);
    require(std::abs(10*std::log10(energy(attenuated)/energy(reference))+6)<0.0001,"Output gain is not calibrated in dB");
    changed.output=6;
    require(std::abs(10*std::log10(energy(impulse(changed))/energy(reference))-6)<0.0001,"Positive output gain incorrect");
    changed=base;
    changed.algorithm=1;
    auto room=impulse(changed);
    changed.algorithm=2;
    auto plate=impulse(changed);
    require(difference(reference,room)>0.01&&difference(room,plate)>0.01&&difference(reference,plate)>0.01,"Algorithm modes are not genuinely distinct");
    require(onset(plate)<onset(room)&&onset(room)<onset(reference),"Algorithm early reflection tuning is not distinct");
    require(Core::tailSeconds(base)>base.decay&&Core::tailSeconds(Settings{.decay=20})>=30,"Conservative host tail estimate incorrect");
    std::cout<<"PASS all controls, modes, calibrated mix/output, diffusion density "<<sparseCount<<" -> "<<denseCount<<", damping roughness "<<roughness(dark)<<" -> "<<roughness(bright)<<'\n';
}

void testRt60() {
    // Low-pass the wet impulse, then regress the Schroeder decay between -5
    // and -25 dB. Nominal RT60 specifies low frequencies; damping shortens HF.
    Settings p;
    p.mix=100; p.decay=2; p.damping=20000; p.diffusion=70;
    for(int mode=0;mode<3;++mode) {
        p.algorithm=mode;
        auto response=impulse(p,48000,5);
        std::vector<double> schroeder(response.left.size());
        double state=0;
        const double alpha=1-std::exp(-2*pi*300/48000);
        for(std::size_t i=0;i<response.left.size();++i) {
            state+=alpha*(response.left[i]-state);
            schroeder[i]=state*state;
        }
        for(std::size_t i=schroeder.size()-1;i>0;--i) schroeder[i-1]+=schroeder[i];
        double sx=0,sy=0,sxx=0,sxy=0,count=0;
        const double total=schroeder[0];
        for(std::size_t i=0;i<schroeder.size();i+=32) {
            const double db=10*std::log10(std::max(schroeder[i]/total,1e-20));
            if(db<-5&&db>-25) {
                const double t=double(i)/48000;
                sx+=t;sy+=db;sxx+=t*t;sxy+=t*db;++count;
            }
        }
        require(count>10,"Not enough decay samples for RT60 estimate");
        const double slope=(count*sxy-sx*sy)/(count*sxx-sx*sx);
        const double rt60=-60/slope;
        std::cout<<"Measured mode "<<mode<<" low-frequency RT60: "<<rt60<<" s (nominal 2 s)\n";
        require(rt60>1.35&&rt60<2.65,"Nominal decay does not match measured low-frequency RT60");
    }
}

void testBypassResetAndStereo() {
    Core core;
    core.prepare(48000);
    Settings p;
    p.mix=100; p.output=6; p.bypass=true;
    Signal fresh(4096);
    fresh.left[0]=0.37f;fresh.right[0]=-0.83f;
    const auto freshOriginal=fresh;
    process(core,fresh,p);
    require(difference(fresh,freshOriginal)==0,"Initial bypass is not exactly dry");
    p.bypass=false;
    Signal constant(48000);
    std::fill(constant.left.begin(),constant.left.end(),0.1f);
    std::fill(constant.right.begin(),constant.right.end(),0.1f);
    process(core,constant,p);
    p.bypass=true;
    Signal fade(2400);
    std::fill(fade.left.begin(),fade.left.end(),0.1f);
    std::fill(fade.right.begin(),fade.right.end(),0.1f);
    process(core,fade,p,31);
    require(std::abs(fade.left[0]-constant.left.back())<0.02f,"Runtime bypass clicks at entry");
    for(std::size_t i=480;i<fade.left.size();++i)
        require(fade.left[i]==0.1f&&fade.right[i]==0.1f,"Settled bypass is not exactly dry");
    require(core.wetPeak.load()>0,"Bypass prematurely cleared the internal tail");
    p.bypass=false;
    Signal returnFade(2400);
    std::fill(returnFade.left.begin(),returnFade.left.end(),0.1f);
    std::fill(returnFade.right.begin(),returnFade.right.end(),0.1f);
    process(core,returnFade,p);
    require(std::abs(returnFade.left[0]-0.1f)<0.01f,"Bypass release clicks at entry");
    { Counting countAllocations; core.reset(); }
    Signal silence(48000);
    process(core,silence,p);
    require(energy(silence)==0&&energy(silence,0,-1,true)==0,"Reset left a stale tail");
    require(core.peak.load()==0&&core.wetPeak.load()==0&&core.tailLevelDb.load()==-120,"Reset/silence telemetry incorrect");
    auto first=impulse(p,48000,1);
    core.reset();
    Signal afterReset(48000);afterReset.left[0]=1;
    process(core,afterReset,p);
    require(difference(first,afterReset)==0,"Reset is not deterministic");
    core.prepare(8000);
    Signal changedRate(8000);changedRate.left[0]=1;
    process(core,changedRate,p);
    require(difference(changedRate,impulse(p,8000,1))==0,"Reprepare retained old-rate state");
    Core sideCore;sideCore.prepare(24000);
    Signal side(24000);side.left[0]=1;side.right[0]=-1;
    process(sideCore,side,p);
    require(energy(side)+energy(side,0,-1,true)>0.001,"Anti-phase stereo input was collapsed to mono");
    auto stereo=impulse(p,24000,1,true);
    Core mono;mono.prepare(24000);
    std::vector<float> monaural(24000);monaural[0]=1;
    for(int start=0;start<24000;start+=71) {
        Counting countAllocations;
        mono.process(monaural.data()+start,nullptr,std::min(71,24000-start),p);
    }
    for(std::size_t i=0;i<monaural.size();++i)
        require(std::abs(monaural[i]-(stereo.left[i]+stereo.right[i])*0.5f)<1e-6f,"Mono output is not consistent with stereo fold-down");
    Core aliased;aliased.prepare(24000);
    auto same=monaural;std::fill(same.begin(),same.end(),0);same[0]=1;
    {Counting countAllocations;aliased.process(same.data(),same.data(),int(same.size()),p);}
    require(same==monaural,"Aliased stereo pointers are not handled as mono");
    std::cout<<"PASS initial/settled bypass, 10 ms transitions, tail persistence, reset/reprepare, mono and aliased buffers\n";
}

void testAutomationAndDefensiveInput() {
    Core core;
    core.prepare(8000);
    Settings p;p.mix=100;
    std::array<float,97> left{},right{};
    std::uint32_t rng=0x12345678;
    double peak=0;
    for(int block=0;block<11000;++block) { // over two minutes, extreme moving delays
        if(block%31==0) {
            const bool odd=(block/31)%2!=0;
            p.size=odd?25:200;p.decay=odd?0.1f:20;p.predelay=odd?0:250;
            p.diffusion=odd?0:100;p.modDepth=10;p.modRate=odd?0.01f:5;
            p.damping=odd?500:20000;p.mix=odd?100:31;p.output=odd?6:-24;
            p.algorithm=(block/31)%3;p.soloWet=block%93==0;p.bypass=block%217==0;
        }
        for(std::size_t i=0;i<left.size();++i) {
            rng=rng*1664525u+1013904223u;
            left[i]=float(double(rng)/4294967296.0-0.5)*0.3f;
            right[i]=std::sin(float(block*97+int(i))*0.032f)*0.2f;
        }
        { Counting countAllocations;core.process(left.data(),right.data(),int(left.size()),p); }
        for(std::size_t i=0;i<left.size();++i) {
            require(std::isfinite(left[i])&&std::isfinite(right[i]),"Automation generated nonfinite audio");
            peak=std::max({peak,double(std::abs(left[i])),double(std::abs(right[i]))});
        }
    }
    require(peak<4,"Extreme automation caused runaway feedback");
    std::cout<<"PASS 133-second full-control automation stress, peak "<<peak<<'\n';
    // Static worst-case long decay/modulation must remain stable too.
    for(int mode=0;mode<3;++mode) {
        core.reset();p=Settings{};p.mix=100;p.decay=20;p.diffusion=100;p.modDepth=10;p.modRate=5;p.algorithm=mode;
        p.size=mode==1?25:200;
        float maxTail=0;
        for(int block=0;block<5000;++block) {
            left.fill(0);right.fill(0);if(block==0)left[0]=1;
            { Counting countAllocations;core.process(left.data(),right.data(),int(left.size()),p); }
            for(float x:left){require(std::isfinite(x),"Long modulation tail nonfinite");maxTail=std::max(maxTail,std::abs(x));}
        }
        require(maxTail<2,"Long maximum-depth/rate feedback became unstable");
        require(core.wetPeak.load()<1e-5f,"Long modulated tail did not decay to silence");
    }
    const float nan=std::numeric_limits<float>::quiet_NaN(), inf=std::numeric_limits<float>::infinity();
    p=Settings{};p.mix=100;p.size=nan;p.decay=-inf;p.predelay=inf;p.diffusion=nan;p.modDepth=nan;p.modRate=inf;p.damping=nan;p.output=nan;p.algorithm=std::numeric_limits<int>::max();
    core.reset();
    Signal hostile(16000);
    hostile.left[0]=nan;hostile.right[0]=inf;hostile.left[1]=-inf;
    hostile.left[2]=std::numeric_limits<float>::max();
    process(core,hostile,p);
    finiteBounded(hostile,128);
    require(std::isfinite(core.peak.load())&&std::isfinite(core.tailLevelDb.load()),"Nonfinite telemetry");
    core.reset();p=Settings{};p.mix=100;p.size=-500;p.decay=-500;p.predelay=-500;p.diffusion=-500;p.modDepth=-500;p.modRate=-500;p.damping=-500;p.output=-500;
    Signal outOfRange(8000);outOfRange.left[0]=1;process(core,outOfRange,p);finiteBounded(outOfRange);
    core.prepare(nan);core.reset();
    Signal fallback(4096);fallback.left[0]=1;process(core,fallback,p);finiteBounded(fallback);
    const auto before=core.peak.load();
    { Counting countAllocations;core.process(nullptr,nullptr,128,p);core.process(left.data(),right.data(),0,p);core.process(left.data(),right.data(),-1,p); }
    require(core.peak.load()==before,"Empty process must not mutate telemetry");
    require(difference(impulse(p,1000,1),impulse(p,8000,0.125))==0,"Low unsupported rates are not safely clamped");
    Core unprepared;
    left.fill(0.25f);right.fill(-0.25f);left[0]=nan;
    {Counting countAllocations;unprepared.process(left.data(),right.data(),int(left.size()),p);}
    require(left[0]==0&&left[1]==0.25f&&right[0]==-0.25f,"Unprepared defensive dry fallback incorrect");
    std::cout<<"PASS long modulation tails, invalid settings/input/rates, empty and unprepared lifecycle\n";
}

void testBlockPartitionAndSmoothing() {
    Settings p;p.mix=100;p.modDepth=7;p.modRate=3;
    const auto a=impulse(p,48000,2,false,1);
    for(int block:{7,64,511,4096})
        require(difference(a,impulse(p,48000,2,false,block))==0,"DSP depends on block partition");
    Core core;core.prepare(48000);
    p=Settings{};
    Signal aDry(64);std::fill(aDry.left.begin(),aDry.left.end(),0.5f);std::fill(aDry.right.begin(),aDry.right.end(),0.5f);
    process(core,aDry,p);
    p.mix=100;p.output=-24;
    Signal transition(2400);std::fill(transition.left.begin(),transition.left.end(),0.5f);std::fill(transition.right.begin(),transition.right.end(),0.5f);
    process(core,transition,p);
    require(std::abs(transition.left[0]-0.5f)<0.002f,"Continuous controls jump instead of smoothing");
    require(core.peak.load()>=0&&core.wetPeak.load()>0&&core.tailLevelDb.load()>-120,"Real tail telemetry missing");
    std::cout<<"PASS block-size independence, smoothed controls, measured tail telemetry\n";
}
} // namespace

int main() {
    try {
        testNeutralAndRates();
        testControls();
        testRt60();
        testBypassResetAndStereo();
        testAutomationAndDefensiveInput();
        testBlockPartitionAndSmoothing();
        require(allocationProbe::count.load()==0,"Realtime processing allocated memory");
        std::cout<<"PASS AlgorithmicReverb: "<<assertions<<" numerical assertions; "<<allocationProbe::count.load()<<" process allocations\n";
        return 0;
    } catch(const std::exception& e) {
        allocationProbe::enabled.store(false);
        std::cerr<<"FAIL AlgorithmicReverb: "<<e.what()<<'\n';
        return 1;
    }
}
