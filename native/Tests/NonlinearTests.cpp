#include "../Source/DSP/NonlinearCore.h"
#include <iostream>
#include <stdexcept>
#include <cstdlib>
#include <new>
static bool counting=false;static size_t allocations=0;
void* operator new(std::size_t n){if(counting)++allocations;if(auto* p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void* operator new[](std::size_t n){return ::operator new(n);}void operator delete(void* p) noexcept{std::free(p);}void operator delete(void* p,std::size_t) noexcept{std::free(p);}void operator delete[](void* p) noexcept{std::free(p);}void operator delete[](void* p,std::size_t) noexcept{std::free(p);}
void require(bool b,const char* message){if(!b)throw std::runtime_error(message);}
using Buffer=std::array<float,257>;
template<class C,class P>void neutral(C& c,P p,double sr,bool settle=false){c.prepare(sr);Buffer l{},r{};if(settle)for(int i=0;i<100;++i){l.fill(0);r.fill(0);c.process(l.data(),r.data(),257,p);}
    for(int start=0;start<8000;){const int n=std::min(8000-start,1+(start*13+7)%257);for(int i=0;i<n;++i){l[size_t(i)]=.2f*std::sin(float((start+i)*.023));r[size_t(i)]=-.1f*std::cos(float((start+i)*.047));}
        counting=true;c.process(l.data(),r.data(),n,p);counting=false;
        for(int i=0;i<n;++i){const int t=start+i-16;require(std::abs(l[size_t(i)]-(t<0?0:.2f*std::sin(float(t*.023))))<3.e-6f,"Left neutral/dry delay mismatch");require(std::abs(r[size_t(i)]-(t<0?0:-.1f*std::cos(float(t*.047))))<3.e-6f,"Right neutral/dry delay mismatch");}start+=n;}
    require(c.latency()==16&&c.latency()<=int(std::ceil(sr*.006)),"Slot latency budget exceeded");}
template<class C,class P>double component(C& c,const P& p,double sr,double hz,double measured){c.prepare(sr);Buffer l{},r{};double re=0,im=0;const int total=int(sr)*2,begin=int(sr);int count=0;
    for(int start=0;start<total;start+=257){const int n=std::min(257,total-start);for(int i=0;i<n;++i)l[size_t(i)]=r[size_t(i)]=.5f*float(std::sin(2*hem::pi*hz*(start+i)/sr));counting=true;c.process(l.data(),r.data(),n,p);counting=false;
        for(int i=0;i<n;++i)if(start+i>=begin){const double phase=2*hem::pi*measured*(start+i)/sr;re+=l[size_t(i)]*std::cos(phase);im+=l[size_t(i)]*std::sin(phase);++count;}}
    return 2*std::hypot(re,im)/count;}
// Deliberately unfiltered references for spectral foldback comparison only.
struct RawTape {void prepare(double){}void process(float* l,float* r,int n,const hem::TapeSettings& p){const float d=hem::gain(p.drive);for(int i=0;i<n;++i){l[i]=std::tanh(l[i]*d)/d;r[i]=std::tanh(r[i]*d)/d;}}};
struct RawExciterHigh {double sr=48000,previous=0;std::array<double,2> low{},last{};void prepare(double s){sr=s;low={};last={};}
    void process(float* l,float* r,int n,const hem::ExciterSettings& p){const double g=std::tan(hem::pi*p.midHigh/sr),b=g/(1+g),z=std::tanh(.25),slope=1-z*z;
        for(int i=0;i<n;++i)for(int ch=0;ch<2;++ch){float& x=ch==0?l[i]:r[i];low[size_t(ch)]=b*(x+last[size_t(ch)])+(1-2*b)*low[size_t(ch)];last[size_t(ch)]=x;const double band=x-low[size_t(ch)];x+=float((std::tanh(band*8+.25)-z)/(8*slope)-band);}}};
template<class C,class P>void partition(const P& p){C a,b;a.prepare(48000);b.prepare(48000);std::array<float,4096> al{},ar{},bl{},br{};for(size_t i=0;i<al.size();++i)al[i]=ar[i]=bl[i]=br[i]=.3f*float(std::sin(i*.1));
    counting=true;a.process(al.data(),ar.data(),4096,p);for(int start=0;start<4096;){const int n=std::min(4096-start,1+(start*7+13)%257);b.process(bl.data()+start,br.data()+start,n,p);start+=n;}counting=false;
    for(size_t i=0;i<al.size();++i)require(std::abs(al[i]-bl[i])<1.e-7f&&std::abs(ar[i]-br[i])<1.e-7f,"Active DSP depends on block partition");}
#include "NonlinearVerification.h"
int main(){try{
    calibratedSplitFixtures();expandedAliasFixtures();transitionFixtures();
    for(double sr:{22050.,44100.,48000.,96000.,192000.}){hem::ParallelCore d;hem::ExciterCore e;hem::TapeCore t;neutral(d,hem::ParallelSettings{},sr);neutral(e,hem::ExciterSettings{},sr);neutral(t,hem::TapeSettings{},sr);
        hem::ParallelSettings pd;pd.drive=24;pd.blend=0;neutral(d,pd,sr,true);pd.blend=100;pd.soloWet=true;pd.output=6;pd.bypass=true;neutral(d,pd,sr,true);
        hem::ExciterSettings pe;pe.amount={100,100,100};pe.mix={100,100,100};neutral(e,pe,sr,true);pe.on={true,true,true};pe.output=6;pe.solo={false,true,false};pe.bypass=true;neutral(e,pe,sr,true);
        Buffer l{},r{};d.prepare(sr);e.prepare(sr);for(int b=0;b<80;++b){for(int i=0;i<257;++i)l[size_t(i)]=r[size_t(i)]=.3f*std::sin(float(b*257+i));pd.type=b%2;pd.quality=b%2;pd.bypass=b%3==0;pd.drive=float(b%25);pe.bypass=b%3==0;pe.lowMid=b%2?80:1500;pe.midHigh=b%2?2000:12000;pe.solo={b%2==0,b%3==0,b%5==0};counting=true;d.process(l.data(),r.data(),257,pd);e.process(l.data(),r.data(),257,pe);counting=false;for(float v:l)require(std::isfinite(v)&&std::abs(v)<10,"Automation instability");}
        std::cout<<"PASS neutral, variable blocks, bypass, stereo, automation, latency "<<sr<<" Hz\n";
    }
    for(double sr:{44100.,48000.,96000.}){hem::ParallelCore d;hem::ParallelSettings p;p.drive=18;p.blend=100;p.quality=0;
        const double hz=sr*.19,alias=sr*.43,one=component(d,p,sr,hz,alias);p.quality=1;const double four=component(d,p,sr,hz,alias);require(one>1.e-4&&four<one*.5,"4x foldback not 6 dB below 1x");
        const double h3=component(d,p,sr,1000,3000);require(h3>1.e-3,"Parallel distortion produces no harmonic");p.blend=50;require(std::abs(component(d,p,sr,1000,3000)/h3-.5)<.01,"Blend does not scale harmonic contribution");p.blend=100;p.output=6;require(std::abs(component(d,p,sr,1000,3000)/h3-hem::gain(6))<.01,"Output gain wrong");p.output=0;p.type=1;require(component(d,p,sr,1000,2000)>1.e-4,"Asymmetric type produces no H2");p.blend=0;p.soloWet=true;require(component(d,p,sr,1000,2000)>1.e-4,"Solo wet not connected");
        hem::ExciterCore e;hem::ExciterSettings q;q.amount={100,100,100};q.mix={100,100,100};q.on={true,true,true};require(component(e,q,sr,1000,2000)>1.e-4,"Exciter produces no harmonic");
        q.on={false,false,false};require(component(e,q,sr,1000,2000)<1.e-5,"Band off creates harmonics");q.solo={true,false,false};const double low=component(e,q,sr,5000,5000);q.solo={false,false,true};const double high=component(e,q,sr,5000,5000);require(high>low*2,"Exciter band solos not frequency selective");
        q.solo={};q.on={false,false,true};RawExciterHigh rawExciter;const double rawE=component(rawExciter,q,sr,hz,alias),filteredE=component(e,q,sr,hz,alias);require(rawE>1.e-4&&filteredE<rawE*.5,"Exciter 4x foldback not 6 dB below raw reference");
        hem::TapeSettings tp;tp.drive=18;hem::TapeCore tape;RawTape rawTape;const double rawT=component(rawTape,tp,sr,hz,alias),filteredT=component(tape,tp,sr,hz,alias);require(rawT>1.e-4&&filteredT<rawT*.5,"Tape 4x foldback not 6 dB below raw reference");
        std::cout<<"Exciter alias raw/4x "<<rawE<<" / "<<filteredE<<" Tape alias raw/4x "<<rawT<<" / "<<filteredT<<"\n";
        std::cout<<"PASS "<<sr<<" Hz / 1x alias "<<one<<" / 4x alias "<<four<<" / H3 "<<h3<<"\n";
    }
    hem::ParallelSettings pd;pd.drive=18;pd.blend=70;pd.type=1;partition<hem::ParallelCore>(pd);hem::ExciterSettings pe;pe.amount={50,80,100};pe.mix={40,60,90};pe.on={true,true,true};partition<hem::ExciterCore>(pe);
    require(allocations==0,"Callback allocation detected");std::cout<<"PASS active partition invariance and no C++ DSP allocations\n";return 0;
}catch(const std::exception& e){counting=false;std::cerr<<e.what()<<'\n';return 1;}}
