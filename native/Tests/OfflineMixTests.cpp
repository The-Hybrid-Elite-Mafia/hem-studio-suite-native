#include "../Source/Systems/OfflineMixRenderer.h"
#include "../Source/CoreMasterStage.h"
#include "../Source/Systems/NativeProcessorState.h"
#include <filesystem>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
void require(bool condition, const juce::String& message) {
    if (!condition) throw std::runtime_error(message.toStdString());
}

struct Fixtures {
    juce::File directory = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getChildFile("hem-offline-mix-" + juce::Uuid().toString());
    Fixtures() { require(directory.createDirectory().wasOk(), "Cannot create fixture directory"); }
    ~Fixtures() { directory.deleteRecursively(); }
    juce::File file(const juce::String& name) const { return directory.getChildFile(name); }
};

void writeWav(const juce::File& file, const juce::AudioBuffer<float>& audio, double rate,
              bool floating = true) {
    std::unique_ptr<juce::OutputStream> stream = file.createOutputStream();
    require(stream != nullptr, "Cannot create fixture WAV");
    juce::WavAudioFormat wav;
    auto writer = wav.createWriterFor(stream,
        juce::AudioFormatWriterOptions().withSampleRate(rate).withNumChannels(audio.getNumChannels())
            .withBitsPerSample(floating ? 32 : 24)
            .withSampleFormat(floating ? juce::AudioFormatWriterOptions::SampleFormat::floatingPoint
                                       : juce::AudioFormatWriterOptions::SampleFormat::integral));
    require(writer && writer->writeFromAudioSampleBuffer(audio, 0, audio.getNumSamples()), "Cannot write fixture WAV");
    require(writer->flush(), "Cannot finalize fixture WAV");
}

juce::AudioBuffer<float> readWav(const juce::File& file, double rate, juce::int64 frames) {
    auto stream = file.createInputStream();
    require(stream && stream->readInt() == static_cast<int>(juce::ByteOrder::littleEndianInt("RIFF")),
            "Output is not RIFF");
    const auto riffSize = static_cast<juce::uint32>(stream->readInt());
    require(static_cast<juce::int64>(riffSize) + 8 == file.getSize(), "RIFF size field is incorrect");
    require(stream->readInt() == static_cast<int>(juce::ByteOrder::littleEndianInt("WAVE")), "Output is not WAVE");
    bool floatFormat = false, dataFound = false;
    while (stream->getPosition() + 8 <= file.getSize()) {
        const auto id = stream->readInt();
        const auto length = static_cast<juce::uint32>(stream->readInt());
        const auto dataStart = stream->getPosition();
        if (id == static_cast<int>(juce::ByteOrder::littleEndianInt("fmt "))) {
            const auto tag = static_cast<juce::uint16>(stream->readShort());
            require(stream->readShort() == 2, "WAV header is not stereo");
            require(stream->readInt() == static_cast<int>(rate), "WAV header rate mismatch");
            require(stream->readInt() == static_cast<int>(rate * 8), "WAV byte-rate mismatch");
            require(stream->readShort() == 8 && stream->readShort() == 32, "WAV alignment/bit depth mismatch");
            if (tag == 3) floatFormat = true;
            if (tag == 0xfffe && length >= 40) {
                stream->setPosition(dataStart + 24);
                floatFormat = stream->readInt() == 3;
            }
        }
        if (id == static_cast<int>(juce::ByteOrder::littleEndianInt("data"))) {
            dataFound = true;
            require(static_cast<juce::int64>(length) == frames * 8, "WAV data length mismatch");
        }
        require(stream->setPosition(dataStart + length + (length & 1)), "Invalid RIFF chunk");
    }
    require(floatFormat && dataFound, "Missing IEEE float format or data chunk");
    stream->setPosition(0);
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatReader> reader(wav.createReaderFor(stream.release(), true));
    require(reader && reader->sampleRate == rate && reader->numChannels == 2
            && reader->bitsPerSample == 32 && reader->usesFloatingPointData
            && reader->lengthInSamples == frames, "Decoded WAV format/frame count mismatch");
    require(frames <= 1000000, "Test fixture is unexpectedly large");
    juce::AudioBuffer<float> result(2, static_cast<int>(frames));
    require(reader->read(&result, 0, static_cast<int>(frames), 0, true, true), "Cannot decode export");
    for (int channel = 0; channel < 2; ++channel)
        for (int i = 0; i < result.getNumSamples(); ++i)
            require(std::isfinite(result.getSample(channel, i)), "Non-finite export sample");
    return result;
}

