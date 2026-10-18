#include "../Source/MainComponent.h"
#include <array>
#include <iostream>
#include <stdexcept>

struct MainComponentTestAccess {
    static void offline(MainComponent& h) {
        h.deviceManager.closeAudioDevice();
        if (h.prepared) h.releaseResources();
        h.timerCallback();
    }
    static hem::TrackBank& bank(MainComponent& h) { return *h.bank; }
    static hem::LiveVocalManager& vocals(MainComponent& h) { return h.vocals; }
    static std::atomic<double>& bpm(MainComponent& h) { return h.projectBpm; }
};
namespace {
void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
struct Observation { double bpm=0; int frames=0; };
struct Probe final : hem::ModuleProcessor {
    std::array<Observation,32> observed{};
    int count=0; bool overflow=false;
    double tempo=0, replacement=0;
    std::atomic<double>* mutate=nullptr;
    Probe() : ModuleProcessor("tempo-probe", "Tempo probe", {}) {}
    void prepareToPlay(double, int) override {}
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    float meter() const noexcept override { return 0; }
    juce::String meterLabel() const override { return {}; }
    void setBlockTempo(double bpm) noexcept override { tempo=bpm; }
    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override {
        // Fixed preallocated observations; no allocation, lock, GUI or logging.
        if (count<int(observed.size())) observed[size_t(count)]={tempo,buffer.getNumSamples()};
        else overflow=true;
        ++count;
        if (mutate) { mutate->store(replacement,std::memory_order_relaxed); mutate=nullptr; }
    }
    void clear() noexcept { count=0; overflow=false; }
};
void verify(const Probe& p, double bpm, const std::array<int,4>& frames) {
    require(!p.overflow && p.count==4,"Probe did not receive exactly four callback subblocks");
    for (int i=0;i<4;++i) {
        require(p.observed[size_t(i)].bpm==bpm,"Tempo changed within an outer callback or next callback missed the change");
        require(p.observed[size_t(i)].frames==frames[size_t(i)],"Subblock frame count incorrect");
    }
}
}
int main() {
    juce::ScopedJuceInitialiser_GUI gui;
    try {
        MainComponent host;
        MainComponentTestAccess::offline(host);
        std::array<Probe*,14> probes{};
        auto& bank=MainComponentTestAccess::bank(host);
        for (int i=0;i<13;++i) {
            auto p=std::make_unique<Probe>(); probes[size_t(i)]=p.get();
            auto& chain=bank.tracks[size_t(i)].chain;
            auto node=chain.stageNode(std::move(p),48000,256,chain.slotLatency());
            require(chain.publishStopped(0,std::move(node)),"Could not install stopped-track probe");
        }
        auto vocalNode=std::make_unique<hem::TrackingInsertChain::Node>();
        auto vocal=std::make_unique<Probe>(); probes[13]=vocal.get();
        vocalNode->processor=std::move(vocal);
        MainComponentTestAccess::vocals(host).rack.exchange(0,std::move(vocalNode));
        MainComponentTestAccess::vocals(host).prepare(48000,256,2);
        juce::AudioBuffer<float> audio(2,800); audio.clear();
        const juce::AudioSourceChannelInfo info(&audio,11,785); // 256+256+256+17, nonzero offset
        auto& bpm=MainComponentTestAccess::bpm(host);
        static_assert(std::atomic<double>::is_always_lock_free);
        const std::array<int,4> frames{256,256,256,17};
        // Mutation first in vocals, then early/middle/last backing track. All
        // thirteen backing chains and vocals must preserve the outer snapshot.
        for (int mutationSite : {13,0,6,12}) {
            bpm.store(93.25);
            for (auto* p:probes) p->clear();
            probes[size_t(mutationSite)]->mutate=&bpm;
            probes[size_t(mutationSite)]->replacement=177.5;
            host.getNextAudioBlock(info);
            require(bpm.load()==177.5,"Test mutation did not occur");
            for (const auto* p:probes) verify(*p,93.25,frames);
            for (auto* p:probes) p->clear();
            host.getNextAudioBlock(info);
            for (const auto* p:probes) verify(*p,177.5,frames);
        }
        std::cout << "PASS actual MainComponent outer callback: four mutation sites, all 13 tracks plus vocal rack, 256/256/256/17 subblocks, nonzero start offset, next-callback BPM adoption; fixed bounded allocation-free probes\n";
        return 0;
    } catch (const std::exception& e) { std::cerr<<"FAIL: "<<e.what()<<'\n'; return 1; }
}
