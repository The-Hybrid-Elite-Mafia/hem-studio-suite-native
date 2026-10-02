#include <JuceHeader.h>
#include "../Source/Plugins/ExternalPluginHost.h"
#include "../Source/InsertChain.h"
#include <array>
#include <bit>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace {
void require(bool condition, const juce::String& text) {
    if (!condition) throw std::runtime_error(text.toStdString());
}
void close(float actual, float expected, const char* text) {
    require(std::abs(actual - expected) < 2.0e-5f, juce::String(text) + ": " + juce::String(actual, 8));
}
constexpr double rate = 48000;
constexpr int block = 128, budget = 288;

// Deliberately knows only the subsequently compiled in-house fixture's state
// contract. It is NOT an attempted decoder/validator for arbitrary VST3 state.
hem::ExternalPluginState fixtureState(const juce::PluginDescription& description, float gain, uint32_t mode) {
    std::array<uint8_t, 24> bytes{{'H','E','M','F','X','0','0','1'}};
    const auto put = [&bytes](size_t offset, uint32_t value) {
        for (unsigned i = 0; i < 4; ++i) bytes[offset + i] = static_cast<uint8_t>(value >> (8 * i));
    };
    put(8, std::bit_cast<uint32_t>(gain)); put(12, mode);
    uint32_t checksum = 2166136261u;
    for (int i = 0; i < 20; ++i) checksum = (checksum ^ bytes[size_t(i)]) * 16777619u;
    put(20, checksum);
    juce::XmlElement xml("VST3PluginState");
    xml.createNewChildElement("IComponent")->addTextElement(juce::MemoryBlock(bytes.data(), bytes.size()).toBase64Encoding());
    hem::ExternalPluginState state;
    state.descriptionXml = description.createXml()->toString();
    juce::AudioProcessor::copyXmlToBinary(xml, state.state);
    return state;
}

std::unique_ptr<hem::ModuleProcessor> stage(hem::ExternalPluginHost& host,
                                          const hem::ExternalPluginState& state, bool offline = false) {
    juce::String error;
    auto result = host.stage(state, rate, block, budget, offline, error);
    require(result != nullptr, "Fixture stage failed: " + error);
    require(result->isExternal() && result->processingReady(), "Staged fixture is not a ready external processor");
    return result;
}

void processConstant(hem::ModuleProcessor& processor, float left, float right, int blocks = 3) {
    juce::AudioBuffer<float> audio(2, block);
    juce::MidiBuffer midi;
    for (int n = 0; n < blocks; ++n) {
        for (int i = 0; i < block; ++i) {
            audio.setSample(0, i, left); audio.setSample(1, i, right);
        }
        processor.processBlock(audio, midi);
    }
    require(processor.processingReady(), "Fixture became unavailable during normal processing");
}

void requireGain(hem::ModuleProcessor& processor, float gain) {
    juce::AudioBuffer<float> audio(2, block);
    juce::MidiBuffer midi;
    for (int n = 0; n < 3; ++n) {
        for (int i = 0; i < block; ++i) {
            audio.setSample(0, i, .2f); audio.setSample(1, i, -.3f);
        }
        processor.processBlock(audio, midi);
    }
    require(processor.processingReady(), "Fixture unexpectedly faulted");
    close(audio.getSample(0, block - 1), .2f * gain, "Left fixture gain");
    close(audio.getSample(1, block - 1), -.3f * gain, "Right fixture gain");
}

std::unique_ptr<juce::AudioProcessorEditor> openEditor(hem::ModuleProcessor& processor) {
    std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditor());
    require(editor != nullptr && editor->getNumChildComponents() == 1,
            "External editor must contain exactly one native or generic child");
    require(editor->getAudioProcessor() != &processor,
            "External editor must retain/bind the actual instance rather than an empty adapter APVTS");
    return editor;
}

