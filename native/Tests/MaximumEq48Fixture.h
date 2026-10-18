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
 static bool defaultSerial(MainComponent& h){return h.trackScheduler.helperCount()==0;}
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
    if(dynamic){
     auto* enabled=p->state.getParameter("enabled");if(!enabled)throw std::runtime_error("missing enable");enabled->setValueNotifyingHost(1.f);
     if(p->state.getRawParameterValue("enabled")->load()!=1.f)throw std::runtime_error("EQ not enabled");
     p->prepareToPlay(h.sampleRate,h.blockSize);juce::AudioBuffer<float> wet(2,4096);juce::MidiBuffer midi;
     for(int c=0;c<2;++c)wet.copyFrom(c,0,pcm,c,0,4096);
     p->processBlock(wet,midi);double delta=0;
     for(int c=0;c<2;++c)for(int n=0;n<4096;++n){const double d=wet.getSample(c,n)-pcm.getSample(c,n);if(!std::isfinite(d))throw std::runtime_error("nonfinite active proof");delta+=d*d;}
     if(delta<=1e-10)throw std::runtime_error("EQ wet output is dry");
     std::cout<<"ACTIVE_EQ track="<<track+1<<" slot="<<slot+1<<" enabled=1 gain_db="<<(track%3-1)*1.5f+1<<" range_db=3 wet_minus_dry_energy="<<delta<<'\n';
    }
    auto node=t.chain.stageNode(std::move(p),h.sampleRate,h.blockSize,t.chain.slotLatency());t.chain.installInitial(slot,std::move(node),false);
   }
  }
 }
 static void verifyRack(MainComponent& h){int active=0;for(auto& t:h.bank->tracks)for(int slot=0;slot<8;++slot){auto* p=t.chain.activeProcessor(slot);if(!p||t.chain.isBypassed(slot)||!p->processingReady()||p->state.getRawParameterValue("enabled")->load()!=1.f)throw std::runtime_error("actual104 rack not active");++active;}if(active!=104)throw std::runtime_error("wrong active rack count");}
 static void controls(MainComponent& h,int block){for(int i=0;i<13;++i){auto& t=h.bank->tracks[size_t(i)];t.trimDb=float((i+block/16)%5-4);t.muted=(block>=50&&block<58&&i%3==0);t.solo=(block>=70&&block<76&&i==2);if(block==40)t.chain.bypass(3,true);if(block==60)t.chain.bypass(3,false);}
  if(block==24){auto& chain=h.bank->tracks[1].chain;auto node=chain.stageNode(std::make_unique<ExternalProbe>(),h.sampleRate,h.blockSize,chain.slotLatency());if(!chain.publish(1,std::move(node)))throw std::runtime_error("external transition publish");}
 }
};

void require(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
