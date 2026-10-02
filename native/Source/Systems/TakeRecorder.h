#pragma once
#include <juce_audio_formats/juce_audio_formats.h>
#include <array>
namespace hem {
// Audio thread is the only FIFO producer. The disk worker is its only reader.
// Observe a producer-quiescent callback barrier before start. Launch start()
// OUTSIDE the device lock; no old take may be active. stop/destruction require
// producer exclusion. The host generation check rejects device changes at arm.
class TakeRecorder final : private juce::Thread {
public:
    enum class State {Idle,Opening,Armed,Recording,Stopping,Complete,Failed};
    struct Setup {
        juce::File file;double rate=96000;
        int channels=1,inputLatency=0,outputLatency=0,backingLatency=0,printedLatency=0;
        juce::String inputName,inputChannelName,insertStateXml;int inputChannel=0;
        double manualOffsetMs=0;
    };
    TakeRecorder();~TakeRecorder() override;
    bool start(const Setup&); // New file only, never overwrites a take.
    void stop() noexcept;
    void push(const float*,const float*,int,double timelineSeconds) noexcept;
    State state() const noexcept {return status.load(std::memory_order_acquire);}
    bool busy() const noexcept {auto s=state();return isThreadRunning()||s==State::Opening||s==State::Armed||s==State::Recording||s==State::Stopping;}
    juce::String result() const; // Message thread only, after !busy().
    juce::File completedFile() const {return state()==State::Complete?setup.file:juce::File();}
    std::uint64_t framesWritten() const noexcept {return written.load();}
private:
    static constexpr int packetFrames=256,capacity=512;
    struct Packet {std::array<float,packetFrames> l{},r{};int frames=0;};
    juce::AbstractFifo fifo{capacity};std::array<Packet,capacity> packets;
    void run() override;
    Setup setup;juce::String outcome;juce::File partial;
    std::atomic<State> status{State::Idle};
    std::atomic<bool> stopRequested{false},overflow{false};
    std::atomic<std::uint64_t> written{0},invalidSamples{0};
    std::atomic<double> firstTimeline{-1};
};
}
