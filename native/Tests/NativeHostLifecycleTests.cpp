#include "../Source/MainComponent.h"
#include <iostream>
#include <stdexcept>
#if JUCE_LINUX
#include <execinfo.h>
#include <csignal>
#include <unistd.h>
#endif

struct MainComponentTestAccess {
 static void offline(MainComponent& host){host.deviceManager.closeAudioDevice();if(host.prepared)host.releaseResources();host.timerCallback();}
 static bool ready(MainComponent& host){return host.prepared&&host.sampleRate==48000&&host.blockSize==256&&!host.outputMeters.snapshot().ready;}
 static hem::TrackBank& bank(MainComponent& host){return *host.bank;}
 static void import(MainComponent& host,const juce::StringArray& files){host.importer->submitFiles(files);}
 static bool pending(MainComponent& host){return host.importer->hasPendingWork();}
 static bool sessionBusy(MainComponent& host){return host.sessionWorker->isBusy();}
 static bool exportBusy(MainComponent& host){return host.exportWorker->isBusy();}
 static juce::String exportResult(MainComponent& host){return host.exportWorker->result();}
 static void install(MainComponent& host,int module){host.installSelected(module);}
 static void tick(MainComponent& host){host.timerCallback();}
 static void open(MainComponent& host){host.openPlugin(0);}
 static bool hasEditor(MainComponent& host){return host.editedProcessor!=nullptr;}
 static hem::SessionSnapshot snapshot(MainComponent& host){return hem::SessionState::capture(*host.bank,host.inputDb,host.outputDb,host.sessionPosition(),host.looping,host.monitorProcessor);}
 static juce::Result save(MainComponent& host,const juce::File& file){return host.saveSessionFile(file);}
 static juce::Result load(MainComponent& host,const juce::File& file){return host.loadSessionFile(file);}
 static juce::Result clear(MainComponent& host,bool all){return host.clearTrackSources(all);}
 static juce::Result reset(MainComponent& host){return host.resetSessionConfirmed();}
 static juce::Result render(MainComponent& host,const juce::File& file){hem::OfflineMixOptions o;o.tailSeconds=0;return host.startExport(file,o);}
 static void play(MainComponent& host){host.playAll();}
 static bool playing(MainComponent& host){return host.playing();}
 static unsigned generation(MainComponent& host){return host.generation;}
 static juce::Result staleImport(MainComponent& host,hem::StemFileImporter::Batch& batch){return host.commitImport(batch);}
 static juce::TimeSliceThread& disk(MainComponent& host){return host.diskThread;}
 static void closeChoice(MainComponent& host,MainComponent::CloseChoice choice){host.closePrompt=[choice](bool,bool,std::function<void(MainComponent::CloseChoice)> resolved){resolved(choice);};}
 static bool closePending(MainComponent& host){return host.closePhase!=MainComponent::ClosePhase::idle;}
 static void saveTarget(MainComponent& host,const juce::File& file){host.currentSessionFile=file;}
 static juce::String status(MainComponent& host){return host.message.getText();}
 static juce::Result slowExport(MainComponent& host,const juce::File& file){hem::OfflineMixOptions options;options.blockSize=1;options.tailSeconds=30;return host.startExport(file,options);}
};
namespace {
void require(bool b,const char* message){if(!b)throw std::runtime_error(message);}
juce::TextButton* button(juce::Component& parent,const juce::String& text){
 if(auto* b=dynamic_cast<juce::TextButton*>(&parent);b&&b->getButtonText()==text)return b;
 for(int i=0;i<parent.getNumChildComponents();++i)if(auto* found=button(*parent.getChildComponent(i),text))return found;
 return nullptr;
}
template<class Condition>void until(Condition done,const char* failure){
 for(int i=0;i<3000&&!done();++i){juce::Thread::sleep(5);juce::Timer::callPendingTimersSynchronously();}
 require(done(),failure);
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
int main(){
#if JUCE_LINUX
 std::signal(SIGSEGV,[](int signal){void* frames[64];int count=backtrace(frames,64);backtrace_symbols_fd(frames,count,2);_exit(128+signal);});
#endif
juce::ScopedJuceInitialiser_GUI gui;try{
 const auto directory=juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("HEM-native-lifecycle-"+juce::Uuid().toString());require(directory.createDirectory().wasOk(),"Fixture directory failed");
 struct Cleanup{juce::File directory;~Cleanup(){directory.deleteRecursively();}} cleanup{directory};
 const auto source=directory.getChildFile("source.wav");fixture(source,4800);juce::MemoryBlock originalBytes;require(source.loadFileAsData(originalBytes),"Cannot retain original fixture bytes");
 std::cerr<<"stage: construct native host\n";auto owner=std::make_unique<MainComponent>();auto& host=*owner;std::cerr<<"stage: force offline\n";MainComponentTestAccess::offline(host);require(MainComponentTestAccess::ready(host),"No-device graph fallback did not prepare 48k/256 or faked output meter readiness");
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
 std::cerr<<"stage: save recall\n";const auto session=directory.getChildFile("session.xml");require(MainComponentTestAccess::save(host,session).wasOk(),"Offline save admission failed");until([&]{return !MainComponentTestAccess::sessionBusy(host);},"Offline save did not complete");require(session.existsAsFile(),"Offline session file missing");
 require(MainComponentTestAccess::reset(host).wasOk(),"New session reset failed");for(const auto& t:MainComponentTestAccess::bank(host).tracks)require(!t.loaded,"New session retained audio");
 require(MainComponentTestAccess::load(host,session).wasOk(),"Offline recall admission failed");until([&]{return !MainComponentTestAccess::sessionBusy(host);},"Offline recall did not complete");snap=MainComponentTestAccess::snapshot(host);require(snap.tracks[12].source==source&&snap.tracks[0].slots[0].pluginId=="vca","Offline recall lost audio or insert state");
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
 std::cout<<"PASS real no-device MainComponent lifecycle: 13-track async import/apply, immediate insert/replace/remove, editor retirement, disconnect settlement, save/recall, float32 export, source unload/new session, stale-generation rejection, cancel-close, save failure, save-before-close and active-export cancellation\n";return 0;
 }catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}}
