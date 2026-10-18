#include "../Source/MainComponent.h"
#include "../Source/Plugins/BandProcessors.h"
#include <iostream>
#include <cstring>
#include <cfenv>
#include <chrono>
#include <thread>
#include <xmmintrin.h>
namespace hem {struct TrackRenderSchedulerTestAccess {
 inline static int starts=0;
 static bool failThird(juce::Thread& thread){return ++starts!=3&&thread.startThread(juce::Thread::Priority::high);}
 static void fail(TrackRenderScheduler& scheduler){starts=0;scheduler.startWorker=failThird;}
 static void clear(TrackRenderScheduler& scheduler){scheduler.startWorker=nullptr;}
 static void stall(TrackRenderScheduler& scheduler,void(*hook)()noexcept){scheduler.beforeHelperDrain=hook;}
};}
namespace hem {struct ReadAheadTransportTestAccess {static void readHook(ReadAheadTransport& transport,std::function<void()> hook){transport.setReadHookForTesting(std::move(hook));}};}
struct ExternalProbe final:hem::ModuleProcessor {
 std::thread::id expected=std::this_thread::get_id();inline static std::atomic<int> wrongThread{0};
 ExternalProbe():ModuleProcessor("scheduler-test-external","Scheduler external affinity",{}){}
 bool isExternal()const noexcept override{return true;}
 void prepareToPlay(double,int)override{setLatencySamples(0);}
 void processBlock(juce::AudioBuffer<float>&,juce::MidiBuffer&)override{if(std::this_thread::get_id()!=expected)++wrongThread;}
 juce::AudioProcessorEditor* createEditor()override{return nullptr;}
 float meter()const noexcept override{return 0;}juce::String meterLabel()const override{return {};}
};
struct SchedulerSourceFile {juce::File file;~SchedulerSourceFile(){if(file!=juce::File())file.deleteFile();}};
struct MainComponentTestAccess {
 static bool helpers(MainComponent& h,int count){h.trackRenderTestBudget=juce::Time::getHighResolutionTicksPerSecond();return h.trackScheduler.configure(count);}
 static void realtimeBudget(MainComponent& h){h.trackRenderTestBudget=0;}
 static uint64_t completed(MainComponent& h){return h.trackRenderCompleted.load();}
 static uint64_t faults(MainComponent& h){return h.trackRenderFaults.load();}
 static uint64_t silences(MainComponent& h){return h.trackRenderSilencedCallbacks.load();}
 static uint64_t deadlines(MainComponent& h){return h.trackRenderDeadlineMisses.load();}
 static uint64_t sourceDropouts(MainComponent& h){return h.transportDropouts.load();}
 static uint64_t controlSilences(MainComponent& h){return h.controlSilencedCallbacks.load();}
 static bool faulted(MainComponent& h){return h.trackScheduler.faulted();}
 static void stall(MainComponent& h,void(*hook)()noexcept){hem::TrackRenderSchedulerTestAccess::stall(h.trackScheduler,hook);h.trackRenderTestBudget=juce::Time::getHighResolutionTicksPerSecond()/200;}
 static void recover(MainComponent& h){h.recoverTrackRenderFault();}
 static void seek(MainComponent& h,double p){h.seekAll(p);}
 static void pollTransport(MainComponent& h){h.pollTransport();}
 static bool transportPending(MainComponent& h){return h.transportBusy();}
 static void cancelTransport(MainComponent& h){h.cancelTransport();}
 static void changeGeneration(MainComponent& h){const MainComponent::GraphControlGuard guard(h);++h.generation;}
 static double position(MainComponent& h,int index){return h.bank->tracks[size_t(index)].transport().getCurrentPosition();}
 static uint64_t underruns(MainComponent& h){return h.transportDropouts.load();}
 static void readHook(MainComponent& h,int index,std::function<void()> hook){hem::ReadAheadTransportTestAccess::readHook(h.bank->tracks[size_t(index)].transport(),std::move(hook));}
 static void startAllForTest(MainComponent& h){for(auto& t:h.bank->tracks)t.transport().start();}
 static void play(MainComponent& h){h.playAll();}
 static void replaceBank(MainComponent& h,std::atomic<bool>& complete){auto replacement=std::make_unique<hem::TrackBank>();{const MainComponent::GraphControlGuard guard(h);h.bank.swap(replacement);}complete=true;}
 static bool callbackAdmissionClosed(MainComponent& h){return (h.callbackAdmission.load()&2)!=0;}
 static void callback(MainComponent& h,juce::AudioBuffer<float>& buffer,int n){h.getNextAudioBlock({&buffer,0,n});}
 static void rate(MainComponent& h,double rate,int block=256){h.prepareToPlay(block,rate);}
 static void setup(MainComponent& h,SchedulerSourceFile& sources,juce::AudioBuffer<float>& pcm,bool dynamic=false,int slots=8,int processingTracks=13){
  h.hardwareOutputs=2;
  sources.file=juce::File::getSpecialLocation(juce::File::tempDirectory).getNonexistentChildFile("hem-scheduler-read-ahead",".wav",false);
  juce::WavAudioFormat wav;auto stream=sources.file.createOutputStream();if(!stream)throw std::runtime_error("fixture stream");
  std::unique_ptr<juce::AudioFormatWriter> writer(wav.createWriterFor(stream.release(),h.sampleRate,2,32,{},0));if(!writer||!writer->writeFromAudioSampleBuffer(pcm,0,pcm.getNumSamples()))throw std::runtime_error("fixture WAV");writer.reset();
  h.looping=true;
  for(int track=0;track<13;++track){auto& t=h.bank->tracks[size_t(track)];
   t.playback->prepareFile(sources.file,h.formats,h.diskThread,h.sampleRate,h.blockSize,true);t.sourceRate=h.sampleRate;t.loaded=true;t.transport().start();
   for(int slot=0;slot<(track<processingTracks?slots:0);++slot){std::unique_ptr<hem::ModuleProcessor> p;if(dynamic)p=std::make_unique<hem::DynamicEq8Processor>();else p=std::make_unique<hem::MidSideEqProcessor>();
    for(auto spec:p->parameterSpecs())if(juce::String(spec.id).endsWith("Gain")){auto* param=p->state.getParameter(spec.id);param->setValueNotifyingHost(param->convertTo0to1((track%3-1)*1.5f+1));}
    if(dynamic)for(auto spec:p->parameterSpecs()){const juce::String id(spec.id);if(id.endsWith("Range")||id.endsWith("DynamicOn")){auto* param=p->state.getParameter(spec.id);param->setValueNotifyingHost(param->convertTo0to1(id.endsWith("Range")?3.f:1.f));}}
    auto node=t.chain.stageNode(std::move(p),h.sampleRate,h.blockSize,t.chain.slotLatency());t.chain.installInitial(slot,std::move(node),false);
   }
  }
 }
 static void controls(MainComponent& h,int block){for(int i=0;i<13;++i){auto& t=h.bank->tracks[size_t(i)];t.trimDb=float((i+block/16)%5-4);t.muted=(block>=50&&block<58&&i%3==0);t.solo=(block>=70&&block<76&&i==2);if(block==40)t.chain.bypass(3,true);if(block==60)t.chain.bypass(3,false);}
  if(block==24){auto& chain=h.bank->tracks[1].chain;auto node=chain.stageNode(std::make_unique<ExternalProbe>(),h.sampleRate,h.blockSize,chain.slotLatency());if(!chain.publish(1,std::move(node)))throw std::runtime_error("external transition publish");}
 }
};
void require(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
struct Probe{std::array<std::atomic<int>,13> hits{};std::atomic<int> bad{0};int round=FE_TONEAREST;unsigned simd=0;};
void probe(void* raw,int index)noexcept{auto& p=*static_cast<Probe*>(raw);if(std::fegetround()!=p.round||(_mm_getcsr()&0xe040)!=(p.simd&0xe040))++p.bad;++p.hits[size_t(index)];if(index==2)std::this_thread::sleep_for(std::chrono::milliseconds(1));}
int main(int argc,char**){juce::ScopedJuceInitialiser_GUI gui;try{
 std::array<int,13> indices{};for(int i=0;i<13;++i)indices[size_t(i)]=i;
 hem::TrackRenderScheduler scheduler;
 const auto initialSimd=_mm_getcsr();
 hem::TrackRenderSchedulerTestAccess::fail(scheduler);require(!scheduler.configure(5)&&scheduler.helperCount()==0,"startup failure did not roll back to serial");hem::TrackRenderSchedulerTestAccess::clear(scheduler);
 for(int helpers:{0,2,3,5,7,12,0,2}){require(scheduler.configure(helpers),"worker startup");for(int cycle=0;cycle<24;++cycle){Probe p;p.round=(cycle&1)?FE_DOWNWARD:FE_TONEAREST;std::fesetround(p.round);_mm_setcsr((_mm_getcsr()&~0x8040u)|((cycle&2)?0x8040u:0));p.simd=_mm_getcsr();const int jobs=cycle%3==0?1:13;const bool done=scheduler.run(&p,probe,indices.data(),jobs);if(!done)scheduler.quiesce();require(done,"bare scheduler unexpectedly faulted");require(!p.bad.load(),"worker rounding/FTZ/DAZ mismatch");for(int i=0;i<13;++i)require(p.hits[size_t(i)]==(i<jobs?1:0),"not exactly once or returned before completion");}}std::fesetround(FE_TONEAREST);_mm_setcsr(initialSimd);
 std::cout<<"PASS exactly-once batches, delayed job barrier, repeated pool teardown/restart, serial fallback and rounding parity\n";
 juce::AudioBuffer<float> pcm(2,65536);for(int i=0;i<pcm.getNumSamples();++i){pcm.setSample(0,i,float(.012*std::sin(i*.071)+.008*std::sin(i*.193)));pcm.setSample(1,i,float(.015*std::sin(i*.053)));}
 for(int helpers:{2,3,5,7,12}){
  SchedulerSourceFile serialSources,parallelSources;
  auto serial=std::make_unique<MainComponent>(false),parallel=std::make_unique<MainComponent>(false);require(MainComponentTestAccess::helpers(*serial,0)&&MainComponentTestAccess::helpers(*parallel,helpers),"host workers");
  MainComponentTestAccess::setup(*serial,serialSources,pcm);MainComponentTestAccess::setup(*parallel,parallelSources,pcm);
  juce::AudioBuffer<float> a(2,257),b(2,257);double elapsed=0;std::vector<double> timings;
  for(int block=0;block<96;++block){MainComponentTestAccess::controls(*serial,block);MainComponentTestAccess::controls(*parallel,block);const int n=block%9==0?257:256;a.clear();b.clear();serial->getNextAudioBlock({&a,0,n});auto start=juce::Time::getHighResolutionTicks();parallel->getNextAudioBlock({&b,0,n});double ms=juce::Time::highResolutionTicksToSeconds(juce::Time::getHighResolutionTicks()-start)*1000;elapsed+=ms;if(block>=32)timings.push_back(ms);
   for(int ch=0;ch<2;++ch)require(std::memcmp(a.getReadPointer(ch),b.getReadPointer(ch),size_t(n)*sizeof(float))==0,"dense host serial/parallel PCM differs");require(!MainComponentTestAccess::faults(*parallel)&&MainComponentTestAccess::completed(*parallel)==uint64_t(block+1),"parity passed through fault silence");
  }
  std::sort(timings.begin(),timings.end());std::cout<<"PASS 13x8 active MidSide EQ 96k host PCM bit parity, helpers="<<helpers<<" / 96 blocks, control changes/mute/solo/bypass and split callback (generous test budget; NOT realtime acceptance)\n";
  require(ExternalProbe::wrongThread.load()==0,"external graph escaped callback thread during/after transition");
  if(argc>1)std::cout<<"Diagnostic only (two hosts/GUI workers alive), milliseconds median="<<timings[timings.size()/2]<<" max="<<timings.back()<<" total="<<elapsed<<"; NOT quiet dense acceptance\n";
 }
 return 0;
 }catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
