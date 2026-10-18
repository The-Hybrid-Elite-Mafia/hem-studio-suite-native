#include "../Source/Plugins/DrumReplacerProcessor.h"
#include "../Source/Systems/NativeProcessorState.h"
#include <iostream>
#include <stdexcept>
#include <thread>
#include <cstdlib>
#include <new>
static thread_local bool probe=false;
static thread_local size_t allocations=0,deallocations=0;
void* operator new(std::size_t n){if(probe)++allocations;if(auto* p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void* operator new[](std::size_t n){return ::operator new(n);}
void operator delete(void* p) noexcept{if(probe&&p)++deallocations;std::free(p);}
void operator delete[](void* p) noexcept{::operator delete(p);}
void operator delete(void* p,std::size_t) noexcept{::operator delete(p);}
void operator delete[](void* p,std::size_t) noexcept{::operator delete(p);}
static void check(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
static void wave(const juce::File& file,float amplitude){
 auto stream=file.createOutputStream();check(bool(stream),"wave open");stream->setPosition(0);stream->truncate();
 juce::WavAudioFormat format;std::unique_ptr<juce::AudioFormatWriter> writer(format.createWriterFor(stream.release(),48000,2,24,{},0));check(bool(writer),"wave writer");
 juce::AudioBuffer<float> b(2,480);b.clear();b.setSample(0,0,amplitude);b.setSample(1,0,-amplitude);check(writer->writeFromAudioSampleBuffer(b,0,480),"wave write");
}
int main(){try{
 const auto root=juce::File::getCurrentWorkingDirectory();const auto file=root.getChildFile("drum-fixture.wav");wave(file,.5f);
 hem::DrumReplacerProcessor p;p.prepareToPlay(48000,64);check(p.processingReady(),"empty prepare");check(p.getLatencySamples()==144,"latency 48k");
 for(int k=0;k<4;++k)check(p.loadSampleUnpublished(k,file,48000).wasOk(),"load layer");
 hem::NativeProcessorSnapshot s;check(p.tryCaptureNativeState(s).wasOk(),"capture");check(s.native->resources.size()==4,"four resources");
 for(auto& d:s.native->resources)check(d.frames==480&&d.channels==2&&d.sha256.length()==64,"resource metadata");
 juce::MemoryBlock binary;p.getStateInformation(binary);check(binary.getSize()>0,"binary capture");hem::NativeProcessorSnapshot decoded;check(hem::NativeProcessorState::decodeBinary(p,binary.getData(),binary.getSize(),decoded).wasOk(),"binary decode");
 auto invalid=s;invalid.parameters=s.parameters.createCopy();invalid.parameters.getChild(0).setProperty("value",99.,nullptr);check(p.validateNativeState(invalid).failed(),"invalid scalar rejected");
 hem::DrumReplacerProcessor busy;hem::NativeRestoreContext busyContext;busyContext.sampleRate=48000;busyContext.blockSize=64;std::atomic<bool> entered{false},release{false};busyContext.cancelled=[&] {entered.store(true);while(!release.load())std::this_thread::yield();return false;};std::thread worker([&] {busy.restoreNativeStateOffThread(s,busyContext);});while(!entered.load())std::this_thread::yield();hem::NativeProcessorSnapshot pendingOutput;pendingOutput.committedRevision=123;const bool pendingRejected=busy.tryCaptureNativeState(pendingOutput).failed()&&pendingOutput.committedRevision==123;release.store(true);worker.join();check(pendingRejected,"pending capture immediate and output unchanged");
 hem::DrumReplacerProcessor copy;hem::NativeRestoreContext context;context.sampleRate=48000;context.blockSize=64;
 check(copy.restoreNativeStateOffThread(s,context).wasOk(),"restore");copy.prepareToPlay(48000,64);copy.prepareToPlay(96000,64);copy.prepareToPlay(48000,64);
 hem::NativeProcessorSnapshot restored;check(copy.tryCaptureNativeState(restored).wasOk(),"reprepare capture");check(restored.native->resources[0].sha256==s.native->resources[0].sha256,"reprepare identity");
 juce::AudioBuffer<float> b(2,256);b.clear();b.setSample(0,0,1);b.setSample(1,0,1);juce::MidiBuffer midi;probe=true;copy.processBlock(b,midi);hem::drum::DetectorHistorySnapshot h;const bool historyReady=copy.detectorHistory(h);probe=false;check(allocations==0&&deallocations==0,"processor callback/history snapshot allocation-free");check(historyReady&&h.count==1,"real processor history");
 check(std::abs(b.getSample(0,144)-2.f)<.001f&&std::abs(b.getSample(1,144)+2.f)<.001f,"four stereo layers exact alignment");
 check(copy.hitCount()==1,"real detector telemetry");hem::PluginTelemetry::Levels levels;check(copy.pluginTelemetry()&&copy.pluginTelemetry()->read(levels)&&levels.valid,"stereo telemetry exists and valid");check(levels.peak[0]==1&&levels.peak[1]==1&&std::abs(levels.peak[2]-2)<.001f&&std::abs(levels.peak[3]-2)<.001f,"real stereo input/output peaks");check(copy.loadSampleUnpublished(0,file,48000).failed(),"live loading forbidden");check(copy.restoreNativeStateOffThread(s,context).failed(),"live restore forbidden");
 hem::DrumReplacerProcessor sealed;sealed.prepareToPlay(48000,64);sealed.sealNativeStateForPublication();check(sealed.loadSampleUnpublished(0,file,48000).failed(),"sealed before first callback");
 wave(file,.25f);hem::DrumReplacerProcessor changed;check(changed.restoreNativeStateOffThread(s,context).failed(),"changed hash rejected");
 hem::DrumReplacerProcessor missing;check(missing.loadSampleUnpublished(0,root.getChildFile("missing.wav"),48000).failed(),"missing rejected");
 context.cancelled=[] {return true;};check(missing.restoreNativeStateOffThread(s,context).failed(),"cancelled restore");
 std::cout<<"PASS DrumResource: four real layers, stereo alignment, state identity, repeated/rate-changed preparation, changed/missing resources, scalar validation, publication restrictions, cancellation\n";return 0;
 }catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<"\n";return 1;}}





