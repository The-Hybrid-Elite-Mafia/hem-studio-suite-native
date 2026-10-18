#include "OfflineMixRenderer.h"
#include "../CoreMasterStage.h"
#include <filesystem>
#include <limits>
#include <stdexcept>

namespace hem {
namespace {

// Leave ample room for JUCE's RIFF/JUNK/fmt/data headers. Never silently switch
// to RF64 or wrap a classic 32-bit RIFF/data length.
constexpr juce::int64 maximumDataBytes = 0xff000000LL;
constexpr juce::int64 maximumFrames = maximumDataBytes / (2 * sizeof(float));

[[noreturn]] void fail(const juce::String& message) {
    throw std::runtime_error(message.toStdString());
}

void require(bool condition, const juce::String& message) {
    if (!condition) fail(message);
}

void validateGain(float db, const juce::String& label) {
    require(std::isfinite(db) && db >= -60.f && db <= 6.f,
            label + " must be finite and within -60 to +6 dB");
}

void validateSamples(const juce::AudioBuffer<float>& buffer, int start, int count,
                     const juce::String& label) {
    for (int channel = 0; channel < buffer.getNumChannels(); ++channel) {
        const auto* samples = buffer.getReadPointer(channel, start);
        for (int i = 0; i < count; ++i)
            if (!std::isfinite(samples[i])) fail("Non-finite audio in " + label);
    }
}

juce::int64 boundedCeil(long double frames, const juce::String& label) {
    require(std::isfinite(frames) && frames >= 0
            && frames <= static_cast<long double>(maximumFrames),
            label + " exceeds classic stereo float WAV capacity");
    return static_cast<juce::int64>(std::ceil(frames));
}

// Unlike AudioTransportSource, this adapter never starts a read-ahead thread,
// never has a transport fade, and propagates a decoder read failure. JUCE's
// reader duplicates mono into both destination channels. The resampler calls
// this synchronously on the export worker and sees explicit zeros after EOF.
class SynchronousWavSource final : public juce::AudioSource {
public:
    explicit SynchronousWavSource(const juce::File& file) : name(file.getFileName()) {
        require(file.existsAsFile(), "Missing source: " + file.getFullPathName());
        auto stream = file.createInputStream();
        require(stream != nullptr && stream->getStatus().wasOk(), "Cannot open source: " + name);
        juce::WavAudioFormat wav;
        reader.reset(wav.createReaderFor(stream.release(), true));
        require(reader != nullptr, "Cannot decode WAV: " + name);
        require(reader->lengthInSamples > 0 && reader->numChannels >= 1 && reader->numChannels <= 2,
                "Use a non-empty mono or stereo WAV: " + name);
        require(std::isfinite(reader->sampleRate) && reader->sampleRate >= 8000
                && reader->sampleRate <= 384000, "Unsupported source sample rate: " + name);
    }

