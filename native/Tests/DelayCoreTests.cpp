#include "../Source/DSP/DelayCore.h"
#include "DelayScaffoldGenerators.h"
#include "DelayAllocationProbe.h"
#include <cstdlib>
#include <iostream>
#include <new>
#include <stdexcept>
void require(bool b,const char* reason){if(!b)throw std::runtime_error(reason);}
using namespace hem::delay;
void process(Core& core,std::vector<float>& l,std::vector<float>& r,const Settings& p,int partition=256){allocationProbe::enabled=true;for(std::size_t i=0;i<l.size();i+=std::size_t(partition))core.process(l.data()+i,r.data()+i,int(std::min(std::size_t(partition),l.size()-i)),p,{120});allocationProbe::enabled=false;}
void filterRegression(){
 for(auto kind:{Kind::multitap,Kind::tape})for(double rate:{8000.,48000.}){
  auto p=defaults(kind);p.mix=100;p.taps[0].on=true;p.taps[0].time.manualMilliseconds=1;
  Core plain(kind),disabled(kind);plain.prepare(rate,p,{120});
  p.taps[0].lowHz=20000;p.taps[0].highHz=20;disabled.prepare(rate,p,{120});
  auto x=hem::delayScaffoldTests::noise(12000),y=x,a=x,b=x;
  process(plain,x,y,p);process(disabled,a,b,p);require(x==a&&y==b,"Disabled cut filters are not bit-null at changed cutoff values");
  const double frequency=rate==8000?2000.:8000.,cutoff=rate==8000?400.:1000.;
  p.taps[0].highHz=cutoff;p.taps[0].highOn=true;disabled.prepare(rate,p,{120});
  const int n=int(rate);x.resize(std::size_t(n));for(int i=0;i<n;++i)x[std::size_t(i)]=float(.25*std::sin(2*hem::pi*frequency*i/rate));y=x;process(disabled,x,y,p);
  double energy=0;for(int i=n/2;i<n;++i)energy+=double(x[std::size_t(i)])*x[std::size_t(i)];
  const double measured=std::sqrt(energy/(n/2))/(.25/std::sqrt(2.));
  const double pole=std::exp(-2*hem::pi*cutoff/rate),w=2*hem::pi*frequency/rate;
  const double expected=(1-pole)/std::sqrt(1+pole*pole-2*pole*std::cos(w));
  require(std::abs(measured-expected)<2e-5&&measured<.25,"High-cut response differs from independent one-pole transfer function");
 }
}
void tailResidualRegression(){
 // The untruncated production Core is the observation path: a residual cannot
 // pass this test merely because the frozen instance faded or erased it.
 for(auto kind:{Kind::multitap,Kind::tape})for(double delayMs:{17.25,4000.}){
  constexpr double rate=8000.;auto p=defaults(kind);p.mix=100;p.outputDb=12;p.driveDb=24;p.wow=100;p.flutter=100;
  const int taps=kind==Kind::tape?1:8;
  for(int i=0;i<taps;++i){auto& t=p.taps[std::size_t(i)];t.on=true;t.time.manualMilliseconds=delayMs;t.feedback=95;t.levelDb=12;t.lowHz=20;t.highHz=20;t.lowOn=t.highOn=true;}
  const auto bound=capturedTail(kind,p,{120},rate);require(bound.valid,"Extreme captured tail rejected");
  const auto beforeFade=bound.taps[0].residualBoundSamples,end=bound.taps[0].samples;
  require(end>beforeFade&&end-beforeFade==160,"Residual fade did not add exactly 20 ms");
  Core live(kind),frozen(kind);live.prepare(rate,p,{120});frozen.prepare(rate,p,{120},true);
  std::array<float,257> l{},r{},fl{},fr{};
  const auto warm=std::uint64_t(std::ceil((delayMs*.001+.006)*rate))*64;
  double earlyPeak=0,rawResidualPeak=0;
  for(std::uint64_t offset=0;offset<warm+end+513;){
   int count=int(std::min<std::uint64_t>(257,warm+end+513-offset));
   if(offset<warm)count=int(std::min<std::uint64_t>(count,warm-offset));
   for(int i=0;i<count;++i){const auto n=offset+std::uint64_t(i);l[std::size_t(i)]=n<warm?float(.1+.9*std::sin(2*hem::pi*31*double(n)/rate)):0;r[std::size_t(i)]=-l[std::size_t(i)];fl[std::size_t(i)]=l[std::size_t(i)];fr[std::size_t(i)]=r[std::size_t(i)];}
   allocationProbe::enabled=true;live.process(l.data(),r.data(),count,p,{120});frozen.process(fl.data(),fr.data(),count,p,{120});allocationProbe::enabled=false;
   for(int i=0;i<count;++i){const auto n=offset+std::uint64_t(i);if(n<warm)continue;const auto silent=n-warm+1;
    const double peak=std::max(std::abs(double(l[std::size_t(i)])),std::abs(double(r[std::size_t(i)])));
    if(silent<beforeFade)earlyPeak=std::max(earlyPeak,peak);
    if(silent<=beforeFade)require(l[std::size_t(i)]==fl[std::size_t(i)]&&r[std::size_t(i)]==fr[std::size_t(i)],"Frozen tail changed audio before its proven fade boundary");
    if(silent>=beforeFade){rawResidualPeak=std::max(rawResidualPeak,peak);require(peak<=residualAmplitude,"Unfaded production residual exceeds bound at/beyond fade start");}
    if(silent>end)require(fl[std::size_t(i)]==0&&fr[std::size_t(i)]==0,"Frozen tail failed exact finite termination");
   }offset+=std::uint64_t(count);
  }
  require(earlyPeak>1e-6,"Tail fixture was vacuously silent");
  std::cout<<"tail kind="<<taps<<" taps, delay="<<delayMs<<" ms, pre-fade bound="<<beforeFade<<" samples, unfaded residual peak="<<rawResidualPeak<<'\n';
 }
}
void feedbackEndpointRegression(){
 for(auto kind:{Kind::multitap,Kind::tape})for(double rate:{8000.,44100.,48000.,96000.,384000.})for(double delay:{1.,17.25,4000.}){
 auto p=defaults(kind);p.mix=100;p.outputDb=12;p.driveDb=24;p.wow=p.flutter=100;
 for(int i=0;i<(kind==Kind::tape?1:8);++i){auto&t=p.taps[i];t.on=true;t.feedback=95;t.levelDb=12;t.lowOn=t.highOn=true;t.lowHz=t.highHz=20;t.time.manualMilliseconds=delay;}
 auto b=capturedTail(kind,p,{120},rate);if(!b.valid)throw std::runtime_error("95% endpoint rejected");
 p.taps[0].feedback=95.000001;if(capturedTail(kind,p,{120},rate).valid)throw std::runtime_error("Invalid over-limit feedback accepted");
 }
 }
