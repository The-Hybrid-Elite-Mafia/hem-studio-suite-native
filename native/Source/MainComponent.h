#pragma once
#include "Systems/DeviceSettingsPanel.h"
#include "Systems/OfflineMixPanel.h"
#include <JuceHeader.h>
#include "SessionState.h"
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
#include "Plugins/ExternalPluginHost.h"

class MainComponent final:public juce::AudioAppComponent,private juce::Timer {
public:
 MainComponent();~MainComponent() override;
 enum class CloseChoice {cancel,discard,save};
 void requestClose(std::function<void()> approved);
 bool hasUnsavedSessionChanges();
 void prepareToPlay(int,double) override;
 void getNextAudioBlock(const juce::AudioSourceChannelInfo&) override;
 void releaseResources() override;
 void paint(juce::Graphics&) override;void resized() override;
private:
 friend struct MainComponentTestAccess;
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
 ShellPage shellPage=ShellPage::home;
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
 void showSettings();void showRack();void openPlugin(int);void moveSlot(int,int);
 double sessionPosition() const;
 void pauseAll();
 void playAll();void stopAll(bool);void seekAll(double);bool playing() const;
 void selectTrack(int);void configureKnob(juce::Slider&,std::atomic<float>&,const juce::String&);
 juce::Rectangle<int> map(float,float,float,float) const;
 hem::TrackEngine& current(){return bank->tracks[size_t(selectedTrack)];}
 HardwareLookAndFeel look,overlayLook{true};juce::Image faceplate,ghostBrand;juce::Rectangle<float> face;
 juce::Slider inputKnob,outputKnob,trackTrim;
 juce::TextButton play,stop,rewind,forward,loop,file,settings;
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
 hem::ModuleProcessor* editedProcessor=nullptr;int editedTrack=-1,editedSlot=-1;
 int hardwareOutputs=2;
 std::unique_ptr<hem::DeviceSettingsPanel> deviceSettings;
 int selected=0,selectedTrack=0;bool looping=false;
 juce::AudioFormatManager formats;juce::TimeSliceThread diskThread{"HEM audio reader"};
 std::unique_ptr<hem::TrackBank> bank=std::make_unique<hem::TrackBank>();
 hem::CoreMasterStage masterLimiter;
 hem::AudioMeters outputMeters;
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
 hem::Ramp inputGain,outputGain;std::vector<float> inputValues;
 juce::AudioBuffer<float> sum,listeningSource;
 JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};
