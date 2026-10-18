#include "../Source/DSP/AmpCabinetCore.h"
#include <iostream>
#include <vector>
#include <limits>
#include <cstdlib>
#include <chrono>
#include <new>
static bool watching=false; static size_t allocations=0;
void* operator new(size_t n){if(watching)++allocations;if(void* p=std::malloc(n))return p;throw std::bad_alloc();}
void operator delete(void* p) noexcept {std::free(p);}
void operator delete(void* p,size_t) noexcept {std::free(p);}
static void require(bool v,const char* s){if(!v){std::cerr<<"FAIL "<<s<<"\n";std::exit(1);}}
static double render(hem::AmpCabinetSettings p,double rate=48000,double hz=440){hem::AmpCabinetCore c;c.prepare(rate);std::vector<float> l(8192),r(8192);for(size_t i=0;i<l.size();++i)l[i]=r[i]=.1f*float(std::sin(2*hem::pi*hz*i/rate));c.process(l.data(),r.data(),int(l.size()),p);double e=0;for(size_t i=4096;i<l.size();++i){require(std::isfinite(l[i]),"finite");require(l[i]==r[i],"stereo symmetry");e+=l[i]*l[i];}return std::sqrt(e/4096);}
int main(){
 hem::AmpCabinetSettings tone;tone.ampEnabled=true;double ref=render(tone,48000,850);tone.mid=12;require(render(tone,48000,850)>ref*2,"mid tone");tone.mid=0;ref=render(tone,48000,7000);tone.treble=12;require(render(tone,48000,7000)>ref*2,"treble tone");
 for(double sr:{44100.,48000.,96000.,192000.}){hem::AmpCabinetCore c;c.prepare(sr);std::vector<float> l(1024),r(1024);l[0]=1;c.process(l.data(),r.data(),1024,{});for(int i=0;i<1024;++i){require(l[size_t(i)]==(i==c.latency()?1.f:0.f),"dry exact latency");require(r[size_t(i)]==0,"channel isolation");}require(c.latency()<=std::ceil(sr*.006),"slot ceiling");}
 hem::AmpCabinetSettings p;p.ampEnabled=true;double model[3];for(int m=0;m<3;++m){p.ampModel=m;model[m]=render(p);std::cout<<"model "<<m<<" rms "<<model[m]<<"\n";}require(std::abs(model[0]-model[1])>.0001&&std::abs(model[1]-model[2])>.0001,"different models");
 p.ampModel=0;double initial=render(p);p.gain=12;require(render(p)>initial*1.5,"gain drive");p.gain=0;p.master=-12;require(render(p)<initial*.4,"master");p.master=0;p.output=-12;require(std::abs(render(p)/initial-std::pow(10.,-12./20))<.001,"output trim");p.output=0;p.bass=12;require(render(p,48000,130)>render(hem::AmpCabinetSettings{0,0,0,true},48000,130)*2,"bass");
 p={};p.cabEnabled=true;for(int m=0;m<3;++m){p.cabModel=m;double hz=3000,actual=render(p,48000,hz)/render({},48000,hz);require(std::abs(actual-hem::AmpCabinetCore::response(hz,48000,p))<.002,"analytic cabinet response");}p.micEnabled=true;p.micPosition=.7f;p.micAngle=.8f;p.micDistance=.6f;require(std::abs(render(p,48000,4000)/render({},48000,4000)-hem::AmpCabinetCore::response(4000,48000,p))<.002,"analytic mic response");
 hem::AmpCabinetCore c;c.prepare(48000);std::vector<float> l(4096,.2f),r(4096,0);p.ampEnabled=true;p.gain=std::numeric_limits<float>::infinity();l[0]=std::numeric_limits<float>::quiet_NaN();c.process(l.data(),r.data(),4096,p);for(size_t i=0;i<l.size();++i){require(std::isfinite(l[i]),"hostile finite");require(r[i]==0,"active stereo isolation");}
 watching=true; c.process(l.data(),r.data(),4096,p); watching=false;require(allocations==0,"callback allocation");
 hem::AmpCabinetCore a,b;a.prepare(48000);b.prepare(48000);std::vector<float> al(4096),ar(4096),bl,br;for(int i=0;i<4096;++i)al[i]=ar[i]=float(.2*std::sin(i*.07));bl=al;br=ar;a.process(al.data(),ar.data(),4096,p);for(int i=0;i<4096;i+=64)b.process(bl.data()+i,br.data()+i,64,p);require(al==bl&&ar==br,"block partition invariance");
 {
 hem::AmpCabinetCore custom;custom.prepare(48000);hem::reverb::PreparedConvolution fir;std::vector<float> il(300),ir(300);il[192]=.5f;ir[192]=.25f;il[224]=.125f;fir.prepare(il.data(),ir.data(),il.size());custom.setCustomIR(&fir);
 hem::AmpCabinetSettings state;state.cabModel=3;state.cabEnabled=true;std::vector<float> x(2048),y(2048);custom.process(x.data(),y.data(),2048,state);x[0]=y[0]=1;
 watching=true;custom.process(x.data(),y.data(),2048,state);watching=false;require(allocations==0,"custom callback allocation");
 for(int i=0;i<2048;++i){require(std::abs(x[i]-(i==224?.5f:(i==256?.125f:0.f)))<1e-6f,"custom left impulse");require(std::abs(y[i]-(i==224?.25f:0.f))<1e-6f,"custom right impulse");}
 }
 auto start=std::chrono::steady_clock::now();for(int i=0;i<24;++i)c.process(l.data(),r.data(),4096,p);std::cout<<"98304 stereo frames elapsed ms "<<std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()<<"\nPASS\n";
}



