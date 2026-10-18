#pragma once
#include "TakeRecorder.h"
#include "../Plugins/PluginRegistry.h"
namespace hem {
// Dedicated tracking rack: only actual processor latency, no fixed 6 ms reserve
// for an empty slot. Structural edits require monitoring OFF and recording idle.
// Parameters/bypass remain live. Structural publication uses host graph-control admission.
class TrackingInsertChain final {
public:
    struct Node {std::unique_ptr<ModuleProcessor> processor;HostDelay dryDelay;Ramp wet;std::atomic<bool> bypassed{false};};
    void prepare(double,int);
    // Legacy/raw construction for restoration and callers with stored state.
    std::unique_ptr<Node> makeNode(const juce::String&,double,int);
    // Explicit user insertion: chooses Reference v2 unless Legacy v1 selected.
    std::unique_ptr<Node> makeNewNode(int choice,double,int);
    std::unique_ptr<Node> exchange(int,std::unique_ptr<Node>);
    ModuleProcessor* processor(int i) const {return slots[size_t(i)]?slots[size_t(i)]->processor.get():nullptr;}
    void bypass(int i,bool b){if(slots[size_t(i)])slots[size_t(i)]->bypassed.store(b,std::memory_order_relaxed);}
    bool bypassed(int i) const {return slots[size_t(i)]&&slots[size_t(i)]->bypassed.load(std::memory_order_relaxed);}
    void process(float*,float*,int,double bpm=120) noexcept;
    int latency() const {int n=0;for(const auto& p:slots)if(p)n+=p->processor->getLatencySamples();return n;}
private:
    std::unique_ptr<Node> prepareNode(std::unique_ptr<ModuleProcessor>,double,int);
    std::array<std::unique_ptr<Node>,8> slots;
    std::array<std::vector<float>,2> dry;
    juce::AudioBuffer<float> view;juce::MidiBuffer midi;
    int maximumBlock=256;
};
class LiveVocalManager final {
public:
    enum class Monitor {Off,DrySoftware,InsertsSoftware,InterfaceDirect};
    void prepare(double,int,int enabledInputCount);
    void release(){stopTake(true);++generation;monitor=Monitor::Off;}
    // info buffer contains NativeAudioDeviceHost compacted active inputs.
    // Call BEFORE clearing/writing the host output region. n <= prepared block.
    void capture(const juce::AudioBuffer<float>&,int start,int n,double timelineSeconds,double bpm=120) noexcept;
    void addMonitor(juce::AudioBuffer<float>& master,int n) noexcept;
    bool setInput(int compactChannel){if(recorder.busy()||compactChannel<0||compactChannel>=channels)return false;input.store(compactChannel);return true;}
    int selectedInput() const {return input.load();}
    unsigned preparationGeneration() const noexcept {return generation;} // serialized host control-thread lifecycle
    void setMonitor(Monitor m){monitor.store(m);}
    Monitor monitoring() const {return monitor.load();}
    bool setPrintInserts(bool b){if(recorder.busy())return false;printInserts.store(b,std::memory_order_relaxed);return true;} // control thread, recording idle
    bool printing() const {return printInserts.load(std::memory_order_relaxed);}
    void setMonitorGain(float db){monitorDb.store(std::clamp(db,-60.f,6.f));}
    float inputPeak() const {return peak.load();}
    int monitorLatency() const {return monitoring()==Monitor::InsertsSoftware?rack.latency():0;}
    bool configureTake(TakeRecorder::Setup&); // callback excluded; then recorder.start(setup) OUTSIDE that lock
    void stopTake(bool endOfStream=false); // callback excluded; drain only reported insert delay
    TrackingInsertChain rack;TakeRecorder recorder;
private:
    // Written by capture; stopTake reads only with the callback excluded.
    double lastCaptureBpm=120;
    double rate=96000,lastCaptureEnd=0;int channels=0,pendingDrain=0;unsigned generation=0,takeGeneration=0;
    juce::AudioBuffer<float> dry,processed;
    std::atomic<int> input{0};std::atomic<Monitor> monitor{Monitor::Off};
    std::atomic<float> monitorDb{0},peak{0};
    std::vector<Ramp> inputWeights;
    Ramp dryMix,wetMix,level;std::atomic<bool> printInserts{false};bool takeDrainDone=true;
};
}
