#pragma once
// Original analysis kernels. No third-party algorithm source was copied.
// Kernels below run on an analysis worker unless explicitly marked audio-safe.
#include "DspCore.h"
#include "AnalysisReference.h"
#include "../Systems/AudioMeters.h"
#include <complex>
#include <cstdint>
#include <limits>
#include <numeric>
#include <stdexcept>

namespace hem::analysis {
constexpr float floorDb=-160.f;
inline float db(double a) noexcept {return a>1.e-8?float(20*std::log10(a)):floorDb;}
inline float loudness(double e) noexcept {return e>0?float(-.691+10*std::log10(e)):floorDb;}
inline double clean(float v) noexcept {return std::isfinite(v)?double(v):0.;}

// Fixed SPSC packet transport. Producer never wakes a thread. prepare/reset only
// while producer and consumer are stopped. A loss creates a detectable timestamp gap.
template<unsigned Channels,unsigned Count=128,unsigned Size=256> class Transport {
public:
    struct Packet {std::array<std::array<float,Size>,Channels> audio{};unsigned count=0,epoch=0;std::uint64_t first=0;};
    void reset() noexcept {write=0;read=0;partial=0;frame=0;lost=0;}
    void push(const std::array<const float*,Channels>& data,int count,unsigned epoch=0) noexcept {
        if(count<=0)return;
        for(int offset=0;offset<count;){
            const auto w=write.load(std::memory_order_relaxed);
            if(w-read.load(std::memory_order_acquire)>=Count){lost.fetch_add(std::uint64_t(count-offset),std::memory_order_relaxed);frame+=std::uint64_t(count-offset);return;}
            auto& p=packets[w%Count];if(partial&&p.epoch!=epoch)partial=0;if(!partial){p.first=frame;p.epoch=epoch;}
            const unsigned n=std::min(Size-partial,unsigned(count-offset));
            for(unsigned c=0;c<Channels;++c)for(unsigned i=0;i<n;++i)p.audio[c][partial+i]=data[c]?data[c][offset+int(i)]:0.f;
            partial+=n;frame+=n;offset+=int(n);
            if(partial==Size){p.count=partial;partial=0;write.store(w+1,std::memory_order_release);}
        }
    }
    template<class F> bool consume(F&& consumePacket) {const auto r=read.load(std::memory_order_relaxed);if(r==write.load(std::memory_order_acquire))return false;consumePacket(packets[r%Count]);read.store(r+1,std::memory_order_release);return true;}
    void skip(int count)noexcept{if(count>0){partial=0;frame+=std::uint64_t(count);}}
    std::uint64_t dropped() const noexcept{return lost.load(std::memory_order_relaxed);}
private:
    static_assert(std::atomic<unsigned>::is_always_lock_free&&std::atomic<std::uint64_t>::is_always_lock_free);
    std::array<Packet,Count> packets{};std::atomic<unsigned> write{0},read{0};std::atomic<std::uint64_t> lost{0};unsigned partial=0;std::uint64_t frame=0;
};

// Transforms a digital 48 kHz biquad to another rate through its exact inverse
// bilinear analog transfer. At 48 kHz coefficients are unchanged, including RLB gain.
struct WeightingFilter {
    double b0=1,b1=0,b2=0,a1=0,a2=0,z1=0,z2=0;
    void set(double B0,double B1,double B2,double A1,double A2,double rate) noexcept {
        const auto remap=[rate](double c0,double c1,double c2){const double k=rate/48000.;return std::array<double,3>{(c0+c1+c2)+(2*c0-2*c2)*k+(c0-c1+c2)*k*k,2*(c0+c1+c2)-2*(c0-c1+c2)*k*k,(c0+c1+c2)-(2*c0-2*c2)*k+(c0-c1+c2)*k*k};};
        const auto b=remap(B0,B1,B2),a=remap(1,A1,A2);b0=b[0]/a[0];b1=b[1]/a[0];b2=b[2]/a[0];a1=a[1]/a[0];a2=a[2]/a[0];z1=z2=0;
    }
    double tick(double x) noexcept{const double y=b0*x+z1;z1=b1*x-a1*y+z2;z2=b2*x-a2*y;return y;}
};
struct LoudnessFrame {
    std::array<float,2> samplePeak{floorDb,floorDb},rms{floorDb,floorDb},vu{floorDb,floorDb},heldPeak{floorDb,floorDb};
    float integrated=floorDb,momentary=floorDb,shortTerm=floorDb,lra=0,truePeakEstimate=floorDb,crest=0,correlation=0;
    double sampleRate=0;std::uint64_t frameEnd=0;bool momentaryReady=false,shortReady=false,integratedReady=false,lraReady=false,correlationReady=false,complete=true,capacityExceeded=false;
};
class LoudnessCore {
public:
    static constexpr size_t capacity=4*60*60*10; // four hours, 10 Hz
    void prepare(double sr){if(!std::isfinite(sr)||sr<8000||sr>384000)throw std::invalid_argument("Analysis sample rate must be 8–384 kHz");rate=sr;momentSize=size_t(std::llround(.4*sr));shortSize=size_t(std::llround(3*sr));energy.assign(shortSize,0);blocks.resize(capacity);shortBlocks.resize(capacity);percentiles.resize(capacity);reset();}
    void reset() noexcept {
        for(auto& c:shelf)c.set(1.53512485958697,-2.69169618940638,1.19839281085285,-1.69065929318241,.73248077421585,rate);
        for(auto& c:highpass)c.set(1,-2,1,-1.99004745483398,.99007225036621,rate);
        std::fill(energy.begin(),energy.end(),0);windowPosition=0;samples=nextStep=steps=blockCount=shortCount=0;momentSum=shortSum=0;absSum=shortAbsSum=0;absCount=shortAbsCount=0;tpRing={};tpPosition=0;maxTruePeak=0;ll=rr=lr=0;vuState={};hold={};meters.prepare(rate);frame={};frame.sampleRate=rate;nextStep=std::uint64_t(std::llround(rate*.1));
    }
    void discontinuity() noexcept{frame.complete=false;frame.integratedReady=frame.lraReady=false;}
    void push(const float* l,const float* r,int n) noexcept {
        meters.push(l,r,n);
        for(int i=0;i<n;++i){const double a=clean(l?l[i]:0),b=clean(r?r[i]:0);const std::array<double,2> x{a,b};double e=0;
            for(size_t c=0;c<2;++c){const double y=highpass[c].tick(shelf[c].tick(x[c]));e+=y*y;tpRing[c][tpPosition]=float(x[c]);hold[c]=std::max(hold[c],std::abs(x[c]));const double memory=std::exp(-1./(.3*rate));vuState[c]=memory*vuState[c]+(1-memory)*std::abs(x[c]);}
            // Existing 8x/32-tap reconstruction, never presented as certified dBTP.
            for(size_t c=0;c<2;++c)for(const auto& phase:reconstruction.h){double y=0;for(int t=0;t<Reconstruction::taps;++t)y+=phase[size_t(t)]*tpRing[c][(tpPosition+Reconstruction::taps-size_t(t))%Reconstruction::taps];maxTruePeak=std::max(maxTruePeak,std::abs(y));}
            tpPosition=(tpPosition+1)%Reconstruction::taps;
            const size_t oldMoment=(windowPosition+shortSize-momentSize)%shortSize;
            momentSum+=e-energy[oldMoment];shortSum+=e-energy[windowPosition];energy[windowPosition]=e;windowPosition=(windowPosition+1)%shortSize;
            const double memory=std::exp(-1./(.4*rate));ll=memory*ll+(1-memory)*a*a;rr=memory*rr+(1-memory)*b*b;lr=memory*lr+(1-memory)*a*b;
            ++samples;if(samples>=nextStep){step();++steps;nextStep=std::uint64_t(std::llround(double(steps+1)*.1*rate));}
        }
        const auto m=meters.snapshot();for(size_t c=0;c<2;++c){const auto& q=c?m.right:m.left;frame.samplePeak[c]=q.peakDb;frame.rms[c]=q.rmsDb;frame.heldPeak[c]=db(hold[c]);frame.vu[c]=db(vuState[c]*pi/(2*std::sqrt(2.)))+18.f;}
        if(m.nonFiniteSamples)discontinuity();frame.truePeakEstimate=db(maxTruePeak);frame.frameEnd=samples;frame.correlationReady=ll*rr>1.e-30;frame.correlation=frame.correlationReady?float(std::clamp(lr/std::sqrt(ll*rr),-1.,1.)):0;
        const double rms=std::sqrt((ll+rr)*.5);frame.crest=rms>1.e-8?std::max(0.f,db(std::max(hold[0],hold[1]))-db(rms)):0;
    }
    // Worker only. Exact two-stage gating; bounded history capacity, no histogram approximation.
    const LoudnessFrame& calculate(){
        frame.integratedReady=frame.complete&&!frame.capacityExceeded&&absCount>0;frame.lraReady=false;
        if(frame.integratedReady){const double threshold=std::max(std::pow(10.,(-70+.691)/10),absSum/double(absCount)*.1);double sum=0;size_t count=0;for(size_t i=0;i<blockCount;++i)if(blocks[i]>threshold){sum+=blocks[i];++count;}frame.integratedReady=count>0;frame.integrated=count?loudness(sum/double(count)):floorDb;}
        if(frame.complete&&!frame.capacityExceeded&&shortAbsCount){const double threshold=std::max(std::pow(10.,(-70+.691)/10),shortAbsSum/double(shortAbsCount)*.01);size_t count=0;for(size_t i=0;i<shortCount;++i)if(shortBlocks[i]>=threshold)percentiles[count++]=shortBlocks[i];if(count>1){std::sort(percentiles.begin(),percentiles.begin()+std::ptrdiff_t(count));const auto lo=size_t(std::llround((count-1)*.1)),hi=size_t(std::llround((count-1)*.95));frame.lra=loudness(percentiles[hi])-loudness(percentiles[lo]);frame.lraReady=true;}}
        return frame;
    }
    const LoudnessFrame& latest() const noexcept{return frame;}
private:
    std::uint64_t stepCount() const noexcept{return std::uint64_t(std::floor(double(samples)/(rate*.1)+1.e-6));}
    void step() noexcept{const double absolute=std::pow(10.,(-70+.691)/10);if(samples>=momentSize){frame.momentaryReady=true;const double e=std::max(0.,momentSum)/double(momentSize);frame.momentary=loudness(e);if(blockCount<capacity){blocks[blockCount++]=e;if(e>absolute){absSum+=e;++absCount;}}else frame.capacityExceeded=true;}
        if(samples>=shortSize){frame.shortReady=true;const double e=std::max(0.,shortSum)/double(shortSize);frame.shortTerm=loudness(e);if(shortCount<capacity){shortBlocks[shortCount++]=e;if(e>=absolute){shortAbsSum+=e;++shortAbsCount;}}else frame.capacityExceeded=true;}}
    double rate=48000,momentSum=0,shortSum=0,absSum=0,shortAbsSum=0,maxTruePeak=0,ll=0,rr=0,lr=0;size_t momentSize=19200,shortSize=144000,windowPosition=0,blockCount=0,shortCount=0,absCount=0,shortAbsCount=0,tpPosition=0;std::uint64_t samples=0,nextStep=4800,steps=0;
    std::array<WeightingFilter,2>shelf,highpass;std::vector<double>energy,blocks,shortBlocks,percentiles;Reconstruction reconstruction;std::array<std::array<float,Reconstruction::taps>,2>tpRing{};std::array<double,2>vuState{},hold{};AudioMeters meters;LoudnessFrame frame;
};

struct SpectrumSettings {int order=11,window=1,channel=0;float averagingMs=100,historySeconds=10,rangeDb=100;bool rms=false,maxHold=false,freeze=false;int view=0;bool operator==(const SpectrumSettings&) const=default;};
struct SpectrumFrame {
    static constexpr size_t maximumBins=4097;
    std::array<float,maximumBins> db{},heldDb{};int bins=0,fftSize=0;double sampleRate=0,timeSeconds=0;std::uint64_t frameEnd=0,sequence=0;float peakHz=0,peakDb=floorDb;bool valid=false;SpectrumSettings settings;
};
class SpectrumCore {
public:
    static constexpr int maximumSize=8192;
    void prepare(double sr){if(!std::isfinite(sr)||sr<8000||sr>384000)throw std::invalid_argument("Analysis sample rate must be 8–384 kHz");rate=sr;reset();}
    void reset() noexcept{cursor=fill=hop=0;samples=sequence=0;ringL={};ringR={};average={};held.fill(floorDb);frame={};frame.db.fill(floorDb);frame.heldDb.fill(floorDb);configure(settings);}
    void configure(SpectrumSettings s) noexcept{s.order=std::clamp(s.order,9,13);s.window=std::clamp(s.window,0,2);s.channel=std::clamp(s.channel,0,4);s.averagingMs=std::isfinite(s.averagingMs)?std::clamp(s.averagingMs,0.f,2000.f):100.f;s.historySeconds=std::isfinite(s.historySeconds)?std::clamp(s.historySeconds,1.f,60.f):10.f;s.rangeDb=std::isfinite(s.rangeDb)?std::clamp(s.rangeDb,24.f,160.f):100.f;
        const bool newWindow=s.order!=settings.order||s.window!=settings.window||windowSum==0;if(s.order!=settings.order||s.window!=settings.window||s.channel!=settings.channel||s.rms!=settings.rms){cursor=fill=hop=0;sequence=0;average={};held.fill(floorDb);}settings=s;size=1<<s.order;if(!newWindow)return;windowSum=0;for(int i=0;i<size;++i){const double t=2*pi*i/(size-1);window[size_t(i)]=s.window==0?1.:s.window==1?.5-.5*std::cos(t):.42-.5*std::cos(t)+.08*std::cos(2*t);windowSum+=window[size_t(i)];}}
    template<class F> void push(const float* l,const float* r,int n,F&& publish) {
        for(int i=0;i<n;++i){ringL[size_t(cursor)]=float(clean(l?l[i]:0));ringR[size_t(cursor)]=float(clean(r?r[i]:0));cursor=(cursor+1)%size;fill=std::min(size,fill+1);++hop;++samples;if(fill==size&&hop>=size/4){hop=0;if(!settings.freeze){transform();publish(frame);}}}
    }
    const SpectrumFrame& latest()const noexcept{return frame;}
private:
    void fft(std::array<std::complex<double>,maximumSize>& x)noexcept {for(int i=1,j=0;i<size;++i){int bit=size>>1;for(;j&bit;bit>>=1)j^=bit;j^=bit;if(i<j)std::swap(x[size_t(i)],x[size_t(j)]);}for(int length=2;length<=size;length<<=1){const auto wlen=std::polar(1.,-2*pi/length);for(int i=0;i<size;i+=length){std::complex<double>w=1;for(int j=0;j<length/2;++j){auto u=x[size_t(i+j)],v=x[size_t(i+j+length/2)]*w;x[size_t(i+j)]=u+v;x[size_t(i+j+length/2)]=u-v;w*=wlen;}}}}
    void transform()noexcept{for(int i=0;i<size;++i){const auto ix=size_t((cursor+i)%size);const double l=ringL[ix],r=ringR[ix];a[size_t(i)]=window[size_t(i)]*(settings.channel==2?r:settings.channel==3?(l+r)*.5:settings.channel==4?(l-r)*.5:l);b[size_t(i)]=window[size_t(i)]*r;}fft(a);if(settings.channel==0)fft(b);const double memory=settings.averagingMs>0?std::exp(-(size/4)/rate/(settings.averagingMs*.001)):0;float peak=floorDb;int peakBin=0;
        for(int i=0;i<=size/2;++i){const bool edge=i==0||i==size/2;double magnitude=std::abs(a[size_t(i)]);if(settings.channel==0)magnitude=std::max(magnitude,std::abs(b[size_t(i)]));magnitude*=(edge?1.:2.)/windowSum;if(settings.rms&&!edge)magnitude/=std::sqrt(2.);const double power=magnitude*magnitude;average[size_t(i)]=sequence==0?power:memory*average[size_t(i)]+(1-memory)*power;const float level=db(std::sqrt(average[size_t(i)]));frame.db[size_t(i)]=level;held[size_t(i)]=settings.maxHold?std::max(held[size_t(i)],level):level;frame.heldDb[size_t(i)]=held[size_t(i)];if(level>peak){peak=level;peakBin=i;}}
        frame.bins=size/2+1;frame.fftSize=size;frame.sampleRate=rate;frame.frameEnd=samples;frame.timeSeconds=double(samples)/rate;frame.sequence=++sequence;frame.peakDb=peak;frame.peakHz=float(peakBin*rate/size);frame.valid=true;frame.settings=settings;}
    double rate=48000,windowSum=0;int size=2048,cursor=0,fill=0,hop=0;std::uint64_t samples=0,sequence=0;SpectrumSettings settings;SpectrumFrame frame;std::array<float,maximumSize>ringL{},ringR{};std::array<double,maximumSize>window{},average{};std::array<float,SpectrumFrame::maximumBins>held{};std::array<std::complex<double>,maximumSize>a{},b{};
};

struct DeltaSettings{int mode=0;float monitorGainDb=0,delayTrimMs=0;bool autoAlign=false;};
struct DeltaFrame{std::array<float,2>dryPeak{floorDb,floorDb},processedPeak{floorDb,floorDb},deltaPeak{floorDb,floorDb};float correlation=0;bool referenceValid=false,correlationValid=false;int alignmentSamples=0;std::uint64_t frameEnd=0;};
class DeltaCore {
public:
    void prepare(double sr){rate=sr;base=std::max(1,int(std::llround(.003*rate)));length=2*base+2;for(auto& v:dry)v.assign(size_t(length),0);for(auto&v:processed)v.assign(size_t(length),0);position=0;samples=0;availableSamples=0;corrDD=corrPP=corrDP=0;correlationMemory=std::exp(-1./(.3*rate));lastTrim=0;trimDelay.reset(rate,float(base));outputGain.reset(rate,1);for(size_t i=0;i<3;++i)mode[i].reset(rate,i==0?1.f:0.f);frame={};}
    int latency()const noexcept{return base;}
    // Audio safe. Input pointers may alias output. Reference pointers may NOT alias
    // a buffer that is modified before this call. Validity is consumed by adapter.
    template<class F> void process(float*l,float*r,int n,const ReferenceBlock& ref,const DeltaSettings&s,F&& observe)noexcept{
        const bool valid=ref.valid&&ref.left&&ref.right&&ref.samples==n&&ref.sampleRate==rate;frame={};frame.referenceValid=valid;
        const int trim=std::clamp(int(std::llround((std::isfinite(s.delayTrimMs)?std::clamp(s.delayTrimMs,-3.f,3.f):0.f)*.001*rate)),-base,base);frame.alignmentSamples=trim;trimDelay.set(float(base+trim));
        if(!valid){corrDD=corrPP=corrDP=0;availableSamples=0;lastTrim=trim;std::array<double,2>pk{};for(int i=0;i<n;++i){dry[0][size_t(position)]=dry[1][size_t(position)]=0;processed[0][size_t(position)]=l[i];processed[1][size_t(position)]=r[i];l[i]=processed[0][size_t((position-base+length)%length)];r[i]=processed[1][size_t((position-base+length)%length)];pk[0]=std::max(pk[0],std::abs(clean(l[i])));pk[1]=std::max(pk[1],std::abs(clean(r[i])));position=(position+1)%length;}samples+=std::uint64_t(n);for(size_t c=0;c<2;++c)frame.processedPeak[c]=db(pk[c]);frame.frameEnd=samples;return;}
        outputGain.set(std::pow(10.f,(std::isfinite(s.monitorGainDb)?std::clamp(s.monitorGainDb,-24.f,24.f):0.f)/20));const int selected=std::clamp(s.mode,0,2);for(size_t i=0;i<3;++i)mode[i].set(int(i)==selected?1.f:0.f);
        double dd=0,pp=0,dp=0;std::array<double,2>pkD{},pkP{},pkDelta{};
        for(int i=0;i<n;++i){const float delayedBy=trimDelay.next();const int delayWhole=int(delayedBy);const float delayFraction=delayedBy-delayWhole;const std::array<float,2>input{float(clean(l[i])),float(clean(r[i]))},reference{float(clean(ref.left[i])),float(clean(ref.right[i]))};std::array<float,2>d{},p{},delta{};
            for(size_t c=0;c<2;++c){dry[c][size_t(position)]=reference[c];processed[c][size_t(position)]=input[c];d[c]=(1-delayFraction)*dry[c][size_t((position-delayWhole+length)%length)]+delayFraction*dry[c][size_t((position-delayWhole-1+length)%length)];p[c]=processed[c][size_t((position-base+length)%length)];delta[c]=p[c]-d[c];pkD[c]=std::max(pkD[c],std::abs(double(d[c])));pkP[c]=std::max(pkP[c],std::abs(double(p[c])));pkDelta[c]=std::max(pkDelta[c],std::abs(double(delta[c])));dd+=double(d[c])*d[c];pp+=double(p[c])*p[c];dp+=double(d[c])*p[c];}
            corrDD=correlationMemory*corrDD+(1-correlationMemory)*dd;corrPP=correlationMemory*corrPP+(1-correlationMemory)*pp;corrDP=correlationMemory*corrDP+(1-correlationMemory)*dp;dd=pp=dp=0;observe(reference,input,d,p,delta);const float g=outputGain.next(),mp=mode[0].next(),md=mode[1].next(),mx=mode[2].next();l[i]=g*(mp*p[0]+md*d[0]+mx*delta[0]);r[i]=g*(mp*p[1]+md*d[1]+mx*delta[1]);position=(position+1)%length;++availableSamples;
        }
        for(size_t c=0;c<2;++c){frame.dryPeak[c]=db(pkD[c]);frame.processedPeak[c]=db(pkP[c]);frame.deltaPeak[c]=db(pkDelta[c]);}frame.referenceValid=availableSamples>=std::uint64_t(2*base);frame.correlationValid=frame.referenceValid&&corrDD*corrPP>1.e-30;frame.correlation=frame.correlationValid?float(std::clamp(corrDP/std::sqrt(corrDD*corrPP),-1.,1.)):0;samples+=std::uint64_t(n);frame.frameEnd=samples;lastTrim=trim;
    }
    const DeltaFrame& latest()const noexcept{return frame;}
    // Worker only, finite lag search. Positive result means processed lags dry.
    struct Alignment{int lag=0;float confidence=0;bool valid=false;};
    static Alignment align(const float*dry,const float*processed,int n,int maxLag)noexcept{Alignment result;double best=0,second=0;maxLag=std::min(maxLag,n/4);for(int lag=-maxLag;lag<=maxLag;++lag){double aa=0,bb=0,ab=0;for(int i=maxLag;i<n-maxLag;++i){const double a=clean(dry[i-lag]),b=clean(processed[i]);aa+=a*a;bb+=b*b;ab+=a*b;}double score=aa*bb>1.e-20?ab/std::sqrt(aa*bb):0;if(score>best){second=best;best=score;result.lag=lag;}else second=std::max(second,score);}result.confidence=float(best);result.valid=best>.8&&best-second>.02;return result;}
private:
    double rate=48000,corrDD=0,corrPP=0,corrDP=0,correlationMemory=0;int base=144,length=290,position=0,lastTrim=0;std::uint64_t samples=0,availableSamples=0;std::array<std::vector<float>,2>dry,processed;Ramp outputGain,trimDelay;std::array<Ramp,3>mode;DeltaFrame frame;
};
} // namespace hem::analysis