juce::AudioBuffer<float> constantAudio(int frames, float left, float right, int channels = 2) {
    juce::AudioBuffer<float> audio(channels, frames);
    juce::FloatVectorOperations::fill(audio.getWritePointer(0), left, frames);
    if (channels == 2) juce::FloatVectorOperations::fill(audio.getWritePointer(1), right, frames);
    return audio;
}

juce::AudioBuffer<float> exportAudio(const hem::SessionSnapshot& snapshot, const juce::File& file,
                                   const hem::OfflineMixOptions& options, juce::int64 frames) {
    double lastProgress = -1;
    int reports = 0;
    bool progressValid = true;
    const auto result = hem::OfflineMixRenderer::render(snapshot, file, options, {}, [&](double p) {
        progressValid = progressValid && std::isfinite(p) && p >= lastProgress && p <= 1;
        lastProgress = p;
        ++reports;
    });
    require(result.status.wasOk(), "Render failed: " + result.status.getErrorMessage());
    require(result.frames == frames, "Returned frame count mismatch");
    hem::MasterTruePeakLimiter baseline;
    baseline.prepare(options.sampleRate, options.blockSize, 2);
    require(result.latencyRemoved == static_cast<int>(std::ceil(options.sampleRate * .006)) * 8 + baseline.latency(),
            "Reported latency does not use actual baseline latency");
    if (options.sampleRate == 48000) require(result.latencyRemoved == 2812, "48 kHz measured chain-plus-master latency changed");
    require(progressValid && reports > 1 && lastProgress == 1, "Progress regressed, escaped [0, 1], or did not complete");
    return readWav(file, options.sampleRate, frames);
}

void compare(const juce::AudioBuffer<float>& actual, const juce::AudioBuffer<float>& expected,
             float tolerance, const juce::String& label) {
    require(actual.getNumChannels() == expected.getNumChannels()
            && actual.getNumSamples() == expected.getNumSamples(), label + " dimensions mismatch");
    float maximum = 0;
    for (int channel = 0; channel < actual.getNumChannels(); ++channel)
        for (int i = 0; i < actual.getNumSamples(); ++i)
            maximum = std::max(maximum, std::abs(actual.getSample(channel, i) - expected.getSample(channel, i)));
    require(maximum <= tolerance, label + " maximum difference " + juce::String(maximum, 10));
}

// Independent reference: feed the active master with the expected mix,
// offset by the host's fixed eight-slot delay, then trim its actual latency.
// No OfflineMixRenderer code or CoreMasterStage adapter is used here.
juce::AudioBuffer<float> baselineReference(const juce::AudioBuffer<float>& input,
                                         const hem::OfflineMixOptions& options, int outputFrames) {
    hem::MasterTruePeakLimiter baseline;
    baseline.prepare(options.sampleRate, options.blockSize, 2);
    const int chainLatency = static_cast<int>(std::ceil(options.sampleRate * .006)) * 8;
    const int latency = chainLatency + baseline.latency();
    juce::AudioBuffer<float> result(2, outputFrames), block(2, options.blockSize);
    result.clear();
    for (int position = 0; position < outputFrames + latency;) {
        const int count = std::min(options.blockSize, outputFrames + latency - position);
        block.clear();
        for (int i = 0; i < count; ++i) {
            const int source = position + i - chainLatency;
            if (source >= 0 && source < input.getNumSamples())
                for (int channel = 0; channel < 2; ++channel)
                    block.setSample(channel, i, input.getSample(channel, source));
        }
        baseline.process(block, count);
        for (int i = 0; i < count; ++i) {
            const int destination = position + i - latency;
            if (destination >= 0)
                for (int channel = 0; channel < 2; ++channel)
                    result.setSample(channel, destination, block.getSample(channel, i));
        }
        position += count;
    }
    return result;
}

