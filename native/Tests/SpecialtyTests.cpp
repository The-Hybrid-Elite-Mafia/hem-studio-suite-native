#include "../Source/DSP/SpecialtyCore.h"
#include "../Source/DSP/RestorerCore.h"
#include <iostream>
#include <stdexcept>
#include <cstdlib>
#include <new>
static bool countAllocations=false;static size_t allocations=0;
void* operator new(std::size_t n){if(countAllocations)++allocations;if(void* p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void operator delete(void* p) noexcept {std::free(p);}void operator delete(void* p,std::size_t) noexcept{std::free(p);}
void* operator new[](std::size_t n){return ::operator new(n);}void operator delete[](void* p) noexcept{std::free(p);}void operator delete[](void* p,std::size_t) noexcept{std::free(p);}
static void require(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
constexpr int block=128;
using Buffer=std::array<float,block>;
template<class Core,class Settings> void neutral(Core& c,const Settings& p,double rate){
 c.prepare(rate);Buffer l{},r{},original{};
 for(int b=0;b<40;++b){for(int i=0;i<block;++i)l[size_t(i)]=r[size_t(i)]=original[size_t(i)]=.2f*std::sin(float((b*block+i)*.7));
 countAllocations=true;c.process(l.data(),r.data(),block,p);countAllocations=false;
 for(int i=0;i<block;++i)require(std::abs(l[size_t(i)]-original[size_t(i)])<2.e-7f,"Neutral module altered samples");}
}
int main(){try{for(double rate:{44100.,48000.,96000.}){
 hem::DhtaCore d;hem::TransientCore t;hem::SubBassCore s;hem::RestorerCore a;
 neutral(d,hem::DhtaSettings{},rate);neutral(t,hem::TransientSettings{},rate);neutral(s,hem::SubBassSettings{},rate);neutral(a,hem::RestorerSettings{},rate);
 Buffer l{},r{},dl{},dr{};hem::DhtaCore delta;d.prepare(rate);delta.prepare(rate);hem::DhtaSettings p;p.threshold=-45;p.range=6;p.attack=.1f;p.release=80;
 double inputEnergy=0,outputEnergy=0;int n=0;
 for(int b=0;b<500;++b){for(int i=0;i<block;++i)l[size_t(i)]=r[size_t(i)]=dl[size_t(i)]=dr[size_t(i)]=.2f*float(std::sin(2*hem::pi*6800*(n+i)/rate));
 countAllocations=true;p.delta=false;d.process(l.data(),r.data(),block,p);p.delta=true;delta.process(dl.data(),dr.data(),block,p);countAllocations=false;
 if(b>400)for(int i=0;i<block;++i){const float x=.2f*float(std::sin(2*hem::pi*6800*(n+i)/rate));inputEnergy+=x*x;outputEnergy+=l[size_t(i)]*l[size_t(i)];require(std::abs(l[size_t(i)]+dl[size_t(i)]-x)<2.e-6f,"Delta and processed do not reconstruct dry");}n+=block;}
 const double reduction=-10*std::log10(outputEnergy/inputEnergy);require(reduction>5.7&&reduction<6.1,"DHTA centre-frequency attenuation wrong");
 s.prepare(rate);hem::SubBassSettings bass;bass.lowGain=6;
 for(int b=0;b<500;++b){l.fill(.05f);r.fill(.05f);s.process(l.data(),r.data(),block,bass);}
 require(std::abs(l.back()-.05f*hem::gain(6))<1.e-5f,"Bass low-band gain wrong");
 bass.lowGain=0;bass.mono=100;double sideEnergy=0,sideInput=0;n=0;
 for(int b=0;b<500;++b){for(int i=0;i<block;++i){l[size_t(i)]=.1f*float(std::sin(2*hem::pi*20*(n+i)/rate));r[size_t(i)]=-l[size_t(i)];}
 s.process(l.data(),r.data(),block,bass);if(b>400)for(int i=0;i<block;++i){const float x=.1f*float(std::sin(2*hem::pi*20*(n+i)/rate));sideEnergy+=l[size_t(i)]*l[size_t(i)];sideInput+=x*x;}n+=block;}
 require(sideEnergy/sideInput<.3,"Low mono failed to reduce low side energy");
 t.prepare(rate);hem::TransientSettings trans;trans.attack=12;
 for(int b=0;b<100;++b){l.fill(0);r.fill(0);t.process(l.data(),r.data(),block,trans);}l.fill(.1f);r.fill(.1f);t.process(l.data(),r.data(),block,trans);require(l[20]>.15f,"Transient attack did not enhance onset");
 a.prepare(rate);hem::RestorerSettings rest;rest.amount=10;rest.floor=-20;rest.protect=0;
 for(int b=0;b<500;++b){l.fill(.001f);r.fill(.001f);a.process(l.data(),r.data(),block,rest);}
 require(l.back()<.0005f&&l.back()>0,"Spectral low-level reduction failed");
 // Exercise changing automation targets; all settings stay inside the UI ranges.
 for(int b=0;b<250;++b){p.frequency=b%2?1000:16000;p.q=b%2?.5f:8;p.delta=false;p.wide=b%3==0;
 for(int i=0;i<block;++i)l[size_t(i)]=r[size_t(i)]=.15f*std::sin(float(b*block+i));
 countAllocations=true;d.process(l.data(),r.data(),block,p);t.process(l.data(),r.data(),block,trans);s.process(l.data(),r.data(),block,bass);a.process(l.data(),r.data(),block,rest);countAllocations=false;
 for(float x:l)require(std::isfinite(x),"Automation produced non-finite output");}
 std::cout<<"PASS "<<rate<<" Hz / DHTA reduction "<<reduction<<" dB / neutral, delta, bass, mono, transient, restoration, automation\n";
 }require(allocations==0,"Process allocated memory");std::cout<<"PASS zero allocations in instrumented specialty DSP calls\n";return 0;
 }catch(const std::exception& e){countAllocations=false;std::cerr<<e.what()<<'\n';return 1;}}
