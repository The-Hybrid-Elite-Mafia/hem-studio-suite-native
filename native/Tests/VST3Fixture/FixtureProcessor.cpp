#include "FixtureProcessor.h"
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstring>

namespace hem::test
{
namespace
{
static_assert(std::atomic<std::uint64_t>::is_always_lock_free,
              "This test fixture requires a lock-free 64-bit atomic target");
constexpr std::array<std::uint8_t, 8> magic { 'H', 'E', 'M', 'F', 'X', '0', '0', '1' };

void write32(std::uint8_t* output, std::uint32_t value) noexcept
{
    for (unsigned n = 0; n != 4; ++n)
        output[n] = static_cast<std::uint8_t>(value >> (8 * n));
}

std::uint32_t read32(const std::uint8_t* input) noexcept
{
    std::uint32_t result = 0;
    for (unsigned n = 0; n != 4; ++n)
        result |= static_cast<std::uint32_t>(input[n]) << (8 * n);
    return result;
}

std::uint32_t checksum(const std::uint8_t* bytes) noexcept
{
    std::uint32_t hash = 2166136261u;
    for (int n = 0; n != 20; ++n)
        hash = (hash ^ bytes[n]) * 16777619u;
    return hash;
}
}

class FixtureProcessor::Parameter final : public juce::RangedAudioParameter
{
public:
    Parameter(FixtureProcessor& ownerIn, bool latencyIn)
        : RangedAudioParameter(juce::ParameterID { latencyIn ? latencyParameterID : gainParameterID, 1 },
                               latencyIn ? "Latency mode" : "Fixture gain"),
          owner(ownerIn), latency(latencyIn), range(0.0f, 2.0f, latencyIn ? 1.0f : 0.0f) {}

