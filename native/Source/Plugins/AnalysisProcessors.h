#pragma once
#include "Processors.h"
#include "../DSP/AnalysisPluginCore.h"
namespace hem {
struct LoudnessDisplay {analysis::LoudnessFrame measurement;std::array<float,600>momentary{},shortTerm{},integrated{};std::array<double,600>times{};unsigned historyCount=0,historyCursor=0,epoch=0;std::uint64_t dropped=0;};
struct SpectrumDisplay {analysis::SpectrumFrame measurement;static constexpr int columns=600,rows=128;std::array<std::array<float,rows>,columns>spectrogram{};std::array<double,columns>times{};unsigned count=0,cursor=0,epoch=0;std::uint64_t dropped=0;};
struct DeltaDisplay {static constexpr int columns=256;std::array<std::array<float,columns>,3>minimum{},maximum{};unsigned count=0,cursor=0;double sampleRate=0;std::uint64_t frameEnd=0;analysis::DeltaCore::Alignment alignment;};
class LoudnessAnalysisWorker final:private juce::Thread {
public:
    LoudnessAnalysisWorker():Thread("HEM loudness analysis"){}
    ~LoudnessAnalysisWorker()override{stop();}
    void prepare(double);void stop();
    // Message/staging-thread lifecycle: restarting a prepared consumer never
    // changes capture counters. This also permits deterministic overrun tests.
    bool startPrepared(){return rate>0&&startThread();}
    void push(const float*l,const float*r,int n)noexcept{
        const auto epoch=generation.load(std::memory_order_acquire);
        // Single audio producer establishes the reset boundary BEFORE capture.
        // A reset racing an in-flight old block cannot charge old drops to a new
        // epoch; each publication carries the epoch observed by that block.
        if(producerEpoch!=epoch){producerEpoch=epoch;producerDropBaseline=transport.dropped();}
        transport.push({l,r},n,epoch);
        dropRevision.fetch_add(1);publishedDropEpoch.store(epoch);
        publishedEpochDrops.store(transport.dropped()-producerDropBaseline);
        dropRevision.fetch_add(1);
    }
    void reset()noexcept{generation.fetch_add(1,std::memory_order_acq_rel);}
    std::uint64_t droppedSamples()const noexcept{return readEpochDrops(generation.load(std::memory_order_acquire));}
    bool copy(LoudnessDisplay&)const;
private:
    std::uint64_t readEpochDrops(unsigned epoch)const noexcept{
        for(int attempt=0;attempt<3;++attempt){
            const auto before=dropRevision.load();if(before&1u)continue;
            const auto publishedEpoch=publishedDropEpoch.load();const auto drops=publishedEpochDrops.load();
            if(before==dropRevision.load())return publishedEpoch==epoch?drops:0;
        }
        return 0; // The consumer retains its previous count and tries next poll.
    }
    void run()override;analysis::Transport<2>transport;analysis::LoudnessCore core;
    std::atomic<unsigned>generation{1},dropRevision{0},publishedDropEpoch{0};
    std::atomic<std::uint64_t>publishedEpochDrops{0};
    unsigned producerEpoch=0;std::uint64_t producerDropBaseline=0;
    mutable juce::CriticalSection lock;LoudnessDisplay display;double rate=0;
    unsigned workerEpoch=0;std::uint64_t expected=0,lastPublish=0;
};
class SpectrumAnalysisWorker final:private juce::Thread {
public:SpectrumAnalysisWorker():Thread("HEM spectrum analysis"){}~SpectrumAnalysisWorker()override{stop();}void prepare(double);void stop();void push(const float*l,const float*r,int n)noexcept{transport.push({l,r},n,generation.load(std::memory_order_relaxed));}void reset()noexcept{generation.fetch_add(1);}void settings(const analysis::SpectrumSettings&)noexcept;bool copy(SpectrumDisplay&)const;
private:void run()override;void publish(const analysis::SpectrumFrame&);analysis::Transport<2>transport;analysis::SpectrumCore core;std::atomic<unsigned>generation{1};std::array<std::atomic<float>,10>controls{};mutable juce::CriticalSection lock;SpectrumDisplay display;double rate=0,lastColumn=0;unsigned workerEpoch=0;std::uint64_t expected=0,timelineOrigin=0;
};
class DeltaAnalysisWorker final:private juce::Thread {
public:DeltaAnalysisWorker():Thread("HEM delta analysis"){}~DeltaAnalysisWorker()override{stop();}void prepare(double);void stop();void push(const std::array<const float*,10>&p,int n)noexcept{transport.push(p,n);}void skip(int n)noexcept{transport.skip(n);}bool copy(DeltaDisplay&)const;int alignmentSamples()const noexcept{return alignment.load(std::memory_order_relaxed);}bool alignmentValid()const noexcept{return valid.load(std::memory_order_acquire);}void requestAlignment()noexcept{alignRequested.store(true,std::memory_order_release);}unsigned resultSerial()const noexcept{return serial.load(std::memory_order_acquire);}
private:void run()override;analysis::Transport<10>transport;mutable juce::CriticalSection lock;DeltaDisplay display;std::array<float,4096>dry{},processed{},dryRight{},processedRight{};int fill=0,decimation=0;std::array<float,3>minimum{},maximum{};double rate=0;std::uint64_t expected=0;std::atomic<int>alignment{0};std::atomic<bool>valid{false},alignRequested{false};std::atomic<unsigned>serial{0};
};
// Strict transactional state boundary shared only by these new modules.
class AnalysisProcessor:public ModuleProcessor {
public:AnalysisProcessor(const char*id,const char*name,std::vector<ParameterSpec>p):ModuleProcessor(id,name,std::move(p)){actualRate.store(0);}bool processingReady()const noexcept override{return ready.load(std::memory_order_acquire);}float meter()const noexcept override{return peak.load(std::memory_order_relaxed);}juce::String meterLabel()const override{return "OUTPUT SAMPLE PEAK / dBFS";}double getTailLengthSeconds()const override{return 0;}juce::ValueTree migrateParameterState(const juce::ValueTree&)const override;void setStateInformation(const void*,int)override;
protected:float value(size_t i)const noexcept{const float v=ModuleProcessor::value(i);return std::isfinite(v)?v:parameterSpecs()[i].initial;}void publishPeak(const juce::AudioBuffer<float>&)noexcept;std::atomic<bool>ready{false};std::atomic<float>peak{0};
};
class LoudnessLabProcessor final:public AnalysisProcessor {
public:LoudnessLabProcessor();void prepareToPlay(double,int)override;void processBlock(juce::AudioBuffer<float>&,juce::MidiBuffer&)override;void releaseResources()override;juce::AudioProcessorEditor*createEditor()override;bool readAnalysis(LoudnessDisplay&d)const{return processingReady()&&worker.copy(d);}void resetAnalysis()noexcept{worker.reset();}juce::String helpText()const override{return "Stereo BS.1770 loudness; 400 ms M, 3 s S; gated I/LRA invalidate after dropped samples. Four-hour capacity. 8x FIR peak estimate, not certified. VU: 300 ms average rectified, 0 VU = -18 dBFS sine RMS. Audio passes unchanged.";}
private:LoudnessAnalysisWorker worker;
};
class SpectrumSpectrogramProcessor final:public AnalysisProcessor {
public:SpectrumSpectrogramProcessor();void prepareToPlay(double,int)override;void processBlock(juce::AudioBuffer<float>&,juce::MidiBuffer&)override;void releaseResources()override;juce::AudioProcessorEditor*createEditor()override;bool readAnalysis(SpectrumDisplay&d)const{return processingReady()&&worker.copy(d);}void resetAnalysis()noexcept{worker.reset();}juce::String helpText()const override{return "Calibrated bin-centered spectrum; RMS = sinusoidal bin RMS, not broadband noise density. Channel 0 stereo max, 1 L, 2 R, 3 mid, 4 side. Times follow captured samples; dropped ranges are not interpolated. Audio passes unchanged.";}
private:SpectrumAnalysisWorker worker;
};
class DeltaMonitorProcessor final:public AnalysisProcessor,public analysis::ReferenceConsumer {
public:DeltaMonitorProcessor();void prepareToPlay(double,int)override;void processBlock(juce::AudioBuffer<float>&,juce::MidiBuffer&)override;void releaseResources()override;juce::AudioProcessorEditor*createEditor()override;
    // Audio-thread only, immediately before processBlock, pointer lifetime one
    // call. Host supplies chain-start tap already delayed by upstream SLOT budgets.
    void setReferenceBlock(analysis::ReferenceBlock b)noexcept override{reference=b;}
    analysis::DeltaFrame deltaSnapshot()const noexcept;
    bool readAnalysis(DeltaDisplay&d)const{return processingReady()&&worker.copy(d);}
    void requestAlignment()noexcept{awaitingAlignment=true;requestedSerial=worker.resultSerial()+1;worker.requestAlignment();}
    bool applyPendingAlignment(); // Message thread only: commit measured result to saved trim parameter
    juce::String helpText()const override{return "Dry = independent chain-start tap, aligned by host slot delay; processed = this insert input. Fixed 3 ms latency. Delay trim +/-3 ms shifts dry relative to processed. Missing/stale reference emits latency-correct processed signal and marks delta unavailable. Auto Align requires broadband correlation; ambiguous material leaves alignment unchanged. Solo/gain affect rendered audio.";}
private:analysis::DeltaCore core;DeltaAnalysisWorker worker;analysis::ReferenceBlock reference;std::array<std::vector<float>,10>scratch;int blockLimit=0;std::uint64_t expectedTimeline=0;bool haveTimeline=false,awaitingAlignment=false;unsigned requestedSerial=0;std::atomic<unsigned>revision{0},flags{0};std::array<std::atomic<float>,7>published{};std::atomic<int>alignment{0};std::atomic<std::uint64_t>frameEnd{0};void publishDelta(const analysis::DeltaFrame&)noexcept;
};
} // namespace hem
