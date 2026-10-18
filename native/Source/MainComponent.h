#pragma once
#include "UI/ButtonLight.h"
#include "Systems/DeviceSettingsPanel.h"
#include "Systems/NativeAudioDeviceHost.h"
#include "Systems/OfflineMixPanel.h"
#include <JuceHeader.h>
#include "SessionState.h"
#include "TrackRenderScheduler.h"
#include "CoreMasterStage.h"
#include "Systems/CalibratedMeterView.h"
#include "Systems/PresetManager.h"
#include "Systems/AnalysisWorker.h"
#include "Systems/AnalysisView.h"
#include "Systems/MonitorProfile.h"
#include "Systems/VocalTrackingPanel.h"
#include "StemFileImporter.h"
#include "StudioWindow.h"
#include "Plugins/PluginRegistry.h"
#include "UI/NativeWorkspace.h"
#include "UI/UtilityPanelSizing.h"
#include "UI/PhysicalValueSlider.h"
#include "UI/PhotorealisticLookAndFeel.h"
#include "UI/ApprovedFaceplates.h"
#include "Plugins/ExternalPluginHost.h"
#include <mutex>

namespace hem::spectral { struct Selection; }
namespace hem::instrumental { struct RenderPlan; }

class MainComponent final:public juce::Component,public juce::AudioSource,private juce::Timer {
public:
 explicit MainComponent(bool startAudioDevice=true);~MainComponent() override;
 enum class CloseChoice {cancel,discard,save};
 void requestClose(std::function<void()> approved);
 bool hasUnsavedSessionChanges();
 void prepareToPlay(int,double) override;
 void getNextAudioBlock(const juce::AudioSourceChannelInfo&) override;
 void releaseResources() override;
 void paint(juce::Graphics&) override;void resized() override;
private:
 hem::NativeAudioDeviceHost deviceManager{*this};
 std::recursive_mutex graphControlMutex;unsigned graphControlDepth=0;
 std::atomic<unsigned> callbackAdmission{0}; // ACTIVE=1, CLOSED=2; callback never waits.
 std::atomic<bool> controlGapPending{false},faultResumeAtCurrent{false};
 std::atomic<uint64_t> controlSilencedCallbacks{0};
 std::atomic<double> callbackSampleRate{hem::defaultSampleRate};
 void latchControlGap()noexcept;
 hem::TrackRenderScheduler trackScheduler;
 // A timed-out batch pins these members until control-thread quiescence.
 struct TrackRenderContext {hem::TrackBank* bank=nullptr;const float* gains=nullptr;double rate=0,bpm=120;int frames=0;bool solo=false;};
 TrackRenderContext trackRenderContext;
 std::atomic<uint64_t> trackRenderCompleted{0},trackRenderFaults{0};
 std::atomic<uint64_t> trackRenderDeadlineMisses{0},trackRenderSilencedCallbacks{0};
 bool trackRenderRecovered=false;
 std::atomic<uint64_t> transportDropouts{0};
 std::array<double,13> callbackSourcePositions{};
 struct TransportRequest {std::array<double,13> positions{};bool play=false,loop=false,explicitPlay=false,recovery=false;unsigned generation=0;};
 struct TransportJob;std::unique_ptr<TransportJob> transportJob;
 std::vector<std::unique_ptr<TransportJob>> retiredTransportJobs;
 std::optional<TransportRequest> nextTransportRequest;
 void requestTransport(TransportRequest);
 void pollTransport();void cancelTransport();bool transportBusy()const;
 juce::int64 trackRenderTestBudget=0; // Friend harness only; production uses frame deadline.
 class GraphControlGuard {
  MainComponent& host;
 public:explicit GraphControlGuard(MainComponent&);~GraphControlGuard();
  GraphControlGuard(const GraphControlGuard&)=delete;GraphControlGuard& operator=(const GraphControlGuard&)=delete;
 };
 void recoverTrackRenderFault();
 friend struct MainComponentTestAccess;
 struct ReferenceOperation;std::shared_ptr<ReferenceOperation> referenceOperation;
 std::atomic<ReferenceOperation*> referenceAuditionAudio{nullptr};
 std::unique_ptr<juce::FileChooser> referencePicker;bool referenceChoosing=false;unsigned referenceRequest=0;
 bool referenceOperationBusy() const;
 juce::String referenceStatus;void setReferenceStatus(const juce::String&);
 void referenceAction(int action,int track,int slot,hem::ModuleProcessor* expected);
 juce::Result startReferenceWork(int mode,const juce::File&,int track,int slot,hem::ModuleProcessor* expected);
 void bindReferenceEditor(hem::ReferenceMatchProcessor&,int,int);
 void pollReferenceOperation();void cancelReferenceOperation();void cancelReferenceCaptures();
 void renderReferenceAudition(juce::AudioBuffer<float>&,int);
 struct SpectralOperation;struct SpectralHistory;
 std::shared_ptr<SpectralOperation> spectralOperation;
 std::atomic<SpectralOperation*> spectralAudio{nullptr};
 std::unique_ptr<juce::ThreadPool> spectralPool;
 std::array<std::shared_ptr<SpectralHistory>,13> spectralHistory;
 std::unique_ptr<juce::Component> embeddedSpectral;
 void openSpectralOperation();void pollSpectralOperation();void closeSpectralOperation();void shutdownSpectralOperations();
 bool spectralOperationBusy()const;void invalidateSpectralAudio()noexcept;
 bool spectralPreviewReady(int frames)const noexcept;void renderSpectralPreview(juce::AudioBuffer<float>&,int);
 void spectralPreview(const hem::spectral::Selection&);void spectralAudition(int);void spectralApply(bool undo=false);void spectralSelectionChanged();
 struct InstrumentalOperation;
 std::shared_ptr<InstrumentalOperation> instrumentalOperation;
 std::unique_ptr<juce::ThreadPool> instrumentalPool;
 std::unique_ptr<juce::Component> embeddedInstrumental;
 std::unique_ptr<juce::FileChooser> instrumentalPicker;
 bool instrumentalChoosing=false;unsigned instrumentalRequest=0;
 void openInstrumentalOperation();void pollInstrumentalOperation();void closeInstrumentalOperation();void shutdownInstrumentalOperations();
 bool instrumentalOperationBusy()const;
 void instrumentalPlanChanged(const hem::instrumental::RenderPlan&);void instrumentalRender(const hem::instrumental::RenderPlan&);
 void instrumentalSavePattern(const hem::instrumental::RenderPlan&);void instrumentalLoadPattern();
 void instrumentalChooseSample(int);void instrumentalApply();void instrumentalCancel();
 struct ClipOperation;std::shared_ptr<ClipOperation> clipOperation;
 std::atomic<ClipOperation*> clipAudio{nullptr};
 struct ClipHistory;std::shared_ptr<ClipHistory> clipHistory;
 enum class EditorOpenIntent { navigation, replacement };
 void openClipOperation(int module,EditorOpenIntent=EditorOpenIntent::navigation);void pollClipOperation();void closeClipOperation();
 bool clipOperationBusy() const;void renderClipPreview(juce::AudioBuffer<float>&,int);void clipCommand(int command);
 void invalidateClipAudio() noexcept;
 bool clipPreviewReady(int frames)const noexcept;
 void captureClipEdits(hem::SessionSnapshot&) const;void restoreClipEdits(const hem::SessionSnapshot&);
 struct DrumSampleJob;
 std::unique_ptr<DrumSampleJob> drumSampleJob;
 std::unique_ptr<juce::FileChooser> drumSamplePicker;
 bool drumSampleChoosing=false;unsigned drumSampleRequest=0;
 bool nativeResourceLoadBusy() const;
 void chooseDrumSample(int layer,int track,int slot,hem::ModuleProcessor* expected);
 juce::Result startDrumSampleLoad(const juce::File&,int layer,int track,int slot,hem::ModuleProcessor* expected);
 void pollDrumSampleLoad();void cancelDrumSampleLoad();
 // Declared before worker/bank owners, and therefore destroyed after them.
 hem::ExternalPluginHost externalHost;
 bool externalLoadPending=false;unsigned externalLoadRequest=0;
 juce::String lastExternalDiagnostic;
 bool hasExternalInserts() const;
 hem::SessionSnapshot captureSession(bool includeExternalState=true);
 juce::Result stageExternalSession(hem::TrackBank&,const hem::SessionSnapshot&,hem::SessionWorker::Context);
 juce::Result installExternal(const hem::ExternalPluginState&,int trackIndex,int slotIndex,unsigned expectedGeneration);
 void chooseExternalPlugin();void reloadExternalPlugin();
 void prepareOfflineSession();
 juce::Result saveSessionFile(const juce::File&);
 juce::Result loadSessionFile(const juce::File&);
 juce::Result clearTrackSources(bool all);
 juce::Result resetSessionConfirmed();
 void closePluginEditor();
 void rememberEditorOrigin(EditorOpenIntent);
 void refreshTrackTrim();
 juce::ValueTree sessionFingerprint(hem::SessionSnapshot) const;
 void sessionFinished(bool saving,const juce::File&,const juce::Result&);
 void resolveCloseChoice(CloseChoice);
 void pumpCloseRequest();
 void abortClose(const juce::String&);
 bool activeCloseWork() const;
 enum class ClosePhase {idle,prompt,waiting,choosingSave,saving,complete};
 ClosePhase closePhase=ClosePhase::idle;
 bool closeNeedsSave=false,closeWasRecording=false;
 std::function<void()> closeApproved;
 using ClosePrompt=std::function<void(bool dirty,bool active,std::function<void(CloseChoice)>)>;
 ClosePrompt closePrompt;
 juce::File currentSessionFile;
 juce::ValueTree lastSavedSessionState,pendingSavedSessionState;
 enum class ShellPage {home,mixer,plugin,vault,routing,settings,utility};
 ShellPage shellPage=ShellPage::home, editorReturnPage=ShellPage::home;
 void showShellPage(ShellPage);
 void showUtility(juce::Component&,const juce::String&);
 void showAnalysis();
 void requestNewSession();
 std::unique_ptr<hem::NativeWorkspace> workspace;
 std::array<juce::TextButton,6> navigation;
 juce::TextButton unloadSourcesButton{"UNLOAD"},newSessionButton{"NEW"};
 juce::Viewport utilityViewport;
 juce::Label utilityTitle,statusBar;
 juce::Component* utilityContent=nullptr;
 juce::Point<int> utilityMinimum{600,350};
 hem::ui::UtilityPanelSizing utilityPanelSizing;
 std::unique_ptr<juce::Component> embeddedAnalysis,embeddedMonitor,embeddedVocal,embeddedExport;
 static constexpr int trackCount=13;
 void timerCallback() override;
 juce::Result commitImport(hem::StemFileImporter::Batch&);
 std::unique_ptr<hem::StemFileImporter> importer;
 void selectSlot(int);void installSelected(int);void openAudio();void loadAudio(const juce::File&,int);
 void showMonitor();
 void showExport();
 juce::Result startExport(const juce::File&,const hem::OfflineMixOptions&);
 std::unique_ptr<hem::OfflineMixWorker> exportWorker;
 std::unique_ptr<StudioWindow> exportWindow;
 juce::TextButton exportButton{"EXPORT MIX"};
 void showSettings();void showRack();void openPlugin(int,EditorOpenIntent=EditorOpenIntent::navigation);void moveSlot(int,int);
 double sessionPosition() const;
 void pauseAll();
 void playAll();void stopAll(bool);void seekAll(double);bool playing() const;
 void selectTrack(int);void configureKnob(juce::Slider&,std::atomic<float>&,const juce::String&);
 juce::Rectangle<int> map(float,float,float,float) const;
 hem::TrackEngine& current(){return bank->tracks[size_t(selectedTrack)];}
 HardwareLookAndFeel look;hem::ui::PhotorealisticLookAndFeel overlayLook;juce::Image faceplate;juce::Rectangle<float> face;
 // The approved printed menu labels are the visuals; these real controls add
 // focus/hover feedback and accessibility without duplicating their artwork.
 class ArtworkMenuButton final:public juce::TextButton {
  void paintButton(juce::Graphics& g,bool over,bool down) override {hem::ui::drawButtonLight(g,getLocalBounds().toFloat().reduced(1),getToggleState(),down,over,isEnabled(),3);}
 };
 ArtworkMenuButton file,headerRack,headerHome,headerMenu;
 hem::ui::PhysicalValueSlider inputKnob,outputKnob;juce::Slider trackTrim;
 juce::TextButton play,stop,rewind,forward,loop,settings;
 std::array<juce::TextButton,8> slotButtons;
 juce::Component bay;juce::ComboBox moduleMenu,trackMenu;
 juce::TextButton insert{"INSERT / REPLACE"},remove{"REMOVE"},bypass{"BYPASS"},rackButton{"INSERT RACK"},openButton{"OPEN PLUGIN"},mute{"MUTE"},solo{"SOLO"},monitorButton{"MONITOR EQ"},pauseButton{"PAUSE"},outputButton{"AUDIO DEVICE / I/O"};
 juce::Label title,track,telemetry,inValue,outValue,message;
 std::unique_ptr<StudioWindow> pluginWindow,rackWindow,monitorWindow;
 hem::MonitorProcessor monitorProcessor;
 std::atomic<bool> monitorRouteReady{false};
 juce::String lastOutputDevice;
 juce::AudioBuffer<float> monitorBuffer,monitorView;
 juce::MidiBuffer monitorMidi;
 RackView* rackView=nullptr;
 enum class EditorKind {none,insert,monitor};
 EditorKind editorKind=EditorKind::none;
 hem::ModuleProcessor* editedProcessor=nullptr;int editedTrack=-1,editedSlot=-1;
 bool trackTrimGesture=false;
 int hardwareOutputs=2;
 std::unique_ptr<hem::DeviceSettingsPanel> deviceSettings;
 int selected=0,selectedTrack=0;bool looping=false;
 juce::AudioFormatManager formats;juce::TimeSliceThread diskThread{"HEM audio reader"};
 std::unique_ptr<hem::TrackBank> bank=std::make_unique<hem::TrackBank>();
 hem::CoreMasterStage masterLimiter;
 hem::AudioMeters inputMeters,outputMeters;
 hem::CalibratedMeterView inputMeterView{inputMeters,"INPUT / dBFS","POST-INSERT MIX"};
 hem::CalibratedMeterView outputMeterView{outputMeters};
 hem::LiveVocalManager vocals;
 std::unique_ptr<StudioWindow> vocalWindow;
 juce::TextButton vocalButton{"VOCAL TRACKING"};
 void showVocals();
 double callbackTimeline() const;
 hem::MonitoringRouter monitoringRouter;
 hem::AnalysisWorker analyser;
 std::unique_ptr<StudioWindow> analysisWindow;
 std::unique_ptr<hem::MonitorProfileLoader> profileLoader;
 juce::TextButton analysisButton{"ANALYSIS"},calibrationButton{"LOAD CALIBRATION"},routeButton{"DIRECT OUTPUT"};
 juce::String calibrationName{"No measurement profile loaded"};
 void openCalibration();
 std::unique_ptr<hem::SessionWorker> sessionWorker;
 juce::TextButton saveSessionButton{"SAVE SESSION"},loadSessionButton{"LOAD SESSION"};
 void chooseSession(bool save);
 juce::Result commitSession(std::unique_ptr<hem::TrackBank>&,const hem::SessionSnapshot&,hem::SessionWorker::Context);
 std::unique_ptr<juce::FileChooser> picker;juce::String deviceError;
 double sampleRate=hem::defaultSampleRate;int blockSize=256;unsigned generation=0;bool prepared=false;
 std::atomic<float> inputDb{0},outputDb{0},peak{0};
 std::atomic<double> projectBpm{120};juce::Slider tempoControl;juce::Label tempoLabel;
 std::atomic<bool> preparationFailed{false};std::atomic<double> failedPreparationRate{0};
 static_assert(std::atomic<double>::is_always_lock_free);
 hem::Ramp inputGain,outputGain;std::vector<float> inputValues;
 juce::AudioBuffer<float> sum,listeningSource;
 JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};






