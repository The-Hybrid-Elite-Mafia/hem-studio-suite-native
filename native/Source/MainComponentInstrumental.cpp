#include "MainComponent.h"
#include "Instrumental/InstrumentalPanel.h"
#include "Instrumental/InstrumentalFiles.h"
#include <functional>

namespace {
namespace instrumental = hem::instrumental;
namespace files = hem::instrumental::files;
class InstrumentalJob final : public juce::ThreadPoolJob {
public:
    explicit InstrumentalJob(std::function<void()> work)
        : ThreadPoolJob("HEM offline instrumental builder"), action(std::move(work)) {}
    JobStatus runJob() override { action(); return jobHasFinished; }
private:
    std::function<void()> action;
};
template<class State, class Work>
void submitInstrumental(juce::ThreadPool& pool, const std::shared_ptr<State>& state, Work work) {
    state->cancel.store(false); state->done.store(false); state->progress.store(0);
    state->error.clear(); state->running = true; state->view.busy = true;
    auto job = std::make_unique<InstrumentalJob>([state, work = std::move(work)] {
        try {
            // Retired samples remain pinned until both host and panel published
            // their replacement. The next serialized worker owns the final free.
            state->retiredPlans.clear(); state->completedPlan = {};
            files::check(state->cancel); work(*state); files::check(state->cancel);
        } catch (const files::Cancelled&) {
            state->error = "Cancelled. Existing mixer tracks and completed files are unchanged.";
            state->completedPlan = {};
        } catch (const std::exception& error) {
            state->error = juce::String(error.what()); state->completedPlan = {};
        } catch (...) {
            state->error = "Instrumental work failed. Existing tracks and files are unchanged.";
            state->completedPlan = {};
        }
        state->done.store(true, std::memory_order_release);
    });
    try { pool.addJob(job.get(), true); job.release(); }
    catch (...) {
        state->running = false; state->view.busy = false;
        state->view.status = "Cannot queue instrumental work; current pattern and mixer are unchanged.";
    }
}
bool sameControls(const instrumental::RenderPlan& left, const instrumental::RenderPlan& right) {
    if (left.bpm != right.bpm || left.swingPercent != right.swingPercent
        || left.repeats != right.repeats || left.tailSeconds != right.tailSeconds) return false;
    for (std::size_t index = 0; index < left.lanes.size(); ++index) {
        const auto& a = left.lanes[index]; const auto& b = right.lanes[index];
        if (a.steps != b.steps || a.gainDb != b.gainDb || a.pan != b.pan
            || a.pitchSemitones != b.pitchSemitones || a.mute != b.mute
            || a.solo != b.solo || a.chokeGroup != b.chokeGroup) return false;
    }
    return true;
}
std::array<juce::String, instrumental::kLaneCount> sampleLabels(const files::Identities& identities) {
    std::array<juce::String, instrumental::kLaneCount> result;
    for (std::size_t lane = 0; lane < result.size(); ++lane)
        if (identities[lane].present()) result[lane] = identities[lane].file.getFullPathName();
    return result;
}
}

struct MainComponent::InstrumentalOperation {
    enum class Work { sample, loadPattern, savePattern, render, verifyStage };
    // These fields belong to the message thread, except during final retirement.
    instrumental::RenderPlan plan;
    files::Identities identities;
    instrumental::ViewState view;
    std::vector<instrumental::RenderPlan> retiredPlans;
    Work work = Work::sample;
    bool running = false, renderedValid = false, stageVerified = false;
    double renderedBpm = 120;
    files::ExportResult rendered;
    // Worker publication payload. Read only after done acquire AND pool idle.
    instrumental::RenderPlan completedPlan;
    files::Identities completedIdentities;
    files::ExportResult completedRender;
    juce::File completedFile, attemptedDirectory;
    juce::String error;
    std::atomic<bool> cancel{false}, done{false};
    std::atomic<double> progress{0};
    void releaseWorkerResources() {
        plan = {}; completedPlan = {}; retiredPlans.clear(); identities = {}; completedIdentities = {};
        rendered = {}; completedRender = {};
    }
};

