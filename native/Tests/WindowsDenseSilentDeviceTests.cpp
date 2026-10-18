// Direct production device bridge driving the host graph. Finished output is
// silenced by this test wrapper; this is not an audible listening test.
#define main unusedSchedulerCorrectnessMain
#include "TrackSchedulerTests.cpp"
#undef main
#include "../Source/Systems/NativeAudioDeviceHost.h"
#include <windows.h>
struct DenseSilentSource final:juce::AudioSource {
 MainComponent& host;std::array<double,4096> timings{};std::atomic<int> count{-96};std::atomic<bool> armed{false};
 explicit DenseSilentSource(MainComponent& value):host(value){}
 void prepareToPlay(int block,double rate)override{host.prepareToPlay(block,rate);}
 void releaseResources()override{host.releaseResources();}
 void getNextAudioBlock(const juce::AudioSourceChannelInfo& info)override{
  if(armed.load(std::memory_order_acquire)){
   const int index=count.load(std::memory_order_relaxed);
   if(index<4096){const auto begin=juce::Time::getHighResolutionTicks();host.getNextAudioBlock(info);
    const double elapsed=juce::Time::highResolutionTicksToSeconds(juce::Time::getHighResolutionTicks()-begin)*1000;
    if(index>=0)timings[size_t(index)]=elapsed;count.store(index+1,std::memory_order_release);}
  }
  info.clearActiveBufferRegion();
 }
};
int main(){juce::ScopedJuceInitialiser_GUI gui;try{
 SchedulerSourceFile sources;auto host=std::make_unique<MainComponent>(false);
 DenseSilentSource source(*host);hem::NativeAudioDeviceHost deviceHost(source);
 auto error=deviceHost.initialise(0,2);require(error.isEmpty(),error.toRawUTF8());
 auto* device=deviceHost.getCurrentAudioDevice();require(device!=nullptr,"No output device");
 require(device->getAvailableBufferSizes().contains(256),"Default output does not support256 frames");
 auto setup=deviceHost.getAudioDeviceSetup();setup.bufferSize=256;
 error=deviceHost.setAudioDeviceSetup(setup,false);require(error.isEmpty(),error.toRawUTF8());
 device=deviceHost.getCurrentAudioDevice();require(device!=nullptr,"No configured output");
 const double rate=device->getCurrentSampleRate();const int block=device->getCurrentBufferSizeSamples();
 require(block==256,"Device did not accept requested256 frames");
 juce::AudioBuffer<float> pcm(2,65536);for(int i=0;i<pcm.getNumSamples();++i){pcm.setSample(0,i,float(.012*std::sin(i*.071)+.008*std::sin(i*.193)));pcm.setSample(1,i,float(.015*std::sin(i*.053)));}
 require(MainComponentTestAccess::helpers(*host,12),"Render worker startup");MainComponentTestAccess::realtimeBudget(*host);
 MainComponentTestAccess::setup(*host,sources,pcm,true,8,13);
 const auto completed0=MainComponentTestAccess::completed(*host),faults0=MainComponentTestAccess::faults(*host);
 const auto silence0=MainComponentTestAccess::silences(*host),deadlines0=MainComponentTestAccess::deadlines(*host),controls0=MainComponentTestAccess::controlSilences(*host);
 std::cout<<"Direct backend: "<<deviceHost.getCurrentAudioDeviceType()<<" / device: "<<device->getName()<<" / rate: "<<rate<<" / frames: "<<block<<" / active Dynamic EQ:104 / requested helpers:12\n";
 source.armed.store(true,std::memory_order_release);const auto until=juce::Time::getMillisecondCounterHiRes()+60000;
 while(source.count.load(std::memory_order_acquire)<4096&&juce::Time::getMillisecondCounterHiRes()<until){MSG message;while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){TranslateMessage(&message);DispatchMessageW(&message);}deviceHost.pollDeviceState();juce::Thread::sleep(10);}
 deviceHost.closeAudioDevice(); // joins callback before non-atomic timing reads
 require(source.count>=4096,"Device callback timeout");
 require(deviceHost.callbackErrors()==0&&deviceHost.silentCallbacks()==0&&deviceHost.oversizedCallbacks()==0&&deviceHost.invalidChannelCallbacks()==0,"Direct device bridge reported error/silence");
 const auto completed=MainComponentTestAccess::completed(*host)-completed0,faults=MainComponentTestAccess::faults(*host)-faults0;
 const auto silences=MainComponentTestAccess::silences(*host)-silence0,deadlines=MainComponentTestAccess::deadlines(*host)-deadlines0,controls=MainComponentTestAccess::controlSilences(*host)-controls0;
 int misses=0;double total=0;for(double duration:source.timings){total+=duration;if(duration>block*1000./rate)++misses;}std::sort(source.timings.begin(),source.timings.end());
 std::cout<<"4096 native-device host renders: mean_ms="<<total/4096<<", p99_ms="<<source.timings[4055]<<", max_ms="<<source.timings.back()<<", deadline_ms="<<block*1000./rate<<", misses="<<misses<<"\n";
 std::cout<<"Production budget;96 warmup+4096 measured: completed="<<completed<<", overload_epochs="<<faults<<", fault_silences="<<silences<<", control_silences="<<controls<<", host_deadline_misses="<<deadlines<<"\n";
 require(completed==4192,"Not every warmup/measured callback fully rendered");require(faults==0&&silences==0&&controls==0&&deadlines==0,"Overload/silence/deadline miss fails qualification");require(misses==0,"Dense real-device processing missed its deadline");
 std::cout<<"PASS direct Windows device bridge with104 active EQs and current master. Hardware received silence only. Timings cover host rendering, excluding bridge copying and final forced output clearing. Backend-suppressed callbacks are not observable here. No microphone/audible listening acceptance.\n";return 0;
 }catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
