#include "../Source/MainComponent.h"
#include <future>
#include <thread>
#include <iostream>
#include <stdexcept>

struct MainComponentTestAccess {
 static void offline(MainComponent& h){h.deviceManager.closeAudioDevice();if(h.prepared)h.releaseResources();h.timerCallback();}
 static hem::TrackBank& bank(MainComponent& h){return *h.bank;}
 static hem::ExternalPluginHost& external(MainComponent& h){return h.externalHost;}
 static void import(MainComponent& h,const juce::StringArray& files){h.importer->submitFiles(files);}
 static bool pending(MainComponent& h){return h.importer->hasPendingWork();}
 static bool sessionBusy(MainComponent& h){return h.sessionWorker->isBusy();}
 static bool exportBusy(MainComponent& h){return h.exportWorker->isBusy();}
 static juce::String exportResult(MainComponent& h){return h.exportWorker->result();}
 static juce::String status(MainComponent& h){return h.message.getText();}
 static juce::Result install(MainComponent& h,const hem::ExternalPluginState& state,unsigned generation){return h.installExternal(state,0,0,generation);}
 static unsigned generation(MainComponent& h){return h.generation;}
 static hem::SessionSnapshot snapshot(MainComponent& h){return h.captureSession();}
 static juce::Result save(MainComponent& h,const juce::File& file){return h.saveSessionFile(file);}
 static juce::Result load(MainComponent& h,const juce::File& file){return h.loadSessionFile(file);}
 static juce::Result renderOptions(MainComponent& h,const juce::File& file,const hem::OfflineMixOptions& options){return h.startExport(file,options);}
 static juce::Result render(MainComponent& h,const juce::File& file,bool slow=false){hem::OfflineMixOptions o;o.tailSeconds=slow?30:0;o.blockSize=slow?1:256;return h.startExport(file,o);}
 static void open(MainComponent& h){h.openPlugin(0);}
 static juce::AudioProcessor* realEditorProcessor(MainComponent& h){
  std::function<juce::AudioProcessor*(juce::Component&)> find=[&](juce::Component& c)->juce::AudioProcessor*{if(auto* e=dynamic_cast<juce::AudioProcessorEditor*>(&c))return e->getAudioProcessor();for(int i=0;i<c.getNumChildComponents();++i)if(auto* p=find(*c.getChildComponent(i)))return p;return nullptr;};return find(*h.workspace);
 }
 static bool hasEditor(MainComponent& h){return h.editedProcessor!=nullptr;}
 static void closeChoice(MainComponent& h,MainComponent::CloseChoice choice){h.closePrompt=[choice](bool,bool,std::function<void(MainComponent::CloseChoice)> fn){fn(choice);};}
 static bool closePending(MainComponent& h){return h.closePhase!=MainComponent::ClosePhase::idle;}
 static void nativeRemove(MainComponent& h){h.installSelected(-1);}
 static void reload(MainComponent& h){h.reloadExternalPlugin();}
 static void select(MainComponent& h,int track){h.selectTrack(track);}
 static void context(MainComponent& h,double rate,int block){h.releaseResources();h.prepareToPlay(block,rate);h.timerCallback();}
};
namespace {
void require(bool b,const std::string& message){if(!b)throw std::runtime_error(message);}
template<class F>auto onMain(F fn){using R=decltype(fn());auto promise=std::make_shared<std::promise<R>>();auto result=promise->get_future();require(juce::MessageManager::callAsync([promise,fn=std::move(fn)]()mutable{try{if constexpr(std::is_void_v<R>){fn();promise->set_value();}else promise->set_value(fn());}catch(...){promise->set_exception(std::current_exception());}}),"Cannot post main-thread test step");return result.get();}
template<class F>void until(F done,const char* failure){for(int i=0;i<800;++i){if(onMain(done))return;juce::Thread::sleep(10);}throw std::runtime_error(failure);}
juce::TextButton* findButton(juce::Component& c,const juce::String& text){if(auto* b=dynamic_cast<juce::TextButton*>(&c);b&&b->getButtonText()==text)return b;for(int i=0;i<c.getNumChildComponents();++i)if(auto* b=findButton(*c.getChildComponent(i),text))return b;return nullptr;}
void writeFixture(const juce::File& file){std::unique_ptr<juce::OutputStream> stream=file.createOutputStream();juce::WavAudioFormat format;auto writer=format.createWriterFor(stream,juce::AudioFormatWriterOptions().withSampleRate(48000).withNumChannels(2).withBitsPerSample(32).withSampleFormat(juce::AudioFormatWriterOptions::SampleFormat::floatingPoint));require(writer!=nullptr,"Fixture writer failed");juce::AudioBuffer<float> b(2,4800);for(int i=0;i<4800;++i){b.setSample(0,i,.005f*std::sin(float(i)*.13f));b.setSample(1,i,.003f*std::cos(float(i)*.11f));}require(writer->writeFromAudioSampleBuffer(b,0,4800),"Fixture write failed");}
juce::AudioBuffer<float> readWav(const juce::File& f){juce::WavAudioFormat format;auto stream=f.createInputStream();require(stream!=nullptr,"Export file missing");std::unique_ptr<juce::AudioFormatReader> reader(format.createReaderFor(stream.release(),true));require(reader&&reader->lengthInSamples==4800&&reader->sampleRate==48000&&reader->numChannels==2&&reader->usesFloatingPointData,"Export format/length mismatch");juce::AudioBuffer<float> b(2,4800);require(reader->read(&b,0,4800,0,true,true),"Export read failed");return b;}
void compare(const juce::AudioBuffer<float>& actual,const juce::AudioBuffer<float>& expected,float ratio,float tolerance){for(int ch=0;ch<2;++ch)for(int i=0;i<4800;++i)require(std::abs(actual.getSample(ch,i)-expected.getSample(ch,i)*ratio)<=tolerance,"Export audio does not match independently rendered native path");}
}
#if JUCE_MAC
void initialiseIntegrationApplication();
#endif
int main(int argc,char** argv){
#if JUCE_MAC
 initialiseIntegrationApplication();
#endif
 std::cout << std::unitbuf;
 std::cerr << "stage: integration application initialized\n";
 if(argc!=2){std::cerr<<"Pass the exact in-house VST3 fixture bundle, never a scan directory\n";return 2;}
 juce::ScopedJuceInitialiser_GUI gui;int result=1;const juce::File plugin(juce::String::fromUTF8(argv[1]));
 std::thread driver([&]{std::unique_ptr<MainComponent> owner;try{
  const auto directory=juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("HEM-vst3-integration-"+juce::Uuid().toString());require(directory.createDirectory().wasOk(),"Temporary directory failed");struct Cleanup{juce::File path;~Cleanup(){path.deleteRecursively();}}cleanup{directory};
  const auto source=directory.getChildFile("fixture.wav");writeFixture(source);juce::MemoryBlock original;require(source.loadFileAsData(original),"Fixture snapshot failed");
  onMain([&]{std::cerr<<"stage: first main-thread callback\n";owner=std::make_unique<MainComponent>();MainComponentTestAccess::offline(*owner);MainComponentTestAccess::import(*owner,{source.getFullPathName()});});
  until([&]{auto* apply=findButton(*owner,"APPLY WHEN STOPPED");return apply&&apply->isEnabled();},"Import not ready");
  onMain([&]{findButton(*owner,"APPLY WHEN STOPPED")->onClick();});until([&]{return !MainComponentTestAccess::pending(*owner);},"Import did not settle");
  const auto reference=directory.getChildFile("native.wav");onMain([&]{require(MainComponentTestAccess::render(*owner,reference).wasOk(),"Native reference export rejected");});until([&]{return !MainComponentTestAccess::exportBusy(*owner);},"Native reference export stalled");auto expected=readWav(reference);
  auto state=onMain([&]{juce::String error;auto descriptions=MainComponentTestAccess::external(*owner).discoverSelected(plugin,error);require(descriptions.size()==1,error.toStdString());hem::ExternalPluginState saved;saved.descriptionXml=descriptions[0].createXml()->toString();return saved;});
  onMain([&]{auto r=MainComponentTestAccess::install(*owner,state,MainComponentTestAccess::generation(*owner));require(r.wasOk(),r.getErrorMessage().toStdString());require(MainComponentTestAccess::bank(*owner).tracks[0].chain.activeProcessor(0)->isExternal(),"External node absent");});
  onMain([&]{hem::OfflineMixOptions invalid;invalid.sampleRate=std::numeric_limits<double>::quiet_NaN();require(MainComponentTestAccess::renderOptions(*owner,directory.getChildFile("invalid.wav"),invalid).failed(),"Nonfinite external export rate accepted before preparation");require(!MainComponentTestAccess::exportBusy(*owner),"Invalid options started export");invalid.sampleRate=48000;invalid.blockSize=0;require(MainComponentTestAccess::renderOptions(*owner,directory.getChildFile("invalid.wav"),invalid).failed(),"Zero external export block accepted");});
  const auto unity=directory.getChildFile("unity.wav");onMain([&]{require(MainComponentTestAccess::render(*owner,unity).wasOk(),"VST3 export rejected");});until([&]{return !MainComponentTestAccess::exportBusy(*owner);},"Private VST3 export stalled");compare(readWav(unity),expected,1,0);
  std::cout<<"PASS actual native host selected VST3 load and independent export with bit-exact unity audio\n";
  const auto xml=directory.getChildFile("external.xml"),json=directory.getChildFile("external.json");
  onMain([&]{require(MainComponentTestAccess::save(*owner,xml).wasOk(),"External XML save admission failed");});until([&]{return !MainComponentTestAccess::sessionBusy(*owner);},"XML save stalled");
  onMain([&]{require(!owner->hasUnsavedSessionChanges(),"Successful opaque save left dirty state");MainComponentTestAccess::open(*owner);auto* actual=MainComponentTestAccess::realEditorProcessor(*owner);require(actual!=nullptr,"VST3 real editor missing");bool changed=false;for(auto* p:actual->getParameters())if(p->getName(100)=="Fixture gain"){p->setValueNotifyingHost(.25f);changed=true;}require(changed,"Fixture gain parameter absent");juce::AudioBuffer<float> b(2,256);b.clear();MainComponentTestAccess::bank(*owner).tracks[0].chain.process(b.getWritePointer(0),b.getWritePointer(1),256);});
  until([&]{return owner->hasUnsavedSessionChanges();},"External parameter change did not mark dirty");
  onMain([&]{MainComponentTestAccess::reload(*owner);require(owner->hasUnsavedSessionChanges(),"Reload of changed external state erased the unsaved-session marker");});
  onMain([&]{require(MainComponentTestAccess::save(*owner,json).wasOk(),"External JSON save admission failed");});until([&]{return !MainComponentTestAccess::sessionBusy(*owner);},"JSON save stalled");
  onMain([&]{require(MainComponentTestAccess::load(*owner,json).wasOk(),"External JSON recall rejected");});until([&]{return !MainComponentTestAccess::sessionBusy(*owner);},"External JSON recall stalled");
  onMain([&]{require(!MainComponentTestAccess::hasEditor(*owner),"Recall retained editor to retired instance");auto snap=MainComponentTestAccess::snapshot(*owner);require(snap.tracks[0].slots[0].pluginId=="external-vst3"&&snap.tracks[0].slots[0].external.state.getSize()>0,"Recall lost opaque state");});
  const auto half=directory.getChildFile("half.wav");onMain([&]{require(MainComponentTestAccess::render(*owner,half).wasOk(),"Restored export rejected");});until([&]{return !MainComponentTestAccess::exportBusy(*owner);},"Restored export stalled");compare(readWav(half),expected,.5f,1.e-7f);
  onMain([&]{require(MainComponentTestAccess::load(*owner,xml).wasOk(),"External XML recall rejected");});until([&]{return !MainComponentTestAccess::sessionBusy(*owner);},"XML recall stalled");
  std::cout<<"PASS opaque XML/JSON recall, live parameter dirtiness, editor retirement and restored gain in export\n";
  auto changeLatency=[&](float normalized){onMain([&]{MainComponentTestAccess::open(*owner);auto* actual=MainComponentTestAccess::realEditorProcessor(*owner);require(actual!=nullptr,"Actual latency editor missing");bool changed=false;for(auto* p:actual->getParameters())if(p->getName(100)=="Latency mode"){p->setValueNotifyingHost(normalized);changed=true;}require(changed,"Latency parameter missing");juce::AudioBuffer<float> block(2,256);block.clear();MainComponentTestAccess::bank(*owner).tracks[0].chain.process(block.getWritePointer(0),block.getWritePointer(1),256);});onMain([&]{MainComponentTestAccess::select(*owner,1);});until([&]{return !MainComponentTestAccess::bank(*owner).tracks[0].chain.activeProcessor(0)->processingReady();},"Actual asynchronous VST3 latency restart did not gate processing");until([&]{return MainComponentTestAccess::status(*owner).contains("TRACK 1 / SLOT 1");},"Unselected external fault did not produce a visible global warning");onMain([&]{MainComponentTestAccess::select(*owner,0);});};
  changeLatency(.5f);
  onMain([&]{auto& chain=MainComponentTestAccess::bank(*owner).tracks[0].chain;juce::AudioBuffer<float> zero(2,256);for(int i=0;i<12;++i){zero.clear();chain.process(zero.getWritePointer(0),zero.getWritePointer(1),256);}juce::AudioBuffer<float> impulse(2,3000);impulse.clear();impulse.setSample(0,0,.125f);impulse.setSample(1,0,-.0625f);chain.process(impulse.getWritePointer(0),impulse.getWritePointer(1),3000);for(int i=0;i<3000;++i){require(impulse.getSample(0,i)==(i==2304?.125f:0.f),"Faulted external latency silently misaligned left dry signal");require(impulse.getSample(1,i)==(i==2304?-.0625f:0.f),"Faulted external latency silently misaligned right dry signal");}MainComponentTestAccess::reload(*owner);auto* p=chain.activeProcessor(0);require(p->processingReady()&&p->getLatencySamples()==64,"Bounded latency reload did not revalidate64 samples");});
  const auto delayed=directory.getChildFile("delayed.wav");onMain([&]{require(MainComponentTestAccess::render(*owner,delayed).wasOk(),"Delayed VST3 export rejected");});until([&]{return !MainComponentTestAccess::exportBusy(*owner);},"Delayed export stalled");compare(readWav(delayed),expected,1,0);
  onMain([&]{MainComponentTestAccess::context(*owner,96000,512);require(!MainComponentTestAccess::bank(*owner).tracks[0].chain.activeProcessor(0)->processingReady(),"Device context change failed to gate external instance");MainComponentTestAccess::reload(*owner);auto* p=MainComponentTestAccess::bank(*owner).tracks[0].chain.activeProcessor(0);require(p->processingReady()&&p->actualRate.load()==96000,"Reload did not use actual new context");MainComponentTestAccess::offline(*owner);require(!MainComponentTestAccess::bank(*owner).tracks[0].chain.activeProcessor(0)->processingReady(),"Offline reprepare did not invalidate mismatched instance");MainComponentTestAccess::reload(*owner);require(MainComponentTestAccess::bank(*owner).tracks[0].chain.activeProcessor(0)->processingReady(),"Offline reload failed");MainComponentTestAccess::context(*owner,48000,256);require(!MainComponentTestAccess::bank(*owner).tracks[0].chain.activeProcessor(0)->processingReady(),"Same-context restart silently retained vendor history");MainComponentTestAccess::reload(*owner);require(MainComponentTestAccess::bank(*owner).tracks[0].chain.activeProcessor(0)->processingReady(),"Same-context restart reload failed");});
  changeLatency(1.f);
  onMain([&]{auto* old=MainComponentTestAccess::bank(*owner).tracks[0].chain.activeProcessor(0);MainComponentTestAccess::reload(*owner);require(MainComponentTestAccess::bank(*owner).tracks[0].chain.activeProcessor(0)==old&&!old->processingReady(),"Excess-latency rejection replaced or re-enabled old faulted state");require(MainComponentTestAccess::status(*owner).contains("6 ms"),"6ms rejection not visible to user");require(MainComponentTestAccess::load(*owner,xml).wasOk(),"Recovery recall rejected");});until([&]{return !MainComponentTestAccess::sessionBusy(*owner);},"Recovery recall stalled");
  std::cout<<"PASS real asynchronous VST3 latency notification, exact aligned dry fallback, 64-sample reload/export, actual-context reprepare and visible 6 ms rejection with state retained\n";

  onMain([&]{auto* old=MainComponentTestAccess::bank(*owner).tracks[0].chain.activeProcessor(0);require(MainComponentTestAccess::install(*owner,state,MainComponentTestAccess::generation(*owner)-1).failed(),"Stale generation accepted");auto missing=state;juce::PluginDescription d;juce::String error;require(missing.getDescription(d,error),"Descriptor decode failed");d.fileOrIdentifier=directory.getChildFile("missing.vst3").getFullPathName();missing.descriptionXml=d.createXml()->toString();require(MainComponentTestAccess::install(*owner,missing,MainComponentTestAccess::generation(*owner)).failed(),"Missing binary accepted");require(MainComponentTestAccess::bank(*owner).tracks[0].chain.activeProcessor(0)==old,"Failed load replaced old insert");});
  int closes=0;const auto cancelled=directory.getChildFile("cancelled.wav");
  onMain([&]{require(MainComponentTestAccess::render(*owner,cancelled,true).wasOk(),"Cancellation export rejected");MainComponentTestAccess::closeChoice(*owner,MainComponent::CloseChoice::cancel);owner->requestClose([&]{++closes;});require(closes==0&&MainComponentTestAccess::exportBusy(*owner),"Keep open cancelled external export");MainComponentTestAccess::closeChoice(*owner,MainComponent::CloseChoice::discard);owner->requestClose([&]{++closes;});});
  until([&]{return closes==1;},"Close during external preparation/export deadlocked");require(!cancelled.exists(),"Cancelled external export published output");
  onMain([&]{require(MainComponentTestAccess::load(*owner,json).wasOk(),"Pending recall admission failed");owner->requestClose([&]{++closes;});});until([&]{return closes==2;},"Close during pending external recall deadlocked");
  until([&]{MainComponentTestAccess::external(*owner).collectGarbage();return MainComponentTestAccess::external(*owner).retainedInstanceCount()==1;},"Retired/export VST3 owners leaked");
  std::cout<<"PASS missing/stale load preserves slot, close cancellation, pending export/recall teardown and owner collection\n";
  juce::MemoryBlock after;require(source.loadFileAsData(after)&&after==original,"Source WAV bytes changed");
  onMain([&]{require(MainComponentTestAccess::render(*owner,directory.getChildFile("shutdown.wav"),true).wasOk(),"Shutdown export rejected");owner.reset();});
  std::cout<<"PASS synchronous host destruction with pending private VST3 preparation, source bytes preserved\n";result=0;
 }catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';try{onMain([&]{owner.reset();});}catch(...){} }
 juce::MessageManager::callAsync([]{juce::MessageManager::getInstance()->stopDispatchLoop();});});
 std::cerr<<"stage: enter dispatch loop\n";juce::MessageManager::getInstance()->runDispatchLoop();std::cerr<<"stage: dispatch loop returned\n";driver.join();return result;
}