void setParameter(hem::SlotSnapshot& slot, const juce::String& id, double value) {
    for (auto child : slot.parameters) {
        if (child["id"].toString() == id) { child.setProperty("value", value, nullptr); return; }
    }
    throw std::runtime_error("Test parameter not found");
}

void install(hem::SlotSnapshot& slot, const juce::String& plugin) {
    slot.pluginId = plugin;
    auto processor = hem::PluginRegistry::create(plugin);
    require(processor != nullptr, "Unknown test insert");
    hem::NativeProcessorSnapshot captured;
    const auto status=hem::NativeProcessorState::capture(*processor,captured);
    require(status.wasOk(),status.getErrorMessage());
    static_cast<hem::NativeProcessorSnapshot&>(slot)=std::move(captured);
}

void failure(const hem::SessionSnapshot& snapshot, const juce::File& file,
             const hem::OfflineMixOptions& options, const juce::String& label,
             const std::function<bool()>& cancelled = {}) {
    const int before = file.getParentDirectory().getNumberOfChildFiles(juce::File::findFilesAndDirectories);
    const auto result = hem::OfflineMixRenderer::render(snapshot, file, options, cancelled);
    require(result.status.failed(), label + " was accepted");
    require(result.frames == 0 && result.latencyRemoved == 0, label + " reports a committed export");
    require(!file.exists(), label + " left a final file");
    require(file.getParentDirectory().getNumberOfChildFiles(juce::File::findFilesAndDirectories) == before,
            label + " leaked a temporary file");
}

void checkBasicMix(Fixtures& fixtures) {
    hem::OfflineMixOptions options;
    options.blockSize = 257; // The latency boundary and final write are unaligned.
    options.tailSeconds = .0125;
    constexpr int frames = 4801, tail = 600;
    hem::SessionSnapshot snapshot;
    snapshot.inputDb = -3;
    snapshot.outputDb = -6;
    snapshot.positionSeconds = 918.5; // Explicitly ignored.
    snapshot.looping = true; // Explicitly ignored; tail must not loop sources.
    std::array<juce::AudioBuffer<float>, 13> tracks;
    for (size_t i = 0; i < tracks.size(); ++i) {
        const int length = frames - static_cast<int>(i) * 37;
        tracks[i] = constantAudio(length, .004f * static_cast<float>(i + 1), -.002f * static_cast<float>(i + 1));
        snapshot.tracks[i].source = fixtures.file("stem-" + juce::String(static_cast<int>(i)) + ".wav");
        snapshot.tracks[i].gainDb = -static_cast<float>(i) * .5f;
        writeWav(snapshot.tracks[i].source, tracks[i], 48000);
    }
    const auto expected = [&](const hem::SessionSnapshot& s) {
        juce::AudioBuffer<float> sum(2, frames);
        sum.clear();
        const bool anySolo = std::any_of(s.tracks.begin(), s.tracks.end(), [](const auto& t) { return t.solo; });
        for (size_t t = 0; t < tracks.size(); ++t) {
            if (s.tracks[t].muted || (anySolo && !s.tracks[t].solo)) continue;
            auto audio = tracks[t];
            audio.applyGain(hem::gain(s.inputDb));
            for (int channel = 0; channel < 2; ++channel)
                sum.addFrom(channel, 0, audio, channel, 0, audio.getNumSamples(), hem::gain(s.tracks[t].gainDb));
        }
        sum.applyGain(hem::gain(s.outputDb));
        return baselineReference(sum, options, frames + tail);
    };
    const auto all = exportAudio(snapshot, fixtures.file("sum.wav"), options, frames + tail);
    compare(all, expected(snapshot), 1.e-7f, "13 stereo stems, unequal lengths, input/track/output gain");
    require(all.getSample(0, 2000) > 0 && all.getSample(1, 2000) < 0, "Stereo channels lost independence");
    require(all.getMagnitude(0, frames + 200, 400) < 1.e-7f, "Loop flag leaked audio into tail");
    snapshot.tracks[0].muted = true;
    compare(exportAudio(snapshot, fixtures.file("mute.wav"), options, frames + tail), expected(snapshot), 1.e-7f, "Mute");
    snapshot.tracks[2].solo = true;
    snapshot.tracks[7].solo = true;
    snapshot.tracks[7].muted = true;
    compare(exportAudio(snapshot, fixtures.file("solo.wav"), options, frames + tail), expected(snapshot), 1.e-7f, "Solo and mute precedence");
    for (auto& t : snapshot.tracks) t.muted = true;
    const auto silence = exportAudio(snapshot, fixtures.file("silence.wav"), options, frames + tail);
    require(silence.getMagnitude(0, frames + tail) == 0, "All-muted export is not silent");
    std::cout << "PASS: 13 stereo stems, exact longest-source/tail length, all gains, mute/solo, zero start, loop ignored\n";
}

