#include "MainComponent.h"
#include "Spectral/SpectralPanel.h"
#include "Spectral/SpectralSourceFiles.h"
#include <functional>

namespace {
namespace sf = hem::spectral::files;
using Audition = hem::spectral::Audition;
class SpectralJob final : public juce::ThreadPoolJob {
public:
    explicit SpectralJob(std::function<void()> task)
        : ThreadPoolJob("HEM selected-source spectral work"), action(std::move(task)) {}
    JobStatus runJob() override { action(); return jobHasFinished; }
private:
    std::function<void()> action;
};
template<class State, class Work>
void submitSpectral(juce::ThreadPool& pool, const std::shared_ptr<State>& state, Work work) {
    state->cancel.store(false); state->done.store(false); state->error.clear();
    state->running = true; state->view.busy = true;
    auto job = std::make_unique<SpectralJob>([state, work = std::move(work)] {
        try { sf::check(state->cancel); work(*state); sf::check(state->cancel); }
        catch (const sf::Cancelled&) { state->error = "Spectral operation cancelled; source unchanged"; }
        catch (const std::exception& error) { state->error = error.what(); }
        catch (...) { state->error = "Spectral operation failed; source unchanged"; }
        if (state->cancel.load() || state->error.isNotEmpty()) state->discardFailedWork();
        state->done.store(true, std::memory_order_release);
    });
    try { pool.addJob(job.get(), true); job.release(); }
    catch (...) {
        state->running = false; state->view.busy = false;
        state->view.status = "Cannot queue spectral worker; source unchanged";
    }
}
void verifyShape(const sf::Identity& original, const sf::Identity& processed) {
    sf::demand(original.frames == processed.frames && original.rate == processed.rate
        && original.channels == processed.channels, "Spectral output changed frame count, rate or channel layout");
}
std::unique_ptr<hem::StemPlayback> prepareSpectralPlayback(const juce::File& file,
    juce::TimeSliceThread& disk, double rate, int block, bool loop = false, double position = 0) {
    juce::AudioFormatManager formats; formats.registerBasicFormats();
    auto playback = std::make_unique<hem::StemPlayback>();
    playback->prepareFile(file, formats, disk, rate, block, loop, position); return playback;
}
}

