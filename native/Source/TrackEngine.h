#pragma once
#include "InsertChain.h"
#include "StemPlayback.h"
#include "Systems/AudioMeters.h"
namespace hem {
// A bounded set of stereo tracks. Readers stream through JUCE read-ahead;
// each track has its own processor ownership and latency-compensated insert bus.
struct TrackEngine {
    std::unique_ptr<StemPlayback> playback=std::make_unique<StemPlayback>();
    juce::AudioTransportSource& transport() noexcept {return playback->transport;}
    const juce::AudioTransportSource& transport() const noexcept {return playback->transport;}
    InsertChain chain;
    juce::AudioBuffer<float> scratch;
    juce::String name{"EMPTY TRACK"};
    double sourceRate=0;
    std::atomic<float> trimDb{0};
    std::atomic<bool> muted{false},solo{false};
    Ramp gainRamp;
    AudioMeters signalMeters;
    bool loaded=false;
    void prepare(double rate,int block){
        scratch.setSize(2,block);chain.prepare(rate,block);signalMeters.prepare(rate);
        gainRamp.reset(rate,gain(trimDb.load()));transport().prepareToPlay(block,rate);
    }
    void release(){transport().releaseResources();chain.release();signalMeters.prepare(0);}
    void render(juce::AudioBuffer<float>& sum,int n,bool anySolo,const float* inputGains){
        juce::AudioSourceChannelInfo region(&scratch,0,n);region.clearActiveBufferRegion();
        transport().getNextAudioBlock(region);
        auto* l=scratch.getWritePointer(0);auto* r=scratch.getWritePointer(1);
        for(int i=0;i<n;++i){l[i]*=inputGains[i];r[i]*=inputGains[i];}
        chain.process(l,r,n);
        const bool audible=!muted.load(std::memory_order_relaxed)&&(!anySolo||solo.load(std::memory_order_relaxed));
        gainRamp.set(audible?gain(trimDb.load(std::memory_order_relaxed)):0.f);
        auto* sl=sum.getWritePointer(0);auto* sr=sum.getWritePointer(1);
        for(int i=0;i<n;++i){const float g=gainRamp.next();l[i]*=g;r[i]*=g;sl[i]+=l[i];sr[i]+=r[i];}
        signalMeters.push(l,r,n);
    }
};
}
