// Reuse the same real-host fixture, not a DSP-only surrogate.
#define main schedulerCorrectnessMain
#include "MaximumEq48Fixture.h"
#undef main
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
struct ScopedAudioPriority {
 HMODULE library=nullptr;HANDLE task=nullptr;int previous=THREAD_PRIORITY_NORMAL;
 using Revert=BOOL(WINAPI*)(HANDLE);Revert revert=nullptr;bool changed=false;
 explicit ScopedAudioPriority(bool enabled){if(!enabled)return;previous=GetThreadPriority(GetCurrentThread());changed=SetThreadPriority(GetCurrentThread(),THREAD_PRIORITY_HIGHEST)!=0;library=LoadLibraryW(L"avrt.dll");if(library){using Register=HANDLE(WINAPI*)(LPCWSTR,LPDWORD);using Priority=BOOL(WINAPI*)(HANDLE,int);auto reg=reinterpret_cast<Register>(GetProcAddress(library,"AvSetMmThreadCharacteristicsW"));auto priority=reinterpret_cast<Priority>(GetProcAddress(library,"AvSetMmThreadPriority"));revert=reinterpret_cast<Revert>(GetProcAddress(library,"AvRevertMmThreadCharacteristics"));DWORD index=0;if(reg&&priority&&revert){task=reg(L"Pro Audio",&index);if(task&&!priority(task,0)){revert(task);task=nullptr;}}}if(!changed||!task){cleanup();throw std::runtime_error("Cannot establish requested production-like thread priority");}}
 void cleanup(){if(task&&revert){revert(task);task=nullptr;}if(changed){SetThreadPriority(GetCurrentThread(),previous);changed=false;}if(library){FreeLibrary(library);library=nullptr;}}~ScopedAudioPriority(){cleanup();}
};
int main(int argc,char** argv){juce::ScopedJuceInitialiser_GUI gui;try{
 const int onlyHelper=argc>1?std::atoi(argv[1]):-1, measured=argc>2?juce::jlimit(256,30000,std::atoi(argv[2])):256;
 const double onlyRate=argc>3?std::atof(argv[3]):0;
 const bool paced=argc>4&&std::atoi(argv[4])!=0;
 const int onlyProfile=argc>5?std::atoi(argv[5]):-1;
 const bool audioPriority=argc>6&&std::atoi(argv[6])!=0;
 juce::AudioBuffer<float> pcm(2,65536);for(int i=0;i<pcm.getNumSamples();++i){pcm.setSample(0,i,float(.012*std::sin(i*.071)+.008*std::sin(i*.193)));pcm.setSample(1,i,float(.015*std::sin(i*.053)));}
 for(int profile:{104})for(double rate:{48000.})for(int helpers:{0,7}){
  if((onlyProfile>=0&&profile!=onlyProfile)||(onlyHelper>=0&&helpers!=onlyHelper)||(onlyRate>0&&rate!=onlyRate))continue;
  if(profile!=104&&rate!=96000.)continue;const int block=profile==104?256:64;
  SchedulerSourceFile sources;auto host=std::make_unique<MainComponent>(false);
  require(MainComponentTestAccess::defaultSerial(*host),"actual production default is not zero helpers");
  require(MainComponentTestAccess::helpers(*host,helpers),"startup failed");MainComponentTestAccess::rate(*host,rate,block);MainComponentTestAccess::setup(*host,sources,pcm,true,profile==104?8:profile,profile==104?13:1);juce::AudioBuffer<float> audio(2,block);
  MainComponentTestAccess::verifyRack(*host);
  MainComponentTestAccess::realtimeBudget(*host);
  ScopedAudioPriority priorityScope(audioPriority);
  auto next=std::chrono::steady_clock::now();const auto cadenceStart=next;double maximumWakeLateMs=0;const auto interval=std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<double>(block/rate));
  auto waitNext=[&]{if(paced){std::this_thread::sleep_until(next);maximumWakeLateMs=std::max(maximumWakeLateMs,std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-next).count());next+=interval;}};
  for(int i=0;i<96;++i){waitNext();audio.clear();host->getNextAudioBlock({&audio,0,block});}
  std::vector<double> times(size_t(measured),0.);int misses=0;double total=0;bool finite=true;int zeroAudio=0;double outputEnergy=0;float outputPeak=0;
  for(auto& ms:times){waitNext();audio.clear();auto begin=juce::Time::getHighResolutionTicks();host->getNextAudioBlock({&audio,0,block});ms=juce::Time::highResolutionTicksToSeconds(juce::Time::getHighResolutionTicks()-begin)*1000;total+=ms;if(ms>block*1000./rate)++misses;
   double energy=0;for(int c=0;c<2;++c)for(int n=0;n<block;++n){const float sample=audio.getSample(c,n);finite=finite&&std::isfinite(sample);energy+=double(sample)*sample;outputPeak=std::max(outputPeak,std::abs(sample));}outputEnergy+=energy;if(energy<=1e-12)++zeroAudio;
  }
  const double elapsedMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-cadenceStart).count();
  std::cout<<"Thread condition: "<<(audioPriority?"JUCE-high / Pro Audio MMCSS NORMAL":"normal-priority main thread")<<", total_elapsed_ms="<<elapsedMs<<", nominal_cadence_ms="<<(measured+96)*block*1000./rate<<", max_wake_lateness_ms="<<maximumWakeLateMs<<'\n';
  std::cout<<"Condition: actual WAV read-ahead / "<<(paced?"paced outside callback timing":"unpaced stress")<<'\n';
  std::sort(times.begin(),times.end());std::cout<<"Dynamic EQ (3dB dynamic ranges) full host, active_slots="<<profile<<", rate="<<rate<<", block="<<block<<", helpers="<<helpers<<", samples="<<measured<<", mean_ms="<<total/measured<<", median_ms="<<times[size_t(measured/2)]<<", p99_ms="<<times[size_t(std::ceil(measured*.99)-1)]<<", max_ms="<<times.back()<<", deadline_misses="<<misses<<"/"<<measured<<"\n";
  std::cout<<"Delivered="<<MainComponentTestAccess::completed(*host)<<", overloads="<<MainComponentTestAccess::faults(*host)<<", silenced_callbacks="<<MainComponentTestAccess::silences(*host)<<", host_deadlines="<<MainComponentTestAccess::deadlines(*host)<<", source_dropouts="<<MainComponentTestAccess::sourceDropouts(*host)<<", control_gaps="<<MainComponentTestAccess::controlSilences(*host)<<'\n';
  std::cout<<"ACTIVE_OUTPUT finite="<<finite<<" zero_audio_measured="<<zeroAudio<<" output_energy="<<outputEnergy<<" output_peak="<<outputPeak<<" active_EQs=104\n";
  require(finite&&zeroAudio==0&&outputEnergy>0,"nonfinite or silent output invalidates timing");
  MainComponentTestAccess::verifyRack(*host);
  require(MainComponentTestAccess::completed(*host)==uint64_t(measured+96)&&MainComponentTestAccess::faults(*host)==0&&MainComponentTestAccess::silences(*host)==0&&MainComponentTestAccess::deadlines(*host)==0&&misses==0,"timing invalid: deadline miss, fault silence or incomplete callbacks");
 }
 std::cout<<"PASS maximum_eq_48k_realtime. No device startup/playback. Fixed precommitted controls; no claim about arbitrary GUI-edit ordering, transitions or other device buffers.\n";return 0;
 }catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}

