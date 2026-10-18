#include "../Source/DSP/FoundationCore.h"
#include "FoundationReference.h"
#include "FoundationAllocationProbe.h"
#include "FoundationPlatformAllocations.h"
#include <iostream>
#include <complex>
#include <limits>
#include <stdexcept>
#include <vector>
#include <chrono>
using hem::FoundationKind;using hem::FoundationSettings;
static void require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
static float randomSample(uint32_t& state){state=1664525u*state+1013904223u;return float(double(state)/4294967296.*2-1);}
static FoundationSettings active(FoundationKind k){
 switch(k){case FoundationKind::cleaner:return {1,1,40,1,100,0};
 case FoundationKind::gate:return {-25,6,3,24,5,50,100,0,100,0};
 case FoundationKind::rider:return {-18,12,12,100,100,-60,100,0};
 case FoundationKind::plosive:return {180,-60,0,24,.1f,200,100,0};
 case FoundationKind::hum:return {50,30,8,18,100,0};}return {};}
static FoundationSettings limits(FoundationKind k,bool upper){
 switch(k){case FoundationKind::cleaner:return upper?FoundationSettings{1,1,80,1,100,6}:FoundationSettings{0,0,10,0,0,-24};
 case FoundationKind::gate:return upper?FoundationSettings{0,24,10,24,50,500,2000,1,100,6}:FoundationSettings{-80,0,1,0,.1f,0,5,0,0,-24};
 case FoundationKind::rider:return upper?FoundationSettings{-6,12,12,2000,2000,-20,100,6}:FoundationSettings{-36,0,0,50,10,-80,0,-24};
 case FoundationKind::plosive:return upper?FoundationSettings{180,-6,18,24,20,500,100,6}:FoundationSettings{40,-60,0,0,.1f,20,0,-24};
 case FoundationKind::hum:return upper?FoundationSettings{70,60,16,36,100,6}:FoundationSettings{40,10,1,0,0,-24};}return {};}
