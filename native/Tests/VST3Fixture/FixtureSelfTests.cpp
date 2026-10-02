#include "FixtureProcessor.h"
#include <array>
#include <bit>
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace
{
void require(bool result, const char* what)
{
    if (!result) throw std::runtime_error(what);
}

void set(hem::test::FixtureProcessor& p, int index, float normalized)
{
    p.getParameters()[index]->setValueNotifyingHost(normalized);
}

void putWord(juce::MemoryBlock& state, int offset, std::uint32_t word)
{
    auto* bytes = static_cast<std::uint8_t*>(state.getData());
    for (int n = 0; n < 4; ++n) bytes[offset + n] = static_cast<std::uint8_t>(word >> (8 * n));
}

void repairChecksum(juce::MemoryBlock& state)
{
    const auto* bytes = static_cast<const std::uint8_t*>(state.getData());
    std::uint32_t hash = 2166136261u;
    for (int n = 0; n < 20; ++n) hash = (hash ^ bytes[n]) * 16777619u;
    putWord(state, 20, hash);
}

void checkDelay(hem::test::FixtureProcessor& p, int mode)
{
    set(p, 0, 0.25f); // linear gain = 0.5
    set(p, 1, static_cast<float>(mode) * 0.5f);
    p.reset();
    const auto latency = hem::test::FixtureProcessor::latencyForMode(mode);
    require(p.getLatencySamples() == latency, "reported latency");
    juce::MidiBuffer midi;
    int offset = 0;
    constexpr std::array<int, 6> sizes { 1, 17, 64, 127, 256, 513 };
    for (int block = 0; offset < latency + 4096; ++block)
    {
        const auto count = sizes[static_cast<std::size_t>(block) % sizes.size()];
        juce::AudioBuffer<float> audio(2, count);
        for (int n = 0; n < count; ++n)
        {
            // Exactly representable samples make this an exact equality test.
            audio.setSample(0, n, static_cast<float>((offset + n) % 1024) / 1024.0f);
            audio.setSample(1, n, -audio.getSample(0, n));
        }
        p.processBlock(audio, midi);
        for (int n = 0; n < count; ++n)
        {
            const auto time = offset + n - latency;
            const auto expected = time < 0 ? 0.0f : static_cast<float>(time % 1024) / 2048.0f;
            require(audio.getSample(0, n) == expected, "exact left delayed gain");
            require(audio.getSample(1, n) == -expected, "exact right delayed gain");
        }
        offset += count;
    }
}

void checkState(hem::test::FixtureProcessor& p)
{
    set(p, 0, 0.375f);
    set(p, 1, 0.5f);
    juce::MemoryBlock state;
    p.getStateInformation(state);
    require(state.getSize() == hem::test::FixtureProcessor::stateSize, "state byte count");
    set(p, 0, 0.9f);
    set(p, 1, 1.0f);
    p.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
    require(p.getParameters()[0]->getValue() == 0.375f, "state restored gain");
    require(p.getParameters()[1]->getValue() == 0.5f, "state restored latency mode");
    require(p.getLatencySamples() == 64, "state restored latency publication");
    juce::MemoryBlock roundtrip;
    p.getStateInformation(roundtrip);
    require(roundtrip == state, "bit-exact state roundtrip");
    const auto rejected = p.rejectedStateCount();
    for (std::size_t byte = 0; byte < state.getSize(); ++byte)
    {
        auto corrupt = state;
        static_cast<unsigned char*>(corrupt.getData())[byte] ^= 0x01;
        p.setStateInformation(corrupt.getData(), static_cast<int>(corrupt.getSize()));
        p.getStateInformation(roundtrip);
        require(roundtrip == state, "corrupt byte changed state");
    }
    for (int size = 0; size < hem::test::FixtureProcessor::stateSize; ++size)
        p.setStateInformation(state.getData(), size);
    auto trailing = state;
    trailing.append("X", 1);
    p.setStateInformation(trailing.getData(), static_cast<int>(trailing.getSize()));
    p.setStateInformation(nullptr, hem::test::FixtureProcessor::stateSize);
    // Even a syntactically checksummed state must meet semantic constraints.
    for (const auto gain : { -0.1f, 2.5f, std::numeric_limits<float>::quiet_NaN(),
                             std::numeric_limits<float>::infinity() })
    {
        auto malformed = state;
        putWord(malformed, 8, std::bit_cast<std::uint32_t>(gain));
        repairChecksum(malformed);
        p.setStateInformation(malformed.getData(), static_cast<int>(malformed.getSize()));
    }
    for (const auto offset : { 0, 12, 16 })
    {
        auto malformed = state;
        putWord(malformed, offset, 3);
        repairChecksum(malformed);
        p.setStateInformation(malformed.getData(), static_cast<int>(malformed.getSize()));
    }
    require(p.rejectedStateCount() == rejected + 57, "all malformed states rejected");
    p.getStateInformation(roundtrip);
    require(roundtrip == state, "invalid sizes changed state");
    set(p, 0, std::numeric_limits<float>::quiet_NaN());
    set(p, 1, std::numeric_limits<float>::infinity());
    p.getStateInformation(roundtrip);
    require(roundtrip == state, "non-finite parameter changed state");
}
}

int main(int argc, char** argv)
{
    try
    {
        juce::ScopedJuceInitialiser_GUI gui;
        hem::test::FixtureProcessor p;
        require(p.getTotalNumInputChannels() == 2 && p.getTotalNumOutputChannels() == 2, "stereo layout");
        require(!p.supportsDoublePrecisionProcessing(), "float-only precision");
        require(p.getParameters().size() == 2, "parameter count");
        require(p.getParameters()[0]->getValue() == 0.5f && p.getParameters()[1]->getValue() == 0.0f, "parameter defaults");
        auto layout = p.getBusesLayout();
        layout.inputBuses.set(0, juce::AudioChannelSet::mono());
        require(!p.isBusesLayoutSupported(layout), "reject mono layout");
        for (const auto rate : { 44100.0, 48000.0, 96000.0 })
        {
            p.setRateAndBufferSizeDetails(rate, 256);
            p.prepareToPlay(rate, 256);
            for (const auto mode : { 0, 1, 2, 0, 1 }) checkDelay(p, mode);
        }
        checkState(p);
        p.releaseResources();
        juce::AudioBuffer<float> silent(2, 16);
        for (int channel = 0; channel < 2; ++channel)
            for (int n = 0; n < 16; ++n) silent.setSample(channel, n, 1.0f);
        juce::MidiBuffer midi;
        p.processBlock(silent, midi);
        require(silent.getMagnitude(0, 16) == 0.0f, "unprepared silence");
        if (argc > 1 && juce::String(argv[1]) == "--editor")
        {
            for (int n = 0; n < 32; ++n)
            {
                std::unique_ptr<juce::AudioProcessorEditor> editor(p.createEditor());
                require(editor != nullptr && editor->getWidth() > 0 && editor->getHeight() > 0, "generic editor dimensions");
                editor->addToDesktop(0);
                editor->setVisible(true);
                editor->setVisible(false);
                editor->removeFromDesktop();
            }
            std::cout << "PASS: 32 native generic-editor create/open/close/delete cycles\n";
        }
        std::cout << "PASS: stereo float, parameter defaults, exact gain/delay at 3 rates and variable blocks,\n"
                     "      0/64/262144 dynamic latency, bit-exact state, 57 corrupt-state rejections, unprepared silence\n";
        return 0;
    }
    catch (const std::exception& e)
    {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
