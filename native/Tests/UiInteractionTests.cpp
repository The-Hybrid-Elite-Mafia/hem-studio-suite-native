#include "../Source/MainComponent.h"
#include "../Source/Instrumental/InstrumentalPanel.h"
#include <iostream>
#include <stdexcept>
namespace hem {struct NativeAudioDeviceHostTestAccess {static void add(NativeAudioDeviceHost& h,std::unique_ptr<juce::AudioIODeviceType> t){h.addDeviceTypeForTesting(std::move(t));}};}
// A genuinely open host device with no delivered callbacks. No hardware opens.
struct UiSilentDevice final:juce::AudioIODevice {
 UiSilentDevice():AudioIODevice("UI test output","UI test"){}
 juce::BigInteger in,out;bool opened=false;double rate=48000;int block=1024;juce::AudioIODeviceCallback* callback=nullptr;
 juce::StringArray getOutputChannelNames()override{return {"Left","Right"};}juce::StringArray getInputChannelNames()override{return {};}
 juce::Array<double> getAvailableSampleRates()override{return {48000};}juce::Array<int> getAvailableBufferSizes()override{return {1024};}int getDefaultBufferSize()override{return 1024;}
 juce::String open(const juce::BigInteger& i,const juce::BigInteger& o,double r,int b)override{in=i;out=o;rate=r;block=b;opened=true;return {};}
 void close()override{stop();opened=false;}bool isOpen()override{return opened;}
 void start(juce::AudioIODeviceCallback* cb)override{callback=cb;cb->audioDeviceAboutToStart(this);}void stop()override{auto* old=callback;callback=nullptr;if(old)old->audioDeviceStopped();}
 bool isPlaying()override{return callback!=nullptr;}juce::String getLastError()override{return {};}
 int getCurrentBufferSizeSamples()override{return block;}double getCurrentSampleRate()override{return rate;}int getCurrentBitDepth()override{return 32;}
 juce::BigInteger getActiveOutputChannels()const override{return out;}juce::BigInteger getActiveInputChannels()const override{return in;}
 int getOutputLatencyInSamples()override{return 0;}int getInputLatencyInSamples()override{return 0;}
};
struct UiSilentType final:juce::AudioIODeviceType {
 UiSilentType():AudioIODeviceType("UI test"){}void scanForDevices()override{}juce::StringArray getDeviceNames(bool input=false)const override{return input?juce::StringArray{}:juce::StringArray{"UI test output"};}
 int getDefaultDeviceIndex(bool)const override{return 0;}int getIndexOfDevice(juce::AudioIODevice*,bool)const override{return 0;}bool hasSeparateInputsAndOutputs()const override{return true;}
 juce::AudioIODevice* createDevice(const juce::String&,const juce::String&)override{return new UiSilentDevice;}
};
struct MainComponentTestAccess {
 static void stopTimer(MainComponent& a){a.stopTimer();}
 static void instrumentalDraftPersistence(MainComponent& a){
  a.openInstrumentalOperation();auto retained=a.instrumentalOperation;
  auto* panel=dynamic_cast<hem::instrumental::InstrumentalPanel*>(a.embeddedInstrumental.get());
  if(!retained||!panel)throw std::runtime_error("Builder did not open");
  auto pattern=panel->getPlan();pattern.bpm=93.5;pattern.repeats=2;pattern.lanes[3].steps=0x00010001u;
  panel->setPlan(pattern);panel->onPlanChanged(pattern);a.showShellPage(MainComponent::ShellPage::home);
  if(a.instrumentalOperation!=retained||a.utilityContent==panel)throw std::runtime_error("Builder navigation lost draft or failed to detach viewport");
  a.openInstrumentalOperation();
  if(a.instrumentalOperation!=retained||a.utilityContent!=panel||panel->getPlan().bpm!=93.5||panel->getPlan().repeats!=2||panel->getPlan().lanes[3].steps!=0x00010001u)throw std::runtime_error("Builder draft did not survive navigation/reopen");
  a.showShellPage(MainComponent::ShellPage::home);
 }

