#include "../Source/DSP/ConvolutionReverbCore.h"
#include "../../fft-audit-20261010/ConvolutionReverbCore-before.h"
#include <iostream>
#include <random>
#include <chrono>
#include <stdexcept>
#include <cstring>
static void check(bool b,const char*m){if(!b)throw std::runtime_error(m);}
static hem::BaselineConvolutionReverbSettings baseline(const hem::ConvolutionReverbSettings&p){hem::BaselineConvolutionReverbSettings q;q.predelay=p.predelay;q.irSize=p.irSize;q.lowCut=p.lowCut;q.highCut=p.highCut;q.mix=p.mix;q.output=p.output;q.lowCutOn=p.lowCutOn;q.highCutOn=p.highCutOn;q.soloWet=p.soloWet;q.bypass=p.bypass;return q;}
int main(){try{
size_t samples=0;
hem::ConvolutionReverbCore current;hem::BaselineConvolutionReverbCore old;
juce::AudioBuffer<float>ir(2,9001);ir.clear();for(int ch=0;ch<2;++ch)for(int i:{0,63,256,2048,8192,9000})ir.setSample(ch,i,(ch?-.03f:.06f));
check(current.loadOffThread(ir,48000).wasOk()&&old.loadOffThread(ir,48000).wasOk(),"IR load");
std::mt19937 rng(481);std::uniform_real_distribution<float>noise(-.25f,.25f);
for(double sr:{8000.,44100.,48000.,96000.,192000.}){current.prepare(sr,256);old.prepare(sr,256);check(current.latency()==old.latency()&&current.tailSeconds()==old.tailSeconds(),"latency/tail changed");
for(int block:{1,17,256,1024}){current.reset();old.reset();hem::ConvolutionReverbSettings p;p.mix=70;p.lowCutOn=p.highCutOn=true;
for(int step=0;step<100;++step){if(step==12){p.lowCut=1750;p.highCut=900;p.predelay=2.5f;}if(step==30){p.lowCut=21;p.highCut=19999;p.bypass=true;}if(step==45){p.lowCut=999;p.highCut=3300;p.bypass=false;p.mix=100;}if(step==70){current.reset();old.reset();}
std::vector<float>l(block),r(block);for(int i=0;i<block;++i){l[i]=noise(rng);r[i]=noise(rng);}auto a=l,b=r;current.process(l.data(),r.data(),block,p);old.process(a.data(),b.data(),block,baseline(p));check(std::memcmp(l.data(),a.data(),block*sizeof(float))==0&&std::memcmp(r.data(),b.data(),block*sizeof(float))==0,"output differs from baseline");samples+=block*2;
}}}
std::cout<<"PASS exact sample equivalence "<<samples<<" samples, 5 rates x 4 blocks; settled/ramped cutoffs, bypass, mix, predelay, reset, prepare rate changes; latency/tail unchanged\n";
for(int pass=0;pass<3;++pass){auto bench=[](auto&core,const auto&p){std::array<float,256>l{},r{};auto begin=std::chrono::steady_clock::now();for(int n=0;n<2000;++n){l.fill(.1f);r.fill(-.2f);core.process(l.data(),r.data(),256,p);}return std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count();};hem::ConvolutionReverbCore c;hem::BaselineConvolutionReverbCore o;c.prepare(96000,256);o.prepare(96000,256);hem::ConvolutionReverbSettings p;p.lowCutOn=p.highCutOn=true;double a,b;if(pass%2){a=bench(c,p);b=bench(o,baseline(p));}else{b=bench(o,baseline(p));a=bench(c,p);}std::cout<<"DIAGNOSTIC steady-cutoff/no-IR filter overhead old_ms="<<b<<" cached_ms="<<a<<" ratio="<<a/b<<" (not full convolution or realtime qualification)\n";}
return 0;}catch(const std::exception&e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
