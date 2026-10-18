#include "../Source/SessionState.h"
#include "../Source/Systems/OfflineMixRenderer.h"
#include <iostream>
#include <stdexcept>
using namespace hem;
static void check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
static void wave(const juce::File& file,int frames,float level){
 auto stream=file.createOutputStream();check(bool(stream),"create fixture");
 juce::WavAudioFormat format;std::unique_ptr<juce::AudioFormatWriter> writer(format.createWriterFor(stream.release(),48000,2,24,{},0));
 check(bool(writer),"create writer");juce::AudioBuffer<float> b(2,frames);b.clear();b.setSample(0,0,level);b.setSample(1,0,level*.5f);
 check(writer->writeFromAudioSampleBuffer(b,0,frames),"write fixture");
}
int main(){juce::ScopedJuceInitialiser_GUI gui;try{
 const auto folder=juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("hem-drum-session-"+juce::Uuid().toString());
 check(folder.createDirectory().wasOk(),"fixture directory");
 const auto sample=folder.getChildFile("sample.wav"),source=folder.getChildFile("source.wav"),session=folder.getChildFile("session.xml");
 wave(sample,480,.25f);wave(source,4800,.8f);
 auto live=PluginRegistry::create("drum-replacer");check(live&&PluginRegistry::idFor(*live)=="drum-replacer","registered identity");
 auto* drum=dynamic_cast<DrumReplacerProcessor*>(live.get());check(drum!=nullptr,"real processor factory");
 check(drum->loadSampleUnpublished(0,sample,48000).wasOk(),"real sample load");
 NativeProcessorSnapshot frozen;check(NativeProcessorState::capture(*drum,frozen).wasOk(),"whole state capture");
 SessionSnapshot snapshot;MonitorProcessor monitor;snapshot.monitorParameters=SessionState::captureParameters(monitor);
 snapshot.tracks[0].source=source;auto& slot=snapshot.tracks[0].slots[0];slot.pluginId="drum-replacer";static_cast<NativeProcessorSnapshot&>(slot)=NativeProcessorState::deepCopy(frozen);
 SessionState::save(snapshot,session);auto restored=SessionState::load(session);
 check(restored.tracks[0].slots[0].native->resources[0].sha256==frozen.native->resources[0].sha256,"session resource identity");
 auto* output=drum->state.getParameter("output");output->setValueNotifyingHost(output->convertTo0to1(-12));
 OfflineMixOptions options;options.tailSeconds=.02;options.allowTailTruncation=true;
 const auto a=folder.getChildFile("a.wav"),b=folder.getChildFile("b.wav");
 auto first=OfflineMixRenderer::render(snapshot,a,options),second=OfflineMixRenderer::render(restored,b,options);
 check(first.status.wasOk()&&second.status.wasOk(),"actual private export");
 check(first.frames==second.frames&&first.frames==5760,"export frame count");
 check(juce::SHA256(a).toHexString()==juce::SHA256(b).toHexString(),"frozen export independent of live edits and session roundtrip");
 check(OfflineMixRenderer::render(restored,a,options).status.failed(),"no overwrite export");
 check(juce::SHA256(sample).toHexString()==frozen.native->resources[0].sha256,"sample bytes preserved");
 std::cout<<"PASS registered Drum session recall, frozen private export, live-edit isolation and no-overwrite\n";
 // Test-only UUID-owned directory; no user files can be present by construction.
 folder.deleteRecursively();return 0;
 }catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<"\n";return 1;}}
