#pragma once
#include "Processors.h"
#include "../DSP/ConvolutionReverbCore.h"
#include "../DSP/AlgorithmicReverbCore.h"
namespace hem {
class ConvolutionReverbProcessor final: public ModuleProcessor, private juce::Thread {
public:
    ConvolutionReverbProcessor();~ConvolutionReverbProcessor()override;
    void prepareToPlay(double,int)override;
    void processBlock(juce::AudioBuffer<float>&,juce::MidiBuffer&)override;
    void reset()override{core.reset();}
    juce::AudioProcessorEditor* createEditor()override;
    double getTailLengthSeconds()const override{return core.tailSeconds(value(0));}
    bool processingReady()const noexcept override{return ready.load();}
    void releaseResources()override{ready.store(false);ModuleProcessor::releaseResources();}
    juce::ValueTree migrateParameterState(const juce::ValueTree&)const override;
    float meter()const noexcept override{return core.peak.load();}
    juce::String meterLabel()const override{return "OUTPUT SAMPLE PEAK / dBFS";}
    juce::String helpText()const override;
    bool parameterActive(const juce::String&)const override;
    void requestIRFile(const juce::File&); // Message thread: worker performs all I/O.
    juce::Result loadIRFileOffThread(const juce::File&,const juce::String& expectedSha256={});
    void captureResourceState(juce::ValueTree& parameters)const;
    juce::Result restoreResourceState(const juce::ValueTree& parameters);
    void getStateInformation(juce::MemoryBlock&)override;
    void setStateInformation(const void*,int)override;
    struct ResourceInfo{juce::String status="No IR loaded",path,name,sha256;double sourceRate=0;int channels=0,frames=0;bool ready=false,loading=false;};
    ResourceInfo resourceInfo()const;
    ConvolutionReverbCore::Display irDisplay()const{return core.displaySnapshot();}
    ConvolutionReverbSettings settings()const noexcept;
private:
    friend struct ConvolutionReverbTestAccess;
    void run()override;
    void loadQueuedFile(const juce::File&,std::uint64_t generation);
    void resourceFailure(const juce::String&);
    juce::Result decodeFile(const juce::File&,const juce::String&,const ResourceInfo* expected=nullptr,bool* metadataMismatch=nullptr);
    ConvolutionReverbCore core;std::atomic<bool>ready{false};
    mutable std::mutex resourceMutex;std::mutex resourceOperationMutex;ResourceInfo resource;
    juce::File requestedFile;bool filePending=false;std::uint64_t requestGeneration=0;
};
class AlgorithmicReverbProcessor final:public ModuleProcessor {
public:
    AlgorithmicReverbProcessor();
    bool processingReady()const noexcept override{return ready.load();}
    void releaseResources()override{ready.store(false);ModuleProcessor::releaseResources();}
    juce::ValueTree migrateParameterState(const juce::ValueTree&)const override;
    void setStateInformation(const void*,int)override;
    void prepareToPlay(double,int)override;
    void processBlock(juce::AudioBuffer<float>&,juce::MidiBuffer&)override;
    void reset()override{core.reset();}
    juce::AudioProcessorEditor* createEditor()override;
    AlgorithmicReverbSettings settings()const noexcept;
    double getTailLengthSeconds()const override{return core.tailSeconds(settings());}
    float meter()const noexcept override{return core.peak.load();}
    juce::String meterLabel()const override{return "OUTPUT SAMPLE PEAK / dBFS";}
    juce::String helpText()const override{return "Stereo feedback-delay network; Hall, Room and Plate are algorithmic tunings. DECAY is the nominal low-frequency RT60. Damping shortens high-frequency decay. Zero added latency. Mix 0% is dry.";}
    float wetTailDb()const noexcept{return core.tailLevelDb.load();}
    std::uint64_t tailSampleTime()const noexcept{return renderedFrames.load();}
private:AlgorithmicReverbCore core;std::atomic<bool>ready{false};std::atomic<std::uint64_t>renderedFrames{0};
};
}