struct MainComponent::SpectralHistory {
    sf::Identity original, rendered;
    hem::spectral::Selection selection;
    bool renderedActive = true;
};
struct MainComponent::SpectralOperation {
    enum class Work { inspect, preview, audition, apply, undo };
    int track = -1, targetBlock = 0;
    unsigned hostGeneration = 0;
    std::uint64_t sourceRevision = 0, applyEpoch = 0;
    double targetRate = 0, applyPosition = 0;
    bool loop = false, running = false, ready = false, previewValid = false;
    Work work = Work::inspect;
    juce::File source, privateDirectory, durableStageDirectory, durableOutput, durableManifest;
    juce::String error;
    sf::Identity originalIdentity, snapshotIdentity, previewIdentity, appliedIdentity;
    std::shared_ptr<hem::ReadAheadTransport> expectedTransport;
    std::shared_ptr<const hem::spectral::Audio> samples;
    std::shared_ptr<const hem::spectral::Grid> grid;
    std::shared_ptr<SpectralHistory> nextHistory;
    hem::spectral::Selection selection;
    hem::spectral::ViewState view;
    std::atomic<bool> cancel{false}, done{false}, previewActive{false}, audioInvalid{false};
    std::atomic<int> auditionMode{int(Audition::stopped)};
    int requestedMode = int(Audition::stopped);
    std::unique_ptr<hem::StemPlayback> originalPlayback, processedPlayback, preparedApply;
    std::uint64_t originalEpoch = 0, processedEpoch = 0;
    juce::AudioBuffer<float> originalBuffer, processedBuffer;
    float originalGain = 0, processedGain = 0; // callback-owned except while excluded
    bool keepDurableOutput = false, ownsDurableOutput = false, ownsDurableManifest = false;
    void clearApplyStaging() {
        preparedApply.reset();
        if (durableStageDirectory != juce::File()) {
            durableStageDirectory.getChildFile("processed.wav").deleteFile();
            durableStageDirectory.getChildFile("recipe.json").deleteFile(); durableStageDirectory.deleteFile();
        }
        if (!keepDurableOutput) {
            if (ownsDurableOutput) durableOutput.deleteFile();
            if (ownsDurableManifest) durableManifest.deleteFile();
        }
        durableStageDirectory = juce::File(); durableOutput = juce::File(); durableManifest = juce::File();
        ownsDurableOutput = ownsDurableManifest = keepDurableOutput = false;
    }
    void releaseWorkerResources() {
        // Explicitly retire payloads in the worker: the last shared_ptr to this
        // small state shell can otherwise race between worker and UI owners.
        if (originalPlayback && originalEpoch) originalPlayback->transport->discardPreparedEpoch(originalEpoch);
        if (processedPlayback && processedEpoch) processedPlayback->transport->discardPreparedEpoch(processedEpoch);
        originalEpoch = processedEpoch = 0;
        originalPlayback.reset(); processedPlayback.reset(); expectedTransport.reset();
        samples.reset(); grid.reset(); view.grid.reset();
        originalBuffer.setSize(0, 0); processedBuffer.setSize(0, 0);
        if (privateDirectory != juce::File()) {
            privateDirectory.getChildFile("original.wav").deleteFile();
            privateDirectory.getChildFile("processed.wav").deleteFile(); privateDirectory.deleteFile();
        }
        privateDirectory = juce::File(); clearApplyStaging();
    }
    void discardFailedWork() {
        if (originalPlayback && originalEpoch) originalPlayback->transport->discardPreparedEpoch(originalEpoch);
        if (processedPlayback && processedEpoch) processedPlayback->transport->discardPreparedEpoch(processedEpoch);
        originalEpoch = processedEpoch = 0;
        if (work == Work::inspect) {
            // The live message-thread poll still reads expectedTransport/view.
            // Only unpublished worker payloads may change until done is acquired.
            originalPlayback.reset(); processedPlayback.reset(); samples.reset(); grid.reset();
            if (privateDirectory != juce::File()) {
                privateDirectory.getChildFile("original.wav").deleteFile();
                privateDirectory.getChildFile("processed.wav").deleteFile(); privateDirectory.deleteFile();
            }
            privateDirectory = juce::File(); clearApplyStaging(); return;
        }
        if (work == Work::preview) {
            processedPlayback.reset();
            if (privateDirectory != juce::File()) privateDirectory.getChildFile("processed.wav").deleteFile();
        }
        if (work == Work::apply || work == Work::undo) clearApplyStaging();
    }
    ~SpectralOperation() { releaseWorkerResources(); }
};

bool MainComponent::spectralOperationBusy() const {
    return (spectralOperation && (spectralOperation->running || spectralOperation->previewActive.load()))
        || (spectralPool && spectralPool->getNumJobs() > 0);
}
void MainComponent::invalidateSpectralAudio() noexcept {
    if (auto* operation = spectralAudio.load(std::memory_order_acquire)) {
        operation->cancel.store(true); operation->previewActive.store(false);
        operation->audioInvalid.store(true); operation->auditionMode.store(int(Audition::stopped));
    }
}
void MainComponent::closeSpectralOperation() {
    auto operation = spectralOperation;
    if (!operation && !embeddedSpectral) return;
    if (operation) operation->cancel.store(true);
    {
        const GraphControlGuard guard(*this);
        spectralAudio.store(nullptr, std::memory_order_release);
        if (operation) operation->previewActive.store(false);
        spectralOperation.reset();
    }
    if (embeddedSpectral && utilityContent == embeddedSpectral.get()) {
        utilityViewport.setViewedComponent(nullptr, false); utilityContent = nullptr;
    }
    if (auto* panel = dynamic_cast<hem::spectral::SpectralPanel*>(embeddedSpectral.get())) {
        panel->onPreview = {}; panel->onApply = {}; panel->onUndo = {}; panel->onAudition = {};
        panel->onSelectionChanged = {}; panel->onCancel = {}; panel->setSnapshot({});
    }
    if (operation && spectralPool) {
        // Serial worker retirement follows any cancelled work. No join, decoder
        // teardown, large PCM free or file deletion happens on Back/navigation.
        auto cleanup = std::make_unique<SpectralJob>([operation] { operation->releaseWorkerResources(); });
        spectralPool->addJob(cleanup.get(), true); cleanup.release();
    }
}
void MainComponent::shutdownSpectralOperations() {
    closeSpectralOperation();
    // Normal window-close flow waits asynchronously for spectralOperationBusy.
    // This is the final lifetime fallback before the host disk thread is freed.
    if (spectralPool) {
        // Do not remove queued retirement jobs: their payload frees belong on the worker.
        while (auto* job = spectralPool->getJob(0)) spectralPool->waitForJobToFinish(job, -1);
        spectralPool.reset();
    }
    embeddedSpectral.reset();
    for (auto& history : spectralHistory) history.reset();
}