void requireImpulse(hem::ExternalPluginHost& host, const hem::ExternalPluginState& state,
                    bool bypassed, bool faulted, bool sameContextRestart = false) {
    hem::InsertChain chain;
    chain.prepare(rate, block);
    auto processor = stage(host, state);
    require(processor->getLatencySamples() == 64, "Fixture did not restore 64-sample latency");
    auto* pointer = processor.get();
    chain.installInitial(0, chain.stageNode(std::move(processor), rate, block, budget), bypassed);
    require(pointer->processingReady(), "First staged adapter validation must remain ready");
    if (sameContextRestart) {
        // Leave nonzero samples in the actual fixture's 64-sample delay before
        // resetting the host compensation at the identical device context.
        std::array<float, block> historyLeft{}, historyRight{};
        historyLeft.fill(.37f); historyRight.fill(-.29f);
        chain.process(historyLeft.data(), historyRight.data(), block);
        const auto priorRevision = pointer->externalStateRevision();
        chain.release();
        require(!pointer->processingReady(), "Release did not immediately quarantine stale VST3 history");
        chain.prepare(rate, block);
        require(!pointer->processingReady() && pointer->getLatencySamples() == 64
                && pointer->externalStateRevision() > priorRevision
                && pointer->externalDiagnostic().contains("Reload VST3"),
                "Same-context device restart incorrectly reused an external instance");
    }
    if (faulted) pointer->prepareToPlay(rate + 1, block);
    const int expectedPosition = budget * hem::InsertChain::slotCount;
    const int total = expectedPosition + 2 * block;
    std::vector<float> left(size_t(total), 0), right(size_t(total), 0);
    left[0] = .8f; right[0] = -.4f;
    chain.process(left.data(), right.data(), total);
    const float gain = bypassed || faulted || sameContextRestart ? 1.0f : .5f;
    for (int i = 0; i < total; ++i) {
        close(left[size_t(i)], i == expectedPosition ? .8f * gain : 0, "Compensated left impulse");
        close(right[size_t(i)], i == expectedPosition ? -.4f * gain : 0, "Compensated right impulse");
    }
    require(chain.latencySamples() == expectedPosition, "External insert changed the eight-slot fixed delay");
}

class MidBlockFault final : public hem::ModuleProcessor {
public:
    MidBlockFault() : ModuleProcessor("HOST_FAULT_TEST", "Host post-process readiness test", {}) {}
    bool isExternal() const noexcept override {return true;}
    bool processingReady() const noexcept override {return ready;}
    void prepareToPlay(double, int) override {setLatencySamples(0);}
    void processBlock(juce::AudioBuffer<float>& audio, juce::MidiBuffer&) override {
        for (int ch = 0; ch < audio.getNumChannels(); ++ch) audio.applyGain(ch, 0, audio.getNumSamples(), 100.0f);
        ready = false;
    }
    using ModuleProcessor::processBlock;
    juce::AudioProcessorEditor* createEditor() override {return nullptr;}
    float meter() const noexcept override {return 0;}
    juce::String meterLabel() const override {return "TEST";}
private: bool ready = true;
};
} // namespace