void checkLatencyAndBaseline(Fixtures& fixtures) {
    for (double rate : {44100., 48000., 96000.}) {
        hem::OfflineMixOptions options;
        options.sampleRate = rate;
        options.blockSize = 127;
        options.tailSeconds = 128. / rate;
        constexpr int sourceFrames = 1003;
        const int outputFrames = sourceFrames + static_cast<int>(std::ceil(options.tailSeconds * rate));
        juce::AudioBuffer<float> source(2, sourceFrames);
        source.clear();
        source.setSample(0, 0, .3f);
        source.setSample(1, 0, -.15f);
        source.setSample(0, 499, 1.8f);
        source.setSample(1, 500, -.8f);
        source.setSample(0, sourceFrames - 1, .1f);
        source.setSample(1, sourceFrames - 1, -.2f);
        hem::SessionSnapshot snapshot;
        snapshot.tracks[0].source = fixtures.file("impulse-" + juce::String(rate, 0) + ".wav");
        writeWav(snapshot.tracks[0].source, source, rate);
        auto actual = exportAudio(snapshot, fixtures.file("impulse-mix-" + juce::String(rate, 0) + ".wav"), options, outputFrames);
        compare(actual, baselineReference(source, options, outputFrames), 0.f, "Exact active master parity");
        require(std::abs(actual.getSample(0, 0)) > .1f, "Start impulse lost during latency trim");
        require(actual.getMagnitude(0, sourceFrames, outputFrames - sourceFrames) > 1.e-7f, "Final impulse tail was discarded");
        std::cout << "PASS: " << rate << " Hz exact baseline impulse/overload parity, actual latency alignment and final-sample tail\n";
    }
    // A single-frame source is shorter than both the chain and limiter delay.
    hem::SessionSnapshot shortSnapshot;
    shortSnapshot.tracks[0].source = fixtures.file("one-frame.wav");
    auto single = constantAudio(1, .2f, -.1f);
    writeWav(shortSnapshot.tracks[0].source, single, 48000);
    hem::OfflineMixOptions tiny;
    tiny.blockSize = 1;
    tiny.tailSeconds = 0;
    compare(exportAudio(shortSnapshot, fixtures.file("one-frame-mix.wav"), tiny, 1), baselineReference(single, tiny, 1), 0.f,
            "One-sample blocks, zero tail, source shorter than latency");
    auto isolatedImpulse = constantAudio(4096, 0, 0);
    isolatedImpulse.setSample(0, 2000, .1f);
    isolatedImpulse.setSample(1, 2000, -.2f);
    writeWav(fixtures.file("isolated-impulse.wav"), isolatedImpulse, 48000);
    shortSnapshot.tracks[0].source = fixtures.file("isolated-impulse.wav");
    tiny.blockSize = 113;
    const auto aligned = exportAudio(shortSnapshot, fixtures.file("isolated-impulse-mix.wav"), tiny, 4096);
    for (int channel = 0; channel < 2; ++channel) {
        int peak = 0;
        for (int i = 1; i < aligned.getNumSamples(); ++i)
            if (std::abs(aligned.getSample(channel, i)) > std::abs(aligned.getSample(channel, peak))) peak = i;
        require(peak == 2000, "Latency compensation did not place impulse on exact source frame 2000");
    }
    std::cout << "PASS: one-frame source, one-sample blocks, zero-tail exact output length\n";
}

