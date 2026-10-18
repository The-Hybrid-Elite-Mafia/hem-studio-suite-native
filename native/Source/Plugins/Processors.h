#pragma once
#include <JuceHeader.h>
#include "../DSP/DspCore.h"
#include <cstdint>
#include "../Systems/PluginTelemetry.h"

namespace hem {
struct ParameterSpec {
    const char* id; const char* name;float minimum,maximum,initial,step;
    const char* unit;bool toggle=false;
};
// Raw detector values are amplitude for peaks, dB for dynamics, and -1..+1
// for correlation. A zero-initialized detector is NOT evidence of audio.
enum class PluginMeterKind { samplePeak, correlation, gainReduction, envelopeGain, firTruePeak };
struct PluginMeterSnapshot {
    bool isPrepared=false, hasProcessedAudio=false, signalPresent=false, valueAvailable=false;
    float value=0, auxiliary=0;
    uint32_t blockSamples=0, processedBlocks=0;
    double sampleRate=0;
    int latencySamples=0;
};
class ModuleProcessor : public juce::AudioProcessor {
public:
    ModuleProcessor(juce::String identifier,juce::String title,std::vector<ParameterSpec>,
                    PluginMeterKind=PluginMeterKind::samplePeak);
    const juce::String getName() const override {return title;}
    bool acceptsMidi() const override {return false;} bool producesMidi() const override {return false;}
    bool isMidiEffect() const override {return false;} double getTailLengthSeconds() const override {return .02;}
    int getNumPrograms() override {return 1;}int getCurrentProgram() override {return 0;}
    void setCurrentProgram(int) override {} const juce::String getProgramName(int) override {return "Current";}
    void changeProgramName(int,const juce::String&) override {}
    bool hasEditor() const override {return true;}
    juce::AudioProcessorEditor* createEditor() override = 0;
    bool isBusesLayoutSupported(const BusesLayout&) const override;
    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*,int) override;
    void releaseResources() override { if(telemetry)telemetry->suspend(); } // Buffers live until off-thread destruction.
    virtual bool monitorOnly() const noexcept {return false;}
    virtual bool realtimeInsertAllowed() const noexcept {return true;}
    // External adapters keep their immutable preparation contract separate from
    // temporary capture gating. These hooks never touch a third-party instance.
    virtual bool isExternal() const noexcept {return false;}
    virtual bool processingReady() const noexcept {return true;}
    virtual bool gateForStateCapture(bool) noexcept {return false;}
    virtual uint64_t externalStateRevision() const noexcept {return 0;}
    virtual juce::String externalDescriptionXml() const {return {};}
    virtual juce::String externalDiagnostic() const {return {};}
    virtual float meter() const noexcept=0;
    virtual juce::String meterLabel() const=0;
    virtual bool usesWetScaledReductionLabel() const noexcept {return true;}
    virtual bool parameterActive(const juce::String&) const {return true;}
    virtual juce::String helpText() const {return {};}
    // One validated project BPM is captured at the outer audio callback boundary.
    virtual void setBlockTempo(double) noexcept {}
    virtual void configureFrozenExport(double bpm,bool) {setBlockTempo(bpm);}
    virtual bool validateStoredParameters(const juce::ValueTree&) const {return true;}
    virtual bool supportsStagedPreparation()const noexcept{return false;}
    virtual void stagePreparation(double,int){}
    virtual void cancelStagedPreparation()noexcept{}
    virtual int stagedLatencySamples()const noexcept{return getLatencySamples();}
    // Staging/message thread: return a validated migrated copy, or an invalid
    // tree. Default modules retain their existing session validation.
    virtual juce::ValueTree migrateParameterState(const juce::ValueTree& values) const {return values;}
    PluginTelemetry* pluginTelemetry() const noexcept {return telemetry.get();}
    PluginMeterKind meterKind() const noexcept {return kind;}
    PluginMeterSnapshot meterSnapshot() const noexcept;
    const std::vector<ParameterSpec>& parameterSpecs() const {return specs;}
    juce::AudioProcessorValueTreeState state;
    std::atomic<double> actualRate{96000};
protected:
    // Single audio writer: prepare/process must be serialized by the host.
    // No allocation, lock, GUI clock, or test-signal source in these calls.
    void captureInput(const juce::AudioBuffer<float>& b) noexcept { capturedGains={};if(telemetry)telemetry->input(b.getReadPointer(0),b.getReadPointer(1),b.getNumSamples()); }
    void captureGains(const std::array<float,4>& g) noexcept {capturedGains=g;}
    void resetMeterTelemetry() noexcept;
    void publishMeterPreparation() noexcept;
    void publishMeterBlock(const juce::AudioBuffer<float>&,float auxiliary=0) noexcept;
    float value(size_t i) const noexcept {return parameters[i]->load(std::memory_order_relaxed);}
private:
    std::unique_ptr<PluginTelemetry> telemetry;std::array<float,4> capturedGains{};
    juce::String id,title;std::vector<ParameterSpec> specs;
    const PluginMeterKind kind;
    // Atomic payload + bounded sequence read: a GUI snapshot cannot combine
    // new readiness with an old detector value. Seq-cst also orders the payload.
    std::atomic<uint32_t> meterSequence{0},meterFlags{0},meterSamples{0},meterBlocks{0};
    std::atomic<float> meterValue{0},meterAuxiliary{0};
    std::atomic<double> meterRate{0};
    std::atomic<int> meterLatency{0};
    static_assert(std::atomic<uint32_t>::is_always_lock_free && std::atomic<float>::is_always_lock_free
                  && std::atomic<double>::is_always_lock_free && std::atomic<int>::is_always_lock_free);
    std::vector<std::atomic<float>*> parameters;
    static juce::AudioProcessorValueTreeState::ParameterLayout layout(const std::vector<ParameterSpec>&);
};
class SipaProcessor final : public ModuleProcessor {
public:
    SipaProcessor();void prepareToPlay(double,int) override;
    void processBlock(juce::AudioBuffer<float>&,juce::MidiBuffer&) override;
    juce::AudioProcessorEditor* createEditor() override;
    float meter() const noexcept override {return core.correlation.load();}
    juce::String meterLabel() const override {return "L/R CORRELATION";}
private:StereoCore core;
};
class SurgicalEqProcessor final : public ModuleProcessor {
public:
    SurgicalEqProcessor();void prepareToPlay(double,int) override;
    void processBlock(juce::AudioBuffer<float>&,juce::MidiBuffer&) override;
    juce::AudioProcessorEditor* createEditor() override;
    EqParameters settings() const noexcept;
    float meter() const noexcept override {return core.peak.load();}
    juce::String meterLabel() const override {return "OUTPUT SAMPLE PEAK";}
private:EqCore core;
};
class VcaProcessor final : public ModuleProcessor {
public:
    VcaProcessor();void prepareToPlay(double,int) override;
    void processBlock(juce::AudioBuffer<float>&,juce::MidiBuffer&) override;
    juce::AudioProcessorEditor* createEditor() override;
    float meter() const noexcept override {return core.reduction.load();}
    juce::String meterLabel() const override {return "GAIN REDUCTION / dB";}
private:CompressorCore core;
};
class TruePeakProcessor final : public ModuleProcessor {
public:
    TruePeakProcessor();void prepareToPlay(double,int) override;
    void processBlock(juce::AudioBuffer<float>&,juce::MidiBuffer&) override;
    juce::AudioProcessorEditor* createEditor() override;
    float meter() const noexcept override {return core.truePeak.load();}
    float reduction() const noexcept {return core.reduction.load();}
    juce::String meterLabel() const override {return "OUTPUT PEAK ESTIMATE / 8x FIR";}
private:LimiterCore core;
};
std::unique_ptr<ModuleProcessor> createModule(int index);
} // namespace hem
