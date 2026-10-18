#pragma once
#include "InstrumentalCore.h"
#include "../Systems/NoReplaceFile.h"
#include <JuceHeader.h>
#include <juce_cryptography/juce_cryptography.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <limits>
#include <stdexcept>
#if JUCE_WINDOWS
#include <io.h>
#else
#include <unistd.h>
#endif

// Worker-only file operations. No reader, hashing, JSON, writer or filesystem
// operation in this header may be called from an audio callback or UI handler.
namespace hem::instrumental::files {
inline constexpr std::uint64_t sampleBudget = 64ULL * 1024ULL * 1024ULL;
inline constexpr std::uint64_t planBudget = 192ULL * 1024ULL * 1024ULL;
inline constexpr std::uint64_t sourceFileBudget = 128ULL * 1024ULL * 1024ULL;
inline constexpr std::uint64_t outputBudget = 4ULL * 1024ULL * 1024ULL * 1024ULL;
inline constexpr std::uint64_t jsonBudget = 1024ULL * 1024ULL;
struct Cancelled {};
inline void demand(bool condition, const juce::String& message) {
    if (!condition) throw std::runtime_error(message.toStdString());
}
inline void demand(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
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
    bool present() const noexcept { return frames > 0; }
};
using Identities = std::array<Identity, kLaneCount>;
inline std::uint64_t decodedBytes(const Identity& source) {
    return std::uint64_t(source.frames) * std::uint64_t(source.channels) * sizeof(float);
}
inline std::uint64_t decodedBytes(const RenderPlan& plan) {
    std::uint64_t result = 0;
    // Count shared aliases conservatively rather than depending on pointer identity.
    for (const auto& lane : plan.lanes) if (lane.sample)
        result += (lane.sample->left.size() + lane.sample->right.size()) * sizeof(float);
    return result;
}
class HashInput final : public juce::InputStream {
public:
    HashInput(const juce::File& file, const std::atomic<bool>& cancellation)
        : input(file), cancel(cancellation) { demand(input.getStatus().wasOk(), "Cannot open sample for hashing"); }
    juce::int64 getTotalLength() override { return input.getTotalLength(); }
    juce::int64 getPosition() override { return input.getPosition(); }
    bool setPosition(juce::int64 position) override { return input.setPosition(position); }
    bool isExhausted() override { return input.isExhausted(); }
    int read(void* data, int size) override {
        check(cancel); const int count = input.read(data, std::min(size, 65536));
        demand(input.getStatus().wasOk(), "Sample hash read failed"); return count;
    }
private:
    juce::FileInputStream input;
    const std::atomic<bool>& cancel;
};
inline juce::String hash(const juce::File& file, const std::atomic<bool>& cancel, juce::int64 expectedBytes) {
    HashInput stream(file, cancel); const auto size = stream.getTotalLength();
    demand(size > 0 && size == expectedBytes && std::uint64_t(size) <= outputBudget,
           "File size changed before hashing");
    // Cap stream reads to the admitted length even if another process appends.
    const auto sha = juce::SHA256(stream, size).toHexString();
    demand(stream.getPosition() == size && file.getSize() == size, "File changed while hashing");
    check(cancel); return sha;
}
inline bool metadataUnchanged(const Identity& source) {
    return source.file.existsAsFile() && source.file.getSize() == source.bytes
        && source.file.getLastModificationTime().toMilliseconds() == source.modified;
}
inline void verify(const Identity& source, const std::atomic<bool>& cancel) {
    demand(metadataUnchanged(source), "Sample metadata changed; reload the sample or pattern");
    demand(hash(source.file, cancel, source.bytes) == source.sha256 && metadataUnchanged(source),
           "Sample bytes changed; reload the sample or pattern");
}
inline Identity identify(const juce::File& file, const std::atomic<bool>& cancel) {
    check(cancel);
    demand(file.existsAsFile() && file.getSize() > 0 && std::uint64_t(file.getSize()) <= sourceFileBudget,
           "Sample file must be nonempty and at most 128 MiB");
    const auto bytes = file.getSize(), modified = file.getLastModificationTime().toMilliseconds();
    juce::AudioFormatManager formats; formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
    demand(reader && reader->lengthInSamples > 0 && reader->numChannels >= 1 && reader->numChannels <= 2,
           "Choose a nonempty mono or stereo audio sample");
    demand(std::isfinite(reader->sampleRate) && reader->sampleRate >= 8000 && reader->sampleRate <= 384000,
           "Sample rate must be between 8 kHz and 384 kHz");
    demand(reader->lengthInSamples <= juce::int64(sampleBudget / (sizeof(float) * reader->numChannels))
        && static_cast<long double>(reader->lengthInSamples) <= reader->sampleRate * 120.0L,
        "Each decoded sample is limited to 64 MiB and 120 seconds");
    Identity result{file, {}, bytes, reader->lengthInSamples, modified, reader->sampleRate, int(reader->numChannels)};
    reader.reset(); result.sha256 = hash(file, cancel, result.bytes);
    demand(metadataUnchanged(result), "Sample changed during inspection"); return result;
}
inline std::shared_ptr<const SampleData> decode(const Identity& source, const std::atomic<bool>& cancel) {
    check(cancel);
    demand(source.frames > 0 && source.channels >= 1 && source.channels <= 2
        && decodedBytes(source) <= sampleBudget, "Sample exceeds decoded memory limit");
    auto sample = std::make_shared<SampleData>(); sample->sampleRate = source.rate;
    sample->left.resize(std::size_t(source.frames));
    if (source.channels == 2) sample->right.resize(std::size_t(source.frames));
    juce::AudioFormatManager formats; formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(source.file));
    demand(reader && reader->lengthInSamples == source.frames && reader->sampleRate == source.rate
        && int(reader->numChannels) == source.channels, "Sample shape changed before decoding");
    juce::AudioBuffer<float> block(source.channels, 8192);
    for (juce::int64 position = 0; position < source.frames; position += 8192) {
        check(cancel); const int count = int(std::min<juce::int64>(8192, source.frames - position));
        demand(reader->read(&block, 0, count, position, true, true), "Sample decode failed");
        for (int channel = 0; channel < source.channels; ++channel) {
            const auto* input = block.getReadPointer(channel);
            auto& output = channel == 0 ? sample->left : sample->right;
            for (int frame = 0; frame < count; ++frame) {
                demand(std::isfinite(input[frame]) && std::abs(input[frame]) <= 1000000.0f,
                       "Sample contains non-finite or unsafe numeric audio");
                output[std::size_t(position) + std::size_t(frame)] = input[frame];
            }
        }
    }
    reader.reset(); verify(source, cancel); return sample;
}

