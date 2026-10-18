#pragma once
#include <JuceHeader.h>
#include <atomic>
#include <functional>
#include <memory>
namespace hem {
// Decoder/resampler and lifecycle methods are non-audio-thread only. The host
// serializes control producers and commits only at a quiescent block boundary.
class ReadAheadTransport final {
public:
    static constexpr int pageFrames=2048,capacityFrames=65536,startupFrames=16384;
    ReadAheadTransport();~ReadAheadTransport();
    ReadAheadTransport(const ReadAheadTransport&)=delete;
    ReadAheadTransport& operator=(const ReadAheadTransport&)=delete;
    void prepareFile(const juce::File&,juce::AudioFormatManager&,juce::TimeSliceThread&,
                     double rate,int maxBlock,bool looping=false,double initialSeconds=0);
    juce::Result prepareEpoch(double seconds,bool looping,uint64_t& token,
                              const std::function<bool()>& cancelled={});
    bool commitPreparedEpoch(uint64_t token,bool play) noexcept;
    void discardPreparedEpoch(uint64_t token); // offthread
    void reprepare(double rate,int maxBlock); // quiescent/offthread; retains position, stopped
    void releaseResources(); // quiescent/offthread; retains file metadata
    void start() noexcept;void stop() noexcept;
    bool isPlaying()const noexcept;
    // Whole-callback admission may exceed nominal maxBlock, up to capacityFrames.
    bool availableRt(int frames)const noexcept;
    bool renderRt(const juce::AudioSourceChannelInfo&) noexcept;
    void getNextAudioBlock(const juce::AudioSourceChannelInfo& i) noexcept {renderRt(i);}
    double getCurrentPosition()const noexcept;
    double getLengthInSeconds()const noexcept;
    double getSourceSampleRate()const noexcept;
    int getSourceChannels()const noexcept;
    bool isLooping()const noexcept;
    bool hasUnderrun()const noexcept;
    bool hasReadError()const noexcept;
    uint64_t underrunCount()const noexcept;
    uint64_t epoch()const noexcept;
    uint64_t preparedEpoch()const noexcept;
    void latchUnderrun() noexcept; // host group-admission failure, counted once per epoch
private:
    friend struct ReadAheadTransportTestAccess;
    void setReadHookForTesting(std::function<void()>);
    struct Impl;std::unique_ptr<Impl> impl;
};
}
