#pragma once
#include "Plugins/Processors.h"
#include <array>

namespace hem {
// Message-thread ownership; audio-thread borrowed pointers and bounded fades.
// Audio process() never acquires a lock, allocates memory, or destroys a node.
// The owner must hold AudioDeviceManager::getAudioCallbackLock() for prepare(),
// release(), publish() and collectRetired(). stageNode() deliberately runs outside
// that lock. These lifecycle methods are not called by process().
class InsertChain {
public:
 static constexpr int slotCount=8;
 struct PreparedNode {
    std::unique_ptr<ModuleProcessor> processor;
    HostDelay compensation;
    void prepare(double sr,int block,int budget);
 };
 InsertChain();
 void prepare(double rate,int maximumBlock);
 void release();
 std::unique_ptr<PreparedNode> stageNode(std::unique_ptr<ModuleProcessor>,double rate,int block,int budget);
 // Only for an unpublished, prepared, empty chain on the staging worker.
 void installInitial(int,std::unique_ptr<PreparedNode>,bool bypassed);
 bool publish(int,std::unique_ptr<PreparedNode>); // null means remove.
 // Message thread only, with NO audio callback running. Publishes without
 // waiting for an unavailable callback; old ownership remains in retired.
 bool publishStopped(int,std::unique_ptr<PreparedNode>);
 void completePendingStopped();
 std::unique_ptr<PreparedNode> collectRetired(int);
 ModuleProcessor* activeProcessor(int) const noexcept;
 bool isBusy(int) const noexcept;
 void bypass(int,bool) noexcept;bool isBypassed(int) const noexcept;
 void process(float* left,float* right,int samples) noexcept;
 bool swapSlotsStopped(int first,int second);
 int latencySamples() const noexcept {return latencyBudget*slotCount;}
 int slotLatency() const noexcept {return latencyBudget;}
private:
 struct Slot {
    std::vector<std::unique_ptr<PreparedNode>> owned;
    std::atomic<PreparedNode*> active{nullptr},incoming{nullptr},retired{nullptr};
    std::atomic<bool> request{false},busy{false},bypassed{false};
    HostDelay dryDelay;Ramp wet,replace;
    bool transitioning=false;int warmup=0;
 };
 std::array<Slot,slotCount> slots;
 std::array<std::vector<float>,2> dry,oldWet,newWet;
 juce::AudioBuffer<float> view;
 juce::MidiBuffer midi;
 double sampleRate=96000;int maximumBlock=256;std::atomic<int> latencyBudget{576};
 bool run(PreparedNode*,float*,float*,int) noexcept;
};
}