// C11 exclusive creation is supported by the Windows UCRT and POSIX libc.
// The x flag rejects every occupied filename, including dangling symlinks.
// A failed or cancelled operation never removes or replaces an existing file.
class ExclusiveOutput final : public juce::OutputStream {
public:
    explicit ExclusiveOutput(const juce::File& destination, bool* result = nullptr) : closedResult(result) {
        const auto path = noReplaceFile::nativePath(destination);
       #if JUCE_WINDOWS
        file = _wfopen(path.c_str(), L"wbx");
       #else
        file = std::fopen(path.c_str(), "wbx");
       #endif
        demand(file != nullptr, "Cannot create output; the name may already exist or storage is unavailable");
    }
    ~ExclusiveOutput() override { close(); }
    bool good() const noexcept { return healthy; }
    void flush() override {
        if (file == nullptr) return;
        healthy = std::fflush(file) == 0 && healthy;
       #if JUCE_WINDOWS
        healthy = _commit(_fileno(file)) == 0 && healthy;
       #else
        healthy = ::fsync(::fileno(file)) == 0 && healthy;
       #endif
    }
    bool close() {
        if (file != nullptr) {
            flush(); const bool closed = std::fclose(file) == 0; healthy = healthy && closed; file = nullptr;
            if (closedResult != nullptr) *closedResult = healthy;
        }
        return healthy;
    }
    bool setPosition(juce::int64 position) override {
        if (position < 0 || position > std::numeric_limits<long>::max()) { healthy = false; return false; }
        const bool ok = std::fseek(file, long(position), SEEK_SET) == 0; healthy = healthy && ok; return ok;
    }
    juce::int64 getPosition() override {
        const auto position = std::ftell(file); if (position < 0) healthy = false; return position;
    }
    bool write(const void* data, std::size_t size) override {
        const bool ok = std::fwrite(data, 1, size, file) == size; healthy = healthy && ok; return ok;
    }
private:
    std::FILE* file = nullptr;
    bool* closedResult = nullptr;
    bool healthy = true;
};
inline juce::File newDirectory(const juce::File& parent) {
    demand(parent.isDirectory(), "Choose an existing output folder");
    const auto child = parent.getChildFile("HEM-Instrumental-"
        + juce::Time::getCurrentTime().formatted("%Y%m%d-%H%M%S") + "-" + juce::Uuid().toString());
    std::error_code error;
    demand(std::filesystem::create_directory(noReplaceFile::nativePath(child), error) && !error,
           "Cannot create a new no-overwrite output folder");
    return child;
}
inline juce::File unusedPatternName(const juce::File& requested) {
    std::error_code error;
    if (noReplaceFile::available(requested, error)) return requested;
    demand(!error, "Cannot inspect the selected pattern path");
    return requested.getSiblingFile(requested.getFileNameWithoutExtension() + "-"
        + juce::Time::getCurrentTime().formatted("%Y%m%d-%H%M%S") + "-" + juce::Uuid().toString() + ".json");
}
inline void writeJson(const juce::File& target, const juce::var& data, const std::atomic<bool>& cancel) {
    check(cancel); const auto text = juce::JSON::toString(data, true);
    demand(std::uint64_t(text.getNumBytesAsUTF8()) <= jsonBudget, "Pattern manifest exceeds the 1 MiB limit");
    // The final name is created atomically only after complete durable bytes.
    // Keep the private partial on failure; do not delete any existing paths.
    const auto temporary = target.getSiblingFile("." + target.getFileName() + ".writing-" + juce::Uuid().toString());
    ExclusiveOutput output(temporary);
    demand(output.write(text.toRawUTF8(), text.getNumBytesAsUTF8()), "Pattern write failed");
    demand(output.close(), "Pattern storage flush or close failed"); check(cancel);
    const auto published = noReplaceFile::publish(temporary, target);
    demand(!published, "Cannot publish without replacing another file. Private JSON retained at " + temporary.getFullPathName());
}
inline juce::var identityJson(const Identity& source) {
    auto* object = new juce::DynamicObject(); juce::var result(object);
    object->setProperty("path", source.file.getFullPathName()); object->setProperty("sha256", source.sha256);
    object->setProperty("bytes", source.bytes); object->setProperty("frames", source.frames);
    object->setProperty("modified", source.modified); object->setProperty("sampleRate", source.rate);
    object->setProperty("channels", source.channels); return result;
}
inline juce::var patternJson(const RenderPlan& plan, const Identities& identities) {
    auto* object = new juce::DynamicObject(); juce::var result(object);
    object->setProperty("schema", "hem.instrumental.pattern"); object->setProperty("version", 1);
    object->setProperty("sourceSequencerVersion", "2.0.0");
    object->setProperty("bpm", plan.bpm); object->setProperty("swingPercent", plan.swingPercent);
    object->setProperty("repeats", plan.repeats); object->setProperty("tailSeconds", plan.tailSeconds);
    juce::Array<juce::var> lanes;
    for (std::size_t index = 0; index < kLaneCount; ++index) {
        const auto& lane = plan.lanes[index]; auto* item = new juce::DynamicObject(); juce::var value(item);
        item->setProperty("steps", juce::int64(lane.steps)); item->setProperty("gainDb", lane.gainDb);
        item->setProperty("pan", lane.pan); item->setProperty("pitchSemitones", lane.pitchSemitones);
        item->setProperty("mute", lane.mute); item->setProperty("solo", lane.solo);
        item->setProperty("chokeGroup", lane.chokeGroup);
        item->setProperty("source", identities[index].present() ? identityJson(identities[index]) : juce::var());
        lanes.add(value);
    }
    object->setProperty("lanes", lanes); return result;
}
inline double number(const juce::var& object, const char* key, double low, double high, bool integer = false) {
    demand(object.isObject() && object.hasProperty(key), "Missing pattern field: " + juce::String(key));
    const auto value = object[key];
    demand(value.isInt() || value.isInt64() || value.isDouble(), "Pattern field is not numeric: " + juce::String(key));
    const double result = double(value);
    demand(std::isfinite(result) && result >= low && result <= high && (!integer || std::floor(result) == result),
           "Pattern field is out of range: " + juce::String(key)); return result;
}
inline bool boolean(const juce::var& object, const char* key) {
    demand(object.isObject() && object.hasProperty(key) && object[key].isBool(),
           "Pattern field must be true or false: " + juce::String(key)); return bool(object[key]);
}
struct LoadedPattern { RenderPlan plan; Identities identities; };
inline LoadedPattern loadPattern(const juce::File& file, const std::atomic<bool>& cancel,
                                 std::atomic<double>& progress) {
    check(cancel);
    demand(file.existsAsFile() && file.getSize() > 0 && std::uint64_t(file.getSize()) <= jsonBudget,
           "Pattern JSON must be nonempty and at most 1 MiB");
    const auto bytes = file.getSize();
    juce::FileInputStream input(file); demand(input.getStatus().wasOk(), "Cannot open pattern JSON");
    juce::MemoryBlock memory; demand(input.readIntoMemoryBlock(memory, std::size_t(bytes)) == bytes
        && input.getStatus().wasOk() && file.getSize() == bytes, "Pattern file changed while reading");
    // Reject pathological nesting before JUCE's recursive JSON parser runs.
    bool quoted = false, escaped = false; int depth = 0;
    const auto* characters = static_cast<const char*>(memory.getData());
    for (std::size_t index = 0; index < memory.getSize(); ++index) {
        const char value = characters[index];
        if (quoted) {
            if (escaped) escaped = false;
            else if (value == '\\') escaped = true;
            else if (value == '\"') quoted = false;
        } else if (value == '\"') quoted = true;
        else if (value == '{' || value == '[') demand(++depth <= 32, "Pattern JSON nesting exceeds the supported limit");
        else if (value == '}' || value == ']') demand(--depth >= 0, "Malformed pattern JSON nesting");
    }
    juce::var root;
    const auto parsed = juce::JSON::parse(juce::String::fromUTF8(characters, int(memory.getSize())), root);
    demand(parsed.wasOk() && root.isObject(), "Pattern is not valid JSON");
    demand(root["schema"].isString() && root["schema"].toString() == "hem.instrumental.pattern"
        && number(root, "version", 1, 1, true) == 1, "Unsupported pattern schema or version");
    LoadedPattern loaded;
    loaded.plan.bpm = number(root, "bpm", 40, 240);
    loaded.plan.swingPercent = number(root, "swingPercent", 50, 75);
    loaded.plan.repeats = int(number(root, "repeats", 1, 128, true));
    loaded.plan.tailSeconds = number(root, "tailSeconds", 0, 30);
    const auto* lanes = root["lanes"].getArray();
    demand(lanes && lanes->size() == int(kLaneCount), "Pattern must contain exactly 13 lanes");
    std::uint64_t decodedTotal = 0;
    for (std::size_t index = 0; index < kLaneCount; ++index) {
        check(cancel); const auto& item = lanes->getReference(int(index)); auto& lane = loaded.plan.lanes[index];
        lane.steps = std::uint32_t(number(item, "steps", 0, double(std::numeric_limits<std::uint32_t>::max()), true));
        lane.gainDb = float(number(item, "gainDb", -60, 12)); lane.pan = float(number(item, "pan", -1, 1));
        lane.pitchSemitones = float(number(item, "pitchSemitones", -24, 24));
        lane.mute = boolean(item, "mute"); lane.solo = boolean(item, "solo");
        lane.chokeGroup = int(number(item, "chokeGroup", 0, 4, true));
        demand(item.hasProperty("source"), "Missing lane source field");
        const auto source = item["source"];
        if (source.isVoid()) continue;
        demand(source.isObject() && source["path"].isString() && source["sha256"].isString(), "Invalid sample identity");
        const auto path = source["path"].toString(), expectedHash = source["sha256"].toString();
        demand(path.isNotEmpty() && path.length() <= 32768 && expectedHash.length() == 64
            && expectedHash.containsOnly("0123456789abcdefABCDEF"), "Invalid sample path or SHA-256");
        const auto sourceFile = juce::File::isAbsolutePath(path) ? juce::File(path) : file.getParentDirectory().getChildFile(path);
        const auto identity = identify(sourceFile, cancel);
        demand(identity.sha256.equalsIgnoreCase(expectedHash)
            && identity.bytes == juce::int64(number(source, "bytes", 1, double(sourceFileBudget), true))
            && identity.frames == juce::int64(number(source, "frames", 1, double(sampleBudget / sizeof(float)), true))
            && identity.rate == number(source, "sampleRate", 8000, 384000)
            && identity.channels == int(number(source, "channels", 1, 2, true)),
            "Pattern sample identity no longer matches: " + sourceFile.getFileName());
        // A copied, byte-identical sample may have a new filesystem timestamp.
        // Validate its recorded timestamp type, then retain its current identity.
        (void)number(source, "modified", 0, 9007199254740991.0, true);
        decodedTotal += decodedBytes(identity);
        demand(decodedTotal <= planBudget, "Pattern exceeds the 192 MiB decoded-sample budget");
        loaded.identities[index] = identity;
    }
    for (std::size_t index = 0; index < kLaneCount; ++index) {
        if (loaded.identities[index].present()) loaded.plan.lanes[index].sample = decode(loaded.identities[index], cancel);
        progress.store(double(index + 1) / double(kLaneCount), std::memory_order_relaxed);
    }
    check(cancel); return loaded;
}
struct ExportResult {
    juce::File directory, manifest;
    std::array<juce::File, kLaneCount> files;
    std::array<Identity, kLaneCount> identities;
    juce::int64 frames = 0;
};
inline ExportResult render(const RenderPlan& plan, const Identities& identities, const juce::File& parent,
                           const std::atomic<bool>& cancel, std::atomic<double>& progress,
                           juce::File* startedDirectory = nullptr) {
    check(cancel); demand(decodedBytes(plan) <= planBudget, "Pattern exceeds decoded-sample budget");
    bool audible = false, anySolo = false;
    for (const auto& lane : plan.lanes) anySolo = anySolo || lane.solo;
    for (const auto& lane : plan.lanes) audible = audible || (lane.sample && lane.steps != 0 && !lane.mute && (!anySolo || lane.solo));
    demand(audible, "Load a sample and enable an audible step before rendering");
    for (std::size_t index = 0; index < kLaneCount; ++index) {
        demand(bool(plan.lanes[index].sample) == identities[index].present(), "Sample identity is missing; reload the lane");
        if (identities[index].present()) verify(identities[index], cancel);
    }
    constexpr int blockFrames = 1024;
    OfflineRenderer renderer; const auto prepared = renderer.prepare(plan, blockFrames, &cancel);
    if (prepared.status == Status::cancelled) throw Cancelled{};
    demand(prepared.status == Status::ok, prepared.message);
    const auto frames = renderer.totalFrames();
    demand(frames > 0 && frames <= kMaxTotalFrames,
           "Render exceeds the combined 4 GiB output limit; reduce repeats or tail");
    const auto required = std::uint64_t(frames) * kLaneCount * 2ULL * sizeof(float) + 64ULL * 1024ULL * 1024ULL;
    std::error_code spaceError; const auto space = std::filesystem::space(noReplaceFile::nativePath(parent), spaceError);
    demand(!spaceError && space.available >= required, "Insufficient free disk space, or free space could not be verified");
    ExportResult result; result.frames = frames; result.directory = newDirectory(parent);
    if (startedDirectory != nullptr) *startedDirectory = result.directory;
    // This marker remains on failure/cancellation. Only manifest.json means a
    // complete verified set. No incomplete or pre-existing folder is deleted.
    writeJson(result.directory.getChildFile("render-started.json"), patternJson(plan, identities), cancel);
    std::array<bool, kLaneCount> closedOk{};
    std::array<std::unique_ptr<juce::AudioFormatWriter>, kLaneCount> writers;
    std::array<ExclusiveOutput*, kLaneCount> streams{};
    juce::WavAudioFormat wav;
    for (std::size_t lane = 0; lane < kLaneCount; ++lane) {
        result.files[lane] = result.directory.getChildFile(juce::String(int(lane + 1)).paddedLeft('0', 2) + "-instrumental.wav");
        auto stream = std::make_unique<ExclusiveOutput>(result.files[lane], &closedOk[lane]); streams[lane] = stream.get();
        // JUCE's 32-bit WAV writer uses IEEE float; verify the headers below.
        writers[lane].reset(wav.createWriterFor(stream.get(), kOutputSampleRate, 2, 32, {}, 0));
        demand(writers[lane] != nullptr, "Cannot create float32 stem writer"); stream.release();
    }
    juce::AudioBuffer<float> block(int(kLaneCount * 2), blockFrames);
    std::array<float*, kLaneCount * 2> pointers{};
    for (std::size_t channel = 0; channel < pointers.size(); ++channel) pointers[channel] = block.getWritePointer(int(channel));
    for (;;) {
        check(cancel); const auto rendered = renderer.renderNext(pointers, blockFrames, &cancel);
        if (rendered.status == Status::cancelled) throw Cancelled{};
        if (rendered.status == Status::complete) break;
        demand(rendered.status == Status::ok && rendered.frames > 0, "Instrumental rendering failed");
        for (std::size_t lane = 0; lane < kLaneCount; ++lane) {
            const float* channels[]{pointers[lane * 2], pointers[lane * 2 + 1]};
            demand(writers[lane]->writeFromFloatArrays(channels, 2, rendered.frames), "Stem storage write failed");
        }
        progress.store(0.85 * double(renderer.renderedFrames()) / double(frames), std::memory_order_relaxed);
    }
    demand(renderer.renderedFrames() == frames, "Rendered stem frame count changed");
    for (std::size_t lane = 0; lane < kLaneCount; ++lane) {
        check(cancel); demand(writers[lane]->flush(), "Stem finalization failed");
        streams[lane]->flush(); demand(streams[lane]->good(), "Stem storage flush failed"); writers[lane].reset();
        demand(closedOk[lane], "Stem final close or storage synchronization failed");
        juce::AudioFormatManager formats; formats.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(result.files[lane]));
        demand(reader && reader->usesFloatingPointData && reader->bitsPerSample == 32
            && reader->numChannels == 2 && reader->sampleRate == kOutputSampleRate && reader->lengthInSamples == frames,
            "Stem failed exact 96 kHz / float32 / equal-length verification");
        Identity identity{result.files[lane], {}, result.files[lane].getSize(), frames,
            result.files[lane].getLastModificationTime().toMilliseconds(), double(kOutputSampleRate), 2};
        reader.reset(); identity.sha256 = hash(identity.file, cancel, identity.bytes);
        demand(metadataUnchanged(identity), "Output changed during verification"); result.identities[lane] = identity;
        progress.store(0.85 + 0.15 * double(lane + 1) / double(kLaneCount), std::memory_order_relaxed);
    }
    for (const auto& source : identities) if (source.present()) verify(source, cancel);
    auto manifest = patternJson(plan, identities); auto* object = manifest.getDynamicObject();
    object->setProperty("complete", true); object->setProperty("outputSampleRate", kOutputSampleRate);
    object->setProperty("outputBitsPerSample", 32); object->setProperty("outputSampleFormat", "IEEE_FLOAT");
    object->setProperty("outputFrames", juce::int64(frames)); object->setProperty("musicalFrames", juce::int64(renderer.musicalFrames()));
    juce::Array<juce::var> outputs;
    for (const auto& identity : result.identities) outputs.add(identityJson(identity));
    object->setProperty("outputs", outputs); result.manifest = result.directory.getChildFile("manifest.json");
    writeJson(result.manifest, manifest, cancel); return result;
}
} // namespace hem::instrumental::files
