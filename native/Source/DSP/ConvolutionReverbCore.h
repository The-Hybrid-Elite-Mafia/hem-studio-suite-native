#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include "PreparedConvolution.h"
#include "ReverbIRResampler.h"
#include <array>
#include <atomic>
#include <mutex>
#include <vector>
#include <cmath>

namespace hem {
struct ConvolutionReverbSettings {
    float predelay=0, irSize=100, lowCut=20, highCut=20000, mix=0, output=0;
    bool lowCutOn=false, highCutOn=false, soloWet=false, bypass=false;
};
// Every allocating operation is explicitly off-thread. prepare/destruction need
// the host's normal quiescent-audio lifecycle. load/resize/reclaim may run while
// process runs; a bounded mailbox hands over exclusive engine ownership.
class ConvolutionReverbCore {
public:
    static constexpr double maxSourceSeconds=10, maxScaledSeconds=20;
    static constexpr int maxSourceFrames=1920000;
    struct Display {
        std::array<float,512> waveform{}, envelope{};
        double sourceRate=0, seconds=0;
        int channels=0, sourceFrames=0, renderedFrames=0;
        float sizePercent=100;
        bool loaded=false;
    };
    ConvolutionReverbCore()=default;
    ~ConvolutionReverbCore(){clearEngines();}
    ConvolutionReverbCore(const ConvolutionReverbCore&)=delete;
    ConvolutionReverbCore& operator=(const ConvolutionReverbCore&)=delete;
    void prepare(double sr,int maximumBlock){
        const std::lock_guard<std::mutex> lock(controlMutex);
        prepared.store(false);clearEngines();
        sampleRate=std::isfinite(sr)&&sr>=8000&&sr<=192000?sr:0;
        blockSize=std::clamp(maximumBlock,1,8192);
        scratch.setSize(6,blockSize);scratch.clear();
        publishedLatency.store(sampleRate>0?reverb::convolutionLatency(sampleRate):0);
        for(auto& d:dryDelay)d.assign(size_t(std::max(1,publishedLatency.load())),0);
        for(auto& d:delay)d.assign(size_t(std::ceil(std::max(8000.,sampleRate)*.250))+4,0);
        resetDsp();
        if(sampleRate>0){if(source.getNumSamples()>0)active=build(source,sourceRate,sizePercent).release();makeDisplay();prepared.store(true);}
        activeLength.store(active?active->frames:0);activePresent.store(active!=nullptr);
    }
    // On failure the previously working resource remains untouched.
    juce::Result loadOffThread(const juce::AudioBuffer<float>& data,double rate,float size=100){
        const auto result=validate(data,rate);if(result.failed())return result;
        const std::lock_guard<std::mutex> lock(controlMutex);
        const float nextSize=safe(size,25,200,100);
        auto next=sampleRate>0?build(data,rate,nextSize):std::unique_ptr<Engine>{};
        source.makeCopyOf(data);sourceRate=rate;sizePercent=nextSize;makeDisplay();
        publish(std::move(next));return juce::Result::ok();
    }
    juce::Result resizeOffThread(float size){
        const std::lock_guard<std::mutex> lock(controlMutex);reclaim();
        const float nextSize=safe(size,25,200,100);if(nextSize==sizePercent)return juce::Result::ok();
        if(source.getNumSamples()==0){sizePercent=nextSize;return juce::Result::ok();}
        auto next=sampleRate>0?build(source,sourceRate,nextSize):std::unique_ptr<Engine>{};
        sizePercent=nextSize;makeDisplay();publish(std::move(next));return juce::Result::ok();
    }
    // Empty resource is represented by a prepared silent engine, so clearing can
    // crossfade and retire the old engine without deleting it on the audio thread.
    void clearOffThread(){const std::lock_guard<std::mutex> lock(controlMutex);source.setSize(0,0);sourceRate=0;{const std::lock_guard<std::mutex> displayLock(displayMutex);display={};}auto e=sampleRate>0?std::make_unique<Engine>():nullptr;if(e){juce::AudioBuffer<float> zero(1,1);zero.clear();e->initialise(std::move(zero),sampleRate,blockSize);e->frames=0;}publish(std::move(e));}
    void reclaimOffThread(){const std::lock_guard<std::mutex> lock(controlMutex);reclaim();}
    Display displaySnapshot() const {const std::lock_guard<std::mutex> lock(displayMutex);return display;}
    bool isPrepared()const noexcept{return prepared.load();}
    bool hasActiveIR()const noexcept{return activePresent.load();}
    int latency()const noexcept{return publishedLatency.load();}
    double tailSeconds(float predelayMs=0)const noexcept {const double sr=publishedRate.load();return sr>0&&activeLength.load()>publishedLatency.load()?(activeLength.load()-publishedLatency.load())/sr+safe(predelayMs,0,250,0)*.001:0;}
    // Host-serialized, allocation-free reset. The mailbox is left intact.
    void reset()noexcept{if(active)active->convolver.reset();if(fading)fading->convolver.reset();resetDsp();}
    void process(float* left,float* right,int samples,const ConvolutionReverbSettings& p)noexcept{
        if(!left||samples<=0)return;
        if(!prepared.load()){for(int i=0;i<samples;++i){left[i]=finite(left[i]);if(right)right[i]=finite(right[i]);}return;}
        adopt();
        const float targetMix=(p.soloWet?1.f:safe(p.mix,0,100,0)*.01f),targetGain=std::pow(10.f,safe(p.output,-24,6,0)*.05f);
        setTarget(mix,targetMix);setTarget(gain,targetGain);setTarget(bypass,p.bypass?1.f:0.f);
        setTarget(pre,safe(p.predelay,0,250,0)*float(sampleRate*.001));
        setTarget(lo,safe(p.lowCut,20,2000,20));setTarget(hi,safe(p.highCut,500,20000,20000));
        setTarget(loOn,p.lowCutOn?1.f:0.f);setTarget(hiOn,p.highCutOn?1.f:0.f);
        first=false;float peakValue=0,wetPeakValue=0;
        for(int offset=0;offset<samples;offset+=std::min(blockSize,samples-offset)){const int n=std::min(blockSize,samples-offset);
            auto*l=scratch.getWritePointer(0);auto*r=scratch.getWritePointer(1);auto*oldL=scratch.getWritePointer(2);auto*oldR=scratch.getWritePointer(3);
            for(int i=0;i<n;++i){l[i]=finite(left[offset+i]);r[i]=right?finite(right[offset+i]):l[i];}
            auto*dryL=scratch.getWritePointer(4);auto*dryR=scratch.getWritePointer(5);
            for(int i=0;i<n;++i){dryL[i]=dryDelay[0][dryPos];dryR[i]=dryDelay[1][dryPos];dryDelay[0][dryPos]=l[i];dryDelay[1][dryPos]=r[i];dryPos=(dryPos+1)%dryDelay[0].size();}
            for(int i=0;i<n;++i){const float d=next(pre);const float b=bypass.remaining>0?bypass.current+bypass.step*float(std::min(i+1,bypass.remaining)):bypass.current;const float excitation=1-b;l[i]=predelay(0,std::clamp(l[i],-8.f,8.f)*excitation,d);r[i]=predelay(1,std::clamp(r[i],-8.f,8.f)*excitation,d);delayPos=(delayPos+1)%delay[0].size();}
            if(fading){std::copy_n(l,n,oldL);std::copy_n(r,n,oldR);fading->process(oldL,oldR,n);}
            if(active)active->process(l,r,n);else{std::fill_n(l,n,0);std::fill_n(r,n,0);}
            for(int i=0;i<n;++i){
                const float fade=transitionWarmup>0?0.f:(transitionRemaining>0?1.f-float(transitionRemaining)/float(transitionLength):1.f);
                if(transitionWarmup>0||transitionRemaining>0){l[i]=(fading?oldL[i]:0.f)*(1-fade)+l[i]*fade;r[i]=(fading?oldR[i]:0.f)*(1-fade)+r[i]*fade;if(transitionWarmup>0)--transitionWarmup;else --transitionRemaining;}
                const float cutoffLow=next(lo),cutoffHigh=next(hi),amountLow=next(loOn),amountHigh=next(hiOn);
                const float al=cutoffCoefficient(cutoffLow,cachedLowCutoff,cachedLowCoefficient);
                const float ah=cutoffCoefficient(cutoffHigh,cachedHighCutoff,cachedHighCoefficient);
                std::array<float,2>w{l[i],r[i]};
                for(int ch=0;ch<2;++ch){float hp=w[ch];for(int stage=0;stage<2;++stage){lowState[ch][stage]+=al*(hp-lowState[ch][stage]);hp-=lowState[ch][stage];}w[ch]+=amountLow*(hp-w[ch]);float lp=w[ch];for(int stage=0;stage<2;++stage){highState[ch][stage]+=ah*(lp-highState[ch][stage]);lp=highState[ch][stage];}w[ch]+=amountHigh*(lp-w[ch]);w[ch]=finite(w[ch]);wetPeakValue=std::max(wetPeakValue,std::abs(w[ch]));}
                const float m=next(mix),g=next(gain),b=next(bypass);
                // No IR means dry-safe, including SOLO WET. During replacement a
                // retiring valid IR is still a valid wet source until faded out.
                const float availableNow=active&&active->frames>0?1.f:0.f;
                const float availableOld=fading&&fading->frames>0?1.f:0.f;
                const float availability=fade<1?availableOld*(1-fade)+availableNow*fade:availableNow;
                const float wetAmount=m*(1-b),dryReduction=wetAmount*availability;
                const float effectiveGain=g+(1-g)*b;
                left[offset+i]=(wetAmount==0||availability==0)&&effectiveGain==1?dryL[i]:finite((dryL[i]*(1-dryReduction)+wetAmount*w[0])*effectiveGain);
                if(right)right[offset+i]=(wetAmount==0||availability==0)&&effectiveGain==1?dryR[i]:finite((dryR[i]*(1-dryReduction)+wetAmount*w[1])*effectiveGain);
                peakValue=std::max(peakValue,std::abs(left[offset+i]));if(right)peakValue=std::max(peakValue,std::abs(right[offset+i]));
            }
            if(transitionRemaining==0&&transitionWarmup==0){if(fading){retired.store(fading,std::memory_order_release);fading=nullptr;}activeLength.store(active?active->frames:0);}
        }
        peak.store(peakValue,std::memory_order_relaxed);wetPeak.store(wetPeakValue,std::memory_order_relaxed);
    }
    std::atomic<float> peak{0},wetPeak{0};
private:
    struct Engine {
        reverb::PreparedConvolution convolver;int frames=0;
        void initialise(juce::AudioBuffer<float>&&ir,double,int){frames=ir.getNumSamples();convolver.prepare(ir.getReadPointer(0),ir.getReadPointer(std::min(1,ir.getNumChannels()-1)),size_t(frames));}
        void process(float*l,float*r,int n)noexcept{convolver.process(l,r,n);}
    };
    struct Smooth {float current=0,target=0,step=0;int remaining=0;};
    static float safe(float x,float a,float b,float fallback)noexcept{return std::isfinite(x)?std::clamp(x,a,b):fallback;}
    static float finite(float x)noexcept{return std::isfinite(x)?x:0;}
    static juce::Result validate(const juce::AudioBuffer<float>& d,double sr){
        if(!std::isfinite(sr)||sr<8000||sr>192000)return juce::Result::fail("IR sample rate must be 8-192 kHz");
        if(d.getNumChannels()<1||d.getNumChannels()>2)return juce::Result::fail("IR must have one or two channels");
        if(d.getNumSamples()<1||d.getNumSamples()>maxSourceFrames||d.getNumSamples()>sr*maxSourceSeconds)return juce::Result::fail("IR must be non-empty and no longer than 10 seconds");
        double energy=0;for(int c=0;c<d.getNumChannels();++c)for(int i=0;i<d.getNumSamples();++i){const float v=d.getSample(c,i);if(!std::isfinite(v)||std::abs(v)>8)return juce::Result::fail("IR contains non-finite samples or samples above +18 dBFS");energy+=double(v)*v;}
        return energy>1.e-20?juce::Result::ok():juce::Result::fail("IR contains no usable signal");
    }
    std::unique_ptr<Engine> build(const juce::AudioBuffer<float>& data,double rate,float size){
        // Complete boundary support and fixed causal latency are prepared here.
        auto rendered=reverb::resampleIR(data,rate/(size*.01),sampleRate);
        auto e=std::make_unique<Engine>();e->initialise(std::move(rendered),sampleRate,blockSize);return e;
    }
    void publish(std::unique_ptr<Engine> e){reclaim();delete pending.exchange(e.release(),std::memory_order_acq_rel);}
    void reclaim(){delete retired.exchange(nullptr,std::memory_order_acq_rel);}
    void adopt()noexcept{
        if(fading||retired.load(std::memory_order_acquire))return;
        if(auto*nextEngine=pending.exchange(nullptr,std::memory_order_acq_rel)){
            fading=active;active=nextEngine;transitionLength=std::max(1,int(sampleRate*.05));transitionRemaining=transitionLength;transitionWarmup=publishedLatency.load();
            activeLength.store(std::max(active->frames,publishedLatency.load()+transitionLength+transitionWarmup));activePresent.store(active->frames>0);
        }
    }
    void clearEngines(){delete pending.exchange(nullptr);delete retired.exchange(nullptr);delete active;delete fading;active=nullptr;fading=nullptr;activePresent.store(false);activeLength.store(0);}
    void resetDsp()noexcept{for(auto&d:delay)std::fill(d.begin(),d.end(),0);for(auto&d:dryDelay)std::fill(d.begin(),d.end(),0);dryPos=0;cachedLowCutoff=cachedHighCutoff=-1.f;lowState={};highState={};delayPos=0;transitionRemaining=transitionWarmup=0;first=true;peak.store(0);wetPeak.store(0);publishedRate.store(sampleRate);}
    // Controls use the same coefficient throughout a settled segment. Preserve
    // every ramp sample exactly while avoiding repeated transcendental work.
    float cutoffCoefficient(float cutoff,float& previous,float& coefficient)noexcept{
        if(cutoff!=previous){coefficient=1-std::exp(float(-2*juce::MathConstants<double>::pi)*std::min(cutoff,float(sampleRate*.45))/float(sampleRate));previous=cutoff;}
        return coefficient;
    }
    void setTarget(Smooth&s,float t)noexcept{if(first){s.current=s.target=t;s.remaining=0;}else if(t!=s.target){s.target=t;s.remaining=std::max(1,int(sampleRate*.02));s.step=(t-s.current)/s.remaining;}}
    static float next(Smooth&s)noexcept{if(s.remaining>0){s.current+=s.step;if(--s.remaining==0)s.current=s.target;}return s.current;}
    float predelay(int channel,float x,float samples)noexcept{auto&d=delay[size_t(channel)];d[delayPos]=x;double read=double(delayPos)-samples;if(read<0)read+=d.size();const auto i=size_t(read);const float frac=float(read-i);return d[i]*(1-frac)+d[(i+1)%d.size()]*frac;}
    void makeDisplay(){Display nextDisplay;auto&display=nextDisplay;display.loaded=source.getNumSamples()>0;display.sourceRate=sourceRate;display.channels=source.getNumChannels();display.sourceFrames=source.getNumSamples();display.sizePercent=sizePercent;display.seconds=sourceRate>0?source.getNumSamples()/sourceRate*sizePercent*.01:0;display.renderedFrames=sampleRate>0&&sourceRate>0?reverb::resampledIRFrames(source.getNumSamples(),sourceRate/(sizePercent*.01),sampleRate):0;
        for(size_t k=0;k<display.waveform.size();++k){const int firstSample=int(k*size_t(source.getNumSamples())/display.waveform.size()),last=std::max(firstSample+1,int((k+1)*size_t(source.getNumSamples())/display.waveform.size()));float p=0;double e=0;int count=0;for(int i=firstSample;i<std::min(last,source.getNumSamples());++i)for(int ch=0;ch<source.getNumChannels();++ch){const float v=source.getSample(ch,i);if(std::abs(v)>std::abs(p))p=v;e+=double(v)*v;++count;}display.waveform[k]=p;display.envelope[k]=count?float(std::sqrt(e/count)):0;}const std::lock_guard<std::mutex> displayLock(displayMutex);this->display=nextDisplay;}
    mutable std::mutex controlMutex,displayMutex;
    juce::AudioBuffer<float>source,scratch;Display display;double sampleRate=0,sourceRate=0;int blockSize=1;float sizePercent=100;
    Engine*active=nullptr,*fading=nullptr;std::atomic<Engine*>pending{nullptr},retired{nullptr};
    std::atomic<bool>prepared{false},activePresent{false};std::atomic<int>activeLength{0},publishedLatency{0};std::atomic<double>publishedRate{0};
    std::array<std::vector<float>,2>delay,dryDelay;size_t delayPos=0,dryPos=0;
    std::array<std::array<float,2>,2>lowState{},highState{};
    float cachedLowCutoff=-1.f,cachedHighCutoff=-1.f,cachedLowCoefficient=0,cachedHighCoefficient=0;
    Smooth mix,gain,bypass,pre,lo,hi,loOn,hiOn;bool first=true;int transitionRemaining=0,transitionLength=1,transitionWarmup=0;
    static_assert(std::atomic<Engine*>::is_always_lock_free);
};
} // namespace hem