void MainComponent::openSpectralOperation() {
    if (nativeResourceLoadBusy() || playing() || vocals.recorder.busy()
        || sessionWorker->isBusy() || exportWorker->isBusy() || importer->hasPendingWork()) {
        message.setText("Stop transport and finish pending work before opening the spectral scanner", juce::dontSendNotification); return;
    }
    if (!current().loaded || current().playback->file == juce::File()) {
        message.setText("Select a loaded mixer track before opening the spectral scanner", juce::dontSendNotification); return;
    }
    closePluginEditor(); prepareOfflineSession();
    if (!spectralPool) spectralPool = std::make_unique<juce::ThreadPool>(1);
    auto operation = std::make_shared<SpectralOperation>();
    operation->track = selectedTrack; operation->hostGeneration = generation;
    operation->targetRate = sampleRate; operation->targetBlock = blockSize; operation->loop = looping;
    operation->source = current().playback->file; operation->expectedTransport = current().playback->transport;
    static std::atomic<std::uint64_t> nextRevision{1};
    operation->sourceRevision = nextRevision.fetch_add(1, std::memory_order_relaxed);
    operation->originalBuffer.setSize(2, blockSize); operation->processedBuffer.setSize(2, blockSize);
    operation->view.sourceLabel = "TRACK " + juce::String(selectedTrack + 1) + " / " + operation->source.getFileName();
    operation->view.status = "Loading an immutable source snapshot and measuring FFT in the background...";
    if (!embeddedSpectral) embeddedSpectral = std::make_unique<hem::spectral::SpectralPanel>();
    auto* panel = static_cast<hem::spectral::SpectralPanel*>(embeddedSpectral.get());
    juce::Component::SafePointer<MainComponent> safe(this);
    panel->onPreview = [safe](hem::spectral::Selection selection) { if (safe) safe->spectralPreview(selection); };
    panel->onApply = [safe] { if (safe) safe->spectralApply(false); };
    panel->onUndo = [safe] { if (safe) safe->spectralApply(true); };
    panel->onAudition = [safe](Audition mode) { if (safe) safe->spectralAudition(int(mode)); };
    panel->onSelectionChanged = [safe] { if (safe) safe->spectralSelectionChanged(); };
    panel->onCancel = [safe] {
        if (safe && safe->spectralOperation) {
            safe->spectralOperation->cancel.store(true);
            safe->spectralOperation->view.status = "Cancelling background spectral work...";
        }
    };
    { const GraphControlGuard guard(*this); spectralOperation = operation; spectralAudio.store(operation.get(), std::memory_order_release); }
    showUtility(*embeddedSpectral, "SELECTED TRACK / MEASURED SPECTRAL SCANNER");
    auto* disk = &diskThread;
    submitSpectral(*spectralPool, operation, [disk](SpectralOperation& job) {
        job.originalIdentity = sf::identify(job.source, job.cancel);
        job.samples = sf::decode(job.originalIdentity, job.cancel);
        job.grid = std::make_shared<const hem::spectral::Grid>(hem::spectral::analyze(*job.samples, &job.cancel));
        job.privateDirectory = sf::privateDirectory(juce::File::getSpecialLocation(juce::File::tempDirectory));
        job.snapshotIdentity = sf::writeFloat(job.privateDirectory.getChildFile("original.wav"), *job.samples, job.cancel);
        verifyShape(job.originalIdentity, job.snapshotIdentity);
        job.originalPlayback = prepareSpectralPlayback(job.snapshotIdentity.file, *disk, job.targetRate, job.targetBlock);
    });
    static_cast<hem::spectral::SpectralPanel*>(embeddedSpectral.get())->setSnapshot(operation->view);
}

