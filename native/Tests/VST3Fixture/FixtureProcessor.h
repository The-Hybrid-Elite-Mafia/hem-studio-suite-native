#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <atomic>
#include <cstdint>
#include <memory>

namespace hem::test
{
// Deliberately in-house, deterministic integration fixture. Not a production DSP.
class FixtureProcessor final : public juce::AudioProcessor
{
public:
    static constexpr int oversizedLatency = 262144;
    static constexpr int stateSize = 24;
    static constexpr const char* gainParameterID = "fixtureGain";
    static constexpr const char* latencyParameterID = "fixtureLatency";

    FixtureProcessor();
    ~FixtureProcessor() override = default;

    const juce::String getName() const override { return "HEM VST3 Fixture"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    bool supportsDoublePrecisionProcessing() const override { return false; }
    double getTailLengthSeconds() const override;
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return "Deterministic fixture"; }
    void changeProgramName(int, const juce::String&) override {}

    bool isBusesLayoutSupported(const BusesLayout&) const override;
    void prepareToPlay(double, int) override;
    void releaseResources() override;
    void reset() override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using juce::AudioProcessor::processBlock;

    bool hasEditor() const override { return true; }
    juce::AudioProcessorEditor* createEditor() override;

    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;

    // Read-only observability for source-level self tests. These are not parameters.
    std::uint32_t rejectedStateCount() const noexcept { return rejectedStates.load(); }
    static int latencyForMode(int) noexcept;

private:
    class Parameter;
    friend class Parameter;
    std::uint64_t readSettings() const noexcept { return settings.load(std::memory_order_acquire); }
    void changeParameter(bool isLatency, float normalized) noexcept;
    static std::uint64_t pack(float gain, int mode) noexcept;
    static float unpackGain(std::uint64_t) noexcept;
    static int unpackMode(std::uint64_t) noexcept;
    void publishLatency(int mode);

    // One word makes gain+mode state restores transactional for the audio thread.
    std::atomic<std::uint64_t> settings;
    std::atomic<std::uint32_t> rejectedStates { 0 };
    std::unique_ptr<float[]> delay;
    int writeHead = 0;
    int available = 0;
    int activeMode = -1;
    static constexpr int capacity = oversizedLatency + 1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FixtureProcessor)
};
}
