#include "../Source/MainComponent.h"
#include "../Source/Plugins/Editors.h"
#include <iostream>
#include <stdexcept>
#include <set>
#include "BandHostInteractionSetup.h"
#include "DiscreteControlChecks.h"
#include "../Source/Plugins/DelayEditors.h"
#if JUCE_LINUX
#include <execinfo.h>
#include <csignal>
#include <unistd.h>
#endif

struct MainComponentTestAccess {
 static void offline(MainComponent& host){host.deviceManager.closeAudioDevice();if(host.prepared)host.releaseResources();host.timerCallback();}
 static bool ready(MainComponent& host){return host.prepared&&host.sampleRate==48000&&host.blockSize==256&&!host.inputMeters.snapshot().ready&&!host.outputMeters.snapshot().ready;}
 static hem::TrackBank& bank(MainComponent& host){return *host.bank;}
 static void import(MainComponent& host,const juce::StringArray& files){host.importer->submitFiles(files);}
 static bool pending(MainComponent& host){return host.importer->hasPendingWork();}
 static bool sessionBusy(MainComponent& host){return host.sessionWorker->isBusy();}
 static bool exportBusy(MainComponent& host){return host.exportWorker->isBusy();}
 static juce::String exportResult(MainComponent& host){return host.exportWorker->result();}
 static void insertMenu(MainComponent& host,int choice){host.moduleMenu.setSelectedId(choice+1,juce::dontSendNotification);host.insert.onClick();}
 static void install(MainComponent& host,int module){host.installSelected(module);}
 static void tick(MainComponent& host){host.timerCallback();}
 static void open(MainComponent& host){host.openPlugin(0);}
 static void slot(MainComponent& host,int slot){host.selected=slot;host.workspace->refresh();}
 static int slot(MainComponent& host){return host.selected;}
 static void navigate(MainComponent& host,int page){host.navigation[size_t(page)].onClick();}
 static void showRack(MainComponent& host){host.rackButton.onClick();}
 static void showVocals(MainComponent& host){host.vocalButton.onClick();}
 static hem::VocalTrackingPanel& vocalPanel(MainComponent& host){return *dynamic_cast<hem::VocalTrackingPanel*>(host.embeddedVocal.get());}
 static juce::Slider& trim(MainComponent& host){return host.trackTrim;}
 static void move(MainComponent& host,int from,int to){host.moveSlot(from,to);}
 static bool monitorEditor(MainComponent& host){return host.editorKind==MainComponent::EditorKind::monitor&&host.editedProcessor==&host.monitorProcessor&&host.editedTrack==-1&&host.editedSlot==-1;}
 static bool clearEditorTarget(MainComponent& host){return host.editorKind==MainComponent::EditorKind::none&&host.editedProcessor==nullptr&&host.editedTrack==-1&&host.editedSlot==-1;}
 static void corruptInsertEditorIndices(MainComponent& host){host.editedTrack=-1;host.editedSlot=8;}
 static juce::Rectangle<int> mapped(MainComponent& host,float x,float y,float w,float h){return host.map(x,y,w,h);}
 static const juce::Image& faceplate(MainComponent& host){return host.faceplate;}
 static std::vector<juce::Component*> homeControls(MainComponent& host){std::vector<juce::Component*> result;for(auto* c:host.bay.getChildren())if(c!=&host.message)result.push_back(c);for(auto& c:host.slotButtons)result.push_back(&c);for(auto& c:host.navigation)result.push_back(&c);result.push_back(&host.unloadSourcesButton);result.push_back(&host.newSessionButton);result.push_back(&host.statusBar);return result;}
 static std::vector<juce::Component*> railControls(MainComponent& host){return {&host.inputKnob,&host.outputKnob,&host.rewind,&host.stop,&host.play,&host.pauseButton,&host.forward,&host.loop,&host.track,&host.telemetry,&host.inValue,&host.outValue,&host.tempoLabel,&host.tempoControl};}
 static void prepareManualMeterPass(MainComponent& host){host.inputMeters.prepare(48000);host.outputMeters.prepare(48000);host.hardwareOutputs=2;host.outputDb=-12;host.outputGain.reset(48000,hem::gain(-12));for(auto& track:host.bank->tracks)if(track.loaded)track.transport().start();}
 static hem::AudioMeters::Snapshot inputMeter(MainComponent& host){return host.inputMeters.snapshot();}
 static hem::AudioMeters::Snapshot outputMeter(MainComponent& host){return host.outputMeters.snapshot();}
 static void restoreOutput(MainComponent& host){host.outputDb=0;host.outputKnob.setValue(0,juce::dontSendNotification);}
 static bool hasEditor(MainComponent& host){return host.editedProcessor!=nullptr;}
 static hem::NativeWorkspace& workspace(MainComponent& host){return *host.workspace;}
 static bool chooserOpen(MainComponent& host){return host.picker!=nullptr;}
 static bool pluginPage(MainComponent& host){return host.shellPage==MainComponent::ShellPage::plugin;}
 static void home(MainComponent& host){host.showShellPage(MainComponent::ShellPage::home);}
 static hem::SessionSnapshot snapshot(MainComponent& host){return host.captureSession();}
 static juce::Result save(MainComponent& host,const juce::File& file){return host.saveSessionFile(file);}
 static juce::Result load(MainComponent& host,const juce::File& file){return host.loadSessionFile(file);}
 static juce::Result clear(MainComponent& host,bool all){return host.clearTrackSources(all);}
 static juce::Result reset(MainComponent& host){return host.resetSessionConfirmed();}
 static juce::Result render(MainComponent& host,const juce::File& file){hem::OfflineMixOptions o;o.tailSeconds=0;return host.startExport(file,o);}
 static juce::Result renderOptions(MainComponent& host,const juce::File& file,const hem::OfflineMixOptions& options){return host.startExport(file,options);}
 static void cancelExport(MainComponent& host){host.exportWorker->cancel();}
 static void play(MainComponent& host){host.playAll();}
 static bool playing(MainComponent& host){return host.playing();}
 static unsigned generation(MainComponent& host){return host.generation;}
 static juce::Result staleImport(MainComponent& host,hem::StemFileImporter::Batch& batch){return host.commitImport(batch);}
 static juce::TimeSliceThread& disk(MainComponent& host){return host.diskThread;}
 static void closeChoice(MainComponent& host,MainComponent::CloseChoice choice){host.closePrompt=[choice](bool,bool,std::function<void(MainComponent::CloseChoice)> resolved){resolved(choice);};}
 static bool closePending(MainComponent& host){return host.closePhase!=MainComponent::ClosePhase::idle;}
 static void saveTarget(MainComponent& host,const juce::File& file){host.currentSessionFile=file;}
 static juce::String status(MainComponent& host){return host.message.getText();}
 static juce::Slider& tempo(MainComponent& host){return host.tempoControl;}
 static juce::Label& tempoLabel(MainComponent& host){return host.tempoLabel;}
 static double bpm(MainComponent& host){return host.projectBpm.load();}
 static juce::Result slowExport(MainComponent& host,const juce::File& file){hem::OfflineMixOptions options;options.blockSize=1;options.tailSeconds=30;return host.startExport(file,options);}
};
namespace {
void require(bool b,const juce::String& message){if(!b)throw std::runtime_error(message.toStdString());}
juce::TextButton* button(juce::Component& parent,const juce::String& text){
 if(auto* b=dynamic_cast<juce::TextButton*>(&parent);b&&b->getButtonText()==text)return b;
 for(int i=0;i<parent.getNumChildComponents();++i)if(auto* found=button(*parent.getChildComponent(i),text))return found;
 return nullptr;
}
template<class Condition>void until(Condition done,const char* failure){
 for(int i=0;i<3000&&!done();++i){juce::Thread::sleep(5);juce::Timer::callPendingTimersSynchronously();}
 require(done(),failure);
}
juce::Component* findControl(juce::Component& parent,const juce::String& id){
 if(parent.getComponentID()==id)return &parent;
 for(auto* child:parent.getChildren())if(auto* found=findControl(*child,id))return found;
 return nullptr;
}
// Software-only fixture visibility: JUCE isShowing() additionally requires an
// OS desktop peer, unavailable in this sandbox. Require every actual ancestor
// through the visible real host, and retain separate root hit-target checks.
bool visibleWithin(const juce::Component& root,const juce::Component& child){
 for(auto* c=&child;c!=nullptr;c=c->getParentComponent()){if(!c->isVisible())return false;if(c==&root)return true;}return false;
}
juce::AudioProcessorEditor* activeEditor(juce::Component& parent){
 if(auto* editor=dynamic_cast<juce::AudioProcessorEditor*>(&parent))return editor;
 for(auto* child:parent.getChildren())if(auto* found=activeEditor(*child))return found;
 return nullptr;
}
struct GestureProbe final:juce::AudioProcessorParameter::Listener {
 juce::RangedAudioParameter& parameter;int begins=0,ends=0,changes=0;
 explicit GestureProbe(juce::RangedAudioParameter& p):parameter(p){parameter.addListener(this);}
 ~GestureProbe() override {parameter.removeListener(this);}
 void parameterValueChanged(int,float) override {++changes;}
 void parameterGestureChanged(int,bool starting) override {if(starting)++begins;else ++ends;}
 void clear(){begins=ends=changes=0;}
};
} // namespace
#include "HostParameterInteractionChecks.h"
namespace {
void verifyEditorInteractions(MainComponent& host){
 auto& workspace=MainComponentTestAccess::workspace(host);
 const auto& entries=hem::PluginRegistry::entries();
 require(entries.size()==hostParameterChecks::orderedIds.size(),"Independent ordered35 registry size mismatch");
 std::set<juce::String> visited;int gestures=0,previews=0;
 for(size_t module=0;module<entries.size();++module){
  require(juce::String(entries[module].id)==hostParameterChecks::orderedIds[module],"Independent ordered35 identity/order mismatch at "+juce::String(int(module)));
  require(entries[module].monitoringOnly==(juce::String(entries[module].id)=="monitor-eq"),"Monitoring-only contract changed");
  if(entries[module].monitoringOnly)continue; // separately covered by monitorOwnership's three routes
  visited.insert(entries[module].id);
  MainComponentTestAccess::install(host,int(module));MainComponentTestAccess::open(host);
  auto* editor=activeEditor(workspace);require(editor!=nullptr,"Installed module has no editor: "+juce::String(entries[module].id));
  juce::Component::SafePointer<juce::AudioProcessorEditor> observed(editor);
  auto* processor=MainComponentTestAccess::bank(host).tracks[0].chain.activeProcessor(0);require(processor!=nullptr,"Installed processor missing");
  const auto factory=hem::SessionState::captureParameters(*processor); // preserves actual insertion profile, not raw APVTS defaults
  auto* viewport=dynamic_cast<juce::Viewport*>(findControl(workspace,"plugin-editor-viewport"));require(viewport!=nullptr,"Missing embedded editor viewport");
  require(viewport->getScrollOnDragMode()==juce::Viewport::ScrollOnDragMode::never,"Parameter drag also scrolls editor");
  int moduleGestures=0;
  for(auto size:{juce::Point<int>(800,450),juce::Point<int>(1280,720),juce::Point<int>(1536,864),juce::Point<int>(1920,1080),juce::Point<int>(1673,940)}){
   host.setSize(size.x,size.y);
   for(bool expanded:{false,true}){
    workspace.setPluginExpanded(expanded);
    const bool preview=bool(editor->getProperties()["previewOnly"]);
    require(editor->isEnabled()!=preview,"Preview-only editor admission mismatch");
    if(preview){
     ++previews;const auto before=hem::SessionState::captureParameters(*processor);int controls=0;
     for(const auto& spec:processor->parameterSpecs())if(auto* control=findControl(*editor,spec.id);control&&visibleWithin(host,*control)){
      require(!control->isEnabled(),hostParameterChecks::context(*processor,spec.id)+"preview widget admits interaction");
      // Disabled actual buttons/sliders must reject mouse-down/up without opening
      // a native popup. Hidden/rebound pages are covered at supported sizes below.
      if(dynamic_cast<juce::Button*>(control)||dynamic_cast<juce::Slider*>(control)){hostParameterChecks::click(*control,control->getLocalBounds().getCentre());++controls;}
     }
     require(controls>0&&before.isEquivalentTo(hem::SessionState::captureParameters(*processor)),juce::String(entries[module].id)+" preview at "+juce::String(size.x)+"x"+juce::String(size.y)+": mutated parameters or no tested controls ("+juce::String(controls)+")");
     continue;
    }
    for(const auto& spec:processor->parameterSpecs()){
     try{
      hem::SessionState::restoreParameters(*processor,factory.createCopy());
      auto& control=hostParameterChecks::prepare(host,*processor,*editor,*viewport,spec);
      hostParameterChecks::exercise(host,*processor,*editor,*viewport,spec,control);++gestures;++moduleGestures;
      MainComponentTestAccess::tick(host);
      require(observed&&activeEditor(workspace)==observed.getComponent(),"Parameter interaction destroyed editor");
      require(MainComponentTestAccess::pluginPage(host)&&!MainComponentTestAccess::chooserOpen(host),"Parameter interaction navigated or opened chooser");
     }catch(const std::exception& e){throw std::runtime_error((hostParameterChecks::context(*processor,spec.id)+"size="+juce::String(size.x)+"x"+juce::String(size.y)+" expanded="+juce::String(expanded?1:0)+" "+e.what()).toStdString());}
    }
   }
  }
  require(moduleGestures>0,"No supported interaction fit for "+juce::String(entries[module].id));
  hem::SessionState::restoreParameters(*processor,factory.createCopy());hostParameterChecks::refresh(*editor);
  require(factory.isEquivalentTo(hem::SessionState::captureParameters(*processor)),"Interaction fixture failed to restore insertion profile");
  const auto scroll=viewport->getViewPosition();MainComponentTestAccess::open(host);MainComponentTestAccess::open(host);
  require(observed&&activeEditor(workspace)==observed.getComponent(),"Repeated open recreated selected editor");
  require(viewport->getViewPosition()==scroll,"Repeated open reset scroll position");
  MainComponentTestAccess::home(host);MainComponentTestAccess::open(host);require(observed!=nullptr,"Returning to PLUGIN destroyed retained editor");
  MainComponentTestAccess::play(host);
  require(!MainComponentTestAccess::chooserOpen(host)&&observed&&MainComponentTestAccess::pluginPage(host),"Offline PLAY opened chooser or destroyed editor");
  require(MainComponentTestAccess::status(host)=="No audio device: editing, sessions and export remain available","Offline PLAY status changed");
 }
 require(previews>0,"No preview-only fit was tested");
 require(visited.size()==34,"Did not visit every independently contracted insert identity");
 for(auto* id:hostParameterChecks::orderedIds)if(juce::String(id)!="monitor-eq")require(visited.count(id)==1,"Missing insert interaction identity");
 MainComponentTestAccess::install(host,-1);workspace.setPluginExpanded(false);host.setSize(1536,864);
 std::cout<<"PASS ordered35 identity contract, 34 actual host insert editors, "<<gestures<<" parameter exercises and "<<previews<<" disabled preview fits; ComboBox/reference choice binding only, native popup NOT RUN\n";
}
void fixture(const juce::File& file,int frames){
 std::unique_ptr<juce::OutputStream> stream=file.createOutputStream();juce::WavAudioFormat format;
 auto writer=format.createWriterFor(stream,juce::AudioFormatWriterOptions().withSampleRate(48000).withNumChannels(2).withBitsPerSample(32).withSampleFormat(juce::AudioFormatWriterOptions::SampleFormat::floatingPoint));
 require(writer!=nullptr,"Fixture writer failed");juce::AudioBuffer<float> data(2,frames);
 for(int i=0;i<frames;++i){data.setSample(0,i,.005f*std::sin(float(i)*.13f));data.setSample(1,i,.003f*std::cos(float(i)*.11f));}
 require(writer->writeFromAudioSampleBuffer(data,0,frames),"Fixture write failed");
}
void applyImport(MainComponent& host,const juce::StringArray& files){
 MainComponentTestAccess::import(host,files);auto* apply=button(host,"APPLY WHEN STOPPED");require(apply!=nullptr,"Real importer Apply control missing");
 until([&]{return apply->isEnabled();},"Importer never became applicable without hardware");apply->onClick();
 until([&]{return !MainComponentTestAccess::pending(host);},"Import cleanup failed");
}
}
#include "GateFactoryChecks.h"
#include "HostControlRegressionChecks.h"
#include "HostCaptureResourceChecks.h"
namespace {
void verifyLifecycle(MainComponent& host,const juce::File& directory){
 const auto source=directory.getChildFile("source.wav");fixture(source,4800);juce::MemoryBlock originalBytes;require(source.loadFileAsData(originalBytes),"Cannot retain source fixture bytes");
 juce::StringArray thirteen;for(int i=0;i<13;++i)thirteen.add(source.getFullPathName());std::cerr<<"stage: import 13\n";applyImport(host,thirteen);
 for(const auto& track:MainComponentTestAccess::bank(host).tracks)require(track.loaded,"13-track offline import did not commit");
 MainComponentTestAccess::play(host);require(!MainComponentTestAccess::playing(host),"No-device PLAY left transport running without callbacks");
 std::cerr<<"stage: edit inserts\n";MainComponentTestAccess::install(host,0);auto snap=MainComponentTestAccess::snapshot(host);require(snap.tracks[0].slots[0].pluginId=="sipa","Offline insert did not become capturable immediately");
 std::cerr<<"stage: opening editor\n";MainComponentTestAccess::open(host);require(MainComponentTestAccess::hasEditor(host),"Real plugin editor failed to open offline");
 std::cerr<<"stage: replacing editor\n";MainComponentTestAccess::install(host,2);require(!MainComponentTestAccess::hasEditor(host),"Retired processor editor was not closed");
 snap=MainComponentTestAccess::snapshot(host);require(snap.tracks[0].slots[0].pluginId=="vca","Offline replacement did not settle without audio");
 MainComponentTestAccess::install(host,-1);snap=MainComponentTestAccess::snapshot(host);require(snap.tracks[0].slots[0].pluginId.isEmpty(),"Offline removal did not settle");
 MainComponentTestAccess::install(host,0);
 auto& track=MainComponentTestAccess::bank(host).tracks[0];auto pending=track.chain.stageNode(std::make_unique<hem::VcaProcessor>(),48000,256,track.chain.slotLatency());require(track.chain.publish(0,std::move(pending)),"Could not stage simulated pre-disconnect transition");
 track.transport().start();MainComponentTestAccess::offline(host);require(!MainComponentTestAccess::playing(host),"Device loss left source playing");
 snap=MainComponentTestAccess::snapshot(host);require(snap.tracks[0].slots[0].pluginId=="vca","Pending insert failed to settle on device loss");
 MainComponentTestAccess::trim(host).setValue(-11.7,juce::sendNotificationSync);MainComponentTestAccess::tempo(host).setValue(97.5,juce::sendNotificationSync);
 std::cerr<<"stage: save recall\n";const auto session=directory.getChildFile("session.xml");require(MainComponentTestAccess::save(host,session).wasOk(),"Offline save admission failed");until([&]{return !MainComponentTestAccess::sessionBusy(host);},"Offline save did not complete");require(session.existsAsFile(),"Offline session file missing");
 require(MainComponentTestAccess::reset(host).wasOk(),"New session reset failed");for(const auto& t:MainComponentTestAccess::bank(host).tracks)require(!t.loaded,"New session retained audio");
 require(MainComponentTestAccess::load(host,session).wasOk(),"Offline recall admission failed");until([&]{return !MainComponentTestAccess::sessionBusy(host);},"Offline recall did not complete");snap=MainComponentTestAccess::snapshot(host);require(snap.tracks[12].source==source&&snap.tracks[0].slots[0].pluginId=="vca","Offline recall lost audio or insert state");
 require(std::abs(MainComponentTestAccess::trim(host).getValue()+11.7)<.001&&snap.tempoBpm==97.5,"Session recall lost trim or nondefault project BPM");
 std::cerr<<"stage: export\n";const auto output=directory.getChildFile("mix.wav");require(MainComponentTestAccess::render(host,output).wasOk(),"Actual host export admission failed");until([&]{return !MainComponentTestAccess::exportBusy(host);},"Host export did not finish");require(output.existsAsFile()&&MainComponentTestAccess::exportResult(host).startsWith("Saved"),"Host did not publish WAV");
 juce::WavAudioFormat format;auto input=output.createInputStream();std::unique_ptr<juce::AudioFormatReader> reader(format.createReaderFor(input.release(),true));require(reader&&reader->lengthInSamples==4800&&reader->sampleRate==48000&&reader->usesFloatingPointData,"Host export format/duration incorrect");
 std::cerr<<"stage: clear/reimport\n";require(MainComponentTestAccess::clear(host,true).wasOk(),"All-source unload failed");snap=MainComponentTestAccess::snapshot(host);require(snap.tracks[0].source==juce::File()&&snap.tracks[0].slots[0].pluginId=="vca","Source unload failed to retain inserts");
 juce::StringArray three;for(int i=0;i<3;++i)three.add(source.getFullPathName());applyImport(host,three);for(int i=0;i<13;++i)require(MainComponentTestAccess::bank(host).tracks[size_t(i)].loaded==(i<3),"13 to 3 session replacement retained stale source audio after explicit unload");
 hem::StemFileImporter::Batch stale;stale.firstTrack=0;stale.count=1;stale.context={48000,256,MainComponentTestAccess::generation(host),false};juce::AudioFormatManager formats;formats.registerBasicFormats();stale.stems[0]=std::make_unique<hem::StemPlayback>();stale.stems[0]->prepareFile(source,formats,MainComponentTestAccess::disk(host),48000,256,false);
 MainComponentTestAccess::offline(host);require(MainComponentTestAccess::staleImport(host,stale).failed(),"Stale device-generation import was accepted");
 std::cerr<<"stage: close safeguard\n";
 int closes=0;require(host.hasUnsavedSessionChanges(),"Source edit did not mark session dirty");
 MainComponentTestAccess::closeChoice(host,MainComponent::CloseChoice::cancel);host.requestClose([&]{++closes;});
 require(closes==0&&!MainComponentTestAccess::closePending(host)&&host.isEnabled(),"Cancel close disabled or destroyed the session");
 require(MainComponentTestAccess::bank(host).tracks[0].loaded,"Cancel close unloaded source data");
 MainComponentTestAccess::saveTarget(host,directory.getChildFile("missing-parent/session.xml"));MainComponentTestAccess::closeChoice(host,MainComponent::CloseChoice::save);host.requestClose([&]{++closes;});
 until([&]{return !MainComponentTestAccess::closePending(host);},"Failed save left close request stuck");
 require(closes==0&&host.isEnabled()&&host.hasUnsavedSessionChanges()&&MainComponentTestAccess::status(host).contains("Save failed"),"Failed close-save silently lost edits or closed the app");
 const auto closeSave=directory.getChildFile("close-save.xml");MainComponentTestAccess::saveTarget(host,closeSave);host.requestClose([&]{++closes;});
 until([&]{return closes==1;},"Successful save did not complete deferred close");
 require(closeSave.existsAsFile()&&!host.hasUnsavedSessionChanges(),"Close approval occurred before a successful save");
 MainComponentTestAccess::bank(host).tracks[0].trimDb=-1;
 const auto cancelledExport=directory.getChildFile("close-cancelled-export.wav");require(MainComponentTestAccess::slowExport(host,cancelledExport).wasOk(),"Active close-export test could not start");
 MainComponentTestAccess::closeChoice(host,MainComponent::CloseChoice::cancel);host.requestClose([&]{++closes;});
 require(closes==1&&MainComponentTestAccess::exportBusy(host),"Cancelling close cancelled the active export or closed Studio");
 MainComponentTestAccess::closeChoice(host,MainComponent::CloseChoice::discard);host.requestClose([&]{++closes;});
 until([&]{return closes==2;},"Active export cancellation deadlocked close");
 require(!MainComponentTestAccess::exportBusy(host)&&!cancelledExport.exists()&&MainComponentTestAccess::exportResult(host).containsIgnoreCase("cancel"),"Approved close did not safely cancel unpublished export");
 juce::MemoryBlock finalBytes;require(source.loadFileAsData(finalBytes)&&finalBytes==originalBytes,"Session/clear/export altered source WAV bytes");
 std::cout<<"PASS real no-device MainComponent lifecycle: 13-track async import/apply, immediate insert/replace/remove, editor retirement, disconnect settlement, save/recall, float32 export, source unload/new session, stale-generation rejection, cancel-close, save failure, save-before-close and active-export cancellation\n";
}
void verifyMeterTaps(MainComponent& host,const juce::File& directory){
 const auto source=directory.getChildFile("meter-source.wav");fixture(source,4800);juce::MemoryBlock original;require(source.loadFileAsData(original),"Meter source bytes missing");
 juce::StringArray paths;for(int n=0;n<13;++n)paths.add(source.getFullPathName());applyImport(host,paths);hostControlChecks::meterTapParity(host);
 juce::MemoryBlock final;require(source.loadFileAsData(final)&&original==final,"Meter callback fixture altered source bytes");
}
}
int main(int argc,char** argv){
#if JUCE_LINUX
 std::signal(SIGSEGV,[](int signal){void* frames[64];int count=backtrace(frames,64);backtrace_symbols_fd(frames,count,2);_exit(128+signal);});
#endif
 juce::ScopedJuceInitialiser_GUI gui;
 const juce::String requested=argc>1?argv[1]:"all";int ran=0,failed=0;
 // Every group gets its own real host and temporary files. Select one by name,
 // or run all; one assertion failure cannot suppress independent lifecycle gates.
 auto section=[&](const char* name,auto check){
  if(requested!="all"&&requested!=name)return;++ran;
  const auto directory=juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("HEM-host-"+juce::String(name)+"-"+juce::Uuid().toString());
  struct Cleanup{juce::File directory;~Cleanup(){directory.deleteRecursively();}} cleanup{directory};
  try{
   require(directory.createDirectory().wasOk(),"Fixture directory failed");
   auto owner=std::make_unique<MainComponent>();auto& host=*owner;MainComponentTestAccess::offline(host);host.setVisible(true);
   require(MainComponentTestAccess::ready(host),"No-device graph fallback preparation/meter-readiness failed");
   std::cerr<<"SECTION "<<name<<"\n";check(host,directory);std::cout<<"PASS section "<<name<<"\n";
  }catch(const std::exception& e){++failed;std::cerr<<"FAIL section "<<name<<": "<<e.what()<<'\n';}
 };
 section("geometry",[](auto& h,const auto&){hostControlChecks::hostGeometry(h);});
 section("monitor",[](auto& h,const auto&){hostControlChecks::monitorOwnership(h);});
 section("trim",[](auto& h,const auto&){hostControlChecks::trackTrim(h);});
 section("tempo",[](auto& h,const auto& d){hostControlChecks::projectTempo(h,d);});
 section("bypass-order",[](auto& h,const auto&){hostControlChecks::bypassAndOrder(h);});
 section("vocal",[](auto& h,const auto& d){hostControlChecks::vocalAdmission(h,d);});
 section("interaction-negative",[](auto&,const auto&){hostParameterChecks::negativeControls();});
 section("interactions",[](auto& h,const auto&){verifyEditorInteractions(h);});
 section("factory",[](auto& h,const auto& d){gateFactoryChecks::host(h,d);});
 section("meters",[](auto& h,const auto& d){verifyMeterTaps(h,d);});
 section("resource",[](auto& h,const auto& d){hostCaptureChecks::run(h,d);});
 section("lifecycle",[](auto& h,const auto& d){verifyLifecycle(h,d);});
 if(!ran){std::cerr<<"Unknown section: "<<requested<<"\n";return 2;}
 std::cout<<"Host lifecycle sections: "<<(ran-failed)<<" passed, "<<failed<<" failed. Native popups/choosers, hardware I/O and Windows acceptance NOT RUN\n";
 return failed?1:0;
}
