#include "../Source/CoreBaseline/Engine.h"
#include "../../microclipper-work/FrozenEngine.h"
#include <chrono>
#include <iostream>
#include <cstring>
#include <stdexcept>
static volatile double checksum=0;
int main(){try{size_t comparisons=0;double oldms=0,newms=0;
 for(double rate:{44100.,48000.,96000.}){hem::MasterTruePeakLimiter current;frozen::MasterTruePeakLimiter original;current.prepare(rate,257,2);original.prepare(rate,257,2);
  if(current.latency()!=original.latency())throw std::runtime_error("Latency differs");
  juce::AudioBuffer<float> a(2,257),b(2,257);unsigned rng=342314;
  for(int block=0;block<750;++block){int count=block%3==0?257:block%3==1?256:63;
   for(int c=0;c<2;++c)for(int i=0;i<count;++i){rng=1664525*rng+1013904223;
    float random=float(rng>>8)/8388608.f-1.f;
    float amp=block<100?.01f:block<200?3.f:block<450?.02f:block<500?2.f:0.f;
    float value=random*amp;if(block==450&&i==0)value=c?30.f:-30.f;
    a.setSample(c,i,value);b.setSample(c,i,value);}
   auto start=std::chrono::steady_clock::now();original.process(a,count);auto middle=std::chrono::steady_clock::now();current.process(b,count);auto end=std::chrono::steady_clock::now();
   oldms+=std::chrono::duration<double,std::milli>(middle-start).count();newms+=std::chrono::duration<double,std::milli>(end-middle).count();
   for(int c=0;c<2;++c)for(int i=0;i<count;++i){float av=a.getSample(c,i),bv=b.getSample(c,i);if(std::memcmp(&av,&bv,sizeof(float)))throw std::runtime_error("PCM bits differ");++comparisons;checksum+=bv;}
  }
 }
 std::cout<<"PASS "<<comparisons<<" bit-exact PCM comparisons; 44.1/48/96k; variable blocks; quiet/loud/quiet/impulse/tail/release continuity; unchanged latency\n";
 std::cout<<"Paired full master-stage timings original_ms="<<oldms<<" optimized_ms="<<newms<<" ratio="<<oldms/newms<<"\n";
 return 0;
 }catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}}