    double sampleRate() const noexcept { return reader->sampleRate; }
    juce::int64 length() const noexcept { return reader->lengthInSamples; }
    void prepareToPlay(int, double) override { position = 0; }
    void releaseResources() override {}
    void getNextAudioBlock(const juce::AudioSourceChannelInfo& info) override {
        info.clearActiveBufferRegion();
        const auto remaining = std::max<juce::int64>(0, length() - position);
        const int count = static_cast<int>(std::min<juce::int64>(info.numSamples, remaining));
        if (count > 0) {
            require(reader->read(info.buffer, info.startSample, count, position, true, true),
                    "Source read failed: " + name);
            validateSamples(*info.buffer, info.startSample, count, name);
            position += count;
        }
    }
private:
    juce::String name;
    std::unique_ptr<juce::AudioFormatReader> reader;
    juce::int64 position = 0;
};

struct OfflineTrack {
    // Reverse destruction keeps the resampler's borrowed source valid.
    std::unique_ptr<SynchronousWavSource> source;
    std::unique_ptr<juce::ResamplingAudioSource> resampler;
    InsertChain chain;
    float gain = 0;
    ~OfflineTrack() {
        if (resampler) resampler->releaseResources();
        chain.release();
    }
};

// All readers, resamplers, prepared nodes and buffers are private to this render.
// No live TrackEngine or live mic/monitor object can enter the export graph.
struct SourceSnapshot {
    std::array<OfflineTrack, 13> tracks;
    juce::int64 sourceFrames = 0;
};

std::filesystem::path nativePath(const juce::File& file) {
    return std::filesystem::u8path(file.getFullPathName().toStdString());
}

void ensureNoDestination(const juce::File& destination) {
    std::error_code error;
    // symlink_status also recognises a dangling symlink, which must never be
    // treated as a free destination. Atomic publication repeats this check in
    // the filesystem, so a race cannot overwrite another writer's file.
    const auto status = std::filesystem::symlink_status(nativePath(destination), error);
    require(!error || error == std::errc::no_such_file_or_directory,
            "Cannot inspect export destination: " + juce::String(error.message()));
    require(status.type() == std::filesystem::file_type::not_found,
            "Export destination already exists; choose a new name");
}

void verifyCompletedWav(const juce::File& file, double rate, juce::int64 frames) {
    require(file.getSize() >= frames * 8 && file.getSize() <= maximumDataBytes + 65536,
            "Export size validation failed");
    auto stream = file.createInputStream();
    require(stream != nullptr && stream->getStatus().wasOk(), "Cannot verify export WAV");
    require(stream->readInt() == static_cast<int>(juce::ByteOrder::littleEndianInt("RIFF")),
            "Export is not a classic RIFF WAV");
    stream->setPosition(0);
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatReader> reader(wav.createReaderFor(stream.release(), true));
    require(reader && reader->numChannels == 2 && reader->bitsPerSample == 32
            && reader->usesFloatingPointData && reader->sampleRate == rate
            && reader->lengthInSamples == frames, "Export WAV header verification failed");
}

} // namespace

OfflineMixResult OfflineMixRenderer::render(const SessionSnapshot& snapshot,
                                           const juce::File& destination,
                                           const OfflineMixOptions& options,
                                           const std::function<bool()>& cancelled,
                                           const std::function<void(double)>& progress,
                                           OfflinePreparedPlugins* preparedExternal,
                                           const Observer& observer) {
    OfflineMixResult result;
    try {
        juce::ScopedNoDenormals noDenormals;
        const auto checkCancellation = [&] {
            if (cancelled && cancelled()) fail("Offline mix cancelled");
        };
        // Progress is informational. A UI callback must not invalidate a
        // successfully published file by throwing after the commit point.
        const auto reportProgress = [&](double amount) noexcept {
            if (progress) { try { progress(amount); } catch (...) {} }
        };
        require(std::isfinite(options.sampleRate) && options.sampleRate >= 8000
                && options.sampleRate <= 384000 && std::floor(options.sampleRate) == options.sampleRate,
                "Export sample rate must be an integer from 8000 to 384000 Hz");
        require(options.blockSize >= 1 && options.blockSize <= 8192,
                "Export block size must be from 1 to 8192 samples");
        require(std::isfinite(options.tailSeconds) && options.tailSeconds >= 0 && options.tailSeconds <= 3600,
                "Export tail must be finite and from 0 to 3600 seconds");
        const auto tailCheck=preflightTail(snapshot,options);
        require(tailCheck.wasOk(),tailCheck.getErrorMessage());
        require(destination != juce::File() && destination.getParentDirectory().isDirectory(),
                "Choose an export destination in an existing directory");
        ensureNoDestination(destination);
        validateGain(snapshot.inputDb, "Input gain");
        validateGain(snapshot.outputDb, "Output gain");
        checkCancellation();
        reportProgress(0);

        SourceSnapshot sources;
        const bool anySolo = std::any_of(snapshot.tracks.begin(), snapshot.tracks.end(),
                                        [](const auto& track) { return track.solo; });
        const float inputGain = gain(snapshot.inputDb);
        const float outputGain = gain(snapshot.outputDb);
        bool loaded = false;
        for (size_t i = 0; i < snapshot.tracks.size(); ++i) {
            checkCancellation();
            const auto& saved = snapshot.tracks[i];
            if(saved.clipEdit){
                require(saved.clipEdit->trackIndex==int(i)&&juce::File(clipEdit::activeMedia(*saved.clipEdit).path)==saved.source,"Clip edit active source mismatch");
                const auto verified=clipEdit::verifyFiles(*saved.clipEdit,cancelled);
                require(verified.wasOk(),verified.getErrorMessage());
            }
            auto& track = sources.tracks[i];
            const auto label = "Track " + juce::String(static_cast<int>(i) + 1);
            validateGain(saved.gainDb, label + " gain");
            track.gain = !saved.muted && (!anySolo || saved.solo) ? gain(saved.gainDb) : 0.f;
            track.chain.prepare(options.sampleRate, options.blockSize);
            if (saved.source != juce::File()) {
                track.source = std::make_unique<SynchronousWavSource>(saved.source);
                const auto length = boundedCeil(static_cast<long double>(track.source->length())
                                                * options.sampleRate / track.source->sampleRate(), label);
                sources.sourceFrames = std::max(sources.sourceFrames, length);
                track.resampler = std::make_unique<juce::ResamplingAudioSource>(track.source.get(), false, 2);
                track.resampler->setResamplingRatio(track.source->sampleRate() / options.sampleRate);
                track.resampler->prepareToPlay(options.blockSize, options.sampleRate);
                loaded = true;
            }
            bool signalCanReach=saved.source!=juce::File();
            for (int slotIndex = 0; slotIndex < InsertChain::slotCount; ++slotIndex) {
                checkCancellation();
                const auto& slot = saved.slots[static_cast<size_t>(slotIndex)];
                require(slot.parameters.hasType("PARAMETERS"), label + " has invalid insert parameters");
                std::unique_ptr<ModuleProcessor> processor;
                if (slot.pluginId.isEmpty()) {
                    require(!slot.native,"Empty export insert contains native resource state");
                    require(slot.parameters.getNumChildren() == 0, label + " empty insert has parameters");
                } else if (slot.pluginId == "external-vst3") {
                    require(!slot.native,"External export insert contains native resource state");
                    require(preparedExternal != nullptr, "External VST3 export requires message-thread staging");
                    processor = std::move(preparedExternal->slots[i][static_cast<size_t>(slotIndex)]);
                    require(processor && processor->isExternal() && processor->processingReady(),
                            label + " external VST3 is not prepared or was invalidated");
                } else {
                    const auto& registry = PluginRegistry::entries();
                    const auto entry = std::find_if(registry.begin(), registry.end(),
                                                   [&](const auto& e) { return slot.pluginId == e.id; });
                    require(entry != registry.end() && !entry->monitoringOnly,
                            label + " has an unknown or monitoring-only insert: " + slot.pluginId);
                    processor = entry->create();
                    require(processor != nullptr && !processor->monitorOnly(), "Cannot create insert: " + slot.pluginId);
                    // Restore only a newly owned private processor. Failed
                    // scalar/resource preparation discards this candidate.
                    std::unique_ptr<ModuleProcessor> restored;
                    const auto restore=NativeProcessorState::restoreUnpublished(std::move(processor),slot,
                        {options.sampleRate,options.blockSize,snapshot.tempoBpm,true,cancelled},restored);
                    require(restore.wasOk(),restore.getErrorMessage());processor=std::move(restored);
                    processor->configureFrozenExport(snapshot.tempoBpm,track.gain!=0.f&&!slot.bypassed&&signalCanReach);
                    processor->setNonRealtime(true);
                }
                auto node = track.chain.stageNode(std::move(processor), options.sampleRate,
                                                 options.blockSize, track.chain.slotLatency());
                if(node){const auto ready=NativeProcessorState::validatePreparedForPublication(*node->processor);
                    require(ready.wasOk(),ready.getErrorMessage());}
                track.chain.installInitial(slotIndex, std::move(node), slot.bypassed);
                if(!slot.bypassed&&slot.pluginId.isNotEmpty()&&slot.pluginId!="multitap-delay"&&slot.pluginId!="tape-delay")signalCanReach=true;
            }
        }
        require(loaded, "Load at least one non-empty WAV stem before exporting");
        // Keep the user-facing double's multiplication in double precision:
        // promoting .0125 before multiplying by 48000 preserves a tiny binary
        // representation residue and would incorrectly request 601, not 600.
        const auto tailFrames = boundedCeil(options.tailSeconds * options.sampleRate,
                                           "Export tail");
        require(sources.sourceFrames <= maximumFrames - tailFrames,
                "Mix plus tail exceeds classic stereo float WAV capacity");
        const auto frames = sources.sourceFrames + tailFrames;
        CoreMasterStage master;
        master.prepare(options.sampleRate, options.blockSize);
        const int latency = sources.tracks.front().chain.latencySamples() + master.latencySamples();
        const auto processingFrames = frames + latency;
        juce::AudioBuffer<float> mix(2, options.blockSize), scratch(2, options.blockSize);

        checkCancellation();
        juce::TemporaryFile temporary(destination);
        require(!temporary.getFile().exists(), "Temporary export path is already in use");
        std::unique_ptr<juce::OutputStream> stream = temporary.getFile().createOutputStream();
        require(stream != nullptr, "Cannot create temporary export WAV");
        auto* fileStream = dynamic_cast<juce::FileOutputStream*>(stream.get());
        require(fileStream && fileStream->getStatus().wasOk(), "Cannot open export stream");
        juce::WavAudioFormat wav;
        auto writer = wav.createWriterFor(stream,
            juce::AudioFormatWriterOptions().withSampleRate(options.sampleRate).withNumChannels(2)
                .withBitsPerSample(32).withSampleFormat(juce::AudioFormatWriterOptions::SampleFormat::floatingPoint));
        require(writer != nullptr, "Cannot create stereo float32 WAV writer");
        juce::int64 written = 0;
        for (juce::int64 position = 0; position < processingFrames;) {
            checkCancellation();
            const int count = static_cast<int>(std::min<juce::int64>(options.blockSize, processingFrames - position));
            mix.clear();
            for (auto& track : sources.tracks) {
                checkCancellation();
                scratch.clear();
                if (track.resampler)
                    track.resampler->getNextAudioBlock({&scratch, 0, count});
                scratch.applyGain(0, count, inputGain);
                track.chain.process(scratch.getWritePointer(0), scratch.getWritePointer(1), count,snapshot.tempoBpm);
                for(int slot=0;slot<InsertChain::slotCount;++slot)
                    if(auto* processor=track.chain.activeProcessor(slot);processor && processor->isExternal())
                        require(processor->processingReady(), "External VST3 changed latency/layout or failed during export; no final file published");
                validateSamples(scratch, 0, count, "track insert output");
                if (track.gain != 0.f)
                    for (int channel = 0; channel < 2; ++channel)
                        mix.addFrom(channel, 0, scratch, channel, 0, count, track.gain);
            }
            mix.applyGain(0, count, outputGain);
            validateSamples(mix, 0, count, "master input");
            if(observer)observer(mix,position,count,false);
            master.process(mix.getWritePointer(0), mix.getWritePointer(1), count);
            validateSamples(mix, 0, count, "master output");
            if(observer)observer(mix,position,count,true);
            const int skip = static_cast<int>(juce::jlimit<juce::int64>(0, count, latency - position));
            if (skip < count) {
                require(writer->writeFromAudioSampleBuffer(mix, skip, count - skip), "Export disk write failed");
                written += count - skip;
            }
            position += count;
            // Reserve the final 1% for flush, verification and publication.
            reportProgress(0.99 * static_cast<double>(position) / static_cast<double>(processingFrames));
        }
        require(written == frames, "Export frame count mismatch");
        checkCancellation();
        require(writer->flush(), "Cannot finalize export WAV header");
        fileStream->flush();
        require(fileStream->getStatus().wasOk(), "Export flush failed");
        writer.reset();
        verifyCompletedWav(temporary.getFile(), options.sampleRate, frames);
        for(const auto& track:snapshot.tracks)if(track.clipEdit){
            const auto verified=clipEdit::verifyFiles(*track.clipEdit,cancelled);
            require(verified.wasOk(),verified.getErrorMessage());
        }
        checkCancellation();
        // create_hard_link is an atomic no-replace operation even if another
        // process creates destination between validation and this commit.
        std::error_code error;
        std::filesystem::create_hard_link(nativePath(temporary.getFile()), nativePath(destination), error);
        // Do not allocate/build an error string after a successful commit.
        // Everything after publication is noexcept/informational.
        if (error)
            fail("Cannot publish export without overwriting; destination may exist or this filesystem may not support hard links: "
                 + juce::String(error.message()));
        result.frames = frames;
        result.latencyRemoved = latency;
        reportProgress(1);
    } catch (const std::exception& exception) {
        result.status = juce::Result::fail(juce::String(exception.what()));
    } catch (...) {
        result.status = juce::Result::fail("Unknown offline mix failure");
    }
    return result;
}

} // namespace hem