void MainComponent::spectralSelectionChanged() {
    auto operation = spectralOperation; if (!operation) return;
    if (operation->running) operation->cancel.store(true);
    { const GraphControlGuard guard(*this); operation->previewActive.store(false); operation->auditionMode.store(int(Audition::stopped)); }
    operation->previewValid = false; operation->view.canApply = operation->view.canAudition = false;
    operation->view.audition = Audition::stopped;
    operation->view.status = "Selection changed. Compute a new preview before audition or Apply.";
}
void MainComponent::spectralPreview(const hem::spectral::Selection& selection) {
    auto operation = spectralOperation;
    if (!operation || operation->running || !operation->ready || playing()) return;
    if (operation->audioInvalid.load() || operation->hostGeneration != generation
        || operation->track != selectedTrack || current().playback->transport != operation->expectedTransport) {
        closeSpectralOperation(); return;
    }
    { const GraphControlGuard guard(*this); operation->previewActive.store(false); }
    operation->selection = selection; operation->work = SpectralOperation::Work::preview;
    operation->previewValid = false; operation->view.canApply = operation->view.canAudition = false;
    operation->view.audition = Audition::stopped;
    operation->view.status = "Computing selected spectral attenuation in the background; production source unchanged...";
    auto* disk = &diskThread;
    submitSpectral(*spectralPool, operation, [disk](SpectralOperation& job) {
        if (job.originalEpoch) { job.originalPlayback->transport->discardPreparedEpoch(job.originalEpoch); job.originalEpoch = 0; }
        if (job.processedPlayback && job.processedEpoch) { job.processedPlayback->transport->discardPreparedEpoch(job.processedEpoch); job.processedEpoch = 0; }
        job.processedPlayback.reset(); job.privateDirectory.getChildFile("processed.wav").deleteFile();
        auto output = hem::spectral::attenuate(*job.samples, job.selection, &job.cancel);
        job.previewIdentity = sf::writeFloat(job.privateDirectory.getChildFile("processed.wav"), output, job.cancel);
        verifyShape(job.originalIdentity, job.previewIdentity);
        job.processedPlayback = prepareSpectralPlayback(job.previewIdentity.file, *disk, job.targetRate, job.targetBlock);
    });
}
void MainComponent::spectralAudition(int mode) {
    auto operation = spectralOperation; if (!operation) return;
    if (mode == int(Audition::stopped)) {
        operation->requestedMode = mode;
        if (operation->running && operation->work == SpectralOperation::Work::audition)
            operation->cancel.store(true);
        operation->auditionMode.store(mode); operation->view.audition = Audition::stopped; return;
    }
    if (mode < int(Audition::original) || mode > int(Audition::delta) || operation->running
        || !operation->previewValid || playing()) return;
    if (!deviceManager.getCurrentAudioDevice()) {
        operation->view.status = "Preview is ready. Connect an audio device to audition; Apply is available offline."; return;
    }
    if (trackScheduler.faulted()) { operation->view.status = "Audio is faulted; stop and recover playback before audition."; return; }
    if (operation->previewActive.load()) {
        operation->auditionMode.store(mode); operation->view.audition = static_cast<Audition>(mode); return;
    }
    operation->requestedMode = mode; operation->work = SpectralOperation::Work::audition;
    operation->view.status = "Preparing synchronized original/processed audition...";
    submitSpectral(*spectralPool, operation, [](SpectralOperation& job) {
        auto cancelled = [&job] { return job.cancel.load(); };
        if (job.originalEpoch) job.originalPlayback->transport->discardPreparedEpoch(job.originalEpoch);
        if (job.processedEpoch) job.processedPlayback->transport->discardPreparedEpoch(job.processedEpoch);
        job.originalEpoch = job.processedEpoch = 0;
        auto result = job.originalPlayback->transport->prepareEpoch(0, false, job.originalEpoch, cancelled);
        sf::demand(result.wasOk(), result.getErrorMessage());
        result = job.processedPlayback->transport->prepareEpoch(0, false, job.processedEpoch, cancelled);
        sf::demand(result.wasOk(), result.getErrorMessage());
    });
}