int main(){try{
 feedbackEndpointRegression();
 require(allocationProbe::calibrate(),"C++ new allocation detector failed positive control");
 filterRegression();tailResidualRegression();
 for(auto kind:{Kind::multitap,Kind::tape})for(double rate:{8000.,44100.,48000.,96000.,384000.}){
    auto p=defaults(kind);p.taps[0].on=true;p.taps[0].time.manualMilliseconds=10;p.mix=100;
    Core core(kind);core.prepare(rate,p,{120},true);
    const int delay=int(std::round(rate*.01)),latency=core.latency();std::vector<float> l(std::size_t(delay+latency+100),0),r=l;l[0]=1;r[0]=-.5f;
    process(core,l,r,p,17);for(int i=0;i<int(l.size());++i){require(std::abs(l[std::size_t(i)]-(i==delay+latency?1.f:0.f))<1e-5f,"Exact impulse timing/amplitude or natural one-shot drain failed");}
    p.mix=0;core.prepare(rate,p,{120});l=hem::delayScaffoldTests::noise(8192);r=l;const auto original=l;process(core,l,r,p,127);
    for(int i=latency;i<int(l.size());++i)require(l[std::size_t(i)]==original[std::size_t(i-latency)],"Neutral dry path is not latency aligned");
    require(Core::requiredRingBytes(rate,kind)==std::uint64_t(std::ceil(rate*4.006)+2)*8*(kind==Kind::tape?1:8),"Native-rate memory formula changed");
 }
 auto p=defaults(Kind::multitap);p.mix=100;p.taps[0].on=true;p.taps[0].feedback=50;p.taps[0].time.manualMilliseconds=1;
 Core echo(Kind::multitap);echo.prepare(8000,p,{120},true);std::vector<float> l(1000),r(1000);l[0]=1;process(echo,l,r,p,3);
 for(int i=0;i<300;++i)require(std::abs(l[std::size_t(i)]-hem::delayScaffoldTests::integerImpulseAt(std::size_t(i),8,.5))<1e-7,"Feedback recurrence differs from independent impulse oracle");
 const auto tail=capturedTail(Kind::multitap,p,{120},8000);for(std::size_t i=std::size_t(tail.seconds*8000)+2;i<l.size();++i)require(l[i]==0,"Frozen residual was not exactly terminated");
 // Reprepare and partition invariance with deterministic modulation and all taps.
 for(auto kind:{Kind::multitap,Kind::tape}){
    p=defaults(kind);p.mix=57;p.wow=81;p.flutter=73;p.driveDb=12;
    for(int i=0;i<(kind==Kind::tape?1:8);++i){p.taps[std::size_t(i)].on=true;p.taps[std::size_t(i)].time.manualMilliseconds=1+i*7;p.taps[std::size_t(i)].feedback=50;}
    Core a(kind),b(kind);a.prepare(8000,p,{120});b.prepare(8000,p,{120});auto x=hem::delayScaffoldTests::noise(5000),y=x,other=x,otherR=x;
    process(a,x,y,p,5000);process(b,other,otherR,p,13);require(x==other&&y==otherR,"Constant-state DSP depends on host block partition");
    a.prepare(8000,p,{120});b.prepare(8000,p,{120});x=hem::delayScaffoldTests::noise(1500);y=x;other=x;otherR=x;process(a,x,y,p);process(b,other,otherR,p);require(x==other,"Reprepare not deterministic");
    // Strong failed prepare preserves the old runtime and its exact continuation.
    bool failed=false;allocationProbe::failAboveBytes=1;try{a.prepare(48000,p,{120});}catch(const std::bad_alloc&){failed=true;}catch(const std::runtime_error&){failed=true;}allocationProbe::failAboveBytes=0;require(failed,"Injected allocation failure did not fail");
    x=hem::delayScaffoldTests::noise(1000);y=x;other=x;otherR=x;process(a,x,y,p);process(b,other,otherR,p);require(x==other&&a.sampleRate()==8000,"Failed prepare altered old runtime");
 }
 p=defaults(Kind::multitap);p.mix=100;p.taps[0].on=true;p.taps[0].feedback=95;p.taps[0].time.manualMilliseconds=1;
 echo.prepare(8000,p,{120});l.assign(80000,1);r.assign(80000,-1);process(echo,l,r,p);require(std::abs(l.back()-20)<1e-3,"Full-scale feedback headroom clipped");
 float a=std::numeric_limits<float>::max(),b=-a;auto meter=echo.process(&a,&b,1,p,{120});require(meter.protection>0,"Protection event not disclosed");
 a=std::numeric_limits<float>::quiet_NaN();b=std::numeric_limits<float>::infinity();echo.process(&a,&b,1,p,{120});require(std::isfinite(a)&&std::isfinite(b),"Nonfinite recovery failed");
 // Filter disabled null / enabled low-frequency attenuation.
 p.taps[0].feedback=0;p.taps[0].lowHz=1000;p.taps[0].highHz=3000;p.taps[0].lowOn=true;
 echo.prepare(8000,p,{120});l.assign(8000,1);r=l;process(echo,l,r,p);require(std::abs(l.back())<1e-5,"Low-cut failed to reject DC");
 // Per-tap independence, pan and mute-over-solo selection.
 p=defaults(Kind::multitap);p.mix=100;p.taps[0].on=true;p.taps[0].pan=-100;p.taps[0].time.manualMilliseconds=1;
 p.taps[1].on=true;p.taps[1].time.manualMilliseconds=2;p.taps[1].solo=true;p.taps[1].mute=true;
 echo.prepare(8000,p,{120},true);l.assign(80,0);r=l;l[0]=r[0]=1;process(echo,l,r,p);require(l[8]==1&&r[8]==0&&l[16]==0,"Tap pan or solo/mute precedence failed");
 // Independent transition oracles: a nonconstant source distinguishes a smooth
 // read change from a discontinuous switch, even when both stay bounded.
 auto source=[](double index){return float(std::sin(2*hem::pi*300*index/8000));};
 for(auto kind:{Kind::multitap,Kind::tape}){
  p=defaults(kind);p.mix=100;p.taps[0].on=true;p.taps[0].time.manualMilliseconds=1;
  Core transition(kind);transition.prepare(8000,p,{120});l.resize(400);for(int i=0;i<400;++i)l[std::size_t(i)]=source(i);r=l;process(transition,l,r,p);
  p.taps[0].time.manualMilliseconds=30;for(int i=0;i<400;++i)l[std::size_t(i)]=source(400+i);r=l;process(transition,l,r,p,11);
  for(int i=0;i<400;++i){double expected=0;
   if(kind==Kind::multitap){const double blend=std::min(1.,double(i+1)/160);expected=(1-blend)*source(400+i-8)+blend*source(400+i-240);}
   else{const int wetTime=400+i-16;const double read=i<16?8.:8.+.25*(i-16+1);const double integral=std::floor(read),fraction=read-integral;expected=(1-fraction)*source(wetTime-integral)+fraction*source(wetTime-integral-1);}
   require(std::abs(l[std::size_t(i)]-expected)<2e-6,"Delay transition differs from independent crossfade/slew oracle");
  }
 }
 // Independent DFT comparison of alias energy with the verified FIR primitive.
 // At 8 kHz, driven 1800 Hz sine produces a 3rd-harmonic alias at 2600 Hz.
 hem::OversampledSaturator saturator;saturator.prepare();double reOS=0,imOS=0,reRaw=0,imRaw=0;
 constexpr int length=8000;for(int n=0;n<length+1000;++n){const double phase=hem::pi*2*1800*n/8000;const float input=float(.8*std::sin(phase));const double out=saturator.tick(input,0,4,0,1),raw=std::tanh(input*4.)/4.;
    if(n>=1000){const double angle=hem::pi*2*2600*n/8000;reOS+=out*std::cos(angle);imOS+=out*std::sin(angle);reRaw+=raw*std::cos(angle);imRaw+=raw*std::sin(angle);}}
 require(std::hypot(reOS,imOS)<std::hypot(reRaw,imRaw)*.5,"Oversampled coloration did not reduce measured alias component");
 require(allocationProbe::count==0,"Process used intercepted C++ new");std::cout<<"PASS delay core timing, tails, state reset, memory, headroom, filters, transitions and alias checks; eight C++ new positive controls calibrated (direct C/OS/DLL heaps not intercepted)\n";return 0;
 }catch(const std::exception& e){allocationProbe::enabled=false;allocationProbe::failAboveBytes=0;std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}}
