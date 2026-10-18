#include "PitchTimeClipRenderer.h"
#include "NoReplaceFile.h"
#include <juce_cryptography/juce_cryptography.h>
#include <filesystem>
#include <stdexcept>

namespace hem::pitchtime {
namespace {
struct Cancelled {};
void demand(bool ok, const juce::String& message) { if (!ok) throw std::runtime_error(message.toStdString()); }
void checkCancel(const Cancel& cancelled) { if (cancelled && cancelled()) throw Cancelled{}; }
class HashInput final : public juce::InputStream {
public:
    HashInput(const juce::File& file, const Cancel& c) : input(file), cancelled(c) {
        demand(input.getStatus().wasOk(), "Cannot open file for hashing");
    }
    juce::int64 getTotalLength() override { return input.getTotalLength(); }
    juce::int64 getPosition() override { return input.getPosition(); }
    bool setPosition(juce::int64 position) override { return input.setPosition(position); }
    bool isExhausted() override { return input.isExhausted(); }
    int read(void* buffer, int bytes) override {
        checkCancel(cancelled);
        const int result = input.read(buffer, bytes);
        demand(input.getStatus().wasOk(), "File read failed during hashing");
        return result;
    }
private:
    juce::FileInputStream input;
    const Cancel& cancelled;
};
juce::String hash(const juce::File& file, const Cancel& cancel) {
    HashInput stream(file, cancel);
    const auto length = stream.getTotalLength();
    demand(length > 0, "Empty file cannot identify an audio source");
    const auto result = juce::SHA256(stream).toHexString();
    demand(stream.getPosition() == length && file.getSize() == length, "File changed/truncated while hashing");
    return result;
}
bool validHash(const juce::String& value) {
    return value.length() == 64 && value.containsOnly("0123456789abcdefABCDEF");
}
struct Staging {
    juce::File directory, file;
    explicit Staging(const juce::File& parent) {
        directory = parent.getChildFile("hem-pitch-stage-" + juce::Uuid().toString());
        std::error_code error;
        demand(std::filesystem::create_directory(noReplaceFile::nativePath(directory), error) && !error,
               "Cannot create exclusive render staging directory");
        file = directory.getChildFile("render.wav");
    }
    ~Staging() {
        // Only these owned entries; no recursive deletion, no caller paths.
        file.deleteFile(); directory.deleteFile();
    }
};
juce::String recipe(const ClipRenderRequest& request) {
    auto object = std::make_unique<juce::DynamicObject>();
    auto set = [&](const char* key, juce::var value) { object->setProperty(key, value); };
    set("version", 1); set("backend", backendIdentity);
    set("sourceSha256", request.expectedSourceSha256.toLowerCase());
    set("sourceGeneration", request.sourceGeneration); set("startFrame", request.startFrame); set("frameCount", request.frameCount);
    const auto& s = request.settings;
    set("sampleRate", s.sampleRate); set("channels", s.channels); set("durationRatio", s.durationRatio);
    set("pitchSemitones", s.pitchSemitones); set("formantSemitones", s.formantSemitones); set("formantBaseHz", s.formantBaseHz);
    set("wetMix", s.wetMix); set("outputDb", s.outputDb);
    set("preserveFormants", s.preserveFormants); set("strategy", int(s.strategy)); set("tempoBpm", request.capturedTempoBpm);
    set("correctionEnabled", s.correction.enabled); set("key", s.correction.key); set("scale", int(s.correction.scale));
    set("amount", s.correction.amount); set("speedMs", s.correction.speedMs);
    return juce::JSON::toString(juce::var(object.release()), true, 17);
}
}

juce::File PitchTimeClipRenderer::uniqueDestination(const juce::File& directory) {
    return directory.getChildFile("HEM-PitchTime-" + juce::Uuid().toString() + ".wav");
}

ClipRenderResult PitchTimeClipRenderer::render(const ClipRenderRequest& request, const Cancel& cancelled, const CorrectionObserver& observer) {
    ClipRenderResult result;
    try {
        checkCancel(cancelled);
        demand(request.source.existsAsFile(), "Source audio is missing");
        demand(request.destination != juce::File() && request.destination != request.source
            && request.destination.getParentDirectory().isDirectory() && request.destination.hasFileExtension("wav"),
            "Choose a new WAV destination in an existing directory");
        std::error_code error;
        demand(noReplaceFile::available(request.destination, error), "Destination is occupied or unavailable; nothing was overwritten");
        demand(validHash(request.expectedSourceSha256), "An exact original source SHA-256 is required");
        demand(request.sourceGeneration >= 0 && request.startFrame >= 0 && request.frameCount > 0,
               "Invalid source generation or frame range");
        demand(std::isfinite(request.capturedTempoBpm) && request.capturedTempoBpm >= 20 && request.capturedTempoBpm <= 400,
               "Tempo snapshot must be between 20 and 400 BPM");
        auto& m = result.metadata;
        m.source = request.source; m.rendered = request.destination;
        m.sourceBytes = request.source.getSize();
        m.sourceSha256 = hash(request.source, cancelled);
        demand(m.sourceSha256.equalsIgnoreCase(request.expectedSourceSha256), "Source bytes changed since selection");
        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(request.source));
        demand(reader != nullptr, "Unsupported or corrupt audio source");
        demand(reader->numChannels >= 1 && reader->numChannels <= 2 && reader->sampleRate == request.settings.sampleRate
            && int(reader->numChannels) == request.settings.channels, "Source format differs from selected recipe");
        demand(reader->lengthInSamples > 0 && request.startFrame <= reader->lengthInSamples
            && request.frameCount <= reader->lengthInSamples-request.startFrame, "Selected frame range exceeds source");
        m.sourceFrames = reader->lengthInSamples;
        demand(std::isfinite(request.settings.durationRatio) && request.settings.durationRatio >= .5 && request.settings.durationRatio <= 2,
               "Duration ratio is outside .5 to 2");
        const long double requestedFrames = std::floor(static_cast<long double>(request.frameCount)*request.settings.durationRatio+.5L);
        // Classic RIFF float output; explicitly reject rather than wrap >4 GiB.
        demand(requestedFrames >= 1 && requestedFrames*reader->numChannels*4 <= 0xff000000LL,
               "Rendered audio exceeds classic float WAV capacity");
        const auto expectedFrames = static_cast<juce::int64>(std::llround(double(request.frameCount)*request.settings.durationRatio));
        Staging staging(request.destination.getParentDirectory());
        auto stream = staging.file.createOutputStream();
        demand(stream && stream->getStatus().wasOk(), "Cannot open staged WAV");
        auto* rawStream = stream.get();
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::AudioFormatWriter> writer(wav.createWriterFor(stream.get(), reader->sampleRate, reader->numChannels, 32, {}, 0));
        demand(writer != nullptr, "Cannot create float WAV writer");
        stream.release();
        result.backend = pitchtime::render(request.settings, request.frameCount,
            [&](std::int64_t offset, float* const* destination, int count) {
                // JUCE reader preserves floating samples and promotes integer formats.
                juce::AudioBuffer<float> view(destination, request.settings.channels, count);
                return reader->read(&view, 0, count, request.startFrame+offset, true, true);
            },
            [&](const float* const* channels, int count) { return writer->writeFromFloatArrays(channels, request.settings.channels, count); },
            cancelled, observer);
        if (result.backend.status == Result::Status::cancelled) throw Cancelled{};
        demand(result.backend.status == Result::Status::complete && result.backend.writtenFrames == expectedFrames,
               "Pitch/time backend failed; partial output discarded");
        demand(writer->flush(), "Cannot finalize staged WAV");
        rawStream->flush();
        demand(rawStream->getStatus().wasOk(), "Storage error while flushing staged WAV");
        writer.reset(); reader.reset();
        checkCancel(cancelled);
        std::unique_ptr<juce::AudioFormatReader> verification(formats.createReaderFor(staging.file));
        demand(verification && verification->usesFloatingPointData && verification->bitsPerSample == 32
            && verification->sampleRate == request.settings.sampleRate && int(verification->numChannels) == request.settings.channels
            && verification->lengthInSamples == expectedFrames, "Completed WAV format/frame verification failed");
        verification.reset();
        m.renderedSha256 = hash(staging.file, cancelled); m.renderedBytes = staging.file.getSize();
        demand(hash(request.source, cancelled) == m.sourceSha256 && request.source.getSize() == m.sourceBytes,
               "Original source changed during rendering; candidate discarded");
        m.sourceGeneration = request.sourceGeneration; m.startFrame = request.startFrame; m.frameCount = request.frameCount;
        m.renderedFrames = expectedFrames; m.sampleRate = request.settings.sampleRate; m.channels = request.settings.channels;
        m.recipeJson = recipe(request); m.recipeSha256 = juce::SHA256(m.recipeJson.toRawUTF8(), size_t(m.recipeJson.getNumBytesAsUTF8())).toHexString();
        checkCancel(cancelled);
        error = noReplaceFile::publish(staging.file, request.destination);
        if (error) demand(false, "Cannot publish without replacement; destination occupied or hard links unsupported");
        // No throwing work after this commit point; cancellation cannot undo a
        // successful publication. Staging destructor only removes its own link.
        result.status = ClipRenderResult::Status::complete;
    } catch (const Cancelled&) { result.status = ClipRenderResult::Status::cancelled; result.error = "Render cancelled; original preserved"; }
      catch (const std::exception& e) { result.error = juce::String(e.what()); }
      catch (...) { result.error = "Unexpected render failure; original preserved"; }
    return result;
}
}