void MainComponent::spectralApply(bool undo) {
    auto operation = spectralOperation;
    if (!operation || operation->running || !operation->ready || (!undo && !operation->previewValid)) return;
    if (playing() || vocals.recorder.busy() || sessionWorker->isBusy() || exportWorker->isBusy()
        || importer->hasPendingWork() || transportBusy()) {
        operation->view.status = "Stop transport and finish pending work before changing the selected source."; return;
    }
    if (operation->track != selectedTrack || operation->hostGeneration != generation
        || current().playback->transport != operation->expectedTransport || current().playback->file != operation->source) {
        operation->view.status = "Selected source changed; reopen the spectral scanner."; return;
    }
    const auto history = spectralHistory[std::size_t(operation->track)];
    if (undo && (!history || (history->renderedActive ? history->rendered.file : history->original.file) != operation->source)) return;
    {
        const GraphControlGuard guard(*this);
        operation->previewActive.store(false);
        operation->applyEpoch = operation->expectedTransport->epoch();
        // Preserve this source's device-frame cursor. EOF is explicit because
        // total device frames use ceil(sourceFrames * deviceRate / sourceRate),
        // whereas rounding duration cannot represent that intent reliably.
        const double position = operation->expectedTransport->getCurrentPosition();
        const double duration = operation->expectedTransport->getLengthInSeconds();
        if (position >= duration)
            operation->applyPosition = duration + 0.25 / operation->targetRate;
        else {
            // Quarter-frame bias prevents floor(n/rate*rate) losing one frame.
            const double frame = std::round(position * operation->targetRate);
            operation->applyPosition = (frame + 0.25) / operation->targetRate;
        }
    }
    operation->work = undo ? SpectralOperation::Work::undo : SpectralOperation::Work::apply;
    operation->view.audition = Audition::stopped;
    operation->view.status = undo ? "Verifying original and rendered media for source Undo/Redo..."
        : "Verifying source identity and staging exact-length float32 output...";
    auto* disk = &diskThread;
    submitSpectral(*spectralPool, operation, [disk, history, undo](SpectralOperation& job) {
        job.clearApplyStaging();
        sf::verify(job.originalIdentity, job.cancel);
        if (undo) {
            sf::verify(history->original, job.cancel); sf::verify(history->rendered, job.cancel);
            job.nextHistory = std::make_shared<SpectralHistory>(*history);
            job.nextHistory->renderedActive = !history->renderedActive;
            job.appliedIdentity = job.nextHistory->renderedActive ? history->rendered : history->original;
        } else {
            sf::verify(job.previewIdentity, job.cancel); verifyShape(job.originalIdentity, job.previewIdentity);
            const auto directory = job.source.getParentDirectory().getChildFile("HEM Spectral Renders");
            sf::demand(directory.createDirectory().wasOk(), "Cannot create HEM Spectral Renders next to source; original unchanged");
            job.durableStageDirectory = sf::privateDirectory(directory);
            const auto staged = job.durableStageDirectory.getChildFile("processed.wav");
            sf::copy(job.previewIdentity.file, staged, job.cancel);
            const auto stagedIdentity = sf::identify(staged, job.cancel);
            sf::demand(stagedIdentity.sha256 == job.previewIdentity.sha256, "Durable spectral copy verification failed");
            verifyShape(job.originalIdentity, stagedIdentity);
            const auto id = juce::Uuid().toString();
            job.durableOutput = directory.getChildFile("HEM-Spectral-" + id + ".wav");
            job.durableManifest = directory.getChildFile("HEM-Spectral-" + id + ".json");
            auto recipe = std::make_unique<juce::DynamicObject>();
            recipe->setProperty("version", 1); recipe->setProperty("operation", "measured-stft-selected-attenuation");
            recipe->setProperty("original", job.source.getFullPathName()); recipe->setProperty("originalSha256", job.originalIdentity.sha256);
            recipe->setProperty("rendered", job.durableOutput.getFullPathName()); recipe->setProperty("renderedSha256", stagedIdentity.sha256);
            recipe->setProperty("sampleRate", job.originalIdentity.rate); recipe->setProperty("channels", job.originalIdentity.channels);
            recipe->setProperty("frames", job.originalIdentity.frames); recipe->setProperty("sourceRevision", juce::int64(job.sourceRevision));
            recipe->setProperty("startFrame", juce::int64(job.selection.startFrame)); recipe->setProperty("endFrame", juce::int64(job.selection.endFrame));
            recipe->setProperty("lowHz", job.selection.lowHz); recipe->setProperty("highHz", job.selection.highHz); recipe->setProperty("gain", job.selection.gain);
            recipe->setProperty("fftSize", juce::int64(hem::spectral::fftSize)); recipe->setProperty("hopSize", juce::int64(hem::spectral::hopSize));
            const auto manifest = job.durableStageDirectory.getChildFile("recipe.json");
            sf::demand(manifest.replaceWithText(juce::JSON::toString(juce::var(recipe.release()), true, 17)), "Cannot write spectral provenance");
            sf::verify(job.originalIdentity, job.cancel); sf::check(job.cancel);
            sf::demand(!hem::noReplaceFile::publish(staged, job.durableOutput), "Cannot publish spectral WAV without replacing a file");
            job.ownsDurableOutput = true;
            sf::demand(!hem::noReplaceFile::publish(manifest, job.durableManifest), "Cannot publish spectral provenance without replacing a file");
            job.ownsDurableManifest = true;
            job.appliedIdentity = sf::identify(job.durableOutput, job.cancel);
            job.nextHistory = std::make_shared<SpectralHistory>();
            job.nextHistory->original = job.originalIdentity; job.nextHistory->rendered = job.appliedIdentity;
            job.nextHistory->selection = job.selection;
        }
        verifyShape(job.originalIdentity, job.appliedIdentity);
        job.preparedApply = prepareSpectralPlayback(job.appliedIdentity.file, *disk, job.targetRate, job.targetBlock, job.loop, job.applyPosition);
        sf::verify(job.originalIdentity, job.cancel); sf::verify(job.appliedIdentity, job.cancel);
    });
}

