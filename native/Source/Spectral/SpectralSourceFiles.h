#pragma once
#include "SpectralAudio.h"
#include "../Systems/NoReplaceFile.h"
#include <JuceHeader.h>
#include <juce_cryptography/juce_cryptography.h>
#include <atomic>
#include <filesystem>

// Worker-only I/O. The UI receives identity/shape metadata and immutable grids;
// the audio callback receives only already-prepared ReadAheadTransport objects.
namespace hem::spectral::files {
inline constexpr std::uint64_t workingBudget = 512ULL * 1024ULL * 1024ULL;
inline constexpr std::uint64_t stagingReserve = 16ULL * 1024ULL * 1024ULL;
struct Cancelled {};
inline void demand(bool value, const juce::String& message) {
    if (!value) throw std::runtime_error(message.toStdString());
}
inline void demand(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
inline void check(const std::atomic<bool>& cancel) {
    if (cancel.load(std::memory_order_relaxed)) throw Cancelled{};
}
struct Identity {
    juce::File file;
    juce::String sha256;
    juce::int64 bytes = 0, frames = 0, modified = 0;
    double rate = 0;
    int channels = 0;
};
class HashInput final : public juce::InputStream {
public:
    HashInput(const juce::File& file, const std::atomic<bool>& cancellation)
        : input(file), cancel(cancellation) { demand(input.getStatus().wasOk(), "Cannot open spectral source"); }
    juce::int64 getTotalLength() override { return input.getTotalLength(); }
    juce::int64 getPosition() override { return input.getPosition(); }
    bool setPosition(juce::int64 position) override { return input.setPosition(position); }
    bool isExhausted() override { return input.isExhausted(); }
    int read(void* buffer, int bytes) override {
        check(cancel);
        const int result = input.read(buffer, std::min(bytes, 65536));
        demand(input.getStatus().wasOk(), "Spectral source hash read failed");
        return result;
    }
private:
    juce::FileInputStream input;
    const std::atomic<bool>& cancel;
};
inline juce::String hash(const juce::File& file, const std::atomic<bool>& cancel) {
    HashInput input(file, cancel);
    const auto size = input.getTotalLength();
    const auto result = juce::SHA256(input).toHexString();
    demand(size > 0 && input.getPosition() == size && file.getSize() == size,
           "Spectral source changed while hashing");
    check(cancel);
    return result;
}
inline Identity identify(const juce::File& file, const std::atomic<bool>& cancel) {
    check(cancel);
    juce::AudioFormatManager formats; formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
    demand(reader && reader->lengthInSamples > 0 && reader->numChannels >= 1 && reader->numChannels <= 2,
           "Choose a nonempty mono or stereo audio source");
    demand(std::isfinite(reader->sampleRate) && reader->sampleRate >= 8000 && reader->sampleRate <= 384000,
           "Unsupported spectral source sample rate");
    demand(std::uint64_t(reader->lengthInSamples) <= (workingBudget - stagingReserve)
               / (2ULL * sizeof(float) * std::uint64_t(reader->numChannels))
        && static_cast<long double>(reader->lengthInSamples) <= reader->sampleRate * 1800.0L,
        "Selected source exceeds the 512 MiB combined spectral working budget or 30-minute limit");
    // Bound hashing even when a short audio stream has oversized metadata.
    demand(file.getSize() > 0 && std::uint64_t(file.getSize()) <= workingBudget,
           "Spectral source file exceeds the 512 MiB input-file limit");
    Identity result{file, {}, file.getSize(), reader->lengthInSamples,
                    file.getLastModificationTime().toMilliseconds(), reader->sampleRate, int(reader->numChannels)};
    reader.reset(); result.sha256 = hash(file, cancel); return result;
}
inline bool metadataUnchanged(const Identity& identity) {
    return identity.file.existsAsFile() && identity.file.getSize() == identity.bytes
        && identity.file.getLastModificationTime().toMilliseconds() == identity.modified;
}
inline void verify(const Identity& identity, const std::atomic<bool>& cancel) {
    demand(metadataUnchanged(identity) && hash(identity.file, cancel) == identity.sha256,
           "Source bytes changed; reopen the spectral scanner");
}
inline std::shared_ptr<const Audio> decode(const Identity& identity, const std::atomic<bool>& cancel) {
    check(cancel);
    // Two whole sample payloads are the maximum live allocation: immutable
    // original plus reconstruction. Reserve transport rings, grids and I/O too.
    const auto frames = static_cast<std::uint64_t>(identity.frames);
    demand(frames <= (workingBudget - stagingReserve) / (2ULL * sizeof(float) * std::uint64_t(identity.channels))
        && static_cast<long double>(frames) <= identity.rate * 1800.0L,
        "Selected source exceeds the 512 MiB combined spectral working budget or 30-minute limit");
    auto audio = std::make_shared<Audio>(); audio->sampleRate = identity.rate;
    audio->channels.resize(static_cast<std::size_t>(identity.channels));
    for (auto& channel : audio->channels) { check(cancel); channel.resize(static_cast<std::size_t>(frames)); }
    juce::AudioFormatManager formats; formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(identity.file));
    demand(reader && reader->lengthInSamples == identity.frames && reader->sampleRate == identity.rate
        && int(reader->numChannels) == identity.channels, "Source shape changed before decoding");
    juce::AudioBuffer<float> block(identity.channels, 8192);
    for (juce::int64 position = 0; position < identity.frames; position += 8192) {
        check(cancel); const int count = int(std::min<juce::int64>(8192, identity.frames - position));
        demand(reader->read(&block, 0, count, position, true, true), "Spectral source decode failed");
        for (int channel = 0; channel < identity.channels; ++channel) {
            const auto* input = block.getReadPointer(channel);
            for (int frame = 0; frame < count; ++frame) {
                demand(std::isfinite(input[frame]), "Spectral source contains non-finite samples");
                audio->channels[std::size_t(channel)][std::size_t(position) + std::size_t(frame)] = input[frame];
            }
        }
    }
    reader.reset(); verify(identity, cancel); return audio;
}
inline juce::File privateDirectory(const juce::File& parent) {
    const auto result = parent.getChildFile("hem-spectral-stage-" + juce::Uuid().toString());
    std::error_code error;
    demand(std::filesystem::create_directory(noReplaceFile::nativePath(result), error) && !error,
           "Cannot create private spectral staging directory");
    return result;
}
inline Identity writeFloat(const juce::File& destination, const Audio& audio, const std::atomic<bool>& cancel) {
    check(cancel);
    std::error_code error;
    demand(noReplaceFile::available(destination, error), "Spectral output name is already occupied");
    auto stream = destination.createOutputStream();
    demand(stream && stream->getStatus().wasOk(), "Cannot create private spectral WAV");
    auto* raw = stream.get(); juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> writer(wav.createWriterFor(stream.get(), audio.sampleRate,
        unsigned(audio.channels.size()), 32, {}, 0));
    demand(writer != nullptr, "Cannot create spectral float32 WAV writer"); stream.release();
    for (std::size_t position = 0; position < audio.frames(); position += 8192) {
        check(cancel); const int count = int(std::min<std::size_t>(8192, audio.frames() - position));
        const float* channels[]{audio.channels[0].data() + position,
            audio.channels.size() == 2 ? audio.channels[1].data() + position : nullptr};
        demand(writer->writeFromFloatArrays(channels, int(audio.channels.size()), count), "Spectral WAV write failed");
    }
    demand(writer->flush(), "Spectral WAV finalization failed"); raw->flush();
    demand(raw->getStatus().wasOk(), "Spectral WAV storage flush failed"); writer.reset();
    juce::AudioFormatManager formats; formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(destination));
    demand(reader && reader->usesFloatingPointData && reader->bitsPerSample == 32
        && reader->sampleRate == audio.sampleRate && reader->numChannels == audio.channels.size()
        && reader->lengthInSamples == juce::int64(audio.frames()), "Spectral WAV failed exact-shape verification");
    reader.reset(); return identify(destination, cancel);
}
inline void copy(const juce::File& source, const juce::File& destination, const std::atomic<bool>& cancel) {
    juce::FileInputStream input(source); auto output = destination.createOutputStream();
    demand(input.getStatus().wasOk() && output && output->getStatus().wasOk(), "Cannot stage durable spectral output");
    std::array<char, 65536> block{};
    while (!input.isExhausted()) {
        check(cancel); const int count = input.read(block.data(), int(block.size()));
        demand(count > 0 && input.getStatus().wasOk() && output->write(block.data(), std::size_t(count)),
               "Spectral output copy failed");
    }
    output->flush(); demand(output->getStatus().wasOk(), "Spectral output copy flush failed");
}
} // namespace hem::spectral::files
