#pragma once
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace hem {
namespace modulation {
constexpr double pi=3.1415926535897932384626433832795;
inline float finite(float x,float fallback=0) noexcept{return std::isfinite(x)?x:fallback;}
inline float clamp(float x,float low,float high,float fallback=0) noexcept{return std::clamp(finite(x,fallback),low,high);}
inline double wrap(double p) noexcept{return p-std::floor(p);}
inline float bounded(float x) noexcept{return clamp(x,-32,32);}
struct Ramp {
    float current=0,target=0,step=0;int remaining=0,length=1;
    void reset(float x,int samples) noexcept{current=target=x;step=0;remaining=0;length=std::max(1,samples);}
    void set(float x) noexcept{if(x!=target){target=x;remaining=length;step=(target-current)/float(remaining);}}
    float next() noexcept{if(remaining>0){if(--remaining==0)current=target;else current+=step;}return current;}
};
class Delay {
public:
    void prepare(double rate,double seconds){data.assign(size_t(std::ceil(rate*seconds))+4,0);position=0;}
    void reset() noexcept{std::fill(data.begin(),data.end(),0);position=0;}
    float read(double samples)const noexcept {
        if(data.empty())return 0;
        const double delay=std::clamp(samples,1.,double(data.size()-2));
        double index=double(position)-delay;if(index<0)index+=double(data.size());
        const auto i=size_t(index);const float fraction=float(index-double(i));
        return data[i]+fraction*(data[(i+1)%data.size()]-data[i]);
    }
    void push(float x)noexcept{data[position]=bounded(x);if(++position==data.size())position=0;}
private:std::vector<float>data;size_t position=0;
};
struct Transport {double bpm=0;bool valid=false;};
struct Display {
    static constexpr size_t capacity=800;
    std::array<float,capacity>left{},right{};size_t count=0,cursor=0;
    std::uint64_t frames=0;double sampleRate=0;float rateHz=0,bpm=0;
    bool valid=false,tempoAvailable=false,syncActive=false;
};
// Bounded, lock-free audio-writer/UI-reader history. Points are actual values
// used by the audio engine, sampled at 200 Hz. No GUI clock or worker wakeup.
class Telemetry {
public:
    void prepare(double rate)noexcept{sampleRate.store(rate);interval=std::max(1,int(std::lround(rate/200)));reset();}
    void reset()noexcept{sequence.fetch_add(1);count.store(0);cursor.store(0);frames.store(0);hz.store(0);bpm.store(0);flags.store(0);sequence.fetch_add(1);until=0;audioFrames=0;}
    void sample(float l,float r)noexcept {
        ++audioFrames;if(++until<interval)return;until=0;
        sequence.fetch_add(1);const auto i=cursor.load();left[i].store(l);right[i].store(r);cursor.store((i+1)%Display::capacity);count.store(std::min(count.load()+1,Display::capacity));frames.store(audioFrames);sequence.fetch_add(1);
    }
    void status(float rate,Transport t,bool sync)noexcept{sequence.fetch_add(1);hz.store(rate);bpm.store(t.valid?float(t.bpm):0);flags.store(1u|(t.valid?2u:0u)|(sync&&t.valid?4u:0u));frames.store(audioFrames);sequence.fetch_add(1);}
    Display snapshot()const noexcept {
        Display d;
        for(int attempt=0;attempt<3;++attempt){const auto before=sequence.load();if(before&1u)continue;d.count=count.load();d.cursor=cursor.load();d.frames=frames.load();d.sampleRate=sampleRate.load();d.rateHz=hz.load();d.bpm=bpm.load();const auto f=flags.load();d.valid=(f&1)!=0;d.tempoAvailable=(f&2)!=0;d.syncActive=(f&4)!=0;for(size_t i=0;i<Display::capacity;++i){d.left[i]=left[i].load();d.right[i]=right[i].load();}if(before==sequence.load())return d;}
        return {};
    }
private:
    std::array<std::atomic<float>,Display::capacity>left{},right{};
    std::atomic<size_t>count{0},cursor{0};std::atomic<std::uint64_t>frames{0};
    std::atomic<unsigned>sequence{0},flags{0};std::atomic<double>sampleRate{0};std::atomic<float>hz{0},bpm{0};
    int interval=1,until=0;std::uint64_t audioFrames=0;
    static_assert(std::atomic<float>::is_always_lock_free&&std::atomic<double>::is_always_lock_free&&std::atomic<size_t>::is_always_lock_free&&std::atomic<std::uint64_t>::is_always_lock_free);
};
inline Transport sanitize(Transport t)noexcept{t.valid=t.valid&&std::isfinite(t.bpm)&&t.bpm>=20&&t.bpm<=400;if(!t.valid)t.bpm=0;return t;}
inline float rate(float freeRate,bool sync,Transport t)noexcept{return sync&&t.valid?float(t.bpm/60)*freeRate:freeRate;}
inline void requireRate(double sr){if(!std::isfinite(sr)||sr<8000||sr>192000)throw std::invalid_argument("Modulation sample rate must be 8-192 kHz");}
inline std::array<float,3> shapes(double phase)noexcept {
    const double p=wrap(phase),s=std::sin(2*pi*p);
    return {float(s),float(2/pi*std::asin(s)),float(std::tanh(3*s)/std::tanh(3.))};
}
inline float wave(double phase,const std::array<float,3>&weights)noexcept{
    const double p=wrap(phase);
    const double sine=(weights[0]!=0||weights[2]!=0)?std::sin(2*pi*p):0;
    float result=weights[0]*float(sine);
    if(weights[1]!=0)result+=weights[1]*float(1-4*std::abs(wrap(p+.25)-.5));
    if(weights[2]!=0)result+=weights[2]*float(std::tanh(3*sine)/.9950547536867305);
    return result;
}
inline void targetChoice(std::array<Ramp,3>&ramps,int value)noexcept{for(int i=0;i<3;++i)ramps[size_t(i)].set(i==value?1.f:0.f);}
inline std::array<float,3> next(std::array<Ramp,3>&ramps)noexcept{return {ramps[0].next(),ramps[1].next(),ramps[2].next()};}
inline float mix(float dry,float wet,float amount,float gain,float active)noexcept{
    if(active==0||(amount==0&&gain==1))return dry;
    const float result=dry+(wet-dry)*amount;
    return bounded(dry+active*(result*gain-dry));
}
} // namespace modulation

struct ChorusFlangerPhaserSettings {
    float depth=0,rate=.5f,feedback=0,mix=0,output=0,delay=12,center=1000;
    int mode=0,shape=0,stages=1;bool sync=false,bypass=false;
};
class ChorusFlangerPhaserCore {
public:
    void prepare(double sr){modulation::requireRate(sr);sampleRate=sr;for(auto&mode:delays)for(auto&d:mode)d.prepare(sr,.1);telemetry.prepare(sr);reset();}
    void reset()noexcept{for(auto&mode:delays)for(auto&d:mode)d.reset();allpass={};previous={};phase=0;first=true;peak.store(0);telemetry.reset();}
    int latency()const noexcept{return 0;}
    void process(float*left,float*right,int n,const ChorusFlangerPhaserSettings&raw,modulation::Transport transport={})noexcept {
        if(!left||n<=0||sampleRate<=0)return;
        using namespace modulation;transport=sanitize(transport);
        const int mode=std::clamp(raw.mode,0,2),shape=std::clamp(raw.shape,0,2),stages=std::clamp(raw.stages,0,3);
        const std::array<float,8>values{clamp(raw.depth,0,100)*.01f,rate(clamp(raw.rate,.02f,10,.5f),raw.sync,transport),clamp(raw.feedback,-85,85)*.01f,clamp(raw.mix,0,100)*.01f,std::pow(10.f,clamp(raw.output,-24,6)/20),clamp(raw.delay,.1f,40,12),clamp(raw.center,20,8000,1000),raw.bypass?0.f:1.f};
        if(first){for(size_t i=0;i<ramps.size();++i)ramps[i].reset(values[i],int(sampleRate*.02));for(int i=0;i<3;++i){modes[size_t(i)].reset(mode==i?1.f:0.f,int(sampleRate*.02));shapes[size_t(i)].reset(shape==i?1.f:0.f,int(sampleRate*.02));}for(int i=0;i<4;++i)taps[size_t(i)].reset(stages==i?1.f:0.f,int(sampleRate*.02));first=false;}
        for(size_t i=0;i<ramps.size();++i)ramps[i].set(values[i]);
        targetChoice(modes,mode);targetChoice(shapes,shape);for(int i=0;i<4;++i)taps[size_t(i)].set(stages==i?1.f:0.f);
        float maxPeak=0,lastRate=0;
        for(int i=0;i<n;++i){std::array<float,8>v{};for(size_t k=0;k<v.size();++k)v[k]=ramps[k].next();lastRate=v[1];const auto m=next(modes),s=next(shapes);std::array<float,4>t{};for(size_t k=0;k<4;++k)t[k]=taps[k].next();const std::array<float,2>lfo{wave(phase,s),wave(phase+.25,s)};const float dry[2]{finite(left[i]),right?finite(right[i]):finite(left[i])};float output[2]{};
            for(size_t ch=0;ch<2;++ch){const float x=bounded(dry[ch]);const double base=v[5]*.001*sampleRate,depth=base*.94*v[0];const float a=delays[0][ch].read(base+depth*lfo[ch]);const float b=delays[0][ch].read(base*1.2+depth*wave(phase+double(ch)*.25+.33,s));const float chorus=(a+b)*.5f;delays[0][ch].push(x+v[2]*chorus);
                const float flange=delays[1][ch].read(base+depth*lfo[ch]);delays[1][ch].push(x+v[2]*flange);
                const double cutoff=std::clamp(double(v[6])*std::exp2(2*v[0]*lfo[ch]),20.,sampleRate*.45);const double tangent=std::tan(modulation::pi*cutoff/sampleRate);const float g=float((tangent-1)/(tangent+1));float ap=bounded(x+v[2]*previous[ch]),phaser=0;
                for(size_t k=0;k<8;++k){const float y=g*ap+allpass[ch][k];allpass[ch][k]=bounded(ap-g*y);ap=bounded(y);if(k%2==1)phaser+=t[k/2]*ap;}previous[ch]=phaser;
                const float wet=m[0]*chorus+m[1]*flange+m[2]*phaser;output[ch]=mix(dry[ch],wet,v[3],v[4],v[7]);maxPeak=std::max(maxPeak,std::abs(output[ch]));}
            left[i]=output[0];if(right)right[i]=output[1];telemetry.sample(v[0]*lfo[0],v[0]*lfo[1]);phase=wrap(phase+v[1]/sampleRate);
        }peak.store(maxPeak);telemetry.status(lastRate,transport,raw.sync);
    }
    modulation::Display display()const noexcept{return telemetry.snapshot();}
    std::atomic<float>peak{0};
private:
    double sampleRate=0,phase=0;bool first=true;
    std::array<std::array<modulation::Delay,2>,2>delays;
    std::array<std::array<float,8>,2>allpass{};std::array<float,2>previous{};
    std::array<modulation::Ramp,8>ramps;std::array<modulation::Ramp,3>modes,shapes;std::array<modulation::Ramp,4>taps;
    modulation::Telemetry telemetry;
};
struct RotaryTremoloAutopanSettings {
    float speed=4,depth=0,width=100,mix=0,output=0,ramp=1;
    int mode=0,shape=0,rotary=0;bool sync=false,bypass=false;
};
// Dual-band rotating-source approximation. This is not an impulse response,
// measured cabinet model, or emulation of a named commercial loudspeaker.
class RotaryTremoloAutopanCore {
public:
    void prepare(double sr){modulation::requireRate(sr);sampleRate=sr;for(auto&band:delays)for(auto&d:band)d.prepare(sr,.012);splitCoefficient=float(1-std::exp(-2*modulation::pi*800/sr));telemetry.prepare(sr);reset();}
    void reset()noexcept{for(auto&band:delays)for(auto&d:band)d.reset();low={};phase=hornPhase=drumPhase=0;hornSpeed=drumSpeed=0;lastRamp=-1;first=true;peak.store(0);telemetry.reset();}
    int latency()const noexcept{return 0;}
    void process(float*left,float*right,int n,const RotaryTremoloAutopanSettings&raw,modulation::Transport transport={})noexcept{
        if(!left||n<=0||sampleRate<=0)return;
        using namespace modulation;transport=sanitize(transport);const int mode=std::clamp(raw.mode,0,2),shape=std::clamp(raw.shape,0,2),rotary=std::clamp(raw.rotary,0,2);
        const std::array<float,7>values{rate(clamp(raw.speed,.02f,10,4),raw.sync,transport),clamp(raw.depth,0,100)*.01f,clamp(raw.width,0,100,100)*.01f,clamp(raw.mix,0,100)*.01f,std::pow(10.f,clamp(raw.output,-24,6)/20),clamp(raw.ramp,.05f,10,1),raw.bypass?0.f:1.f};
        if(first){for(size_t i=0;i<ramps.size();++i)ramps[i].reset(values[i],int(sampleRate*.02));for(int i=0;i<3;++i){modes[size_t(i)].reset(mode==i?1.f:0.f,int(sampleRate*.02));shapes[size_t(i)].reset(shape==i?1.f:0.f,int(sampleRate*.02));}hornSpeed=rotary==2?0:values[0]*(rotary==0?.15:1);drumSpeed=hornSpeed*.82;first=false;}
        for(size_t i=0;i<ramps.size();++i)ramps[i].set(values[i]);
        targetChoice(modes,mode);targetChoice(shapes,shape);
        float maxPeak=0,lastRate=0;
        for(int i=0;i<n;++i){std::array<float,7>v{};for(size_t k=0;k<v.size();++k)v[k]=ramps[k].next();const auto m=next(modes),s=next(shapes);const double target=rotary==2?0:v[0]*(rotary==0?.15:1);if(v[5]!=lastRamp){lastRamp=v[5];hornCoefficient=1-std::exp(-1/(sampleRate*v[5]));drumCoefficient=1-std::exp(-1/(sampleRate*v[5]*1.35));}hornSpeed+=(target-hornSpeed)*hornCoefficient;drumSpeed+=(target*.82-drumSpeed)*drumCoefficient;lastRate=mode==0?float(hornSpeed):v[0];const float dry[2]{finite(left[i]),right?finite(right[i]):finite(left[i])};float trace[2]{},out[2]{};
            for(size_t ch=0;ch<2;++ch){const float x=bounded(dry[ch]);const double offset=double(ch)*.5*v[2];const float lfo=wave(phase+offset,s),h=wave(hornPhase+offset,s),d=wave(drumPhase+offset,s);low[ch]+=splitCoefficient*(x-low[ch]);const float high=x-low[ch];delays[0][ch].push(high);delays[1][ch].push(low[ch]);const float horn=delays[0][ch].read(sampleRate*(.0025+.0015*v[1]*h)),drum=delays[1][ch].read(sampleRate*(.0035+.001*v[1]*d));const float rotation=horn*(1-.35f*v[1]+.35f*v[1]*h)+drum*(1-.2f*v[1]+.2f*v[1]*d);
                // Depth zero is exactly neutral for every mode. Rotary delay
                // and attenuation are introduced continuously with depth.
                const float rotaryWet=x+v[1]*(rotation-x);const float tremolo=x*(1-.5f*v[1]*(1-lfo));
                // Equal-power pan excursions normalised so centre is unity.
                const float position=v[1]*wave(phase,s);const float panGain=ch==0?std::sqrt(std::max(0.f,1-position*v[2])):std::sqrt(std::max(0.f,1+position*v[2]));const float pan=x*panGain;
                const float wet=m[0]*rotaryWet+m[1]*tremolo+m[2]*pan;out[ch]=mix(dry[ch],wet,v[3],v[4],v[6]);maxPeak=std::max(maxPeak,std::abs(out[ch]));trace[ch]=m[0]*v[1]*h+m[1]*v[1]*lfo+m[2]*(ch==0?-position*v[2]:position*v[2]);}
            left[i]=out[0];if(right)right[i]=out[1];telemetry.sample(trace[0],trace[1]);phase=wrap(phase+v[0]/sampleRate);hornPhase=wrap(hornPhase+hornSpeed/sampleRate);drumPhase=wrap(drumPhase+drumSpeed/sampleRate);
        }peak.store(maxPeak);telemetry.status(lastRate,transport,raw.sync);
    }
    modulation::Display display()const noexcept{return telemetry.snapshot();}
    std::atomic<float>peak{0};
private:
    double sampleRate=0,phase=0,hornPhase=0,drumPhase=0,hornSpeed=0,drumSpeed=0;float splitCoefficient=0,lastRamp=-1;double hornCoefficient=0,drumCoefficient=0;bool first=true;
    std::array<std::array<modulation::Delay,2>,2>delays;std::array<float,2>low{};
    std::array<modulation::Ramp,7>ramps;std::array<modulation::Ramp,3>modes,shapes;modulation::Telemetry telemetry;
};
} // namespace hem
