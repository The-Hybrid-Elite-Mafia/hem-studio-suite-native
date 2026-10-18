#include "ExternalPluginHost.h"
#include <algorithm>
#include <cmath>
#if JUCE_PLUGINHOST_VST3
#include <pluginterfaces/vst/ivstcomponent.h>
#include <pluginterfaces/vst/ivstaudioprocessor.h>
#endif

namespace hem {
namespace {
bool onMessageThread() noexcept {
    const auto* manager = juce::MessageManager::getInstanceWithoutCreating();
    return manager != nullptr && manager->isThisTheMessageThread();
}

bool requireMessageThread(juce::String& error) {
    if (onMessageThread()) return true;
    error = "VST3 discovery, preparation, state capture, and editor operations require the message thread.";
    return false;
}

enum class ExternalFault : int {
    none, contextChanged, restartRequired, modeChanged, latencyChanged, configurationChanged,
    invalidBuffer, nonFiniteAudio, processingException, directStateRestore
};

// A replacement/reload can have the same descriptor but different opaque
// state. Never reset its dirty token to zero or reuse another owner's token.
// Allocation is one lock-free atomic operation, also in audio-thread callbacks.
std::atomic<uint64_t> nextExternalRevision{1};
uint64_t allocateExternalRevision() noexcept {
    return nextExternalRevision.fetch_add(1, std::memory_order_relaxed);
}

#if JUCE_PLUGINHOST_VST3
// Public JUCE extension visitor + public VST3 interfaces, used only on the
// message thread before publication. Never retain the temporary VST3Client.
// The queried audio interface owns one reference; the component is borrowed
// from the still-live AudioPluginInstance.
class NativeVST3Probe final : public juce::ExtensionsVisitor {
public:
    ~NativeVST3Probe() override {if (audio != nullptr) audio->release();}
    void visitVST3Client(const VST3Client& client) override {
        component = client.getIComponentPtr();
        void* queried = nullptr;
        if (component != nullptr
            && component->queryInterface(Steinberg::Vst::IAudioProcessor_iid, &queried) == Steinberg::kResultOk)
            audio = static_cast<Steinberg::Vst::IAudioProcessor*>(queried);
    }
    bool hasAudioInterface(juce::String& error) const {
        if (component != nullptr && audio != nullptr) return true;
        error = "The VST3 does not expose a verifiable native audio-processing interface.";
        return false;
    }
    Steinberg::Vst::IComponent* component = nullptr;
    Steinberg::Vst::IAudioProcessor* audio = nullptr;
};
#endif

bool verifyNativeFloat32(juce::AudioPluginInstance& instance, juce::String& error) {
   #if JUCE_PLUGINHOST_VST3
    NativeVST3Probe probe;
    instance.getExtensions(probe);
    if (!probe.hasAudioInterface(error)) return false;
    if (probe.audio->canProcessSampleSize(Steinberg::Vst::kSample32) != Steinberg::kResultTrue) {
        error = "The VST3 does not support 32-bit float audio processing. Double-only effects are unsupported.";
        return false;
    }
    return true;
   #else
    juce::ignoreUnused(instance);
    error = "This build does not have VST3 hosting enabled.";
    return false;
   #endif
}

bool verifyNativePreparation(juce::AudioPluginInstance& instance, int budget,
                             int& latency, juce::String& error) {
   #if JUCE_PLUGINHOST_VST3
    NativeVST3Probe probe;
    instance.getExtensions(probe);
    if (!probe.hasAudioInterface(error)) return false;
    const auto nativeLatency = probe.audio->getLatencySamples(); // uint32, not JUCE's clamped int.
    if (nativeLatency > static_cast<Steinberg::uint32>(budget)) {
        error = "VST3 latency is " + juce::String(static_cast<juce::int64>(nativeLatency))
            + " samples; this slot permits at most " + juce::String(budget) + " samples (6 ms).";
        return false;
    }
    latency = static_cast<int>(nativeLatency); // Safe only after checking the small positive budget.
    if (instance.getLatencySamples() != latency) {
        error = "The VST3 latency changed during preparation. Reload VST3 to obtain a stable configuration.";
        return false;
    }
    using namespace Steinberg::Vst;
    if (probe.component->getBusCount(kAudio, kInput) != instance.getBusCount(true)
        || probe.component->getBusCount(kAudio, kOutput) != 1) {
        error = "The VST3 native bus count changed during preparation."; return false;
    }
    for (const auto direction : {kInput, kOutput}) {
        BusInfo info{};
        SpeakerArrangement arrangement{};
        if (probe.component->getBusInfo(kAudio, direction, 0, info) != Steinberg::kResultOk
            || info.mediaType != kAudio || info.direction != direction
            || info.busType != kMain || info.channelCount != 2
            || probe.audio->getBusArrangement(direction, 0, arrangement) != Steinberg::kResultOk
            || arrangement != SpeakerArr::kStereo) {
            error = "The prepared VST3 native input/output arrangement is not ordinary stereo.";
            return false;
        }
    }
    // Disabled auxiliary buses can retain their channel arrangements in VST3.
    // setBusesLayout has already verified they accept deactivation. The public
    // native interface has no getter for current bus activation state.
    return true;
   #else
    juce::ignoreUnused(instance, budget, latency);
    error = "This build does not have VST3 hosting enabled.";
    return false;
   #endif
}

bool hasStereoOnlyLayout(const juce::AudioPluginInstance& instance) {
    if (instance.getBusCount(true) < 1 || instance.getBusCount(false) != 1
        || instance.getTotalNumInputChannels() != 2 || instance.getTotalNumOutputChannels() != 2)
        return false;
    const auto layout = instance.getBusesLayout();
    if (layout.getMainInputChannelSet() != juce::AudioChannelSet::stereo()
        || layout.getMainOutputChannelSet() != juce::AudioChannelSet::stereo()) return false;
    for (int i = 1; i < layout.inputBuses.size(); ++i)
        if (!layout.inputBuses.getReference(i).isDisabled()) return false;
    return true;
}

bool configureStereoOnly(juce::AudioPluginInstance& instance, juce::String& error) {
    if (instance.getBusCount(true) < 1 || instance.isMidiEffect()) {
        error = "VST3 instruments and MIDI-only effects are unsupported; a stereo audio input is required.";
        return false;
    }
    if (instance.getBusCount(false) != 1) {
        error = "Multi-output VST3 effects are unsupported; exactly one stereo output bus is required.";
        return false;
    }
    auto layout = instance.getBusesLayout();
    layout.inputBuses.getReference(0) = juce::AudioChannelSet::stereo();
    layout.outputBuses.getReference(0) = juce::AudioChannelSet::stereo();
    for (int i = 1; i < layout.inputBuses.size(); ++i)
        layout.inputBuses.getReference(i) = juce::AudioChannelSet::disabled();
    if (!instance.setBusesLayout(layout) || !hasStereoOnlyLayout(instance)) {
        error = "The VST3 does not support stereo-in/stereo-out with every auxiliary input disabled. Mandatory sidechains are unsupported.";
        return false;
    }
    return true;
}
} // namespace

// Registered on the real instance rather than the short-lived adapter. Listener
// callbacks may run in processBlock: only lock-free atomics are touched here.
class ExternalInstanceOwner final : private juce::AudioProcessorListener {
public:
    ExternalInstanceOwner(std::unique_ptr<juce::AudioPluginInstance> p, juce::String xml,
                          double rate, int block, int latency, bool offline, double tail)
        : instance(std::move(p)), descriptionXml(std::move(xml)), preparedRate(rate),
          preparedBlock(block), preparedLatency(latency), preparedOffline(offline), preparedTail(tail) {
        jassert(onMessageThread());
        instance->addListener(this);
    }
    ~ExternalInstanceOwner() override {
        jassert(onMessageThread());
        jassert(!editorOpen);
        instance->removeListener(this);
        instance->releaseResources();
        // JUCE's VST3 destructor itself calls callSync. Since this owner is
        // destroyed on the message thread, it cannot deadlock a worker join.
        instance.reset();
    }
    void invalidate(ExternalFault reason) noexcept {
        auto expected = ExternalFault::none;
        if (fault.compare_exchange_strong(expected, reason, std::memory_order_acq_rel))
            revision.store(allocateExternalRevision(), std::memory_order_relaxed);
    }
    bool ready() const noexcept {
        return fault.load(std::memory_order_acquire) == ExternalFault::none
            && !captureGated.load(std::memory_order_acquire);
    }
    juce::String diagnostic() const {
        switch (fault.load(std::memory_order_acquire)) {
            case ExternalFault::none:
                return captureGated.load(std::memory_order_acquire)
                    ? "VST3 temporarily gated for state capture." : juce::String{};
            case ExternalFault::contextChanged: return "The audio sample rate or maximum block changed. Reload VST3 to resume.";
            case ExternalFault::restartRequired: return "The VST3 processing lifecycle restarted. Reload VST3 to reset its audio history.";
            case ExternalFault::modeChanged: return "The realtime/offline mode changed. Reload VST3 for this render context.";
            case ExternalFault::latencyChanged: return "The VST3 reported a latency change. Reload VST3 to revalidate alignment.";
            case ExternalFault::configurationChanged: return "The VST3 reported an I/O, program, or parameter-layout change. Reload VST3 to revalidate it.";
            case ExternalFault::invalidBuffer: return "The VST3 received an unsupported channel count or oversized block. Reload VST3.";
            case ExternalFault::nonFiniteAudio: return "The VST3 produced non-finite audio and was disabled. Reload VST3 after checking its settings.";
            case ExternalFault::processingException: return "The VST3 threw an exception while processing and was disabled. Reload VST3.";
            case ExternalFault::directStateRestore: return "VST3 state must be restored into a separately staged instance. Reload VST3.";
        }
        return "The VST3 is unavailable. Reload VST3.";
    }