template<FoundationKind K> static void streamTests(double rate){
 constexpr int total=16001;const auto settings=active(K);std::vector<float> l(total),r(total);
 uint32_t rng=17;for(int i=0;i<total;++i){l[size_t(i)]=randomSample(rng)*.5f;r[size_t(i)]=randomSample(rng)*.3f;}
 hem::FoundationCore<K> core;core.prepare(rate);auto neutralL=l,neutralR=r;
 core.process(neutralL.data(),neutralR.data(),total,hem::foundationDefaults(K));
 for(int i=0;i<total;++i)require(std::abs(neutralL[size_t(i)]-l[size_t(i)])<=2e-7f&&std::abs(neutralR[size_t(i)]-r[size_t(i)])<=2e-7f,"neutral error");
 core.prepare(rate,settings);auto expectedL=l,expectedR=r;core.process(expectedL.data(),expectedR.data(),total,settings);
 reference::Engine ref(static_cast<int>(K),rate,settings);double maximumError=0;
 for(int i=0;i<total;++i){const auto y=ref.tick(l[size_t(i)],r[size_t(i)]);maximumError=std::max({maximumError,std::abs(double(y[0])-expectedL[size_t(i)]),std::abs(double(y[1])-expectedR[size_t(i)])});}
 require(maximumError<=2e-5,"independent constant reference mismatch");
 for(int block:{1,17,64,128,257,1024,0}){core.prepare(rate,settings);auto a=l,b=r;int offset=0,turn=0;const int irregular[]{3,311,1,1024,19,71};
  while(offset<total){const int n=std::min(total-offset,block?block:irregular[(turn++)%6]);
   allocationProbe::enabled=true;core.process(a.data()+offset,b.data()+offset,n,settings);allocationProbe::enabled=false;offset+=n;}
  for(int i=0;i<total;++i)require(std::abs(a[size_t(i)]-expectedL[size_t(i)])<=2e-6f&&std::abs(b[size_t(i)]-expectedR[size_t(i)])<=2e-6f,"block partition dependence");
 }
 // Sample-indexed changes exercise every continuous setting and each switch.
 core.prepare(rate,settings);reference::Engine automation(static_cast<int>(K),rate,settings);auto target=settings;
 double discontinuityError=0;float previousActual=0,previousReference=0;
 for(int i=0;i<total;++i){if(i%503==0){const int index=(i/503)%hem::FoundationCore<K>::count;
   const auto defaults=hem::foundationDefaults(K);target[size_t(index)]=target[size_t(index)]==settings[size_t(index)]?defaults[size_t(index)]:settings[size_t(index)];automation.set(target);}
  float x=l[size_t(i)],y=r[size_t(i)];const auto independent=automation.tick(x,y);core.process(&x,&y,1,target);
  require(std::abs(x-independent[0])<=2e-5f&&std::abs(y-independent[1])<=2e-5f,"smooth automation reference mismatch");
  discontinuityError=std::max(discontinuityError,std::abs(double(x-previousActual)-double(independent[0]-previousReference)));previousActual=x;previousReference=independent[0];
 }
 auto dry=settings;dry[size_t(hem::FoundationCore<K>::count-2)]=0;core.prepare(rate,dry);auto a=l,b=r;core.process(a.data(),b.data(),total,dry);require(a==l&&b==r,"mix zero not dry");
 for(int fixture=0;fixture<7;++fixture){core.prepare(rate,settings);reference::Engine oracle(static_cast<int>(K),rate,settings);
  for(int i=0;i<1024;++i){float x=fixture==0?0:fixture==1?(i==0?1.f:0.f):fixture==2?.25f:float(.5*std::sin(i*.17));
   float y=fixture==3?0:fixture==4?-x:fixture==5?x*.1f:x;if(fixture==6)x=0;
   const auto expected=oracle.tick(x,y);core.process(&x,&y,1,settings);require(std::abs(x-expected[0])<=2e-5f&&std::abs(y-expected[1])<=2e-5f,"stereo/impulse reference");}}
 for(float output:{-24.f,-6.f,6.f}){auto p=hem::foundationDefaults(K);p[size_t(hem::FoundationCore<K>::count-1)]=output;core.prepare(rate,p);float x=.25f,y=-.1f;core.process(&x,&y,1,p);require(std::abs(20*std::log10(x/.25)/1.-output)<.01,"output calibration");}
 for(float scale:{8.f,1.e6f}){core.prepare(rate,settings);for(int i=0;i<20000;++i){float x=randomSample(rng)*scale,y=-x*.37f;core.process(&x,&y,1,settings);require(std::isfinite(x)&&std::isfinite(y),"extreme non-finite");}}
 for(bool upper:{false,true}){const auto p=limits(K,upper);core.prepare(rate,p);for(int i=0;i<8192;++i){float x=randomSample(rng)*8,y=randomSample(rng)*8;core.process(&x,&y,1,p);require(std::isfinite(x)&&std::isfinite(y),"parameter corner failure");}}
 {auto p=limits(K,false);core.prepare(rate,p);reference::Engine reference(static_cast<int>(K),rate,p);
  for(int i=0;i<20000;++i){if(i%997==0){p=limits(K,(i/997)%2!=0);reference.set(p);}float x=randomSample(rng)*.5f,y=randomSample(rng)*.5f;const auto expected=reference.tick(x,y);core.process(&x,&y,1,p);
   require(std::isfinite(x)&&std::isfinite(y)&&std::abs(x-expected[0])<=2e-5f&&std::abs(y-expected[1])<=2e-5f,"extreme automation recurrence");}}
 for(float invalid:{std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity(),-std::numeric_limits<float>::infinity()}){
  core.prepare(rate,settings);float x=invalid,y=.1f;core.process(&x,&y,1,settings);require(std::isfinite(x)&&std::isfinite(y),"invalid input not sanitized");
  for(int i=0;i<1000;++i){x=.1f;y=-.1f;core.process(&x,&y,1,settings);require(std::isfinite(x)&&std::isfinite(y),"persistent poison");}}
 const auto before=core.processedSamples();core.process(nullptr,nullptr,0,settings);require(before==core.processedSamples(),"zero count advanced");
 require(allocationProbe::count==0&&platformProbe::count==0,"prepared kernel allocated");
 std::cout<<"stream kind="<<int(K)<<" rate="<<rate<<" reference_max="<<maximumError<<" discontinuity_reference_error="<<discontinuityError<<'\n';
}
static void decayTests(double rate){
 for(int order:{0,1})for(double frequency:{0.,10.,40.}){
  auto p=active(FoundationKind::cleaner);p[2]=10;p[3]=float(order);hem::DcSubsonicCore core;core.prepare(rate,p);
  for(int i=0;i<int(rate*3);++i){float l=i<int(rate*2)?(frequency==0?1.f:float(std::sin(2*reference::pi*frequency*i/rate))):0,r=-l*.4f;
   core.process(&l,&r,1,p);if(i>=int(rate*2.9))require(std::abs(l)<1e-6&&std::abs(r)<1e-6,"cleaner declared tail");}
 }
 {auto p=hem::foundationDefaults(FoundationKind::cleaner);p[0]=1;hem::DcSubsonicCore core;core.prepare(rate,p);
  for(int i=0;i<int(rate);++i){float l=.25f,r=-.1f;core.process(&l,&r,1,p);if(i>=int(rate*.9))require(std::abs(l)<1e-6&&std::abs(r)<1e-6,"independent DC rejection");}}
 for(int count:{1,8,16})for(int fixture:{0,1}){auto p=active(FoundationKind::hum);p[0]=40;p[1]=60;p[2]=float(count);p[3]=36;hem::HumBuzzCore core;core.prepare(rate,p);
  double previousWindow=1.e100,windowPeak=0;
  for(int i=0;i<int(rate*13);++i){float l=i<int(rate*5)?(fixture==0?(i==0?1.f:0.f):float(std::sin(2*reference::pi*40*i/rate))):0,r=-l*.3f;core.process(&l,&r,1,p);
   if(i>=int(rate*5)){windowPeak=std::max(windowPeak,std::abs(double(l)));if((i+1)%int(rate)==0){require(windowPeak<=previousWindow+1e-8,"hum tail envelope grows");previousWindow=windowPeak;windowPeak=0;}}
   if(i>=int(rate*12))require(std::abs(l)<1e-6&&std::abs(r)<1e-6,"hum declared eight-second tail");}
 }
}
using Complex=std::complex<double>;
static Complex rbj(double rate,double frequency,double q,double probe,int mode){
 const double w=2*reference::pi*frequency/rate,c=std::cos(w),a=std::sin(w)/(2*q);
 const Complex z=std::exp(Complex(0,-2*reference::pi*probe/rate));
 const double b0=mode==0?(1+c)/2:1,b1=mode==0?-(1+c):-2*c,b2=b0;
 return (b0+b1*z+b2*z*z)/((1+a)-2*c*z+(1-a)*z*z);
}
template<FoundationKind K> static Complex response(double rate,FoundationSettings p,double hz,double settle){
 hem::FoundationCore<K> core;core.prepare(rate,p);const int settling=int(settle*rate),window=int(rate);Complex input{},output{};
 // Coherent integration is exact for the integer-frequency probes used here.
 for(int i=0;i<settling+window;++i){const double angle=2*reference::pi*hz*i/rate;float l=float(.25*std::sin(angle)),r=0;const float dry=l;core.process(&l,&r,1,p);
  require(r==0,"filter channel cross-talk");if(i>=settling){const auto phasor=std::exp(Complex(0,-angle));input+=double(dry)*phasor;output+=double(l)*phasor;}}
 return output/input;
}
static void filterTests(double rate){
 for(double hz:{5.,20.,100.,1000.}){auto p=hem::foundationDefaults(FoundationKind::cleaner);p[0]=1;const auto measured=response<FoundationKind::cleaner>(rate,p,hz,2);
  const double pole=std::exp(-2*reference::pi*5/rate);const auto z=std::exp(Complex(0,-2*reference::pi*hz/rate));const auto expected=(1+pole)*.5*(1.-z)/(1.-pole*z);
  require(std::abs(20*std::log10(std::abs(measured/expected)))<.05,"DC magnitude");require(std::abs(std::arg(measured/expected)*180/reference::pi)<.5,"DC phase");}
 for(int order:{0,1})for(double hz:{10.,20.,40.,80.,1000.}){auto p=hem::foundationDefaults(FoundationKind::cleaner);p[1]=1;p[2]=40;p[3]=float(order);
  const auto measured=response<FoundationKind::cleaner>(rate,p,hz,2);auto expected=rbj(rate,40,1/std::sqrt(2.),hz,0);if(order)expected*=expected;
  require(std::abs(20*std::log10(std::abs(measured/expected)))<.08,"HP response");}
 // Full harmonic/Q/count/depth matrix; one multisine per combination probes all
 // selected centers simultaneously, plus analytic off-notch single probes.
 for(double fundamental:{50.,60.})for(int count:{1,8,16})for(double q:{10.,30.,60.})for(double depth:{6.,18.,36.}){
  auto p=hem::foundationDefaults(FoundationKind::hum);p[0]=float(fundamental);p[1]=float(q);p[2]=float(count);p[3]=float(depth);
  hem::HumBuzzCore core;core.prepare(rate,p);std::array<Complex,16> in{},out{};
  const int settle=int(rate*8),window=int(rate);
  for(int i=0;i<settle+window;++i){double sum=0;for(int k=1;k<=count;++k)sum+=.4/count*std::sin(2*reference::pi*fundamental*k*i/rate);
   float l=float(sum),r=0;core.process(&l,&r,1,p);require(r==0,"hum cross-talk");
   if(i>=settle)for(int k=1;k<=count;++k){const auto z=std::exp(Complex(0,-2*reference::pi*fundamental*k*i/rate));in[size_t(k-1)]+=sum*z;out[size_t(k-1)]+=double(l)*z;}}
  for(int k=0;k<count;++k)require(std::abs(20*std::log10(std::abs(out[size_t(k)]/in[size_t(k)]))+depth)<.15,"hum center depth");
 }
 for(double probe:{58.,997.,4000.}){auto p=active(FoundationKind::hum);const auto measured=response<FoundationKind::hum>(rate,p,probe,8);Complex bank=1;
  for(int k=1;k<=8;++k)bank*=rbj(rate,50*k,30,probe,2);const auto expected=1.+(1-reference::gain(-18))*(bank-1.);
  require(std::abs(20*std::log10(std::abs(measured/expected)))<.15,"hum blended response");}
}
static void malformedHoldTests(double rate){
 const std::array<float,3> invalid{std::numeric_limits<float>::quiet_NaN(),
     std::numeric_limits<float>::infinity(),-std::numeric_limits<float>::infinity()};
 for(float initialHold:{0.f,50.f,500.f})for(float bad:invalid)for(int block:{1,64,128,257}){
  auto p=hem::foundationDefaults(FoundationKind::gate);p[3]=24;p[5]=initialHold;p[7]=1;
  hem::SmartGateCore core;core.prepare(rate,p);reference::Engine expected(1,rate,p);
  // A zero-length callback must not accept a new hold target.
  auto empty=p;empty[5]=initialHold==0?500.f:0.f;
  core.process(nullptr,nullptr,0,empty);
  const int change=int(rate*.5),total=int(rate*1.4);int offset=0;
  std::array<float,257> l{},r{},expectedL{},expectedR{};
  while(offset<total){auto sent=p;sent[5]=bad;
   int n=std::min(block,total-offset);
   if(offset<change)n=std::min(n,change-offset);
   if(offset==change){p[5]=initialHold==500?0.f:500.f;sent=p;expected.set(p);n=1;}
   for(int i=0;i<n;++i){const int at=offset+i;
    l[size_t(i)]=(at==0||at==change||at==change+int(rate*.2))?.1f:.001f;
    r[size_t(i)]=l[size_t(i)]*.25f;const auto out=expected.tick(l[size_t(i)],r[size_t(i)]);
    expectedL[size_t(i)]=out[0];expectedR[size_t(i)]=out[1];}
   allocationProbe::enabled=true;core.process(l.data(),r.data(),n,sent);allocationProbe::enabled=false;
   require(core.isOpen()==expected.opened,"malformed hold altered state boundary");
   require(std::abs(core.envelopeDb()-expected.env)<1e-9,"malformed hold altered envelope timing");
   for(int i=0;i<n;++i)require(std::isfinite(l[size_t(i)])&&std::isfinite(r[size_t(i)])
       &&std::abs(l[size_t(i)]-expectedL[size_t(i)])<=2e-6f
       &&std::abs(r[size_t(i)]-expectedR[size_t(i)])<=2e-6f,"malformed hold render mismatch");
   offset+=n;
  }
 }
 // Invalid prepare snapshots fall back to the specified 50 ms hold, rather
 // than inheriting an earlier preparation's target or converting NaN/Inf.
 for(float bad:invalid){auto p=hem::foundationDefaults(FoundationKind::gate);p[3]=24;p[5]=500;p[7]=1;
  hem::SmartGateCore core;core.prepare(rate,p);p[5]=bad;core.prepare(rate,p);
  auto valid=p;valid[5]=50;reference::Engine expected(1,rate,valid);
  for(int i=0;i<int(rate);++i){float l=i==0?.1f:.001f,r=l*.25f;const auto out=expected.tick(l,r);
   core.process(&l,&r,1,p);require(core.isOpen()==expected.opened&&std::abs(core.envelopeDb()-expected.env)<1e-9,
       "invalid prepare hold did not use 50 ms fallback");require(std::abs(l-out[0])<=2e-6f,"invalid prepare hold output");}
 }
 // Check exact crossing/count timing sample by sample with a one-sample break
 // above the close threshold but below the open threshold.
 for(float hold:{0.f,50.f,500.f}){auto p=hem::foundationDefaults(FoundationKind::gate);p[3]=24;p[5]=hold;p[7]=1;
  hem::SmartGateCore core;core.prepare(rate,p);double detector=0;uint64_t consecutive=0;
  bool opened=false;int firstBelow=-1,breakAt=-1;const auto count=uint64_t(std::ceil(rate*double(hold)/1000.));
  for(int i=0;i<int(rate*1.2);++i){float l=i==0?.1f:.001f;
   if(i==breakAt)l=float(std::pow(10.,-46./20.)*1.1);
   float r=l*.25f;detector=std::max(double(l),detector*std::exp(-1/(rate*.05)));
   const double db=20*std::log10(std::max(detector,1e-12));
   if(db>=-40){opened=true;consecutive=0;}
   else if(db<=-46){if(firstBelow<0){firstBelow=i;breakAt=i+std::max(1,int(count/2));}
    ++consecutive;if(consecutive>=std::max(uint64_t(1),count))opened=false;}
   else consecutive=0;
   auto sent=p;if(i%3==0)sent[5]=invalid[size_t((i/3)%3)];core.process(&l,&r,1,sent);
   require(core.isOpen()==opened,"hold close/break timing off by a sample");
  }
 }
 require(allocationProbe::count==0&&platformProbe::count==0,"malformed hold path allocated");
}
static void extremeHarmonicTests(double rate){
 const std::array<float,11> targets{-std::numeric_limits<float>::max(),-1e30f,-1.f,1.f,1.5f,16.f,1e30f,
     std::numeric_limits<float>::max(),std::numeric_limits<float>::infinity(),
     -std::numeric_limits<float>::infinity(),std::numeric_limits<float>::quiet_NaN()};
 for(float target:targets){const float safe=!std::isfinite(target)?8.f:target<=1?1.f:target>=16?16.f:2.f;
  for(bool duringPrepare:{false,true}){auto p=active(FoundationKind::hum);p[2]=duringPrepare?target:8.f;
   auto referenceSettings=p;referenceSettings[2]=duringPrepare?safe:8.f;
   hem::HumBuzzCore core;core.prepare(rate,p);reference::Engine expected(4,rate,referenceSettings);
   p[2]=target;referenceSettings[2]=safe;expected.set(referenceSettings);uint32_t rng=71;
   for(int block=0;block<64;++block){std::array<float,257> l{},r{},a{},b{};
    for(size_t i=0;i<l.size();++i){l[i]=randomSample(rng)*.5f;r[i]=randomSample(rng)*.5f;const auto out=expected.tick(l[i],r[i]);a[i]=out[0];b[i]=out[1];}
    allocationProbe::enabled=true;core.process(l.data(),r.data(),257,p);allocationProbe::enabled=false;
    for(size_t i=0;i<l.size();++i)require(std::isfinite(l[i])&&std::isfinite(r[i])
        &&std::abs(l[i]-a[i])<=2e-5f&&std::abs(r[i]-b[i])<=2e-5f,"extreme harmonic target mismatch");
   }
  }
 }
 require(allocationProbe::count==0&&platformProbe::count==0,"extreme harmonic path allocated");
}
static void dynamicsTests(double rate){
 auto p=hem::foundationDefaults(FoundationKind::gate);p[3]=24;hem::SmartGateCore gate;gate.prepare(rate,p);
 for(int i=0;i<int(rate*3);++i){float l=.001f,r=.0001f;gate.process(&l,&r,1,p);}
 require(std::abs(gate.envelopeDb()-14)<.02,"expansion boundary law");
 for(float ratio:{1.f,2.f})for(float range:{6.f,24.f}){auto q=p;q[2]=ratio;q[3]=range;gate.prepare(rate,q);
  for(int i=0;i<int(rate*2);++i){float l=.001f,r=.0001f;gate.process(&l,&r,1,q);}
  require(std::abs(gate.envelopeDb()-std::min(double(range),double((ratio-1)*14)))<.02,"gate ratio/range calibration");}
 {auto q=p;q[5]=0;q[7]=1;q[8]=50;gate.prepare(rate,q);const int tau=int(std::round(rate*q[6]/1000));
  for(int i=0;i<tau;++i){float l=0,r=0;gate.process(&l,&r,1,q);}
  require(std::abs(gate.envelopeDb()-24*(1-std::exp(-double(tau)/(rate*q[6]*.001))))<.02,"gate close time constant");
  for(int i=0;i<int(rate*2);++i){float l=0,r=0;gate.process(&l,&r,1,q);}
  require(std::abs(gate.measurement.load()+20*std::log10(.5+.5*reference::gain(-24)))<.02,"gate effective blend meter");
  const double start=gate.envelopeDb();const int opening=int(std::round(rate*q[4]/1000));
  for(int i=0;i<opening;++i){float l=.5f,r=.05f;gate.process(&l,&r,1,q);require(std::abs(double(l)/r-10)<1e-5,"linked gate channel ratio");}
  require(std::abs(gate.envelopeDb()-start*std::exp(-double(opening)/(rate*q[4]*.001)))<.02,"gate open time constant");}
 for(int hold:{0,50,500}){p[5]=float(hold);p[7]=1;gate.prepare(rate,p);reference::Engine ref(1,rate,p);
  for(int i=0;i<int(rate*2);++i){float l=(i==0||i==int(rate*.2))?.1f:0,r=l;ref.tick(l,r);gate.process(&l,&r,1,p);require(gate.isOpen()==ref.opened,"gate hold/state timing");require(std::abs(gate.envelopeDb()-ref.env)<.02,"gate envelope recurrence");}}
 auto rider=active(FoundationKind::rider);hem::VocalRiderCore core;core.prepare(rate,rider);
 for(int i=0;i<int(rate*4);++i){float l=float(std::sqrt(2.)*reference::gain(-24)*std::sin(2*reference::pi*1000*i/rate)),r=-l;core.process(&l,&r,1,rider);
  if(i<int(std::ceil(rate*.1)))require(core.envelopeDb()==0,"rider warm-up");}
 require(std::abs(core.envelopeDb()-6)<.03,"rider RMS target");
 for(auto test:{std::array<double,4>{-6,12,12,-12},std::array<double,4>{-50,6,12,6},std::array<double,4>{-6,12,3,-3},std::array<double,4>{-70,12,12,0}}){auto q=rider;q[1]=float(test[1]);q[2]=float(test[2]);core.prepare(rate,q);
  for(int i=0;i<int(rate*4);++i){float l=float(std::sqrt(2.)*reference::gain(test[0])*std::sin(2*reference::pi*1000*i/rate)),r=0;core.process(&l,&r,1,q);require(r==0,"rider silent-channel output");}
  require(std::abs(core.envelopeDb()-test[3])<.03,"rider cap/activity floor");}
 for(int i=0;i<int(rate*4);++i){float l=0,r=0;core.process(&l,&r,1,rider);require(l==0&&r==0&&std::isfinite(core.measurement.load()),"rider silence");}
 require(hem::foundation::plosiveRequest(-18,-30,-30,0,6)==6,"plosive severity max");
 require(hem::foundation::plosiveRequest(-18,-30,-30,9,6)==3,"plosive severity half");
 require(hem::foundation::plosiveRequest(-18,-30,-30,12,6)==0,"plosive severity zero");
 auto plosive=active(FoundationKind::plosive);hem::PlosiveCore burst;burst.prepare(rate,plosive);reference::Engine ref(3,rate,plosive);double dryPower=0,wetPower=0,maxError=0;
 for(int i=0;i<int(rate*2.58);++i){const bool sounding=i>=int(rate*2)&&i<int(rate*2.08);float l=sounding?float(.8*std::sin(2*reference::pi*60*(i-int(rate*2))/rate)):0,r=l*.7f;
  const auto expected=ref.tick(l,r);if(sounding)dryPower+=l*l;burst.process(&l,&r,1,plosive);if(sounding)wetPower+=l*l;maxError=std::max(maxError,std::abs(double(l)-expected[0]));
  if(i>int(rate*2.48))require(std::abs(l)<1e-6,"plosive tail");}
 require(10*std::log10(wetPower/dryPower)<=-3,"plosive burst reduction");require(maxError<=2e-5,"plosive independent burst reference");
 for(double frequency:{60.,2000.}){auto q=plosive;q[0]=100;q[1]=-24;q[2]=6;burst.prepare(rate,q);double in=0,out=0;
  for(int i=0;i<int(rate*4);++i){float l=float(reference::gain(-12)*std::sin(2*reference::pi*frequency*i/rate)),r=l*.3f;const auto dry=l;burst.process(&l,&r,1,q);
   if(i>=int(rate*3.5)){in+=dry*dry;out+=l*l;if(frequency==2000)require(burst.envelopeDb()<.02,"plosive high-frequency detector");}}
  require(std::abs(10*std::log10(out/in))<.05,"plosive sustained recovery");}
}
template<FoundationKind K> static void rateAndOverflowTests(){
 using Core=hem::FoundationCore<K>;Core core;auto p=hem::foundationDefaults(K);
 float l=.25f,r=-.125f;core.process(&l,&r,1,p);
 require(l==0&&r==0&&!core.isPrepared()&&core.sampleRate()==0,"unprepared core safety");
 for(double rate:{0.,-1.,7999.,384001.,std::nextafter(8000.,0.),std::nextafter(384000.,1.e6),
     std::numeric_limits<double>::denorm_min(),std::numeric_limits<double>::max(),
     -std::numeric_limits<double>::max(),std::numeric_limits<double>::infinity(),
     -std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN()}){
  require(core.prepare(48000,p),"valid rate rejected");l=.2f;r=.1f;core.process(&l,&r,1,p);
  require(!core.prepare(rate,p)&&!core.isPrepared()&&core.sampleRate()==0,"invalid rate accepted/reported");
  l=.2f;r=.1f;core.process(&l,&r,0,p);require(l==.2f&&r==.1f,"zero block touched");
  allocationProbe::enabled=true;core.process(&l,&r,1,p);allocationProbe::enabled=false;
  require(l==0&&r==0&&core.processedSamples()==0&&core.measurement.load()==0,"failed prepare emitted/advanced");
 }
 for(double rate:{8000.,44100.,48000.,96000.,384000.}){
  require(core.prepare(rate,p)&&core.isPrepared()&&core.sampleRate()==rate,"supported rate mismatch");
  l=.25f;r=-.125f;core.process(&l,&r,1,p);require(l==.25f&&r==-.125f,"valid reprepare neutral");
  auto q=p;q[Core::count-1]=6;require(core.prepare(rate,q),"overflow preparation");
  for(float sign:{-1.f,1.f}){
   l=sign*std::numeric_limits<float>::max();r=.125f;
   allocationProbe::enabled=true;core.process(&l,&r,1,q);allocationProbe::enabled=false;
   require(l==0&&std::isfinite(r)&&r>0,"emitted overflow not isolated");
   l=.125f;r=.125f;core.process(&l,&r,1,q);
   const float expected=float(.125*hem::foundation::dbGain(6));
   require(std::abs(l-expected)<1e-6f&&std::abs(r-expected)<1e-6f,"overflow poisoned following audio");
  }
  core.prepare(rate,p);l=std::numeric_limits<float>::max()*.25f;r=-l;const float original=l;
  core.process(&l,&r,1,p);require(l==original&&r==-original,"finite audio clipped");
 }
 // Exercise stored channel histories with active filters. Output overflow on
 // left must not reset the right filter's history or contaminate future left.
 if constexpr(K==FoundationKind::cleaner||K==FoundationKind::hum){
  auto q=active(K);q[Core::count-1]=6;Core control;core.prepare(48000,q);control.prepare(48000,q);
  for(int i=0;i<1000;++i){l=.2f;r=.1f;float a=l,b=r;core.process(&l,&r,1,q);control.process(&a,&b,1,q);}
  l=std::numeric_limits<float>::max();r=.1f;float a=0,b=r;
  core.process(&l,&r,1,q);control.process(&a,&b,1,q);require(l==0&&r==b,"healthy filter history reset");
  double maximum=0;for(int i=0;i<1000;++i){l=0;r=.1f;a=0;b=r;core.process(&l,&r,1,q);control.process(&a,&b,1,q);
   require(std::isfinite(l)&&r==b,"overflow channel recovery");maximum=std::max(maximum,std::abs(double(l)));}
  require(maximum<1e-6,"affected filter history survived overflow");
 }
}
int main(){allocationProbe::testThread=true;try{const int imports=platformProbe::install();
 allocationProbe::enabled=true;void* calibration=::operator new(17);allocationProbe::enabled=false;::operator delete(calibration);
 require(allocationProbe::count==1,"C++ allocation probe calibration");
 require(imports==0||platformProbe::count>=1,"import allocation probe calibration");
 allocationProbe::count=0;platformProbe::count=0;std::cout<<"Platform executable imports instrumented="<<imports<<"; internal DLL allocations are outside coverage.\n";
 rateAndOverflowTests<FoundationKind::cleaner>();rateAndOverflowTests<FoundationKind::gate>();rateAndOverflowTests<FoundationKind::rider>();rateAndOverflowTests<FoundationKind::plosive>();rateAndOverflowTests<FoundationKind::hum>();
 for(double rate:{44100.,48000.,96000.}){
  streamTests<FoundationKind::cleaner>(rate);streamTests<FoundationKind::gate>(rate);streamTests<FoundationKind::rider>(rate);streamTests<FoundationKind::plosive>(rate);streamTests<FoundationKind::hum>(rate);
  malformedHoldTests(rate);extremeHarmonicTests(rate);dynamicsTests(rate);filterTests(rate);decayTests(rate);
 }
 std::cout<<"PASS foundation numeric/reference/partition/C++ allocation gates. Imported platform allocation paths counted; internal DLL paths NOT covered.\n";return 0;
}catch(const std::exception& e){allocationProbe::enabled=false;std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}}
