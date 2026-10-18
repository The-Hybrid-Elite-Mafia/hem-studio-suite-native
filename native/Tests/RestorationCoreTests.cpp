#include "../Source/DSP/RestorationCore.h"
#include <iostream>
#include <random>
#include <vector>
#include <stdexcept>
#include <cstdlib>
#include <new>
#include <chrono>
static thread_local bool measuring=false;
static uint64_t allocations=0,frees=0,checks=0;
void*operator new(std::size_t n){if(measuring)++allocations;if(auto*p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void*operator new[](std::size_t n){return ::operator new(n);}
void operator delete(void*p)noexcept{if(measuring&&p)++frees;std::free(p);}
void operator delete[](void*p)noexcept{::operator delete(p);}
void operator delete(void*p,std::size_t)noexcept{::operator delete(p);}
void operator delete[](void*p,std::size_t)noexcept{::operator delete(p);}
namespace{
void check(bool result,const char*what){++checks;if(!result)throw std::runtime_error(what);}
template<class Core,class Settings>std::vector<float>render(Core&c,Settings p,const std::vector<float>&input,int block){std::vector<float>l=input,r=input;for(size_t start=0;start<input.size();start+=size_t(block)){const int n=int(std::min(size_t(block),input.size()-start));measuring=true;c.process(l.data()+start,r.data()+start,n,p);measuring=false;}for(float x:l)check(std::isfinite(x),"finite output");return l;}
std::vector<float>sine(double rate,double hz,int count,float level=.3f){std::vector<float>x(static_cast<size_t>(count));for(int i=0;i<count;++i)x[size_t(i)]=float(level*std::sin(2*hem::pi*hz*i/rate));return x;}
double rms(const std::vector<float>&x,size_t start=0){double sum=0;for(size_t i=start;i<x.size();++i)sum+=double(x[i])*x[i];return std::sqrt(sum/(x.size()-start));}
void lifecycle(){for(double rate:{8000.,44100.,48000.,96000.,192000.,384000.})for(int block:{1,17,128,1001}){
 hem::DynamicResonanceCore a;hem::BreathMouthCore b;check(a.prepare(rate),"resonance prepare");check(b.prepare(rate),"breath prepare");auto input=sine(rate,345,4001);auto out=render(a,hem::DynamicResonanceSettings{},input,block);for(size_t i=0;i<input.size();++i)check(out[i]==input[i],"resonance neutral identity");out=render(b,hem::BreathMouthSettings{},input,block);check(b.latency()<=int(std::ceil(rate*.006)),"breath latency cap");for(size_t i=0;i<input.size();++i)check(out[i]==(i>=size_t(b.latency())?input[i-size_t(b.latency())]:0),"breath delayed neutral identity");a.reset();auto again=render(a,hem::DynamicResonanceSettings{},input,block);check(again==input,"resonance reset");b.reset();check(render(b,hem::BreathMouthSettings{},input,block)==out,"breath reset");}
 for(double rate:{0.,7999.,384001.,std::numeric_limits<double>::quiet_NaN()}){hem::DynamicResonanceCore a;hem::BreathMouthCore b;check(!a.prepare(rate)&&!b.prepare(rate),"invalid rate accepted");std::array<float,8>l{},r{};l.fill(1);r.fill(1);a.process(l.data(),r.data(),8,{});for(float x:l)check(x==0,"invalid resonance not silent");l.fill(1);b.process(l.data(),r.data(),8,{});for(float x:l)check(x==0,"invalid breath not silent");}
 std::cout<<"PASS neutral identity, exact latency, reset and invalid-rate lifecycle at six rates/four block sizes\n";
}
void resonance(){hem::DynamicResonanceSettings p;p.maxReduction=12;p.sensitivity=80;p.attack=10;auto input=sine(48000,1000,96000);hem::DynamicResonanceCore a,b;a.prepare(48000,p);b.prepare(48000,p);auto out=render(a,p,input,17),other=render(b,p,input,1001);check(out==other,"resonance block partition dependence");const double ratio=rms(out,48000)/rms(input,48000);check(ratio<.8&&ratio>.2,"resonant tone attenuation missing or excess");auto display=a.display();check(display.valid&&display.spectralCount>0,"real spectra missing");check(display.maximumCut<=12.001f&&display.maximumCut>1,"shared cut budget");for(float x:display.deltaDb)check(std::isfinite(x),"delta spectrum finite");
 p.profile=2;a.prepare(48000,p);const auto high=render(a,p,input,128);check(rms(high,48000)/rms(input,48000)>.98,"high profile attenuated low tone");p.profile=0;p.low=4000;p.high=8000;a.prepare(48000,p);const auto focused=render(a,p,input,128);check(rms(focused,48000)/rms(input,48000)>.98,"focus excludes tone");
 p.low=80;p.high=18000;p.soloDelta=true;a.prepare(48000,p);const auto removed=render(a,p,input,128);p.soloDelta=false;b.prepare(48000,p);const auto normal=render(b,p,input,128);for(size_t i=0;i<input.size();++i)check(std::abs(removed[i]+normal[i]-input[i])<1.e-6,"delta not aligned complement");
 p.bypass=true;a.prepare(48000,p);check(render(a,p,input,128)==input,"bypass identity");p.bypass=false;p.maxReduction=24;p.sensitivity=100;a.prepare(48000,p);std::vector<float>impulse(4800);impulse[0]=1;auto transient=render(a,p,impulse,128);check(transient==impulse,"onset protection changed isolated onset");
 p.maxReduction=6;p.output=-6;a.prepare(48000,p);auto gained=render(a,p,input,128);check(rms(gained,48000)<rms(input,48000)*.6,"output gain ineffective");
 std::cout<<"PASS resonance attenuation ratio="<<ratio<<", profile/focus exclusion, bounded cut budget, exact partition, onset preservation, aligned delta, bypass/output\n";
}
void breaths(){hem::BreathMouthSettings p;p.breathReduction=12;p.breathDetection=80;std::vector<float>noise(96000);std::mt19937 gen(17);std::uniform_real_distribution<float>d(-.1f,.1f);for(auto&x:noise)x=d(gen);hem::BreathMouthCore a,b;a.prepare(48000,p);b.prepare(48000,p);const auto out=render(a,p,noise,17),other=render(b,p,noise,1001);check(out==other,"breath block partition dependence");const auto snapshot=a.display();check(snapshot.detection[0]>.4&&snapshot.reduction[0]>4,"sustained noise not detected");check(rms(out,48000)<rms(noise,48000)*.65,"noise attenuation ineffective");
 auto tone=sine(48000,1000,48000,.2f);a.prepare(48000,p);const auto tonal=render(a,p,tone,128);check(a.display().reduction[0]<.05,"breath detector altered pure tone");
 p.breathReduction=0;p.mouthReduction=18;p.mouthDetection=80;std::vector<float>click(4800);click[2400]=.8f;a.prepare(48000,p);auto repaired=render(a,p,click,128);check(repaired[2400+size_t(a.latency())]<.4,"isolated impulse attenuation missing");check(a.display().events>0,"impulse event telemetry missing");
 p.listenMouth=true;a.prepare(48000,p);const auto delta=render(a,p,click,128);for(size_t i=0;i<click.size();++i){const float dry=i>=size_t(a.latency())?click[i-size_t(a.latency())]:0;check(std::abs(delta[i]+repaired[i]-dry)<1.e-6,"mouth removed complement misaligned");}
 p.listenMouth=false;p.bypass=true;a.prepare(48000,p);const auto bypass=render(a,p,click,128);for(size_t i=0;i<click.size();++i)check(bypass[i]==(i>=size_t(a.latency())?click[i-size_t(a.latency())]:0),"breath bypass delay mismatch");
 std::cout<<"PASS noise attenuation, tonal exclusion, isolated-impulse detection, aligned listen complement, bypass and partition invariance\n";
}
void extremes(){hem::DynamicResonanceCore a;hem::BreathMouthCore b;hem::DynamicResonanceSettings p;p.sensitivity=100;p.maxReduction=24;p.attack=.5f;p.release=10;p.selectivity=100;p.output=6;hem::BreathMouthSettings q;q.breathDetection=q.mouthDetection=100;q.breathReduction=q.mouthReduction=24;a.prepare(48000,p);b.prepare(48000,q);auto x=sine(48000,1731,48000,.95f);x[33]=std::numeric_limits<float>::infinity();x[100]=std::numeric_limits<float>::quiet_NaN();render(a,p,x,31);render(b,q,x,31);std::vector<float>silence(10000);const auto aa=render(a,p,silence,31),bb=render(b,q,silence,31);check(rms(aa,9000)<1.e-5&&rms(bb,9000)<1.e-8,"nonfinite poisoned persistent state");check(allocations==0&&frees==0,"callback allocation/free");std::cout<<"PASS nonfinite recovery and zero audio callback allocations/frees\n";}
}
int main(){try{lifecycle();resonance();breaths();extremes();std::cout<<"PASS "<<checks<<" assertions\n";return 0;}catch(const std::exception&e){measuring=false;std::cerr<<"FAIL "<<e.what()<<" at "<<checks<<"\n";return 1;}}