    std::unique_ptr<juce::AudioPluginInstance> instance;
    const juce::String descriptionXml;
    const double preparedRate;
    const int preparedBlock, preparedLatency;
    const bool preparedOffline;
    const double preparedTail;
    std::atomic<ExternalFault> fault{ExternalFault::none};
    std::atomic<bool> captureGated{false};
    std::atomic<uint64_t> revision{allocateExternalRevision()};
    bool editorOpen = false; // Message thread only.

private:
    void audioProcessorParameterChanged(juce::AudioProcessor*, int, float) override {
        revision.store(allocateExternalRevision(), std::memory_order_relaxed);
    }
    void audioProcessorChanged(juce::AudioProcessor*, const ChangeDetails& details) override {
        revision.store(allocateExternalRevision(), std::memory_order_relaxed);
        if (details.latencyChanged) invalidate(ExternalFault::latencyChanged);
        // JUCE 8's VST3 kIoChanged restart emits parameterInfoChanged and
        // programChanged, not a dedicated I/O flag. Fail closed, including
        // metadata-only changes, rather than reading mutable bus/latency fields
        // from the processing thread. The prepared latency stays immutable.
        else if (details.parameterInfoChanged || details.programChanged)
            invalidate(ExternalFault::configurationChanged);
    }
};
static_assert(std::atomic<ExternalFault>::is_always_lock_free
              && std::atomic<bool>::is_always_lock_free
              && std::atomic<uint64_t>::is_always_lock_free);

namespace {
// This first base is destroyed last, after AudioProcessorEditor's destructor.
// Even an editor retained past adapter replacement keeps the actual instance
// alive. Normal host teardown still closes all editors before collecting owners.
struct EditorOwnerLifetime {
    explicit EditorOwnerLifetime(std::shared_ptr<ExternalInstanceOwner> value) : owner(std::move(value)) {}
    std::shared_ptr<ExternalInstanceOwner> owner;
};

class ExternalEditor final : private EditorOwnerLifetime,
                             public juce::AudioProcessorEditor,
                             private juce::ComponentListener {
   #if JUCE_LINUX || JUCE_BSD
    class EmbeddingMovementWatcher final : public juce::ComponentMovementWatcher {
    public:
        explicit EmbeddingMovementWatcher(ExternalEditor& editor)
            : juce::ComponentMovementWatcher(&editor), outer(editor) {}
        void componentMovedOrResized(bool, bool) override {outer.refreshEmbeddedBounds();}
        void componentPeerChanged() override {outer.refreshEmbeddedBounds();}
        void componentVisibilityChanged() override {outer.refreshEmbeddedBounds();}
    private:
        ExternalEditor& outer;
    };
   #endif
public:
    ExternalEditor(std::shared_ptr<ExternalInstanceOwner> held,
                   std::unique_ptr<juce::AudioProcessorEditor> content)
        : EditorOwnerLifetime(std::move(held)), juce::AudioProcessorEditor(*owner->instance),
          child(std::move(content)) {
        jassert(onMessageThread());
        owner->editorOpen = true;
        addAndMakeVisible(*child);
        child->addComponentListener(this);
        setResizable(child->isResizable(), false);
        setSize(juce::jlimit(64, 8192, child->getWidth()), juce::jlimit(64, 8192, child->getHeight()));
       #if JUCE_LINUX || JUCE_BSD
        // Observe all ancestor moves, not just resize notifications on the
        // plug-in child. JUCE's VST3PluginWindow ignores moved-only callbacks,
        // which otherwise leaves its native XEmbed window at the old origin.
        movementWatcher = std::make_unique<EmbeddingMovementWatcher>(*this);
        refreshEmbeddedBounds();
       #endif
    }
    ~ExternalEditor() override {
        jassert(onMessageThread());
        closing = true;
       #if JUCE_LINUX || JUCE_BSD
        movementWatcher.reset(); // Detach ancestor listeners before destroying the child.
       #endif
        child->removeComponentListener(this);
        owner->instance->editorBeingDeleted(child.get());
        child.reset();
        owner->editorOpen = false;
    }
    void resized() override {
        if (resizing || !child) return;
        const juce::ScopedValueSetter<bool> guard(resizing, true);
        child->setBounds(getLocalBounds());
        refreshEmbeddedBounds();
    }
private:
    void componentMovedOrResized(juce::Component& component, bool, bool wasResized) override {
        if (!wasResized || resizing || &component != child.get()) return;
        const juce::ScopedValueSetter<bool> guard(resizing, true);
        setSize(juce::jlimit(64, 8192, child->getWidth()), juce::jlimit(64, 8192, child->getHeight()));
        child->setTopLeftPosition(0, 0);
        refreshEmbeddedBounds();
    }
    void refreshEmbeddedBounds() {
       #if JUCE_LINUX || JUCE_BSD
        if (closing || syncingEmbedded || !child) return;
        jassert(onMessageThread());
        juce::Component::SafePointer<ExternalEditor> safe(this);
        syncingEmbedded = true;
        const juce::ScopeGuard resetGuard{[safe] {if (safe != nullptr) safe->syncingEmbedded = false;}};
        refreshEmbeddedSubtree(*child);
       #endif
    }
   #if JUCE_LINUX || JUCE_BSD
    static void refreshEmbeddedSubtree(juce::Component& component) {
        // Updating a native child can synchronously notify components. Never
        // retain a raw descendant across that callback boundary.
        juce::Component::SafePointer<juce::Component> safe(&component);
        if (auto* embedded = dynamic_cast<juce::XEmbedComponent*>(&component))
            embedded->updateEmbeddedBounds();
        for (int i = 0; safe != nullptr && i < safe->getNumChildComponents(); ++i)
            if (auto* descendant = safe->getChildComponent(i)) refreshEmbeddedSubtree(*descendant);
    }
    std::unique_ptr<EmbeddingMovementWatcher> movementWatcher;
    bool syncingEmbedded = false;
   #endif
    std::unique_ptr<juce::AudioProcessorEditor> child;
    bool resizing = false, closing = false;
};

class ExternalModuleProcessor final : public ModuleProcessor {
public:
    explicit ExternalModuleProcessor(std::shared_ptr<ExternalInstanceOwner> instanceOwner,
                                     const juce::String& name)
        : ModuleProcessor("EXTERNAL_VST3", name, {}), owner(std::move(instanceOwner)) {
        // This is the wrapper's latency. It is never changed in response to a
        // plugin callback; InsertChain's dry/compensation delays depend on it.
        setLatencySamples(owner->preparedLatency);
        actualRate.store(owner->preparedRate, std::memory_order_relaxed);
        juce::AudioProcessor::setNonRealtime(owner->preparedOffline);
        publishMeterPreparation();
    }
    bool isExternal() const noexcept override {return true;}
    double getTailLengthSeconds() const override {return owner->preparedTail;}
    bool processingReady() const noexcept override {return owner->ready();}
    bool gateForStateCapture(bool gated) noexcept override {
        return owner->captureGated.exchange(gated, std::memory_order_acq_rel);
    }
    uint64_t externalStateRevision() const noexcept override {
        return owner->revision.load(std::memory_order_relaxed);
    }
    juce::String externalDescriptionXml() const override {return owner->descriptionXml;}
    juce::String externalDiagnostic() const override {return owner->diagnostic();}
    void prepareToPlay(double rate, int maximumBlock) override {
        // Called from both device callbacks and offline workers. Never forward
        // to VST3 setupProcessing here, and never wait on the message thread.
        if (rate != owner->preparedRate || maximumBlock != owner->preparedBlock) {
            owner->invalidate(ExternalFault::contextChanged); return;
        }
        // The first call validates the fresh, already-prepared adapter when
        // stageNode installs it. Any later call represents another lifecycle
        // preparation, even at identical rate/block. The vendor's delay/history
        // cannot safely be reset on this arbitrary caller thread.
        if (preparationValidated.exchange(true, std::memory_order_acq_rel)) {
            owner->invalidate(ExternalFault::restartRequired); return;
        }
        if (owner->fault.load(std::memory_order_acquire) != ExternalFault::none) return;
        resetMeterTelemetry();
        outputPeak.store(0, std::memory_order_relaxed);
        publishMeterPreparation();
    }
    void releaseResources() override {
        // Actual release/destruction remains with the message-thread owner.
        // Quarantine immediately so host-delay resets cannot reuse stale DSP.
        owner->invalidate(ExternalFault::restartRequired);
    }
    void setNonRealtime(bool offline) noexcept override {
        if (offline != owner->preparedOffline) owner->invalidate(ExternalFault::modeChanged);
        juce::AudioProcessor::setNonRealtime(offline);
    }
    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override {
        if (!processingReady()) return;
        if (buffer.getNumChannels() != 2 || buffer.getNumSamples() > owner->preparedBlock) {
            owner->invalidate(ExternalFault::invalidBuffer); return;
        }
        if (buffer.getNumSamples() <= 0) return;
        juce::ScopedNoDenormals noDenormals;
        try {
            // JUCE's VST3 wrapper takes an internal process mutex. This adapter
            // does not claim arbitrary third-party code is lock/alloc-free.
            owner->instance->processBlock(buffer, midi);
        } catch (...) {
            owner->invalidate(ExternalFault::processingException); return;
        }
        if (!processingReady()) return; // Caller also checks and selects aligned dry.
        float peak = 0;
        for (int channel = 0; channel < 2; ++channel) {
            const auto* samples = buffer.getReadPointer(channel);
            for (int i = 0; i < buffer.getNumSamples(); ++i) {
                if (!std::isfinite(samples[i])) {
                    owner->invalidate(ExternalFault::nonFiniteAudio); return;
                }
                peak = juce::jmax(peak, std::abs(samples[i]));
            }
        }
        outputPeak.store(peak, std::memory_order_relaxed);
        publishMeterBlock(buffer);
    }
    using ModuleProcessor::processBlock;
    float meter() const noexcept override {return outputPeak.load(std::memory_order_relaxed);}
    juce::String meterLabel() const override {return "VST3 OUTPUT SAMPLE PEAK";}
    juce::AudioProcessorEditor* createEditor() override {
        if (!onMessageThread() || owner->editorOpen || owner->instance->getActiveEditor() != nullptr)
            return nullptr;
        bool allowNativeEditor = true;
       #if JUCE_LINUX || JUCE_BSD
        // JUCE's XEmbed native editor requires an X display. Offscreen tests
        // and headless exports can still use the genuine parameter editor.
        allowNativeEditor = juce::SystemStats::getEnvironmentVariable("DISPLAY", {}).isNotEmpty();
       #endif
        std::unique_ptr<juce::AudioProcessorEditor> child;
        if (allowNativeEditor) child.reset(owner->instance->createEditorIfNeeded());
        if (!child) child = std::make_unique<juce::GenericAudioProcessorEditor>(*owner->instance);
        return new ExternalEditor(owner, std::move(child));
    }
    void getStateInformation(juce::MemoryBlock& destination) override {
        // This override deliberately never serializes ModuleProcessor's empty
        // APVTS. Host capture provides errors; generic callers get no state if
        // they have not established the required capture contract.
        destination.reset();
        if (!onMessageThread() || !owner->captureGated.load(std::memory_order_acquire)) return;
        owner->instance->getStateInformation(destination);
        if (destination.getSize() > ExternalPluginState::maxStateBytes) destination.reset();
    }
    void setStateInformation(const void*, int) override {
        // Mutating an installed instance would bypass bus/latency validation.
        owner->invalidate(ExternalFault::directStateRestore);
    }

