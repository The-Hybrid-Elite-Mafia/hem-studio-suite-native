#include "../Source/MainComponent.h"
#include "../Source/Plugins/ReferenceMatchProcessor.h"
#include <random>
#include <iostream>
struct MainComponentTestAccess {
 static void playFixture(MainComponent& app,const juce::File& f){auto& t=app.bank->tracks[0].transport();t.prepareFile(f,app.formats,app.diskThread,app.sampleRate,app.blockSize);t.start();}
 static void stopFixture(MainComponent& app){app.bank->tracks[0].transport().stop();}
 static juce::String status(MainComponent& app){return app.referenceStatus;}
 static void bind(MainComponent& app,hem::ReferenceMatchProcessor& p){app.bindReferenceEditor(p,0,0);}
 static void prepare(MainComponent& app){app.prepareOfflineSession();}
 static hem::ReferenceMatchProcessor* install(MainComponent& app,std::unique_ptr<hem::ReferenceMatchProcessor> p){auto& chain=app.bank->tracks[0].chain;auto node=chain.stageNode(std::move(p),app.sampleRate,app.blockSize,chain.slotLatency());if(!node)throw std::runtime_error("stage failed");const MainComponent::GraphControlGuard lock(app);if(!chain.publishStopped(0,std::move(node)))throw std::runtime_error("publish failed");return dynamic_cast<hem::ReferenceMatchProcessor*>(chain.activeProcessor(0));}
 static juce::Result start(MainComponent& app,int mode,const juce::File& file,hem::ModuleProcessor* expected){return app.startReferenceWork(mode,file,0,0,expected);}
 static bool wait(MainComponent& app){for(int i=0;i<2000;++i){app.pollReferenceOperation();if(!app.referenceOperationBusy())return true;juce::Thread::sleep(5);}return false;}
 static hem::ReferenceMatchProcessor* current(MainComponent& app){return dynamic_cast<hem::ReferenceMatchProcessor*>(app.bank->tracks[0].chain.activeProcessor(0));}
 static void render(MainComponent& app,juce::AudioBuffer<float>& b){app.renderReferenceAudition(b,b.getNumSamples());}
 static void action(MainComponent& app,int action,hem::ModuleProcessor* expected){app.referenceAction(action,0,0,expected);}
 static bool audition(MainComponent& app){return app.referenceAuditionAudio.load()!=nullptr;}
 static void cancel(MainComponent& app){app.cancelReferenceOperation();}
};
static void check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
int main(){try{juce::ScopedJuceInitialiser_GUI juce;auto appOwner=std::make_unique<MainComponent>(false);auto& app=*appOwner;MainComponentTestAccess::prepare(app);
 std::vector<float> source(48000),reference(48000);std::mt19937 rng(9);for(size_t i=0;i<source.size();++i){source[i]=float(int(rng()%10000)-5000)/25000;reference[i]=.4f*source[i]+(i?.5f*reference[i-1]:0);}
 const float* s[]={source.data()},*r[]={reference.data()};auto processor=std::make_unique<hem::ReferenceMatchProcessor>();check(processor->setSourceUnpublished(hem::reference_match::analyze(s,1,source.size(),48000)).wasOk(),"source");check(processor->setReferenceUnpublished(s,1,source.size(),48000).wasOk()&&processor->matchUnpublished().wasOk(),"initial neutral match");auto* original=MainComponentTestAccess::install(app,std::move(processor));check(original,"installed private fixture");
 auto file=juce::File::getCurrentWorkingDirectory().getChildFile("reference-host-fixture.wav");file.deleteFile();auto output=file.createOutputStream();juce::WavAudioFormat format;std::unique_ptr<juce::AudioFormatWriter> writer(format.createWriterFor(output.get(),48000,1,24,{},0));check(bool(writer),"writer");output.release();check(writer->writeFromFloatArrays(r,1,int(reference.size())),"write fixture");writer.reset();
 MainComponentTestAccess::bind(app,*original);
 MainComponentTestAccess::action(app,1,original);check(!original->captureInProgress(),"stopped transport capture refused");
 check(original->editorActions.status().contains("Play source audio"),"capture prerequisite exposed inside plugin");
 MainComponentTestAccess::playFixture(app,file);original->editorActions.loadReference();
 check(original->editorActions.status().contains("STOP PLAYBACK"),"Load during playback explains prerequisite in plugin without opening picker");
 MainComponentTestAccess::stopFixture(app);
 check(MainComponentTestAccess::start(app,0,file,original).wasOk(),"host reference load begins");check(MainComponentTestAccess::wait(app),"host load settles");check(MainComponentTestAccess::current(app)==original,"Load preserves live processor until Match");
 check(original->editorActions.referenceLabel()==file.getFileName(),"loaded draft filename exposed in plugin before Match");
 check(original->editorActions.status().contains("Reference loaded"),"load result exposed inside plugin");
 check(MainComponentTestAccess::start(app,2,{},original).wasOk(),"host Match begins");check(MainComponentTestAccess::wait(app),"host Match settles");auto* matched=MainComponentTestAccess::current(app);check(matched&&matched!=original,"Match publishes private node");check(matched->referenceIdentityOffThread().path==file.getFullPathName(),"committed file identity");
 hem::NativeProcessorSnapshot before;check(hem::NativeProcessorState::capture(*matched,before).wasOk(),"before audition state");
 check(MainComponentTestAccess::start(app,5,{},matched).wasOk(),"audition worker begins");check(MainComponentTestAccess::wait(app)&&MainComponentTestAccess::audition(app),"audition prepared");juce::AudioBuffer<float> listening(2,1024);for(int c=0;c<2;++c)juce::FloatVectorOperations::fill(listening.getWritePointer(c),.125f,1024);MainComponentTestAccess::render(app,listening);check(listening.getMagnitude(0,0,1024)>.001f&&listening.getMagnitude(0,0,1024)<=.50001f,"bounded actual audition PCM");
 hem::NativeProcessorSnapshot after;check(hem::NativeProcessorState::capture(*matched,after).wasOk()&&before.parameters.isEquivalentTo(after.parameters)&&before.matchProfile->isEquivalentTo(*after.matchProfile),"audition excluded from production state");
 MainComponentTestAccess::action(app,4,matched);check(!MainComponentTestAccess::audition(app),"SOURCE detaches reference");listening.clear();MainComponentTestAccess::render(app,listening);check(listening.getMagnitude(0,0,1024)==0,"SOURCE leaves listening input untouched");
 check(MainComponentTestAccess::start(app,3,{},matched).wasOk(),"host Revert begins");check(MainComponentTestAccess::wait(app),"host Revert settles");auto* reverted=MainComponentTestAccess::current(app);check(reverted&&reverted!=matched&&reverted->referenceIdentityOffThread().path.isEmpty(),"Revert restores original no-file profile");
 // Stale pointer action must be rejected before RTTI/dereference.
 MainComponentTestAccess::action(app,2,matched);check(MainComponentTestAccess::current(app)==reverted,"stale editor action harmless");MainComponentTestAccess::cancel(app);
 std::cout<<"PASS ReferenceMatchHostTests: Load/Match/Revert, isolated audition, stopped capture and stale actions\n";return 0;
 }catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