void MainComponent::pollSpectralOperation() {
    auto operation = spectralOperation; if (!operation) return;
    if (operation->audioInvalid.load() || operation->hostGeneration != generation || operation->track != selectedTrack
        || operation->loop != looping || current().playback->transport != operation->expectedTransport
        || current().playback->file != operation->source) {
        closeSpectralOperation(); message.setText("Source or device changed; private spectral operation cancelled", juce::dontSendNotification); return;
    }
    if (operation->running && operation->done.load(std::memory_order_acquire)) {
        operation->running = false; operation->view.busy = false;
        if (operation->cancel.load() || operation->error.isNotEmpty()) {
            operation->view.status = operation->cancel.load() ? "Spectral operation cancelled; source unchanged" : operation->error;
            operation->view.canApply = operation->view.canAudition = false;
            operation->previewValid = false;
            if (operation->work == SpectralOperation::Work::apply || operation->work == SpectralOperation::Work::undo) {
                const auto error = operation->view.status;
                closeSpectralOperation(); showShellPage(ShellPage::mixer);
                message.setText(error, juce::dontSendNotification); return;
            }
        } else if (operation->work == SpectralOperation::Work::inspect) {
            operation->ready = true; operation->view.grid = operation->grid;
            operation->view.sourceRevision = operation->sourceRevision;
            operation->view.frames = std::size_t(operation->originalIdentity.frames); operation->view.sampleRate = operation->originalIdentity.rate;
            operation->view.sourceReady = true;
            operation->view.status = "Measured source FFT ready. Select time/frequency bounds and compute attenuation; original stays unchanged.";
        } else if (operation->work == SpectralOperation::Work::preview) {
            operation->previewValid = true; operation->view.canApply = operation->view.canAudition = true;
            operation->view.status = "Preview ready. ORIGINAL / PROCESSED / DELTA audition privately; Apply changes only this selected source.";
        } else if (operation->work == SpectralOperation::Work::audition) {
            bool committed = false;
            {
                const GraphControlGuard guard(*this);
                if (!playing() && !trackScheduler.faulted() && deviceManager.getCurrentAudioDevice()
                    && !operation->cancel.load() && !operation->audioInvalid.load()
                    && operation->requestedMode != int(Audition::stopped)
                    && operation->hostGeneration == generation && operation->track == selectedTrack
                    && operation->loop == looping && current().playback->transport == operation->expectedTransport
                    && current().playback->file == operation->source
                    && operation->originalEpoch && operation->processedEpoch
                    && operation->originalPlayback->transport->preparedEpoch() == operation->originalEpoch
                    && operation->processedPlayback->transport->preparedEpoch() == operation->processedEpoch) {
                    const bool first = operation->originalPlayback->transport->commitPreparedEpoch(operation->originalEpoch, true);
                    const bool second = operation->processedPlayback->transport->commitPreparedEpoch(operation->processedEpoch, true);
                    operation->originalEpoch = operation->processedEpoch = 0;
                    committed = first && second;
                    if (committed) {
                        operation->originalGain = operation->processedGain = 0;
                        operation->auditionMode.store(operation->requestedMode); operation->previewActive.store(true);
                    }
                }
            }
            operation->view.audition = committed ? static_cast<Audition>(operation->requestedMode) : Audition::stopped;
            operation->view.status = committed ? "Private, synchronized audition. DELTA is original minus processed." : "Audition cancelled after source/device change.";
        } else {
            bool committed = false;
            // Content hashes and exact shape were verified on the worker. Small
            // stat checks close the ordinary external-write race before commit.
            const bool identitiesMatch = sf::metadataUnchanged(operation->originalIdentity)
                && sf::metadataUnchanged(operation->appliedIdentity);
            {
                const GraphControlGuard guard(*this);
                auto& track = bank->tracks[std::size_t(operation->track)];
                if (identitiesMatch && prepared && !playing() && !vocals.recorder.busy()
                    && !transportBusy() && operation->expectedTransport->epoch() == operation->applyEpoch
                    && operation->hostGeneration == generation && operation->loop == looping
                    && track.playback->transport == operation->expectedTransport && track.playback->file == operation->source
                    && operation->preparedApply) {
                    track.playback.swap(operation->preparedApply); ++generation; committed = true;
                }
            }
            if (committed) {
                auto& track = bank->tracks[std::size_t(operation->track)];
                track.loaded = true; track.name = track.playback->file.getFileName(); track.sourceRate = track.playback->sourceRate;
                track.clipEdit.reset(); // Never label spectral output as a pitch/time recipe.
                spectralHistory[std::size_t(operation->track)] = operation->nextHistory;
                operation->keepDurableOutput = true;
                const bool undo = operation->work == SpectralOperation::Work::undo;
                closeSpectralOperation();
                showShellPage(ShellPage::mixer);
                message.setText(undo ? "Spectral source Undo/Redo applied; exact frame alignment retained. Reopen scanner for further edits."
                    : "Spectral attenuation applied; original retained, exact frame count unchanged. Reopen scanner for Undo or further edits.", juce::dontSendNotification);
                return;
            }
            closeSpectralOperation(); showShellPage(ShellPage::mixer);
            message.setText("Source/device changed before stopped commit; original retained.", juce::dontSendNotification);
            return;
        }
    }
    const auto history = spectralHistory[std::size_t(operation->track)];
    operation->view.canUndo = operation->ready && history
        && (history->renderedActive ? history->rendered.file : history->original.file) == operation->source;
    operation->view.undoRestoresOriginal = !history || history->renderedActive;
    if (!operation->previewActive.load() && !operation->running) operation->view.audition = Audition::stopped;
    if (auto* panel = dynamic_cast<hem::spectral::SpectralPanel*>(embeddedSpectral.get())) panel->setSnapshot(operation->view);
}

