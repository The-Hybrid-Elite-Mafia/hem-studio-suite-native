#pragma once
#include "Processors.h"
#include "../DSP/DelayCore.h"

namespace hem {
class DelayProcessor : public ModuleProcessor {
public:
    explicit DelayProcessor(delay::Kind);
    void prepareToPlay(double,int) override;
    void releaseResources() override;
    bool supportsStagedPreparation()const noexcept override{return true;}
    void stagePreparation(double,int)override;
    void cancelStagedPreparation()noexcept override{staged.reset();}
    int stagedLatencySamples()const noexcept override{return core.latency();}
    void processBlock(juce::AudioBuffer<float>&,juce::MidiBuffer&) override;
    void setStateInformation(const void*,int) override;
    bool validateStoredParameters(const juce::ValueTree&) const override;
    bool processingReady()const noexcept override{return ready.load();}
    void setBlockTempo(double bpm)noexcept override;
    void configureFrozenExport(double bpm,bool audible) override;
    double getTailLengthSeconds()const override{return frozen?preparedTail.load():delay::liveSafetyTailSeconds;}
    float meter()const noexcept override{return outputPeak.load(std::memory_order_relaxed);}
    juce::String meterLabel()const override{return "OUTPUT SAMPLE PEAK / dBFS";}
    juce::String helpText()const override;
    juce::AudioProcessorEditor* createEditor() override;
    delay::Settings settings()const noexcept;
    delay::CapturedTail capturedTail(double rate,double bpm,bool audible=true)const noexcept;
    delay::Kind delayKind()const noexcept{return kind;}
    double bpm()const noexcept{return publishedBpm.load();}
    bool syncConflict()const noexcept{return conflict.load();}
    std::uint64_t protectionCount()const noexcept{return protection.load();}
    std::uint64_t meterPublication()const noexcept{return publication.load();}
    bool tapTempo(double monotonicMilliseconds); // message thread only
    struct Wave {std::array<float,64> samples{};double durationMs=0;bool valid=false;};
    Wave waveform()const noexcept;
private:
    bool validateTree(const juce::ValueTree&,bool root)const;
    delay::Kind kind;delay::Core core;
    std::unique_ptr<delay::Core> staged;double stagedRate=0;
    std::atomic<bool> ready{false},conflict{false};
    std::atomic<float> outputPeak{0};std::atomic<double> publishedBpm{120},preparedTail{2500};
    std::atomic<std::uint64_t> protection{0};
    std::atomic<std::uint64_t> publication{0};
    static_assert(std::atomic<std::uint64_t>::is_always_lock_free);
    std::atomic<unsigned> waveSequence{0};std::array<std::atomic<float>,64> wave{};
    std::atomic<double> waveDuration{0};
    bool frozen=false,frozenAudible=true;double previousTap=-1;
};
class MultitapDelayProcessor final:public DelayProcessor {public:MultitapDelayProcessor():DelayProcessor(delay::Kind::multitap){}};
class TapeDelayProcessor final:public DelayProcessor {public:TapeDelayProcessor():DelayProcessor(delay::Kind::tape){}};
}
