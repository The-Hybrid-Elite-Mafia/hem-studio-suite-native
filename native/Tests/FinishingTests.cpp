#include "../Source/DSP/FinishingCore.h"
#include <iostream>
#include <stdexcept>
#include <cstdlib>
#include <new>
static bool counting=false;static size_t allocations=0;
void* operator new(std::size_t n){if(counting)++allocations;if(auto* p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void* operator new[](std::size_t n){return ::operator new(n);}void operator delete(void* p) noexcept{std::free(p);}void operator delete(void* p,std::size_t) noexcept{std::free(p);}void operator delete[](void* p) noexcept{std::free(p);}void operator delete[](void* p,std::size_t) noexcept{std::free(p);}
void require(bool ok,const char* text){if(!ok)throw std::runtime_error(text);}
constexpr int block=128;using Buffer=std::array<float,block>;
template<class Core,class Settings>void neutral(Core& core,const Settings& p,double sr,int latency,bool settle=false){
 core.prepare(sr);Buffer l{},r{};
 if(settle)for(int b=0;b<100;++b){l.fill(0);r.fill(0);core.process(l.data(),r.data(),block,p);}
 for(int b=0;b<30;++b){for(int i=0;i<block;++i)l[size_t(i)]=r[size_t(i)]=.2f*std::sin(float((b*block+i)*.037));
 counting=true;core.process(l.data(),r.data(),block,p);counting=false;
 for(int i=0;i<block;++i){const int n=b*block+i-latency;const float expected=n<0?0:.2f*std::sin(float(n*.037));if(std::abs(l[size_t(i)]-expected)>=2.e-6f){std::cerr<<typeid(Core).name()<<" block "<<b<<" sample "<<i<<" got "<<l[size_t(i)]<<" expected "<<expected<<"\n";throw std::runtime_error("Neutral or latency test failed");}}}
}
template<class Core,class Settings>std::array<double,4> tone(Core& c,const Settings& p,double sr,double hz){
 c.prepare(sr);Buffer l{},r{};double in=0,out=0,s2=0,c2=0,s3=0,c3=0;int count=0;
 const int end=int(sr),begin=int(sr/2);
 for(int start=0;start<end;start+=block){const int n=std::min(block,end-start);
 for(int i=0;i<n;++i)l[size_t(i)]=r[size_t(i)]=.25f*float(std::sin(2*hem::pi*hz*(start+i)/sr));
 counting=true;c.process(l.data(),r.data(),n,p);counting=false;
 for(int i=0;i<n;++i)if(start+i>=begin){const double phase=2*hem::pi*hz*(start+i)/sr,x=.25*std::sin(phase),y=l[size_t(i)];in+=x*x;out+=y*y;s2+=y*std::sin(phase*2);c2+=y*std::cos(phase*2);s3+=y*std::sin(phase*3);c3+=y*std::cos(phase*3);++count;}}
 return {10*std::log10(out/in),2*std::hypot(s2,c2)/count,2*std::hypot(s3,c3)/count,std::sqrt(out/count)};
}
int main(){try{for(double sr:{44100.,48000.,96000.}){
 hem::VocalCore v;hem::TubeCore u;hem::TapeCore t;hem::MonitorCore m;
 neutral(v,hem::VocalSettings{},sr,0);neutral(u,hem::TubeSettings{},sr,16);neutral(t,hem::TapeSettings{},sr,16);neutral(m,hem::MonitorSettings{},sr,0);
 hem::VocalSettings voice;voice.body=6;auto vocal=tone(v,voice,sr,150);require(std::abs(vocal[0]-6)<.02,"Vocal body EQ not 6 dB");
 voice.body=0;voice.resonance=6800;voice.threshold=-45;voice.range=6;auto notch=tone(v,voice,sr,6800);require(std::abs(notch[0]+6)<.03,"Vocal resonance reduction wrong");
 hem::TubeSettings tube;tube.drive=12;tube.bias=30;auto tubeResult=tone(u,tube,sr,1000);require(tubeResult[1]>.002&&tubeResult[2]>.002,"Tube did not create even and odd harmonics");
 tube.mix=0;neutral(u,tube,sr,16,true);
 hem::TapeSettings tape;tape.drive=12;auto tapeResult=tone(t,tape,sr,1000);require(tapeResult[2]>.002,"Tape saturation harmonic missing");tape.mix=0;neutral(t,tape,sr,16,true);
 hem::MonitorSettings monitor;monitor.body=-6;monitor.enabled=true;auto correction=tone(m,monitor,sr,250);require(std::abs(correction[0]+6)<.02,"Monitor EQ attenuation wrong");monitor.enabled=false;neutral(m,monitor,sr,0);
 Buffer l{},r{};for(int b=0;b<100;++b){voice.cutEnabled=b%2;voice.body=b%2?12:-12;voice.range=b%2?18:0;voice.resonance=b%2?1000:10000;tube.drive=b%2?24:0;tube.bias=b%2?50:-50;tube.mix=100;tape.drive=b%2?24:0;tape.rolloff=b%2?-12:0;tape.mix=100;monitor.enabled=b%2;monitor.air=b%2?6:-12;
 for(int i=0;i<block;++i)l[size_t(i)]=r[size_t(i)]=.1f*std::sin(float(b*block+i));
 counting=true;v.process(l.data(),r.data(),block,voice);u.process(l.data(),r.data(),block,tube);t.process(l.data(),r.data(),block,tape);m.process(l.data(),r.data(),block,monitor);counting=false;
 for(float x:l)require(std::isfinite(x)&&std::abs(x)<10,"Automation unstable");}
 std::cout<<"PASS "<<sr<<" Hz / vocal "<<vocal[0]<<" dB / dynamic "<<notch[0]<<" dB / tube H2 "<<tubeResult[1]<<" H3 "<<tubeResult[2]<<" / tape H3 "<<tapeResult[2]<<" / monitor "<<correction[0]<<" dB\n";
 }require(allocations==0,"Audio DSP allocated");std::cout<<"PASS neutral startup, 16-sample saturation latency, dry mix, disabled monitor, finite automation, zero DSP allocations\n";return 0;
 }catch(const std::exception& e){counting=false;std::cerr<<e.what()<<'\n';return 1;}}
