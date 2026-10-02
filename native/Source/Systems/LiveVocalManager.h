#pragma once
#include "TakeRecorder.h"
#include "../Plugins/PluginRegistry.h"
namespace hem {
// Dedicated tracking rack: only actual processor latency, no fixed 6 ms reserve
// for an empty slot. Structural edits require monitoring OFF and recording idle.
// Parameters/bypass remain live. All publication uses the host callback lock.
class TrackingInsertChain final {
public:
    struct Node {std::unique_ptr<ModuleProcessor> processor;HostDelay dryDelay;Ramp wet;bool bypassed=false;};
    void prepare(double,int);
    std::unique_ptr<Node> makeNode(const juce::String&,double,int);
    std::unique_ptr<Node> exchange(int,std::unique_ptr<Node>);
    ModuleProcessor* processor(int i) const {return slots[size_t(i)]?slots[size_t(i)]->processor.get():nullptr;}
    void bypass(int i,bool b){if(slots[size_t(i)])slots[size_t(i)]->bypassed=b;}
    bool bypassed(int i) const {return slots[size_t(i)]&&slots[size_t(i)]->bypassed;}
    void process(float*,float*,int) noexcept;
    int latency() const {int n=0;for(const auto& p:slots)if(p)n+=p->processor->getLatencySamples();return n;}
private:
    std::array<std::unique_ptr<Node>,8> slots;
    std::array<std::vector<float>,2> dry;
    juce::AudioBuffer<float> view;juce::MidiBuffer midi;
    int maximumBlock=256;
};
class LiveVocalManager final {
public:
    enum class Monitor {Off,DrySoftware,InsertsSoftware,InterfaceDirect};
    void prepare(double,int,int enabledInputCount);
    void release(){recorder.stop();++generation;monitor=Monitor::Off;}
    // info buffer still contains AudioSourcePlayer's compacted active inputs.
    // Call BEFORE clearing/writing the host output region. n <= prepared block.
    void capture(const juce::AudioBuffer<float>&,int start,int n,double timelineSeconds) noexcept;
    void addMonitor(juce::AudioBuffer<float>& master,int n) noexcept;
    bool setInput(int compactChannel){if(recorder.busy()||compactChannel<0||compactChannel>=channels)return false;input.store(compactChannel);return true;}
    int selectedInput() const {return input.load();}
    void setMonitor(Monitor m){monitor.store(m);}
    Monitor monitoring() const {return monitor.load();}
    bool setPrintInserts(bool b){if(recorder.busy())return false;printInserts=b;return true;} // callback excluded, recording idle
    bool printing() const {return printInserts;}
    void setMonitorGain(float db){monitorDb.store(std::clamp(db,-60.f,6.f));}
    float inputPeak() const {return peak.load();}
    int monitorLatency() const {return monitoring()==Monitor::InsertsSoftware?rack.latency():0;}
    bool configureTake(TakeRecorder::Setup&); // callback excluded; then recorder.start(setup) OUTSIDE that lock
    void stopTake(){recorder.stop();}
    TrackingInsertChain rack;TakeRecorder recorder;
private:
    double rate=96000;int channels=0;unsigned generation=0,takeGeneration=0;
    juce::AudioBuffer<float> dry,processed;
    std::atomic<int> input{0};std::atomic<Monitor> monitor{Monitor::Off};
    std::atomic<float> monitorDb{0},peak{0};
    std::vector<Ramp> inputWeights;
    Ramp dryMix,wetMix,level;bool printInserts=false;
};
}