    const std::shared_ptr<ExternalInstanceOwner> owner;
private:
    std::atomic<bool> preparationValidated{false};
    std::atomic<float> outputPeak{0};
};

// The owner pool is the normal last reference. If a caller violates the host
// lifetime contract, never let JUCE's synchronous VST3 destructor run on a
// worker that the message thread may be joining. Queue cleanup, without a wait.
// If the message system has already gone away, retain the orphan rather than
// invoke undefined third-party teardown off-thread during process shutdown.
void destroyOwnerOnMessageThread(ExternalInstanceOwner* owner) {
    if (onMessageThread()) { delete owner; return; }
    jassertfalse;
    if (juce::MessageManager::getInstanceWithoutCreating() != nullptr)
        juce::MessageManager::callAsync([owner] {delete owner;});
}
} // namespace

ExternalPluginHost::ExternalPluginHost() {
    jassert(onMessageThread());
   #if JUCE_PLUGINHOST_VST3
    // Do not use addDefaultFormats(): this bounded host accepts only VST3.
    formats.addFormat(new juce::VST3PluginFormat());
   #endif
}

ExternalPluginHost::~ExternalPluginHost() {
    jassert(onMessageThread());
    for (const auto& owner : owners) {
        juce::ignoreUnused(owner);
        jassert(owner.use_count() == 1); // Join workers and close editors first.
    }
    owners.clear();
}

juce::Array<juce::PluginDescription> ExternalPluginHost::discoverSelected(const juce::File& file,
                                                                        juce::String& error) {
    error.clear();
    juce::Array<juce::PluginDescription> result;
    if (!requireMessageThread(error)) return result;
    if (!file.hasFileExtension(".vst3") || !file.exists()) {
        error = "Select an existing .vst3 file or bundle."; return result;
    }
    if (formats.getNumFormats() != 1) {
        error = "This build does not have VST3 hosting enabled."; return result;
    }
    juce::OwnedArray<juce::PluginDescription> descriptions;
    try {formats.getFormat(0)->findAllTypesForFile(descriptions, file.getFullPathName());}
    catch (...) {error = "The selected VST3 threw an exception during discovery."; return result;}
    if (descriptions.size() > 64) {
        error = "The selected VST3 container exceeds the 64-class discovery limit."; return result;
    }
    for (const auto* description : descriptions) {
        ExternalPluginState candidate;
        candidate.descriptionXml = description->createXml()->toString();
        juce::String candidateError;
        if (candidate.validate(candidateError)) result.add(*description);
        else if (error.isEmpty()) error = candidateError;
    }
    if (result.isEmpty() && error.isEmpty()) error = "No supported VST3 audio effect was found in the selected file.";
    if (!result.isEmpty()) error.clear();
    return result;
}

std::unique_ptr<ModuleProcessor> ExternalPluginHost::stage(const ExternalPluginState& saved,
                                                         double rate, int block, int budget,
                                                         bool offline, juce::String& error) {
    error.clear();
    if (!requireMessageThread(error)) return {};
    juce::PluginDescription description;
    if (!saved.getDescription(description, error)) return {};
    if (!std::isfinite(rate) || rate < 8000 || rate > 768000 || block < 1 || block > 1048576
        || budget != static_cast<int>(std::ceil(rate * 0.006))) {
        error = "Invalid VST3 render context: an actual sample rate, maximum block, and fixed 6 ms slot budget are required.";
        return {};
    }
    if (!juce::File(description.fileOrIdentifier).exists()) {
        error = "The saved VST3 file is missing: " + description.fileOrIdentifier; return {};
    }
    if (formats.getNumFormats() != 1) {
        error = "This build does not have VST3 hosting enabled."; return {};
    }
    // Nothing below can mutate the old node. Failure destroys this unpublished
    // instance here, on the message thread.
    std::unique_ptr<juce::AudioPluginInstance> instance;
    try {
        instance = formats.createPluginInstance(description, rate, block, error);
        if (!instance) {
            if (error.isEmpty()) error = "The selected VST3 could not be instantiated.";
            return {};
        }
        juce::PluginDescription actualDescription;
        instance->fillInPluginDescription(actualDescription);
        if (actualDescription.pluginFormatName != "VST3" || actualDescription.isInstrument
            || actualDescription.uniqueId != description.uniqueId) {
            error = "The loaded VST3 does not match the saved stereo-effect descriptor."; return {};
        }
        instance->releaseResources();
        instance->setNonRealtime(offline);
        instance->setProcessingPrecision(juce::AudioProcessor::singlePrecision);
        instance->setRateAndBufferSizeDetails(rate, block);
        if (saved.state.getSize() != 0)
            instance->setStateInformation(saved.state.getData(), static_cast<int>(saved.state.getSize()));
        instance->releaseResources();
        if (!configureStereoOnly(*instance, error)) return {};
        if (!verifyNativeFloat32(*instance, error)) return {};
        // JUCE's public prepareToPlay is void and internally discards native
        // setup/activation results. Read-only capability/layout/latency checks
        // below narrow acceptance; they cannot certify arbitrary vendor setup
        // success, and the adapter does not bypass JUCE's lifecycle machinery.
        instance->prepareToPlay(rate, block);
        if (!hasStereoOnlyLayout(*instance)) {
            error = "The VST3 changed to an unsupported bus layout during preparation."; return {};
        }
        int latency = 0;
        if (!verifyNativePreparation(*instance, budget, latency, error)) return {};
        const auto reportedTail = instance->getTailLengthSeconds();
        const double tail = std::isfinite(reportedTail) && reportedTail >= 0 ? reportedTail : 0;
        auto owner = std::shared_ptr<ExternalInstanceOwner>(
            new ExternalInstanceOwner(std::move(instance), saved.descriptionXml, rate, block, latency, offline, tail),
            destroyOwnerOnMessageThread);
        auto adapter = std::make_unique<ExternalModuleProcessor>(owner, description.name);
        owners.push_back(std::move(owner));
        return adapter;
    } catch (...) {
        error = "The VST3 threw an exception while loading, restoring, or preparing. The previous insert was left unchanged.";
        return {};
    }
}

bool ExternalPluginHost::capture(ModuleProcessor& processor, ExternalPluginState& destination,
                                 juce::String& error) {
    error.clear();
    if (!requireMessageThread(error)) return false;
    auto* adapter = dynamic_cast<ExternalModuleProcessor*>(&processor);
    if (!adapter) {error = "The selected processor is not a hosted VST3."; return false;}
    if (!adapter->owner->captureGated.load(std::memory_order_acquire)) {
        error = "Gate VST3 processing and wait for audio/worker quiescence before capturing state."; return false;
    }
    ExternalPluginState captured;
    captured.descriptionXml = adapter->owner->descriptionXml;
    try {adapter->owner->instance->getStateInformation(captured.state);}
    catch (...) {error = "The VST3 threw an exception while capturing state."; return false;}
    if (!captured.validate(error)) return false;
    destination = std::move(captured);
    return true;
}

void ExternalPluginHost::collectGarbage() {
    jassert(onMessageThread());
    if (!onMessageThread()) return;
    owners.erase(std::remove_if(owners.begin(), owners.end(), [] (const auto& owner) {
        return owner.use_count() == 1;
    }), owners.end());
}
} // namespace hem