 // Prepared 2026-10-16; execution deferred to the user's PC.
 static void instrumentalImportProtection(MainComponent& a){
  auto& track=a.bank->tracks[0];const bool oldLoaded=track.loaded;const double oldTempo=a.projectBpm.load();auto* before=track.playback.get();track.loaded=true;
  hem::StemFileImporter::Batch batch;batch.context={a.sampleRate,a.blockSize,a.generation,a.looping};batch.firstTrack=0;batch.count=1;batch.requireEmptyTargets=true;batch.projectTempo=93.5;batch.stems[0]=std::make_unique<hem::StemPlayback>();
  const auto result=a.commitImport(batch);track.loaded=oldLoaded;
  if(result.wasOk()||!result.getErrorMessage().contains("empty tracks")||track.playback.get()!=before||a.projectBpm.load()!=oldTempo||!batch.stems[0])throw std::runtime_error("Instrumental import replaced or consumed an occupied target");
 }

 static void home(MainComponent& a){a.showShellPage(MainComponent::ShellPage::home);}
 static void vault(MainComponent& a){a.showShellPage(MainComponent::ShellPage::vault);}
 static void showExport(MainComponent& a){a.showExport();}
 static void rack(MainComponent& a){a.showRack();}
 static void monitor(MainComponent& a){a.showMonitor();}
 static int page(MainComponent& a){return int(a.shellPage);}
 static void replaceEditor(MainComponent& a){a.closePluginEditor();a.openPlugin(1,MainComponent::EditorOpenIntent::replacement);}
 static void clip(MainComponent& a){for(size_t i=0;i<hem::PluginRegistry::entries().size();++i)if(juce::String(hem::PluginRegistry::entries()[i].id)=="pitch-formant"){a.openClipOperation(int(i));return;}throw std::runtime_error("Pitch registry entry missing");}
 static bool clipOwned(MainComponent& a){return bool(a.clipOperation)||a.clipAudio.load()!=nullptr;}
 static void showAnalysis(MainComponent& a){a.showAnalysis();}
 static juce::Point<int> utilityMinimum(MainComponent& a){return a.utilityMinimum;}
 static juce::Point<int> utilitySize(MainComponent& a){return {a.utilityContent->getWidth(),a.utilityContent->getHeight()};}
 static void install(MainComponent& a,int module){a.selected=1;a.installSelected(module);}
 static void open(MainComponent& a){a.openPlugin(1);}
 static juce::Component& exportButton(MainComponent& a){return a.exportButton;}
 static juce::String status(MainComponent& a){return a.message.getText();}
 static hem::NativeWorkspace& workspace(MainComponent& a){return *a.workspace;}
 static bool installed(MainComponent& a){return a.current().chain.activeProcessor(1)!=nullptr;}
 static void openSilentDevice(MainComponent& a){hem::NativeAudioDeviceHostTestAccess::add(a.deviceManager,std::make_unique<UiSilentType>());auto error=a.deviceManager.setCurrentAudioDeviceType("UI test",false);if(error.isNotEmpty())throw std::runtime_error(error.toStdString());if(!a.deviceManager.getCurrentAudioDevice())throw std::runtime_error("test device absent");a.trackScheduler.latchFault();a.trackRenderRecovered=true;}
 static void queueStalled(MainComponent& a,int module){auto& c=a.current().chain;c.publish(1,c.stageNode(hem::createModule(module),a.sampleRate,a.blockSize,c.slotLatency()));if(!c.isBusy(1))throw std::runtime_error("test request not pending");}
 static void notReady(MainComponent& a,int module){a.prepared=false;a.installSelected(module);if(!a.message.getText().contains("not ready"))throw std::runtime_error("missing prerequisite feedback");a.prepared=true;}
};
static juce::Component* find(juce::Component& c,const juce::String& id){if(c.getComponentID()==id)return &c;for(auto* child:c.getChildren())if(auto* p=find(*child,id))return p;return nullptr;}
static void hit(juce::Component& root,juce::Component& target,const char* name){auto point=root.getLocalPoint(&target,target.getLocalBounds().getCentre());auto* got=root.getComponentAt(point);bool ok=got==&target||target.isParentOf(got);std::cout<<name<<" hit="<<ok<<" enabled="<<target.isEnabled()<<" visible="<<target.isVisible()<<" got="<<(got?got->getComponentID().toStdString():"null")<<std::endl;if(!ok)throw std::runtime_error(name);}
#include "EditorNavigationChecks.h"
static void utilitySizing(MainComponent& app){
 auto expectMinimum=[&](juce::Point<int> expected){if(MainComponentTestAccess::utilityMinimum(app)!=expected)throw std::runtime_error("Utility minimum changed after reopen/resize/switch");};
 app.setSize(1280,720);MainComponentTestAccess::showExport(app);expectMinimum({850,470});
 app.setSize(2560,1440);if(MainComponentTestAccess::utilitySize(app).x<=850)throw std::runtime_error("Export expansion fixture did not expand");
 MainComponentTestAccess::home(app);MainComponentTestAccess::showExport(app);expectMinimum({850,470});
 app.setSize(960,540);if(MainComponentTestAccess::utilitySize(app)!=juce::Point<int>{850,470})throw std::runtime_error("Export did not shrink back to its intrinsic minimum");
 MainComponentTestAccess::showAnalysis(app);expectMinimum({900,520});app.setSize(2560,1440);
 MainComponentTestAccess::showExport(app);expectMinimum({850,470});MainComponentTestAccess::showAnalysis(app);expectMinimum({900,520});
 for(int cycle=0;cycle<3;++cycle){app.setSize(960,540);MainComponentTestAccess::showExport(app);expectMinimum({850,470});app.setSize(2560,1440);MainComponentTestAccess::home(app);MainComponentTestAccess::showExport(app);expectMinimum({850,470});}
 MainComponentTestAccess::home(app);
}
int main(){try{juce::ScopedJuceInitialiser_GUI gui;auto owner=std::make_unique<MainComponent>(false);auto& app=*owner;MainComponentTestAccess::stopTimer(app);app.setVisible(true);
 int eq=-1;const auto& entries=hem::PluginRegistry::entries();for(int i=0;i<int(entries.size());++i)if(juce::String(entries[size_t(i)].id)=="dynamic-eq-8")eq=i;if(eq<0)throw std::runtime_error("EQ registry");
 MainComponentTestAccess::openSilentDevice(app);MainComponentTestAccess::instrumentalImportProtection(app);MainComponentTestAccess::instrumentalDraftPersistence(app);MainComponentTestAccess::install(app,eq);std::cout<<MainComponentTestAccess::status(app)<<std::endl;if(!MainComponentTestAccess::installed(app))throw std::runtime_error("Slot2 EQ not installed with open callback-silenced device");
 MainComponentTestAccess::queueStalled(app,eq);MainComponentTestAccess::install(app,eq);if(!MainComponentTestAccess::installed(app)||!MainComponentTestAccess::status(app).contains("ready"))throw std::runtime_error("stalled insert not recovered");MainComponentTestAccess::notReady(app,eq);
 for(auto size:{juce::Point<int>{960,540},juce::Point<int>{1280,720},juce::Point<int>{1920,1080},juce::Point<int>{2560,1440}}){app.setSize(size.x,size.y);std::cout<<"SIZE "<<size.x<<"x"<<size.y<<std::endl;
 MainComponentTestAccess::home(app);hit(app,MainComponentTestAccess::exportButton(app),"Home Export");
 MainComponentTestAccess::vault(app);auto* search=dynamic_cast<juce::TextEditor*>(find(app,"vault-search"));if(!search)throw std::runtime_error("search missing");hit(app,*search,"Vault search");std::cout<<"search wantsFocus="<<search->getWantsKeyboardFocus()<<" readonly="<<search->isReadOnly()<<std::endl;
 search->setText("Dynamic",true);search->onTextChange();if(MainComponentTestAccess::workspace(app).filteredRegistryIndices().empty())throw std::runtime_error("search filter");search->setText("",true);
 MainComponentTestAccess::open(app);auto* back=dynamic_cast<juce::Button*>(find(app,"plugin-expand"));if(!back)throw std::runtime_error("Back missing");hit(app,*back,"Plugin Back");auto action=back->onClick;action();if(MainComponentTestAccess::workspace(app).hasEditor())throw std::runtime_error("Back did not close editor");
 }
 editorNavigationChecks(app,eq);
 utilitySizing(app);
 std::cout<<"PASS component hit-routing at four client sizes, search filter, Back callback and utility resize/reopen minima; NOT OS mouse/keyboard/DPI acceptance"<<std::endl;return 0;
 }catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<std::endl;return 1;}}