bool MainComponent::spectralPreviewReady(int frames) const noexcept {
    auto* operation = spectralAudio.load(std::memory_order_acquire);
    if (!operation || !operation->previewActive.load(std::memory_order_relaxed) || operation->audioInvalid.load()) return true;
    return operation->originalPlayback && operation->processedPlayback
        && operation->originalPlayback->transport->availableRt(frames)
        && operation->processedPlayback->transport->availableRt(frames);
}
void MainComponent::renderSpectralPreview(juce::AudioBuffer<float>& output, int frames) {
    auto* operation = spectralAudio.load(std::memory_order_acquire);
    if (!operation || !operation->previewActive.load(std::memory_order_relaxed) || operation->audioInvalid.load()) return;
    if (!operation->originalPlayback || !operation->processedPlayback || frames > operation->originalBuffer.getNumSamples()) {
        operation->previewActive.store(false); output.clear(); return;
    }
    const bool first = operation->originalPlayback->transport->renderRt({&operation->originalBuffer, 0, frames});
    const bool second = operation->processedPlayback->transport->renderRt({&operation->processedBuffer, 0, frames});
    if (!first || !second) {
        operation->previewActive.store(false); output.clear(); transportDropouts.fetch_add(1);
        trackRenderFaults.fetch_add(1); trackRenderSilencedCallbacks.fetch_add(1); trackScheduler.latchFault(); return;
    }
    const int mode = operation->auditionMode.load(std::memory_order_relaxed);
    const float targetOriginal = mode == int(Audition::original) || mode == int(Audition::delta) ? 1.f : 0.f;
    const float targetProcessed = mode == int(Audition::processed) ? 1.f : mode == int(Audition::delta) ? -1.f : 0.f;
    // Fixed-storage, sample-wise ramp prevents switching clicks. Both readers
    // always advance together; DELTA never introduces a third PCM allocation.
    for (int frame = 0; frame < frames; ++frame) {
        operation->originalGain += std::clamp(targetOriginal - operation->originalGain, -1.f / 128, 1.f / 128);
        operation->processedGain += std::clamp(targetProcessed - operation->processedGain, -1.f / 128, 1.f / 128);
        for (int channel = 0; channel < 2; ++channel) {
            const double sample = double(operation->originalGain) * operation->originalBuffer.getSample(channel, frame)
                + double(operation->processedGain) * operation->processedBuffer.getSample(channel, frame);
            if (!std::isfinite(sample) || std::abs(sample) > double(std::numeric_limits<float>::max())) {
                operation->previewActive.store(false); output.clear(); trackRenderFaults.fetch_add(1);
                trackRenderSilencedCallbacks.fetch_add(1); trackScheduler.latchFault(); return;
            }
            output.setSample(channel, frame, float(sample));
        }
    }
    if ((!operation->originalPlayback->transport->isPlaying() && !operation->processedPlayback->transport->isPlaying())
        || (mode == int(Audition::stopped) && operation->originalGain == 0 && operation->processedGain == 0))
        operation->previewActive.store(false);
}