int main(int argc, char** argv) {
    juce::ScopedJuceInitialiser_GUI init;
    try {
        require(argc == 2, "Usage: hem_external_host_tests <explicit in-house HEM VST3 Fixture.vst3 path>");
        const auto fixture = juce::File::getCurrentWorkingDirectory().getChildFile(argv[1]);
        require(fixture.getFileName() == "HEM VST3 Fixture.vst3", "Only the explicitly supplied in-house fixture may be tested");
        hem::ExternalPluginHost host;
        juce::String error;
        const auto found = host.discoverSelected(fixture, error);
        require(found.size() == 1, "Selected-file discovery failed: " + error);
        const auto description = found.getFirst();
        require(description.name == "HEM VST3 Fixture" && description.pluginFormatName == "VST3"
                && !description.isInstrument, "Fixture descriptor mismatch");
        auto saved = fixtureState(description, .5f, 1);
        require(saved.validate(error), "Valid fixture container rejected: " + error);

        // Message-only API failures cannot instantiate or mutate an old node.
        bool offThreadRejected = false;
        std::thread invalidCaller([&] {
            juce::String threadError;
            offThreadRejected = host.stage(saved, rate, block, budget, false, threadError) == nullptr
                && threadError.contains("message thread");
        });
        invalidCaller.join();
        require(offThreadRejected && host.retainedInstanceCount() == 0, "Worker performed VST3 creation");

        auto live = stage(host, saved);
        require(live->getLatencySamples() == 64, "Saved fixture latency not restored");
        requireGain(*live, .5f);
        const auto meter = live->meterSnapshot();
        require(meter.hasProcessedAudio && meter.valueAvailable, "External sample-peak telemetry missing");
        close(meter.value, .15f, "External meter must measure actual output sample peak");
        juce::MemoryBlock notApvts;
        live->getStateInformation(notApvts);
        require(notApvts.getSize() == 0, "Ungated external state capture leaked the empty adapter APVTS");
        hem::ExternalPluginState captured;
        require(!hem::ExternalPluginHost::capture(*live, captured, error), "Ungated VST3 capture was accepted");
        require(!live->gateForStateCapture(true) && !live->processingReady(), "Capture gate failed");
        require(hem::ExternalPluginHost::capture(*live, captured, error), "Gated state capture failed: " + error);
        require(captured.state.getSize() != 0 && captured.descriptionXml == saved.descriptionXml,
                "Opaque capture lost descriptor or vendor state");
        require(live->gateForStateCapture(false) && live->processingReady(), "Gate restoration failed");
        auto offline = stage(host, captured, true);
        require(offline->externalDescriptionXml() == live->externalDescriptionXml()
                && offline->externalStateRevision() != live->externalStateRevision()
                && offline->externalStateRevision() != 0 && live->externalStateRevision() != 0,
                "Separate same-descriptor instances reused a dirty-state revision token");
        requireGain(*offline, .5f);
        offline->setNonRealtime(true);
        require(offline->processingReady(), "Matching offline mode unexpectedly invalidated fixture");

        // Opaque vendor restore has no universal success status. Our fixture's
        // known corrupt checksum is refused, leaving its fresh default gain.
        auto corrupt = fixtureState(description, .5f, 0);
        auto xml = juce::AudioProcessor::getXmlFromBinary(corrupt.state.getData(), int(corrupt.state.getSize()));
        auto* component = xml->getChildByName("IComponent");
        juce::MemoryBlock bytes;
        require(bytes.fromBase64Encoding(component->getAllSubText()), "Fixture component framing invalid");
        static_cast<uint8_t*>(bytes.getData())[20] ^= 1;
        component->deleteAllTextElements(); component->addTextElement(bytes.toBase64Encoding());
        juce::AudioProcessor::copyXmlToBinary(*xml, corrupt.state);
        auto refusedVendorState = stage(host, corrupt);
        requireGain(*refusedVendorState, 1.0f);

        const auto retainedBeforeFailure = host.retainedInstanceCount();
        auto excessive = fixtureState(description, .5f, 2);
        require(!host.stage(excessive, rate, block, budget, false, error) && error.contains("latency"),
                "Over-budget VST3 latency accepted");
        require(host.retainedInstanceCount() == retainedBeforeFailure && live->processingReady(),
                "Failed stage mutated an old instance or retained an unusable one");
        auto missing = saved;
        auto missingXml = juce::XmlDocument::parse(saved.descriptionXml);
        missingXml->setAttribute("file", fixture.getParentDirectory().getChildFile("Missing-Owned-Fixture.vst3").getFullPathName());
        missing.descriptionXml = missingXml->toString();
        require(missing.validate(error), "Missing binary must remain representable in saved metadata");
        require(!host.stage(missing, rate, block, budget, false, error) && error.contains("missing"),
                "Missing VST3 was not rejected before loading");
        require(!host.stage(saved, rate, block, budget + 1, false, error), "Non-6-ms slot budget accepted");

        auto editor = openEditor(*live);
        require(live->createEditor() == nullptr, "A second owned editor was allowed");
        auto* actual = editor->getAudioProcessor();
        auto* child = editor->getChildComponent(0);
        editor->setSize(600, 350);
        require(child->getBounds() == editor->getLocalBounds(), "Host-initiated editor resize not propagated");
        child->setSize(530, 310);
        require(editor->getWidth() == 530 && editor->getHeight() == 310, "Plugin-initiated editor resize not propagated");
        const auto oldRevision = live->externalStateRevision();
        bool gainFound = false;
        for (auto* parameter : actual->getParameters())
            if (parameter->getName(128).containsIgnoreCase("fixture gain")) {
                parameter->setValueNotifyingHost(.375f); gainFound = true; break;
            }
        require(gainFound && live->externalStateRevision() > oldRevision,
                "Actual hosted parameter callback did not update dirtiness revision");
        requireGain(*live, .75f);
        editor.reset();
        editor = openEditor(*live); // Closing and reopening cannot orphan activeEditor.
        actual = editor->getAudioProcessor();
        const int immutableLatency = live->getLatencySamples();
        // Exercise the actual instance's synchronous latency notification; no
        // inspection of a concurrently changing non-atomic JUCE latency field.
        actual->setLatencySamples(immutableLatency + 1);
        require(!live->processingReady() && live->getLatencySamples() == immutableLatency
                && live->externalDiagnostic().contains("latency"), "Dynamic latency was not safely faulted");
        live->gateForStateCapture(true);
        require(hem::ExternalPluginHost::capture(*live, captured, error), "Faulted instance state could not be preserved");
        live->gateForStateCapture(false);
        require(!live->processingReady(), "Removing temporary capture gate cleared a permanent fault");
        editor.reset();
        auto reloaded = stage(host, captured);
        requireGain(*reloaded, .75f);
        require(reloaded->getLatencySamples() == 64, "Reload retained a false mutable wrapper latency");
        reloaded->prepareToPlay(44100, block);
        require(!reloaded->processingReady() && reloaded->getLatencySamples() == 64
                && reloaded->externalDiagnostic().contains("sample rate"), "Device context change did not fail closed");
        reloaded->prepareToPlay(rate, block);
        require(!reloaded->processingReady(), "A worker prepare incorrectly cleared a reload-required fault");

        requireImpulse(host, saved, false, false);
        requireImpulse(host, saved, true, false);
        requireImpulse(host, saved, false, true);
        requireImpulse(host, saved, false, false, true);
        {
            auto repeatedPrepare = stage(host, saved);
            repeatedPrepare->prepareToPlay(rate, block);
            require(repeatedPrepare->processingReady(), "Initial same-context validation was rejected");
            repeatedPrepare->prepareToPlay(rate, block);
            require(!repeatedPrepare->processingReady()
                    && repeatedPrepare->externalDiagnostic().contains("Reload VST3"),
                    "Repeated same-context prepare without release reused stale VST3 history");
        }
        {
            hem::InsertChain chain;
            chain.prepare(rate, block);
            chain.installInitial(0, chain.stageNode(std::make_unique<MidBlockFault>(), rate, block, budget), false);
            std::vector<float> left(size_t(budget * 8 + block), 0), right(left.size(), 0);
            left[0] = .4f; right[0] = -.2f;
            chain.process(left.data(), right.data(), int(left.size()));
            for (size_t i = 0; i < left.size(); ++i) {
                close(left[i], i == size_t(budget * 8) ? .4f : 0, "Post-process fault must choose aligned dry");
                close(right[i], i == size_t(budget * 8) ? -.2f : 0, "Post-process fault right aligned dry");
            }
        }

        live.reset(); offline.reset(); refusedVendorState.reset(); reloaded.reset();
        host.collectGarbage();
        require(host.retainedInstanceCount() == 0, "Main-thread collection failed after adapters were retired");
        auto workerOwned = stage(host, saved, true);
        std::exception_ptr workerFailure;
        std::thread worker([processor = std::move(workerOwned), &workerFailure]() mutable {
            try {
                processor->prepareToPlay(rate, block); // Validation only, never actual VST3 prepare.
                processConstant(*processor, .1f, .2f);
                processor.reset(); // Does not run the real VST3 destructor on this worker.
            } catch (...) {workerFailure = std::current_exception();}
        });
        worker.join();
        if (workerFailure) std::rethrow_exception(workerFailure);
        require(host.retainedInstanceCount() == 1, "Worker destroyed the host-retained actual instance");
        host.collectGarbage();
        require(host.retainedInstanceCount() == 0, "Joined worker owner was not collected on message thread");
        auto editorOwned = stage(host, saved);
        auto heldEditor = openEditor(*editorOwned);
        editorOwned.reset();
        host.collectGarbage();
        require(host.retainedInstanceCount() == 1, "Editor did not keep the actual VST3 alive");
        heldEditor.reset(); host.collectGarbage();
        require(host.retainedInstanceCount() == 0, "Editor-close final ownership did not collect");
        std::cout << "PASS: explicit in-house VST3 discovery; stereo gain; opaque capture/reload; 64-sample latency and fixed aligned bypass; 6 ms rejection; callback dirtiness; editor reopen/resize/lifetime; worker destruction/main collection; context/latency faults; same-context release/reprepare quarantine; post-process dry fallback.\n";
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << "FAIL: " << exception.what() << '\n'; return 1;
    }
}