bool MainComponent::instrumentalOperationBusy() const {
    return instrumentalChoosing || (instrumentalOperation && instrumentalOperation->running)
        || (instrumentalPool && instrumentalPool->getNumJobs() > 0);
}
void MainComponent::closeInstrumentalOperation() {
    // Navigation hides the independent draft; it does not discard its samples,
    // controls, completed export or callbacks. Reopening uses this same state.
    ++instrumentalRequest; instrumentalChoosing = false;
    instrumentalPicker.reset();
    if (instrumentalOperation && instrumentalOperation->running)
        instrumentalOperation->cancel.store(true);
    if (embeddedInstrumental && utilityContent == embeddedInstrumental.get()) {
        utilityViewport.setViewedComponent(nullptr, false); utilityContent = nullptr;
    }
    // Hidden work is still polled by timerCallback, including late cancellation.
}
void MainComponent::shutdownInstrumentalOperations() {
    closeInstrumentalOperation();
    auto operation = std::move(instrumentalOperation);
    if (auto* panel = dynamic_cast<instrumental::InstrumentalPanel*>(embeddedInstrumental.get())) {
        panel->clearCallbacks();
        // Operation retains every sample while the panel releases its refs.
        panel->setPlan({}); panel->setSampleLabels({}); panel->setViewState({});
    }
    if (operation && instrumentalPool) {
        operation->cancel.store(true);
        auto cleanup = std::make_unique<InstrumentalJob>([operation] { operation->releaseWorkerResources(); });
        instrumentalPool->addJob(cleanup.get(), true); cleanup.release();
    }
    // Normal window-close polls active work; only final lifetime teardown waits
    // for the cached draft's serialized retirement. Navigation never waits.
    if (instrumentalPool) {
        // removeAllJobs would discard queued retirement without executing it.
        while (auto* job = instrumentalPool->getJob(0)) instrumentalPool->waitForJobToFinish(job, -1);
        instrumentalPool.reset();
    }
    embeddedInstrumental.reset();
}
void MainComponent::openInstrumentalOperation() {
    if (instrumentalOperation && embeddedInstrumental) {
        if (utilityContent != embeddedInstrumental.get()) closePluginEditor();
        showUtility(*embeddedInstrumental, "INSTRUMENTAL BUILDER / 13 LANES / 32 STEPS"); return;
    }
    if (closePhase != ClosePhase::idle || nativeResourceLoadBusy() || externalLoadPending || playing()
        || vocals.recorder.busy() || sessionWorker->isBusy() || exportWorker->isBusy() || importer->hasPendingWork()) {
        message.setText("Stop transport and finish pending work before opening Instrumental Builder", juce::dontSendNotification); return;
    }
    closePluginEditor(); prepareOfflineSession();
    if (!instrumentalPool) instrumentalPool = std::make_unique<juce::ThreadPool>(1);
    auto operation = std::make_shared<InstrumentalOperation>();
    operation->plan.bpm = juce::jlimit(40.0, 240.0, projectBpm.load());
    operation->view.status = "Load samples and program steps. Render creates 13 aligned stereo 96 kHz / float32 stems. "
                             "Draft stays available while Studio is open. Save pattern to keep it between launches.";
    instrumentalOperation = operation;
    if (!embeddedInstrumental) embeddedInstrumental = std::make_unique<instrumental::InstrumentalPanel>();
    auto* panel = static_cast<instrumental::InstrumentalPanel*>(embeddedInstrumental.get());
    juce::Component::SafePointer<MainComponent> safe(this);
    panel->onPlanChanged = [safe](instrumental::RenderPlan plan) { if (safe) safe->instrumentalPlanChanged(plan); };
    panel->onLoadSample = [safe](int lane) { if (safe) safe->instrumentalChooseSample(lane); };
    panel->onRender = [safe](instrumental::RenderPlan plan) { if (safe) safe->instrumentalRender(plan); };
    panel->onSavePattern = [safe](instrumental::RenderPlan plan) { if (safe) safe->instrumentalSavePattern(plan); };
    panel->onLoadPattern = [safe] { if (safe) safe->instrumentalLoadPattern(); };
    panel->onApply = [safe] { if (safe) safe->instrumentalApply(); };
    panel->onCancel = [safe] { if (safe) safe->instrumentalCancel(); };
    panel->setPlan(operation->plan); panel->setSampleLabels({}); panel->setViewState(operation->view);
    showUtility(*embeddedInstrumental, "INSTRUMENTAL BUILDER / 13 LANES / 32 STEPS");
}
void MainComponent::instrumentalPlanChanged(const instrumental::RenderPlan& plan) {
    auto operation = instrumentalOperation;
    if (!operation || operation->running || instrumentalChoosing) return;
    // Only controls may change via the panel. Sample identities stay host-owned.
    auto next = plan;
    for (std::size_t lane = 0; lane < next.lanes.size(); ++lane)
        next.lanes[lane].sample = operation->plan.lanes[lane].sample;
    if (sameControls(next, operation->plan)) return;
    operation->plan = std::move(next); operation->renderedValid = false; operation->stageVerified = false;
    operation->view.canApply = false;
    operation->view.status = "Pattern changed. Render again before staging. Save pattern to keep it between launches.";
}
void MainComponent::instrumentalCancel() {
    auto operation = instrumentalOperation; if (!operation) return;
    if (instrumentalChoosing) {
        ++instrumentalRequest; instrumentalChoosing = false;
        // This is a panel command, never the chooser's own callback.
        instrumentalPicker.reset(); operation->view.status = "File selection cancelled; current pattern is unchanged.";
    } else if (operation->running) {
        operation->cancel.store(true); operation->view.status = "Cancelling background work; existing files are retained...";
    }
}