void checkInsertState(Fixtures& fixtures) {
    hem::OfflineMixOptions options;
    options.tailSeconds = 0;
    constexpr int frames = 4096;
    hem::SessionSnapshot snapshot;
    snapshot.tracks[0].source = fixtures.file("insert-source.wav");
    writeWav(snapshot.tracks[0].source, constantAudio(frames, .1f, -.04f), 48000);
    const auto dry = exportAudio(snapshot, fixtures.file("insert-dry.wav"), options, frames);
    for (auto& slot : snapshot.tracks[0].slots) {
        install(slot, "sipa");
        setParameter(slot, "output", -1.0);
    }
    const auto wet = exportAudio(snapshot, fixtures.file("insert-wet.wav"), options, frames);
    const float expectedGain = hem::gain(-8.f);
    require(std::abs(wet.getSample(0, 3000) / dry.getSample(0, 3000) - expectedGain) < 1.e-5f,
            "Eight restored inserts did not process in order");
    snapshot.tracks[0].slots[0].bypassed = true;
    snapshot.tracks[0].slots[7].bypassed = true;
    const auto bypass = exportAudio(snapshot, fixtures.file("insert-bypass.wav"), options, frames);
    require(std::abs(bypass.getSample(0, 3000) / dry.getSample(0, 3000) - hem::gain(-6.f)) < 1.e-5f,
            "Saved bypass states were not installed immediately");
    for (auto& slot : snapshot.tracks[0].slots) slot.bypassed = true;
    compare(exportAudio(snapshot, fixtures.file("insert-all-bypass.wav"), options, frames), dry, 0.f,
            "All-bypass maintains fixed latency with no initial fade");
    // Each registered non-monitor processor stages through the actual chain.
    // Bypassing tests its compensated dry path without expensive output claims.
    size_t registryIndex=0;
    for (const auto& entry:hem::PluginRegistry::entries()) {
        if(entry.monitoringOnly||!entry.realtimeInsert)continue;
        auto& slot = snapshot.tracks[1+registryIndex%12].slots[(registryIndex/12)%8];
        ++registryIndex;
        install(slot, entry.id);
        slot.bypassed = true;
    }
    compare(exportAudio(snapshot, fixtures.file("registry-bypass.wav"), options, frames), dry, 0.f,
            "Registry stages every non-monitor processor");
    // Monitor settings are deliberately not validated, copied into processors,
    // or rendered. Even extreme/malformed monitor state cannot alter a mix.
    snapshot.monitorParameters = juce::ValueTree("IGNORED_MONITOR_CALIBRATION");
    snapshot.monitorParameters.setProperty("gain", std::numeric_limits<double>::infinity(), nullptr);
    compare(exportAudio(snapshot, fixtures.file("monitor-excluded.wav"), options, frames), dry, 0.f,
            "Monitor EQ/calibration exclusion");
    hem::SessionSnapshot ordered;
    ordered.tracks[0].source = snapshot.tracks[0].source;
    install(ordered.tracks[0].slots[0], "sipa");
    install(ordered.tracks[0].slots[7], "sipa");
    setParameter(ordered.tracks[0].slots[0], "invertL", 1);
    setParameter(ordered.tracks[0].slots[7], "swap", 1);
    const auto orderedAudio = exportAudio(ordered, fixtures.file("insert-order.wav"), options, frames);
    require(std::abs(orderedAudio.getSample(0, 3000) - dry.getSample(1, 3000)) < 1.e-6f
            && std::abs(orderedAudio.getSample(1, 3000) + dry.getSample(0, 3000)) < 1.e-6f,
            "Noncommuting invert/swap inserts were reordered");
    std::cout << "PASS: eight restored insert parameters, saved bypass, all eligible registered insert types staged, monitor-state isolation\n";
}

