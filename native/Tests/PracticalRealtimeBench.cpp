#include "../Source/MainComponent.h"
#include "../Source/Plugins/BandProcessors.h"
#include "../Source/Plugins/ReverbProcessors.h"
#include <chrono>
#include <thread>
#include <iostream>
#include <fstream>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
static constexpr double rate=48000.;
static void require(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
static void set(hem::ModuleProcessor& p,const juce::String& id,float value){auto* parameter=p.state.getParameter(id);require(parameter!=nullptr,"missing requested parameter");parameter->setValueNotifyingHost(parameter->convertTo0to1(value));require(std::abs(p.state.getRawParameterValue(id)->load()-value)<.011f,"parameter did not retain requested value");}
static std::unique_ptr<hem::DynamicEq8Processor> eq(int track){
 auto p=std::make_unique<hem::DynamicEq8Processor>();set(*p,"enabled",1);set(*p,"output",0);
 for(int band=1;band<=8;++band){const auto stem="b"+juce::String(band);set(*p,stem+"On",1);set(*p,stem+"Gain",band%2?-3.f:3.f);set(*p,stem+"Q",1);set(*p,stem+"Threshold",-36);set(*p,stem+"Range",3);set(*p,stem+"Attack",10);set(*p,stem+"Release",100);set(*p,stem+"DynamicOn",1);set(*p,stem+"Up",0);set(*p,stem+"Type",0);}
 set(*p,"b4Freq",float(120+40*track));return p;
}
static std::unique_ptr<hem::VcaProcessor> compressor(){auto p=std::make_unique<hem::VcaProcessor>();set(*p,"threshold",-30);set(*p,"ratio",4);set(*p,"attack",10);set(*p,"release",120);set(*p,"knee",6);set(*p,"makeup",0);set(*p,"mix",100);set(*p,"scOn",0);set(*p,"autoRelease",0);return p;}
static std::unique_ptr<hem::AlgorithmicReverbProcessor> reverb(int index){auto p=std::make_unique<hem::AlgorithmicReverbProcessor>();set(*p,"size",100);set(*p,"decay",index?2.f:1.5f);set(*p,"predelay",15);set(*p,"diffusion",70);set(*p,"modDepth",.5f);set(*p,"modRate",.5f);set(*p,"damping",8000);set(*p,"mix",25);set(*p,"output",0);set(*p,"algorithm",float(index));set(*p,"soloWet",0);set(*p,"bypass",0);return p;}
static void signal(juce::AudioBuffer<float>& b,int track,std::int64_t start=0){
 for(int n=0;n<b.getNumSamples();++n){const double t=(start+n)/rate;const double env=.4+.6*std::exp(-8*std::fmod(t*2+track*.071,1.));const double f=120+40*track;
  b.setSample(0,n,float(env*(.12*std::sin(juce::MathConstants<double>::twoPi*f*t)+.04*std::sin(juce::MathConstants<double>::twoPi*(f*3+17)*t))));
  b.setSample(1,n,float(env*(.11*std::sin(juce::MathConstants<double>::twoPi*(f+7)*t+.2)+.045*std::sin(juce::MathConstants<double>::twoPi*(f*2+29)*t))));
 }
}
static void proveProcessing(){
 juce::AudioBuffer<float> input(2,512),wet(2,512);juce::MidiBuffer midi;
 for(int kind=0;kind<3;++kind){std::unique_ptr<hem::ModuleProcessor> p=kind==0?std::unique_ptr<hem::ModuleProcessor>(eq(0)):kind==1?std::unique_ptr<hem::ModuleProcessor>(compressor()):std::unique_ptr<hem::ModuleProcessor>(reverb(0));p->prepareToPlay(rate,512);double delta=0;float meter=0;
  for(int b=0;b<96;++b){signal(input,0,std::int64_t(b)*512);wet.makeCopyOf(input,true);p->processBlock(wet,midi);meter=std::max(meter,p->meter());for(int c=0;c<2;++c)for(int n=0;n<512;++n){const double d=wet.getSample(c,n)-input.getSample(c,n);require(std::isfinite(d),"nonfinite wet proof");delta+=d*d;}}
  require(delta>1e-5,"configured processor is dry/neutral");if(kind<2)require(meter>.1f,"configured EQ/compressor never engaged reduction");
  double tail=0;if(kind==2){for(int b=0;b<96;++b){wet.clear();p->processBlock(wet,midi);for(int c=0;c<2;++c)for(int n=0;n<512;++n)tail+=double(wet.getSample(c,n))*wet.getSample(c,n);}require(tail>1e-6,"reverb has no wet tail on silent input");}
  std::cout<<"ACTIVE_PROOF kind="<<kind<<" wet_minus_dry_energy="<<delta<<" maximum_meter="<<meter<<" silent_input_tail_energy="<<tail<<'\n';
 }
}
struct MainComponentTestAccess {
 static int helpers(const MainComponent& h){return h.trackScheduler.helperCount();}
 static void setup(MainComponent& h,const juce::File& directory,int block){
  h.prepareToPlay(block,rate);h.hardwareOutputs=2;h.looping=true;h.inputDb=0;h.outputDb=0;h.trackRenderTestBudget=0;
  juce::ValueTree rack("PRACTICAL_REALTIME_RACK");rack.setProperty("rate",rate,nullptr);rack.setProperty("block",block,nullptr);rack.setProperty("helpers",h.trackScheduler.helperCount(),nullptr);
  juce::AudioBuffer<float> pcm(2,192000);
  for(int track=0;track<13;++track){signal(pcm,track);const auto file=directory.getChildFile("stem-"+juce::String(track+1)+".wav");juce::WavAudioFormat wav;auto stream=file.createOutputStream();require(bool(stream),"WAV stream creation");std::unique_ptr<juce::AudioFormatWriter> writer(wav.createWriterFor(stream.release(),rate,2,32,{},0));require(writer&&writer->writeFromAudioSampleBuffer(pcm,0,pcm.getNumSamples()),"WAV write failed");writer.reset();
   const auto leftRms=pcm.getRMSLevel(0,0,pcm.getNumSamples()),rightRms=pcm.getRMSLevel(1,0,pcm.getNumSamples());
   std::cout<<"STEM track="<<track+1<<" frames=192000 stereo=1 bits=32 fundamental="<<120+40*track<<" peak="<<pcm.getMagnitude(0,pcm.getNumSamples())<<" rmsL="<<leftRms<<" rmsR="<<rightRms<<" post_insert_trim_db=-12\n";
   auto& t=h.bank->tracks[size_t(track)];t.playback->prepareFile(file,h.formats,h.diskThread,rate,block,true);t.sourceRate=rate;t.loaded=true;t.trimDb=-12;t.transport().start();
   juce::ValueTree trackTree("TRACK");trackTree.setProperty("number",track+1,nullptr);trackTree.setProperty("trimDb",-12,nullptr);trackTree.setProperty("source",file.getFileName(),nullptr);
   auto install=[&](int slot,std::unique_ptr<hem::ModuleProcessor> p){auto parameters=p->state.copyState();parameters.setProperty("slot",slot,nullptr);trackTree.addChild(parameters,-1,nullptr);auto node=t.chain.stageNode(std::move(p),rate,block,t.chain.slotLatency());t.chain.installInitial(slot,std::move(node),false);require(!t.chain.isBypassed(slot)&&t.chain.activeProcessor(slot)->processingReady(),"rack bypassed or unprepared");};
   install(0,eq(track));if(track<4)install(1,compressor());if(track==4||track==5)install(1,reverb(track-4));rack.addChild(trackTree,-1,nullptr);
  }
  require(directory.getChildFile("rack-settings.xml").replaceWithText(rack.toXmlString()),"rack manifest write failed");
 }
 static void activity(MainComponent& h,std::array<float,13>& eqMax,std::array<float,4>& compMax,std::array<float,2>& wetMax){
  for(int track=0;track<13;++track){auto& chain=h.bank->tracks[size_t(track)].chain;auto* e=chain.activeProcessor(0);require(e&&e->state.getRawParameterValue("enabled")->load()==1&&!chain.isBypassed(0),"EQ not enabled in actual rack");eqMax[size_t(track)]=std::max(eqMax[size_t(track)],e->meter());if(track<4)compMax[size_t(track)]=std::max(compMax[size_t(track)],chain.activeProcessor(1)->meter());if(track==4||track==5){auto* r=dynamic_cast<hem::AlgorithmicReverbProcessor*>(chain.activeProcessor(1));require(r&&r->state.getRawParameterValue("bypass")->load()==0&&!chain.isBypassed(1),"reverb bypassed");wetMax[size_t(track-4)]=std::max(wetMax[size_t(track-4)],r->wetTailDb());}}
 }
 static void report(MainComponent& h){std::cout<<"COUNTERS delivered="<<h.trackRenderCompleted.load()<<" overloads="<<h.trackRenderFaults.load()<<" silences="<<h.trackRenderSilencedCallbacks.load()<<" host_deadlines="<<h.trackRenderDeadlineMisses.load()<<" source_dropouts="<<h.transportDropouts.load()<<" control_gaps="<<h.controlSilencedCallbacks.load()<<'\n';}
 static bool valid(MainComponent& h){return h.trackRenderCompleted.load()==4192&&h.trackRenderFaults.load()==0&&h.trackRenderSilencedCallbacks.load()==0&&h.trackRenderDeadlineMisses.load()==0&&h.transportDropouts.load()==0&&h.controlSilencedCallbacks.load()==0;}
};
struct ScopedAudioPriority {
 HMODULE library=nullptr;HANDLE task=nullptr;int previous=THREAD_PRIORITY_NORMAL;bool changed=false;using Revert=BOOL(WINAPI*)(HANDLE);Revert revert=nullptr;
 ScopedAudioPriority(){previous=GetThreadPriority(GetCurrentThread());changed=SetThreadPriority(GetCurrentThread(),THREAD_PRIORITY_HIGHEST)!=0;library=LoadLibraryExW(L"avrt.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);if(library){using Register=HANDLE(WINAPI*)(LPCWSTR,LPDWORD);using Priority=BOOL(WINAPI*)(HANDLE,int);auto reg=reinterpret_cast<Register>(GetProcAddress(library,"AvSetMmThreadCharacteristicsW"));auto pri=reinterpret_cast<Priority>(GetProcAddress(library,"AvSetMmThreadPriority"));revert=reinterpret_cast<Revert>(GetProcAddress(library,"AvRevertMmThreadCharacteristics"));DWORD index=0;if(reg&&pri&&revert){task=reg(L"Pro Audio",&index);if(task&&!pri(task,0)){revert(task);task=nullptr;}}}if(!changed||!task){cleanup();throw std::runtime_error("audio priority setup failed");}}
 void cleanup(){if(task&&revert){revert(task);task=nullptr;}if(changed){SetThreadPriority(GetCurrentThread(),previous);changed=false;}if(library){FreeLibrary(library);library=nullptr;}}~ScopedAudioPriority(){cleanup();}
};
int main(int argc,char** argv){juce::ScopedJuceInitialiser_GUI gui;try{
 require(argc==3,"usage PracticalRealtimeBench block evidenceDirectory");const int block=std::atoi(argv[1]);require(block==1024,"unsupported practical buffer");const juce::File directory(juce::String::fromUTF8(argv[2]));require(directory.createDirectory().wasOk(),"evidence directory creation failed");
 const auto override=juce::SystemStats::getEnvironmentVariable("HEM_TRACK_HELPERS","");require(override.isEmpty()||override=="0","unexpected helper environment override");
 proveProcessing();auto host=std::make_unique<MainComponent>(false);require(MainComponentTestAccess::helpers(*host)==0,"production default not serial");MainComponentTestAccess::setup(*host,directory,block);
 std::array<float,13> eqMax{};std::array<float,4> compMax{};std::array<float,2> wetMax{-200,-200};juce::AudioBuffer<float> audio(2,block);std::vector<double> timings(4096);double outputEnergy=0;float peak=0;int misses=0,zeroMeasured=0;bool finite=true;
 ScopedAudioPriority priority;auto next=std::chrono::steady_clock::now();const auto start=next;const auto interval=std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<double>(block/rate));double wakeLate=0;
 for(int callback=0;callback<4192;++callback){std::this_thread::sleep_until(next);wakeLate=std::max(wakeLate,std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-next).count());next+=interval;audio.clear();const auto begin=juce::Time::getHighResolutionTicks();host->getNextAudioBlock({&audio,0,block});const double ms=1000*juce::Time::highResolutionTicksToSeconds(juce::Time::getHighResolutionTicks()-begin);
  // Validation intentionally outside callback timing, never substitutes for counters.
  double energy=0;for(int c=0;c<2;++c)for(int n=0;n<block;++n){const float sample=audio.getSample(c,n);finite=finite&&std::isfinite(sample);energy+=double(sample)*sample;peak=std::max(peak,std::abs(sample));}
  MainComponentTestAccess::activity(*host,eqMax,compMax,wetMax);
  if(callback>=96){timings[size_t(callback-96)]=ms;if(ms>1000*block/rate)++misses;if(energy<=1e-12)++zeroMeasured;outputEnergy+=energy;}
 }
 const double elapsed=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();double total=0;for(auto t:timings)total+=t;std::sort(timings.begin(),timings.end());
 std::cout<<"PRACTICAL_REALTIME rate=48000 block="<<block<<" stems=13 active_eq=13 active_vca=4 active_algorithmic_reverb=2 helpers=0 measured=4096 warmup=96 deadline_ms="<<1000*block/rate<<" mean_ms="<<total/4096<<" median_ms="<<timings[2048]<<" p99_ms="<<timings[4055]<<" max_ms="<<timings.back()<<" measured_deadlines="<<misses<<" elapsed_ms="<<elapsed<<" max_wake_lateness_ms="<<wakeLate<<'\n';
 MainComponentTestAccess::report(*host);std::cout<<"OUTPUT finite="<<finite<<" zero_measured="<<zeroMeasured<<" energy="<<outputEnergy<<" peak="<<peak<<'\n';
 bool engaged=true;for(size_t i=0;i<eqMax.size();++i){std::cout<<"EQ_GR track="<<i+1<<" max_db="<<eqMax[i]<<'\n';engaged=engaged&&eqMax[i]>.1f;}for(size_t i=0;i<compMax.size();++i){std::cout<<"VCA_GR track="<<i+1<<" max_db="<<compMax[i]<<'\n';engaged=engaged&&compMax[i]>.1f;}for(size_t i=0;i<wetMax.size();++i){std::cout<<"REVERB_WET_TAIL track="<<i+5<<" max_db="<<wetMax[i]<<'\n';engaged=engaged&&wetMax[i]>-90;}
 require(MainComponentTestAccess::valid(*host)&&misses==0&&finite&&zeroMeasured==0&&outputEnergy>0&&engaged,"practical realtime acceptance failed");std::cout<<"PASS practical_realtime; original production objects/default0; no native device playback or GUI-edit stress claim\n";return 0;
 }catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}