void MainComponent::instrumentalChooseSample(int lane) {
    auto operation = instrumentalOperation;
    if (!operation || operation->running || instrumentalChoosing || lane < 0 || lane >= instrumental::kLaneCount) return;
    instrumentalChoosing = true; const auto request = ++instrumentalRequest;
    instrumentalPicker = std::make_unique<juce::FileChooser>("Choose sample: mono/stereo, up to 120 seconds and 64 MiB decoded", juce::File(), "*.wav;*.aif;*.aiff;*.flac");
    juce::Component::SafePointer<MainComponent> safe(this); std::weak_ptr<InstrumentalOperation> expected(operation);
    instrumentalPicker->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [safe, expected, request, lane](const juce::FileChooser& chooser) {
            if (!safe || request != safe->instrumentalRequest) return;
            safe->instrumentalChoosing = false; auto state = expected.lock();
            if (!state || state != safe->instrumentalOperation) return;
            const auto selectedFile = chooser.getResult(); if (selectedFile == juce::File()) return;
            if (safe->closePhase != ClosePhase::idle || safe->nativeResourceLoadBusy() || safe->playing()
                || safe->vocals.recorder.busy() || safe->sessionWorker->isBusy() || safe->exportWorker->isBusy()
                || safe->importer->hasPendingWork()) {
                state->view.status = "Finish pending work and stop transport before loading a sample."; return;
            }
            const auto snapshot = state->plan; const auto identities = state->identities;
            state->work = InstrumentalOperation::Work::sample; state->view.status = "Hashing and decoding the sample in the background...";
            submitInstrumental(*safe->instrumentalPool, state, [snapshot, identities, selectedFile, lane](InstrumentalOperation& job) {
                auto identity = files::identify(selectedFile, job.cancel);
                auto candidate = snapshot;
                const auto old = candidate.lanes[std::size_t(lane)].sample;
                const auto oldBytes = old ? (old->left.size() + old->right.size()) * sizeof(float) : 0;
                files::demand(files::decodedBytes(candidate) - oldBytes + files::decodedBytes(identity) <= files::planBudget,
                              "Loaded samples would exceed the 192 MiB pattern budget");
                candidate.lanes[std::size_t(lane)].sample = files::decode(identity, job.cancel);
                job.completedPlan = std::move(candidate); job.completedIdentities = identities;
                job.completedIdentities[std::size_t(lane)] = std::move(identity); job.progress.store(1);
            });
        });
}
void MainComponent::instrumentalRender(const instrumental::RenderPlan& plan) {
    auto operation = instrumentalOperation;
    if (!operation || operation->running || instrumentalChoosing) return;
    instrumentalPlanChanged(plan);
    instrumentalChoosing = true; const auto request = ++instrumentalRequest;
    instrumentalPicker = std::make_unique<juce::FileChooser>("Choose parent folder for a new 13-stem render; existing files are never replaced", juce::File::getSpecialLocation(juce::File::userDocumentsDirectory), "");
    juce::Component::SafePointer<MainComponent> safe(this); std::weak_ptr<InstrumentalOperation> expected(operation);
    instrumentalPicker->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
        [safe, expected, request](const juce::FileChooser& chooser) {
            if (!safe || request != safe->instrumentalRequest) return;
            safe->instrumentalChoosing = false; auto state = expected.lock();
            if (!state || state != safe->instrumentalOperation) return;
            const auto folder = chooser.getResult(); if (folder == juce::File()) return;
            if (safe->closePhase != ClosePhase::idle || safe->nativeResourceLoadBusy() || safe->playing()
                || safe->vocals.recorder.busy() || safe->sessionWorker->isBusy() || safe->exportWorker->isBusy()
                || safe->importer->hasPendingWork()) {
                state->view.status = "Finish pending work and stop transport before rendering."; return;
            }
            const auto snapshot = state->plan; const auto identities = state->identities;
            state->work = InstrumentalOperation::Work::render; state->renderedValid = state->stageVerified = false;
            state->attemptedDirectory = juce::File(); state->completedRender = {}; state->renderedBpm = snapshot.bpm;
            state->view.status = "Rendering 13 exact-length 96 kHz / float32 stems. Maximum combined output: 4 GiB...";
            submitInstrumental(*safe->instrumentalPool, state, [snapshot, identities, folder](InstrumentalOperation& job) {
                job.completedRender = files::render(snapshot, identities, folder, job.cancel, job.progress, &job.attemptedDirectory);
            });
        });
}
void MainComponent::instrumentalSavePattern(const instrumental::RenderPlan& plan) {
    auto operation = instrumentalOperation;
    if (!operation || operation->running || instrumentalChoosing) return;
    // Synchronize pending UI controls without invalidating an identical render.
    instrumentalPlanChanged(plan);
    auto snapshot = operation->plan;
    // A chooser can outlive page closure. Its callback retains controls only,
    // never the last sample reference (which would free PCM on the UI thread).
    for (auto& lane : snapshot.lanes) lane.sample.reset();
    instrumentalChoosing = true; const auto request = ++instrumentalRequest;
    instrumentalPicker = std::make_unique<juce::FileChooser>("Save a new pattern JSON; occupied names receive a unique suffix",
        juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("HEM-instrumental-pattern.json"), "*.json");
    juce::Component::SafePointer<MainComponent> safe(this); std::weak_ptr<InstrumentalOperation> expected(operation);
    instrumentalPicker->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles,
        [safe, expected, request, snapshot](const juce::FileChooser& chooser) {
            if (!safe || request != safe->instrumentalRequest) return;
            safe->instrumentalChoosing = false; auto state = expected.lock();
            if (!state || state != safe->instrumentalOperation) return;
            auto target = chooser.getResult(); if (target == juce::File()) return;
            if (!target.hasFileExtension("json")) target = target.withFileExtension("json");
            if (safe->closePhase != ClosePhase::idle || safe->nativeResourceLoadBusy()) return;
            const auto identities = state->identities;
            state->work = InstrumentalOperation::Work::savePattern; state->completedFile = juce::File();
            state->view.status = "Saving a new pattern manifest; sample WAVs remain at their original locations...";
            submitInstrumental(*safe->instrumentalPool, state, [snapshot, identities, target](InstrumentalOperation& job) {
                for (const auto& source : identities) if (source.present()) files::verify(source, job.cancel);
                job.completedFile = files::unusedPatternName(target);
                files::writeJson(job.completedFile, files::patternJson(snapshot, identities), job.cancel); job.progress.store(1);
            });
        });
}
void MainComponent::instrumentalLoadPattern() {
    auto operation = instrumentalOperation;
    if (!operation || operation->running || instrumentalChoosing) return;
    instrumentalChoosing = true; const auto request = ++instrumentalRequest;
    instrumentalPicker = std::make_unique<juce::FileChooser>("Load an Instrumental Builder pattern JSON or render manifest", juce::File(), "*.json");
    juce::Component::SafePointer<MainComponent> safe(this); std::weak_ptr<InstrumentalOperation> expected(operation);
    instrumentalPicker->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [safe, expected, request](const juce::FileChooser& chooser) {
            if (!safe || request != safe->instrumentalRequest) return;
            safe->instrumentalChoosing = false; auto state = expected.lock();
            if (!state || state != safe->instrumentalOperation) return;
            const auto selectedFile = chooser.getResult(); if (selectedFile == juce::File()) return;
            if (safe->closePhase != ClosePhase::idle || safe->nativeResourceLoadBusy() || safe->playing()
                || safe->vocals.recorder.busy() || safe->sessionWorker->isBusy() || safe->exportWorker->isBusy()
                || safe->importer->hasPendingWork()) {
                state->view.status = "Finish pending work and stop transport before loading a pattern."; return;
            }
            state->work = InstrumentalOperation::Work::loadPattern;
            state->view.status = "Validating pattern schema and sample hashes, then decoding in the background...";
            submitInstrumental(*safe->instrumentalPool, state, [selectedFile](InstrumentalOperation& job) {
                auto loaded = files::loadPattern(selectedFile, job.cancel, job.progress);
                job.completedPlan = std::move(loaded.plan); job.completedIdentities = std::move(loaded.identities);
            });
        });
}
void MainComponent::instrumentalApply() {
    auto operation = instrumentalOperation;
    if (!operation || operation->running || instrumentalChoosing || !operation->renderedValid) return;
    if (closePhase != ClosePhase::idle || nativeResourceLoadBusy() || externalLoadPending || playing()
        || vocals.recorder.busy() || sessionWorker->isBusy() || exportWorker->isBusy() || importer->hasPendingWork()) {
        operation->view.status = "Stop transport and finish pending work before staging. Completed stems remain in "
                                + operation->rendered.directory.getFullPathName(); return;
    }
    for (const auto& track : bank->tracks) if (track.loaded) {
        operation->view.status = "Staging requires all 13 mixer tracks to be empty. Keep these exports and import them manually into your chosen session: "
                                + operation->rendered.directory.getFullPathName(); return;
    }
    if (!operation->stageVerified) {
        const auto identities = operation->rendered.identities;
        operation->work = InstrumentalOperation::Work::verifyStage;
        operation->view.status = "Verifying all 13 completed stem hashes before staging; no mixer changes yet...";
        submitInstrumental(*instrumentalPool, operation, [identities](InstrumentalOperation& job) {
            for (std::size_t lane = 0; lane < identities.size(); ++lane) {
                files::verify(identities[lane], job.cancel);
                job.progress.store(double(lane + 1) / double(identities.size()), std::memory_order_relaxed);
            }
        }); return;
    }
    juce::StringArray paths; for (const auto& file : operation->rendered.files) paths.add(file.getFullPathName());
    const auto tempo = operation->renderedBpm;
    operation->stageVerified = false; // Every later staging attempt rechecks the output hashes.
    operation->view.status = "Stems sent to the importer. Use APPLY or PLAY when ready; this draft remains available. Exports: "
                             + operation->rendered.directory.getFullPathName();
    // Explicit Stage only prepares the existing importer's batch. It never calls
    // applyPreparedBatch. Final Apply/Play rechecks the protected empty targets.
    closeInstrumentalOperation(); showShellPage(ShellPage::home);
    importer->submitFiles(paths, 0, true, tempo);
    message.setText("13 instrumental stems are staging. Use importer APPLY or PLAY when ready; project tempo will become "
                    + juce::String(tempo, 2) + " BPM. Occupied tracks cannot be replaced.", juce::dontSendNotification);
}
void MainComponent::pollInstrumentalOperation() {
    auto operation = instrumentalOperation; if (!operation) return;
    auto* panel = dynamic_cast<instrumental::InstrumentalPanel*>(embeddedInstrumental.get()); if (!panel) return;
    if (operation->running && operation->done.load(std::memory_order_acquire)
        && (!instrumentalPool || instrumentalPool->getNumJobs() == 0)) {
        operation->running = false; operation->view.busy = false;
        if (operation->cancel.load() && operation->error.isEmpty())
            operation->error = "Cancelled before publication; current pattern and mixer are unchanged. Completed files are retained.";
        if (operation->error.isNotEmpty()) {
            // A late cancel can arrive after decoding finished. Retire that
            // unpublished payload on the worker even if the draft stays hidden.
            operation->retiredPlans.push_back(std::move(operation->completedPlan));
            operation->view.status = operation->error;
            if (operation->work == InstrumentalOperation::Work::render && operation->attemptedDirectory != juce::File())
                operation->view.status += " Incomplete files retained in " + operation->attemptedDirectory.getFullPathName();
            if (operation->work == InstrumentalOperation::Work::savePattern && operation->completedFile != juce::File())
                operation->view.status += " Any private partial JSON is retained beside " + operation->completedFile.getFullPathName();
        } else {
            switch (operation->work) {
                case InstrumentalOperation::Work::sample:
                case InstrumentalOperation::Work::loadPattern:
                    // Keep old payloads alive across BOTH host and panel swaps.
                    operation->retiredPlans.push_back(std::move(operation->plan));
                    operation->plan = std::move(operation->completedPlan);
                    operation->identities = std::move(operation->completedIdentities);
                    operation->renderedValid = operation->stageVerified = false;
                    panel->setPlan(operation->plan); panel->setSampleLabels(sampleLabels(operation->identities));
                    operation->view.status = "Samples verified and loaded. Draft is retained while Studio is open; save pattern to keep it between launches."; break;
                case InstrumentalOperation::Work::savePattern:
                    operation->view.status = "Pattern saved: " + operation->completedFile.getFullPathName()
                        + ". Keep the referenced original samples available."; break;
                case InstrumentalOperation::Work::render:
                    operation->rendered = std::move(operation->completedRender); operation->renderedValid = true;
                    operation->view.status = "13 verified stereo 96 kHz / float32 stems and pattern manifest saved: "
                        + operation->rendered.directory.getFullPathName() + ". Stage only into an empty mixer; final APPLY sets the pattern tempo."; break;
                case InstrumentalOperation::Work::verifyStage:
                    operation->stageVerified = true; instrumentalApply();
                    if (instrumentalOperation != operation) return;
                    break;
            }
        }
        // Reclaim replaced or cancelled sample payloads promptly even if this
        // cached draft is hidden indefinitely. The panel released its old refs.
        if (instrumentalPool && !operation->retiredPlans.empty()) {
            auto retired = std::move(operation->retiredPlans);
            auto cleanup = std::make_unique<InstrumentalJob>([retired = std::move(retired)]() mutable { retired.clear(); });
            instrumentalPool->addJob(cleanup.get(), true); cleanup.release();
        }
    }
    bool empty = true; for (const auto& track : bank->tracks) empty = empty && !track.loaded;
    operation->view.busy = operation->running || instrumentalChoosing;
    operation->view.progress = operation->progress.load(std::memory_order_relaxed);
    operation->view.canRender = !operation->view.busy && closePhase == ClosePhase::idle;
    operation->view.canApply = !operation->view.busy && operation->renderedValid && empty && !playing()
        && !vocals.recorder.busy() && !sessionWorker->isBusy() && !exportWorker->isBusy() && !importer->hasPendingWork();
    panel->setViewState(operation->view);
}