    float getValue() const override
    {
        const auto value = owner.readSettings();
        return latency ? static_cast<float>(unpackMode(value)) * 0.5f : unpackGain(value) * 0.5f;
    }
    void setValue(float normalized) override { owner.changeParameter(latency, normalized); }
    float getDefaultValue() const override { return latency ? 0.0f : 0.5f; }
    bool isDiscrete() const override { return latency; }
    int getNumSteps() const override { return latency ? 3 : juce::AudioProcessor::getDefaultNumParameterSteps(); }
    const juce::NormalisableRange<float>& getNormalisableRange() const override { return range; }
    juce::String getText(float normalized, int maximumLength) const override
    {
        const auto finite = std::isfinite(normalized) ? juce::jlimit(0.0f, 1.0f, normalized) : 0.0f;
        auto result = latency ? juce::String(latencyForMode(static_cast<int>(std::lround(finite * 2.0f)))) + " samples"
                              : juce::String(finite * 2.0f, 6);
        return maximumLength > 0 ? result.substring(0, maximumLength) : result;
    }
    float getValueForText(const juce::String& text) const override
    {
        if (latency)
        {
            const auto samples = text.getIntValue();
            return samples == 64 ? 0.5f : (samples == oversizedLatency ? 1.0f : 0.0f);
        }
        const auto value = text.getFloatValue();
        return std::isfinite(value) ? juce::jlimit(0.0f, 1.0f, value * 0.5f) : 0.5f;
    }
    juce::StringArray getAllValueStrings() const override
    {
        return latency ? juce::StringArray { "0 samples", "64 samples", "262144 samples" } : juce::StringArray {};
    }

private:
    FixtureProcessor& owner;
    const bool latency;
    const juce::NormalisableRange<float> range;
};

std::uint64_t FixtureProcessor::pack(float gain, int mode) noexcept
{
    return std::bit_cast<std::uint32_t>(gain) | (static_cast<std::uint64_t>(mode) << 32);
}

float FixtureProcessor::unpackGain(std::uint64_t value) noexcept
{
    return std::bit_cast<float>(static_cast<std::uint32_t>(value));
}

int FixtureProcessor::unpackMode(std::uint64_t value) noexcept { return static_cast<int>(value >> 32); }

int FixtureProcessor::latencyForMode(int mode) noexcept
{
    return mode == 1 ? 64 : (mode == 2 ? oversizedLatency : 0);
}

FixtureProcessor::FixtureProcessor()
    : AudioProcessor(BusesProperties().withInput("Input", juce::AudioChannelSet::stereo(), true)
                                      .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      settings(pack(1.0f, 0))
{
    addParameter(new Parameter(*this, false));
    addParameter(new Parameter(*this, true));
}

bool FixtureProcessor::isBusesLayoutSupported(const BusesLayout& layout) const
{
    return layout.inputBuses.size() == 1 && layout.outputBuses.size() == 1
        && layout.getMainInputChannelSet() == juce::AudioChannelSet::stereo()
        && layout.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

double FixtureProcessor::getTailLengthSeconds() const
{
    return static_cast<double>(latencyForMode(unpackMode(readSettings()))) / std::max(1.0, getSampleRate());
}

void FixtureProcessor::publishLatency(int mode)
{
    // JUCE owns and transports this host notification; there is no fixture worker.
    setLatencySamples(latencyForMode(mode));
}

void FixtureProcessor::changeParameter(bool isLatency, float normalized) noexcept
{
    if (!std::isfinite(normalized))
        return;
    normalized = juce::jlimit(0.0f, 1.0f, normalized);
    auto previous = readSettings();
    std::uint64_t next;
    do
    {
        next = isLatency ? pack(unpackGain(previous), static_cast<int>(std::lround(normalized * 2.0f)))
                         : pack(normalized * 2.0f, unpackMode(previous));
    }
    while (!settings.compare_exchange_weak(previous, next, std::memory_order_release, std::memory_order_acquire));
    if (isLatency)
        publishLatency(unpackMode(next));
}

void FixtureProcessor::prepareToPlay(double, int)
{
    // Fixed 2 MiB allocation outside processBlock, independent of host block size.
    delay = std::make_unique<float[]>(2 * capacity);
    reset();
    publishLatency(unpackMode(readSettings()));
}

void FixtureProcessor::releaseResources()
{
    delay.reset();
    reset();
}

void FixtureProcessor::reset()
{
    writeHead = 0;
    available = 0;
    activeMode = -1;
}

void FixtureProcessor::processBlock(juce::AudioBuffer<float>& audio, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    midi.clear();
    if (delay == nullptr || audio.getNumChannels() != 2)
    {
        audio.clear();
        return;
    }

    const auto blockSettings = readSettings();
    const auto mode = unpackMode(blockSettings);
    const auto gain = unpackGain(blockSettings);
    const auto latency = latencyForMode(mode);
    if (activeMode != mode)
    {
        // No buffer clearing on the audio thread. Old samples stay inaccessible
        // until the new delay has primed entirely with samples from this mode.
        activeMode = mode;
        available = 0;
        writeHead = 0;
    }

    auto* left = audio.getWritePointer(0);
    auto* right = audio.getWritePointer(1);
    for (int n = 0; n < audio.getNumSamples(); ++n)
    {
        const auto readHead = (writeHead + capacity - latency) % capacity;
        const auto inputL = left[n];
        const auto inputR = right[n];
        delay[writeHead] = inputL;
        delay[capacity + writeHead] = inputR;
        left[n] = available >= latency ? delay[readHead] * gain : 0.0f;
        right[n] = available >= latency ? delay[capacity + readHead] * gain : 0.0f;
        writeHead = (writeHead + 1) % capacity;
        available = std::min(available + 1, capacity);
    }
}

juce::AudioProcessorEditor* FixtureProcessor::createEditor()
{
    return new juce::GenericAudioProcessorEditor(*this);
}

void FixtureProcessor::getStateInformation(juce::MemoryBlock& destination)
{
    const auto value = readSettings();
    std::array<std::uint8_t, stateSize> bytes {};
    std::copy(magic.begin(), magic.end(), bytes.begin());
    write32(bytes.data() + 8, std::bit_cast<std::uint32_t>(unpackGain(value)));
    write32(bytes.data() + 12, static_cast<std::uint32_t>(unpackMode(value)));
    write32(bytes.data() + 20, checksum(bytes.data()));
    destination.replaceAll(bytes.data(), bytes.size());
}

void FixtureProcessor::setStateInformation(const void* data, int size)
{
    const auto reject = [this] { rejectedStates.fetch_add(1, std::memory_order_relaxed); };
    if (data == nullptr || size != stateSize)
    {
        reject();
        return;
    }
    const auto* bytes = static_cast<const std::uint8_t*>(data);
    const auto gain = std::bit_cast<float>(read32(bytes + 8));
    const auto mode = read32(bytes + 12);
    if (std::memcmp(bytes, magic.data(), magic.size()) != 0 || read32(bytes + 16) != 0
        || read32(bytes + 20) != checksum(bytes) || !std::isfinite(gain)
        || gain < 0.0f || gain > 2.0f || mode > 2)
    {
        reject();
        return;
    }
    settings.store(pack(gain, static_cast<int>(mode)), std::memory_order_release);
    publishLatency(static_cast<int>(mode));
    // Report the new controls without calling their setters (which would split
    // the atomic state publication). Editor refresh uses these notifications.
    getParameters()[0]->sendValueChangedMessageToListeners(gain * 0.5f);
    getParameters()[1]->sendValueChangedMessageToListeners(static_cast<float>(mode) * 0.5f);
}
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new hem::test::FixtureProcessor();
}
