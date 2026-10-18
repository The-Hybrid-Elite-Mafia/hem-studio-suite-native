#include "../Source/MainComponent.h"
#include "../Source/Plugins/DrumReplacerProcessor.h"
#include "../Source/Plugins/AmpCabinetProcessor.h"
#include <iostream>
#include <stdexcept>
struct MainComponentTestAccess {
 static hem::DrumReplacerProcessor* active(MainComponent& h){return dynamic_cast<hem::DrumReplacerProcessor*>(h.bank->tracks[0].chain.activeProcessor(0));}
 static void install(MainComponent& h){auto& chain=h.bank->tracks[0].chain;auto node=chain.stageNode(std::make_unique<hem::DrumReplacerProcessor>(),h.sampleRate,h.blockSize,chain.slotLatency());chain.installInitial(0,std::move(node),false);}
 static juce::Result start(MainComponent& h,const juce::File& f,int layer=0){return h.startDrumSampleLoad(f,layer,0,0,active(h));}
 static void poll(MainComponent& h){h.pollDrumSampleLoad();}
 static bool busy(MainComponent& h){return h.nativeResourceLoadBusy();}
 static void cancel(MainComponent& h){h.cancelDrumSampleLoad();}
 static void invalidate(MainComponent& h){++h.generation;}
 static void replaceDrum(MainComponent& h){auto& chain=h.bank->tracks[0].chain;auto node=chain.stageNode(std::make_unique<hem::DrumReplacerProcessor>(),h.sampleRate,h.blockSize,chain.slotLatency());chain.publishStopped(0,std::move(node));chain.collectRetired(0);}
 static bool guarded(MainComponent& h,const juce::File& f){return h.saveSessionFile(f).failed()&&h.loadSessionFile(f).failed()&&h.clearTrackSources(true).failed()&&h.resetSessionConfirmed().failed();}
 static hem::AmpCabinetProcessor* amp(MainComponent& h){return dynamic_cast<hem::AmpCabinetProcessor*>(h.bank->tracks[0].chain.activeProcessor(2));}
 static void installAmp(MainComponent& h){auto& chain=h.bank->tracks[0].chain;auto node=chain.stageNode(std::make_unique<hem::AmpCabinetProcessor>(),h.sampleRate,h.blockSize,chain.slotLatency());chain.installInitial(2,std::move(node),false);}
 static juce::Result startAmp(MainComponent& h,const juce::File& f){return h.startDrumSampleLoad(f,-1,0,2,amp(h));}
 static void requestDiscardClose(MainComponent& h,bool& approved){h.closePrompt=[](bool,bool,std::function<void(MainComponent::CloseChoice)> done){done(MainComponent::CloseChoice::discard);};h.requestClose([&approved]{approved=true;});}
 static void pumpClose(MainComponent& h){h.pumpCloseRequest();}
 static juce::String status(MainComponent& h){return h.message.getText();}
};
void check(bool b,const char* why){if(!b)throw std::runtime_error(why);}
void finish(MainComponent& h){const auto until=juce::Time::getMillisecondCounter()+10000;while(MainComponentTestAccess::busy(h)&&juce::Time::getMillisecondCounter()<until){MainComponentTestAccess::poll(h);juce::Thread::sleep(1);}check(!MainComponentTestAccess::busy(h),"loader timeout");}
int main(){juce::ScopedJuceInitialiser_GUI gui;try{
 const auto fixture=juce::File::getCurrentWorkingDirectory().getChildFile("drum-host-fixture.wav");
 {auto out=fixture.createOutputStream();check(out!=nullptr,"fixture open");juce::WavAudioFormat format;std::unique_ptr<juce::AudioFormatWriter> writer(format.createWriterFor(out.get(),48000,1,24,{},0));check(writer!=nullptr,"writer");out.release();juce::AudioBuffer<float> data(1,256);data.clear();data.setSample(0,0,.8f);check(writer->writeFromAudioSampleBuffer(data,0,256),"fixture write");}
 auto owner=std::make_unique<MainComponent>(false);auto& host=*owner;MainComponentTestAccess::install(host);auto* before=MainComponentTestAccess::active(host);
 check(MainComponentTestAccess::start(host,fixture).wasOk(),"start real WAV");check(MainComponentTestAccess::guarded(host,fixture),"host work not gated");
 auto* output=before->state.getParameter("output");output->setValueNotifyingHost(output->convertTo0to1(-7));finish(host);
 auto* current=MainComponentTestAccess::active(host);check(current!=before,"candidate not published");check(current->sampleDescriptors()[0].path==fixture.getFullPathName(),"resource missing");check(current->state.getRawParameterValue("output")->load()==-7,"latest scalar edit lost");check(current->clearSampleUnpublished(0,48000).failed(),"published node mutable");
 before=current;check(MainComponentTestAccess::start(host,fixture.getSiblingFile("missing-drum-file.wav"),1).wasOk(),"missing start");finish(host);check(MainComponentTestAccess::active(host)==before,"failed load replaced node");
 check(MainComponentTestAccess::start(host,fixture,1).wasOk(),"cancel start");MainComponentTestAccess::cancel(host);finish(host);check(MainComponentTestAccess::active(host)==before,"cancel replaced node");
 check(MainComponentTestAccess::start(host,fixture,1).wasOk(),"generation start");MainComponentTestAccess::invalidate(host);finish(host);check(MainComponentTestAccess::active(host)==before,"generation mismatch replaced node");
 check(MainComponentTestAccess::start(host,fixture,1).wasOk(),"second layer start");finish(host);current=MainComponentTestAccess::active(host);auto descriptors=current->sampleDescriptors();check(descriptors[0].path==fixture.getFullPathName()&&descriptors[1].path==fixture.getFullPathName(),"cloned first layer lost");
 MainComponentTestAccess::installAmp(host);auto* oldAmp=MainComponentTestAccess::amp(host);
 check(MainComponentTestAccess::startAmp(host,fixture).wasOk(),"IR start");finish(host);auto* amp=MainComponentTestAccess::amp(host);
 check(amp!=oldAmp&&amp->irDescriptor().path==fixture.getFullPathName(),"IR not published");check(amp->state.getRawParameterValue("cabModel")->load()==3,"IR custom selector lost");
 oldAmp=amp;check(MainComponentTestAccess::startAmp(host,fixture).wasOk(),"IR second start");auto* cab=amp->state.getParameter("cabModel");cab->setValueNotifyingHost(cab->convertTo0to1(1));finish(host);amp=MainComponentTestAccess::amp(host);check(amp->state.getRawParameterValue("cabModel")->load()==1,"cabinet edit during load lost");
 check(amp->loadIRUnpublished(fixture,48000).failed(),"published amp mutable");
 check(MainComponentTestAccess::start(host,fixture,2).wasOk(),"stale processor start");MainComponentTestAccess::replaceDrum(host);auto* replacement=MainComponentTestAccess::active(host);finish(host);check(MainComponentTestAccess::active(host)==replacement,"stale pointer replaced new insert");
 before=MainComponentTestAccess::active(host);check(MainComponentTestAccess::start(host,fixture,2).wasOk(),"close cancellation start");bool closed=false;MainComponentTestAccess::requestDiscardClose(host,closed);finish(host);MainComponentTestAccess::pumpClose(host);MainComponentTestAccess::pumpClose(host);check(closed&&MainComponentTestAccess::active(host)==before,"close cancellation/publication gate failed");
 std::cout<<"PASS Amp shared worker IR load, Custom auto-selection, concurrent model edit retention and publication seal.\n";
 std::cout<<"PASS real host background WAV load, private clone, latest scalar retention, publication seal, second layer preservation, invalid-file/cancel/generation rollback, stale expected-pointer refusal, discard-close cancellation, session/clear/reset work gates. Audio device startup disabled; native chooser/hardware/UI interaction NOT RUN.\n";return 0;
 }catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}