void checkRateConversion(Fixtures& fixtures) {
    hem::OfflineMixOptions options;
    options.blockSize = 113;
    options.tailSeconds = 0;
    constexpr int outputFrames = 9600;
    for (double rate : {24000., 44100., 96000.}) {
        const int sourceFrames = static_cast<int>(rate / 5);
        auto audio = constantAudio(sourceFrames, .08f, 0.f, 1);
        for (int i = 0; i < sourceFrames; ++i)
            audio.setSample(0, i, .08f * std::sin(static_cast<float>(2 * juce::MathConstants<double>::pi * 997 * i / rate)));
        hem::SessionSnapshot snapshot;
        snapshot.tracks[0].source = fixtures.file("rate-" + juce::String(rate, 0) + ".wav");
        writeWav(snapshot.tracks[0].source, audio, rate, false);
        const auto actual = exportAudio(snapshot, fixtures.file("converted-" + juce::String(rate, 0) + ".wav"), options, outputFrames);
        int crossings = 0;
        double square = 0;
        for (int i = 1001; i < 8500; ++i) {
            require(actual.getSample(0, i) == actual.getSample(1, i), "Mono was not duplicated to stereo");
            if (actual.getSample(0, i - 1) < 0 && actual.getSample(0, i) >= 0) ++crossings;
            square += actual.getSample(0, i) * actual.getSample(0, i);
        }
        require(crossings >= 155 && crossings <= 157, "Source rate was relabelled instead of resampled");
        require(std::sqrt(square / 7499.) > .05 && std::sqrt(square / 7499.) < .061,
                "Resampled tone level is invalid");
    }
    // Fractional duration rounds upward rather than dropping its last frame.
    hem::SessionSnapshot fractional;
    fractional.tracks[0].source = fixtures.file("fractional.wav");
    writeWav(fractional.tracks[0].source, constantAudio(101, .1f, -.02f), 44100);
    options.tailSeconds = .00001;
    const auto expected = static_cast<juce::int64>(std::ceil(101.L * 48000 / 44100)) + 1;
    exportAudio(fractional, fixtures.file("fractional-mix.wav"), options, expected);
    // Three different source rates are loaded at once. Duration follows the
    // longest source even when that track is muted or excluded by solo.
    hem::SessionSnapshot mixed;
    const std::array<double, 3> rates{24000., 44100., 96000.};
    const std::array<int, 3> lengths{101, 200, 400};
    for (size_t i = 0; i < rates.size(); ++i) {
        mixed.tracks[i].source = fixtures.file("mixed-rate-" + juce::String(static_cast<int>(i)) + ".wav");
        writeWav(mixed.tracks[i].source, constantAudio(lengths[i], .01f, -.02f), rates[i]);
    }
    mixed.tracks[0].solo = true;
    mixed.tracks[1].muted = true;
    exportAudio(mixed, fixtures.file("mixed-rates-mix.wav"), options,
                static_cast<juce::int64>(std::ceil(200.L * 48000 / 44100)) + 1);
    std::cout << "PASS: synchronous 24/44.1/96 kHz resampling to 48 kHz, PCM decoding, mono-to-stereo, fractional frame rounding\n";
}

