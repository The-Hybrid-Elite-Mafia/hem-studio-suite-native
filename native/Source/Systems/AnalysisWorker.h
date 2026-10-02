#pragma once
#include <juce_core/juce_core.h>
#include <juce_dsp/juce_dsp.h>
#include <array>
namespace hem {
// One audio producer, one analysis consumer. No GUI lock, allocation, FFT or
// event notification occurs in push(). A slow analyser loses display packets,
// never audio. The GUI uses a non-blocking snapshot read.
class AnalysisWorker final : private juce::Thread {
public:
    static constexpr int fftOrder=13,fftSize=1<<fftOrder,bins=fftSize/2+1;
    static constexpr int columns=2048,bucketSize=64,packetSize=256,packetCount=128;
    struct Peak {float lowL=0,highL=0,lowR=0,highR=0;};
    struct Snapshot {
        std::array<float,bins> leftDb{},rightDb{};
        std::array<Peak,columns> waveform{}; // Oldest to newest; first validColumns entries.
        int validColumns=0;
        double sampleRate=0;
        std::uint64_t frameEnd=0,sequence=0;
        std::uint32_t droppedFrames=0;
        bool discontinuity=false;
    };
    AnalysisWorker();~AnalysisWorker() override;
    // Called with the audio callback excluded, e.g. prepareToPlay(). Only
    // producer metadata changes; worker FIFO/history is never reset here.
    bool isOperational() const {return isThreadRunning();}
    void prepare(double sampleRate) noexcept;
    void push(const float* left,const float* right,int count) noexcept;
    bool copyLatest(Snapshot&) const; // Message thread, returns false if busy.
private:
    struct Packet {std::array<float,packetSize> left{},right{};int size=0;double rate=0;std::uint64_t first=0;unsigned epoch=0;};
    void run() override;void consume(const Packet&);void publish();void resetHistory();
    juce::AbstractFifo fifo{packetCount};
    std::array<Packet,packetCount> packets;
    double producerRate=96000,workerRate=96000;unsigned producerEpoch=0,workerEpoch=0;
    std::uint64_t producerFrame=0,expectedFrame=0,sequence=0;
    std::atomic<std::uint32_t> drops{0};
    juce::dsp::FFT fft{fftOrder};
    std::array<float,fftSize> ringL{},ringR{},window{};
    std::array<float,2*fftSize> fftL{},fftR{};
    std::array<Peak,columns> peaks{};
    Peak accumulator;int bucketFill=0,peakWrite=0,peakCount=0,write=0,filled=0,hop=0;
    double windowSum=0;bool gap=false;
    mutable juce::CriticalSection snapshotLock;
    Snapshot latest,work;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AnalysisWorker)
};
}