void checkFailureAndTransaction(Fixtures& fixtures) {
    hem::OfflineMixOptions options;
    options.tailSeconds = .001;
    hem::SessionSnapshot snapshot;
    snapshot.tracks[0].source = fixtures.file("failure-source.wav");
    writeWav(snapshot.tracks[0].source, constantAudio(2000, .03f, -.01f), 48000);
    failure({}, fixtures.file("empty.wav"), options, "Empty session");
    auto bad = snapshot;
    bad.tracks[0].source = fixtures.file("does-not-exist.wav");
    failure(bad, fixtures.file("missing.wav"), options, "Missing source");
    const auto corrupt = fixtures.file("not-a-wav.wav");
    require(corrupt.replaceWithText("This is not a WAV"), "Cannot create corrupt fixture");
    bad.tracks[0].source = corrupt;
    failure(bad, fixtures.file("corrupt-result.wav"), options, "Corrupt source");
    bad.tracks[0].source = fixtures.file("empty-source.wav");
    writeWav(bad.tracks[0].source, constantAudio(0, 0, 0), 48000);
    failure(bad, fixtures.file("empty-source-result.wav"), options, "Zero-frame source");
    juce::AudioBuffer<float> multichannel(3, 32);
    multichannel.clear();
    bad.tracks[0].source = fixtures.file("three-channel-source.wav");
    writeWav(bad.tracks[0].source, multichannel, 48000);
    failure(bad, fixtures.file("multichannel-result.wav"), options, "More-than-stereo source");
    bad = snapshot;
    bad.inputDb = std::numeric_limits<float>::quiet_NaN();
    failure(bad, fixtures.file("nan-gain.wav"), options, "Non-finite gain");
    bad = snapshot;
    bad.outputDb = 7;
    failure(bad, fixtures.file("large-gain.wav"), options, "Out-of-range gain");
    bad = snapshot;
    bad.tracks[12].gainDb = -61;
    failure(bad, fixtures.file("empty-track-gain.wav"), options, "Invalid unloaded-track gain");
    for (double rate : {0., 7999., 48000.5, 384001., std::numeric_limits<double>::infinity()}) {
        auto invalid = options;
        invalid.sampleRate = rate;
        failure(snapshot, fixtures.file("invalid-rate.wav"), invalid, "Invalid sample rate");
    }
    for (int block : {0, -1, 8193}) {
        auto invalid = options;
        invalid.blockSize = block;
        failure(snapshot, fixtures.file("invalid-block.wav"), invalid, "Invalid block size");
    }
    for (double tail : {-1., 3601., std::numeric_limits<double>::quiet_NaN()}) {
        auto invalid = options;
        invalid.tailSeconds = tail;
        failure(snapshot, fixtures.file("invalid-tail.wav"), invalid, "Invalid tail");
    }
    bad = snapshot;
    bad.tracks[12].slots[7].pluginId = "unknown-plugin";
    bad.tracks[12].slots[7].bypassed = true;
    failure(bad, fixtures.file("unknown-plugin.wav"), options, "Unknown bypassed insert on empty track");
    install(bad.tracks[12].slots[7], "monitor-eq");
    failure(bad, fixtures.file("monitor-insert.wav"), options, "Prohibited monitor insert");
    bad = snapshot;
    install(bad.tracks[0].slots[0], "sipa");
    setParameter(bad.tracks[0].slots[0], "width", std::numeric_limits<double>::infinity());
    failure(bad, fixtures.file("bad-parameter.wav"), options, "Non-finite insert parameter");
    bad = snapshot;
    install(bad.tracks[0].slots[0], "sipa");
    bad.tracks[0].slots[0].parameters.removeChild(0, nullptr);
    failure(bad, fixtures.file("missing-parameter.wav"), options, "Missing insert parameter");
    bad = snapshot;
    juce::ValueTree value("PARAM");
    bad.tracks[0].slots[0].parameters = juce::ValueTree("PARAMETERS");
    bad.tracks[0].slots[0].parameters.addChild(value, -1, nullptr);
    failure(bad, fixtures.file("empty-slot-parameter.wav"), options, "Parameters on empty slot");

    auto nonfinite = constantAudio(2000, .02f, .01f);
    nonfinite.setSample(1, 1000, std::numeric_limits<float>::quiet_NaN());
    bad = snapshot;
    bad.tracks[0].source = fixtures.file("nonfinite-source.wav");
    writeWav(bad.tracks[0].source, nonfinite, 48000);
    failure(bad, fixtures.file("nonfinite-result.wav"), options, "Non-finite source samples");

    failure(snapshot, fixtures.file("cancel-before.wav"), options, "Cancellation before render", [] { return true; });
    failure(snapshot, fixtures.file("cancel-callback-throws.wav"), options, "Throwing cancellation callback",
            []() -> bool { throw std::runtime_error("Cancellation callback failed"); });
    double progress = 0;
    const auto cancelledFile = fixtures.file("cancel-midway.wav");
    const int before = fixtures.directory.getNumberOfChildFiles(juce::File::findFilesAndDirectories);
    const auto cancelled = hem::OfflineMixRenderer::render(snapshot, cancelledFile, options,
        [&] { return progress > .15; }, [&](double p) { progress = p; });
    require(cancelled.status.failed() && cancelled.frames == 0 && !cancelledFile.exists(), "Mid-render cancel published a file");
    require(fixtures.directory.getNumberOfChildFiles(juce::File::findFilesAndDirectories) == before,
            "Mid-render cancellation leaked a temporary file");

    // Cancellation after every audio frame is written must still prevent commit.
    progress = 0;
    const auto lateFile = fixtures.file("cancel-after-write.wav");
    const auto late = hem::OfflineMixRenderer::render(snapshot, lateFile, options,
        [&] { return progress >= .989; }, [&](double p) { progress = p; });
    require(late.status.failed() && !lateFile.exists(), "Late cancellation published a final file");

    const auto existing = fixtures.file("existing.wav");
    require(existing.replaceWithText("precious existing bytes"), "Cannot create protected destination");
    const auto existsResult = hem::OfflineMixRenderer::render(snapshot, existing, options);
    require(existsResult.status.failed() && existing.loadFileAsString() == "precious existing bytes",
            "Existing destination was overwritten");

    // A destination created after validation is protected by the atomic
    // no-replace publication, rather than only an early exists() check.
    const auto raced = fixtures.file("publication-race.wav");
    bool created = false;
    const auto racedResult = hem::OfflineMixRenderer::render(snapshot, raced, options, {}, [&](double p) {
        if (p > 0 && !created) { created = true; require(raced.replaceWithText("concurrent writer"), "Cannot create race fixture"); }
    });
    require(created && racedResult.status.failed() && raced.loadFileAsString() == "concurrent writer",
            "Publication race replaced a concurrent file");

    // A dangling symlink is an existing destination too.
    const auto symlink = fixtures.file("dangling.wav");
    std::error_code linkError;
    std::filesystem::create_symlink(std::filesystem::u8path(fixtures.file("absent-target.wav").getFullPathName().toStdString()),
                                   std::filesystem::u8path(symlink.getFullPathName().toStdString()), linkError);
    if (!linkError) {
        const auto symlinkResult = hem::OfflineMixRenderer::render(snapshot, symlink, options);
        require(symlinkResult.status.failed() && symlink.isSymbolicLink(), "Dangling symlink destination changed");
    }

    // The RIFF check is arithmetic and runs before opening the writer. This
    // large requested tail at 384 kHz exceeds the bounded 32-bit data capacity.
    auto tooLarge = options;
    tooLarge.sampleRate = 384000;
    tooLarge.tailSeconds = 3600;
    failure(snapshot, fixtures.file("riff-overflow.wav"), tooLarge, "Classic RIFF capacity overflow");
    // Progress callbacks are observational and cannot sabotage a valid commit.
    const auto progressThrows = fixtures.file("throwing-progress.wav");
    const auto observed = hem::OfflineMixRenderer::render(snapshot, progressThrows, options, {},
        [](double) { throw std::runtime_error("UI callback unavailable"); });
    require(observed.status.wasOk() && progressThrows.existsAsFile(), "Progress callback invalidated export");
    std::cout << "PASS: invalid config/source/insert rejection, no-file cancellation, existing bytes and race protection, RIFF guard, cleanup\n";
}
} // namespace

int main() {
    juce::ScopedJuceInitialiser_GUI runtime;
    try {
        Fixtures fixtures;
        checkBasicMix(fixtures);
        checkLatencyAndBaseline(fixtures);
        checkInsertState(fixtures);
        checkRateConversion(fixtures);
        checkFailureAndTransaction(fixtures);
        std::cout << "PASS: offline rendering uses no physical audio device, live mic, monitor EQ or calibration.\n"
                     "NOTE: active master parity is not reconstructed true-peak certification.\n";
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << "FAIL: " << exception.what() << '\n';
        return 1;
    }
}
