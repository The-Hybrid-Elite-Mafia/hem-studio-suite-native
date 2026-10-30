#include "DSP/DspCore.h"
#include <iostream>
#include <random>
#include <stdexcept>
#include <cstdlib>
#include <new>
// Detect allocations while the kernels execute, including in Release builds.
static bool checking=false;static size_t allocations=0;
void* operator new(std::size_t n){if(checking)++allocations;if(void* p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void operator delete(void* p) noexcept {std::free(p);}void operator delete(void* p,std::size_t) noexcept {std::free(p);}
void* operator new[](std::size_t n){return ::operator new(n);}void operator delete[](void* p) noexcept {::operator delete(p);}void operator delete[](void* p,std::size_t) noexcept {::operator delete(p);}
static void require(bool b,const char* message){if(!b)throw std::runtime_error(message);}
static float rms(const std::vector<float>& v,int from=2000){double e=0;for(size_t i=size_t(from);i<v.size();++i)e+=v[i]*v[i];return float(std::sqrt(e/double(v.size()-size_t(from))));}
// Independent 4x true-peak meter for the limiter accuracy checks. It shares
// nothing with the limiter: Kaiser (beta 9) windowed sinc, 4 phases x 64 taps,
// cut-off at the original Nyquist, built and run in double precision. Phase 0
// is the sample itself, so sample peaks are always included.
struct IndependentTruePeak4x {
 static constexpr int phases=4,taps=64;std::array<std::array<double,taps>,phases> h{};
 static double besselI0(double x){double sum=1,term=1;for(int k=1;k<60;++k){term*=(x/(2*k))*(x/(2*k));sum+=term;}return sum;}
 IndependentTruePeak4x(){constexpr double beta=9,half=taps/2;
  for(int p=0;p<phases;++p){double sum=0;for(int k=0;k<taps;++k){
    const double x=double(k)-(taps/2-1)-double(p)/phases; // distance in input samples from the interpolated point
    const double sinc=std::abs(x)<1e-12?1:std::sin(hem::pi*x)/(hem::pi*x);const double u=x/half;
    const double w=besselI0(beta*std::sqrt(std::max(0.0,1-u*u)))/besselI0(beta);h[size_t(p)][size_t(k)]=sinc*w;sum+=sinc*w;}
   for(auto& c:h[size_t(p)])c/=sum;}}
 // Highest |interpolated value| over [from, to) of x (zero outside the signal).
 double measure(const std::vector<float>& x,int from=0,int to=-1) const {if(to<0)to=int(x.size());double peak=0;
  for(int i=from;i<to;++i)for(const auto& phase:h){double v=0;for(int k=0;k<taps;++k){const int j=i-k;if(j>=0&&j<int(x.size()))v+=phase[size_t(k)]*x[size_t(j)];}peak=std::max(peak,std::abs(v));}
  return peak;}
};
static double db(double x){return 20*std::log10(std::max(x,1e-12));}
static double rmsRange(const std::vector<float>& v,size_t from,size_t to){double e=0;for(size_t i=from;i<to;++i)e+=double(v[i])*v[i];return std::sqrt(e/double(to-from));}
// Pass/fail accuracy contract for LimiterCore (ceiling -1 dBTP). The older
// checks above only prove "never over the ceiling", which a limiter that
// over-reduces also passes. These checks pin the level it must reach.
static void limiterAccuracy(){
 const IndependentTruePeak4x meter;bool ok=true;
 for(double rate:{44100.0,48000.0,96000.0}){
  const int n=int(rate);const int block=512;hem::LimiterCore limiter;hem::LimiterParameters lp;limiter.prepare(rate);
  std::vector<float> l(static_cast<size_t>(n)),r(static_cast<size_t>(n)),in(static_cast<size_t>(n));
  auto run=[&]{checking=true;for(int s=0;s<n;s+=block)limiter.process(l.data()+s,r.data()+s,std::min(block,n-s),lp);checking=false;};
  // (a) 997 Hz sine at 0 dBFS: output true peak -1.0 +/- 0.1 dBTP, steady level within 0.2 dB of -1.0.
  for(int i=0;i<n;++i)l[size_t(i)]=r[size_t(i)]=float(std::sin(2*hem::pi*997*i/rate));
  run();
  const double tpA=db(std::max(meter.measure(l),meter.measure(r)));
  const size_t steadyFrom=size_t(n/2);const double levelA=db(rmsRange(l,steadyFrom,size_t(n))*std::sqrt(2.0));
  const bool passA=std::abs(tpA+1)<=.1&&std::abs(levelA+1)<=.2;
  std::cout<<"Limiter accuracy "<<rate<<" Hz (a) 0 dBFS 997 Hz sine: output true peak "<<tpA<<" dBTP, steady level "<<levelA<<" dB  "<<(passA?"PASS":"FAIL")<<'\n';
  ok=ok&&passA;
  // (b) -6 dBFS sine is 5 dB under the ceiling: gain change must be < 0.05 dB.
  limiter.prepare(rate);const int lat=limiter.latency();
  for(int i=0;i<n;++i)l[size_t(i)]=r[size_t(i)]=in[size_t(i)]=hem::gain(-6)*float(std::sin(2*hem::pi*997*i/rate));
  run();
  double worst=0;{std::vector<float> aligned(size_t(n-lat));for(int i=lat;i<n;++i)aligned[size_t(i-lat)]=l[size_t(i)];
   const double change=db(rmsRange(aligned,0,aligned.size())/rmsRange(in,0,aligned.size()));worst=std::abs(change);
   for(int i=lat;i<n;++i)worst=std::max(worst,std::abs(db(std::abs(l[size_t(i)])+1e-9)-db(std::abs(in[size_t(i-lat)])+1e-9))*(std::abs(in[size_t(i-lat)])>.1f?1:0));}
  const bool passB=worst<.05;
  std::cout<<"Limiter accuracy "<<rate<<" Hz (b) -6 dBFS sine: largest gain change "<<worst<<" dB, reduction meter "<<limiter.reduction.load()<<" dB  "<<(passB?"PASS":"FAIL")<<'\n';
  ok=ok&&passB;
  // (c) +12 dB hot noise (independent L/R): no output true peak above the ceiling on the independent 4x meter.
  limiter.prepare(rate);std::mt19937 rng(1207);std::uniform_real_distribution<float> random(-1,1);
  for(int i=0;i<n;++i){l[size_t(i)]=hem::gain(12)*random(rng);r[size_t(i)]=hem::gain(12)*random(rng);}
  run();
  const double tpC=std::max(meter.measure(l),meter.measure(r));const bool passC=tpC<=double(hem::gain(-1))+1e-6;
  std::cout<<"Limiter accuracy "<<rate<<" Hz (c) +12 dB noise: output true peak (independent 4x) "<<db(tpC)<<" dBTP  "<<(passC?"PASS":"FAIL")<<'\n';
  ok=ok&&passC;
 }
 // (d) every limiter call above ran with the allocation counter armed.
 std::cout<<"Limiter accuracy (d) heap allocations inside process: "<<allocations<<"  "<<(allocations==0?"PASS":"FAIL")<<'\n';
 ok=ok&&allocations==0;
 require(ok,"Limiter accuracy contract failed (over/under-limiting or ceiling breach)");
}
int main(){try {
 constexpr double sr=96000;constexpr int n=24000;std::vector<float> l(n),r(n),original(n);
 auto tone=[&](double hz,float amp){for(int i=0;i<n;++i)l[size_t(i)]=r[size_t(i)]=original[size_t(i)]=amp*float(std::sin(2*hem::pi*hz*i/sr));};
 hem::StereoCore stereo;hem::StereoParameters sp;stereo.prepare(sr);tone(1000,.2f);checking=true;stereo.process(l.data(),r.data(),n,sp);checking=false;
 for(int i=0;i<n;++i)require(std::abs(l[size_t(i)]-original[size_t(i)])<1e-6f,"SIPA neutral changed mono signal");
 stereo.prepare(sr);tone(300,.2f);for(auto& v:r)v=-v;sp.width=0;stereo.process(l.data(),r.data(),n,sp);require(rms(l)<1e-7f,"SIPA width zero did not collapse side signal");
 stereo.prepare(sr);tone(30,.2f);for(auto& v:r)v=-v;sp.width=100;sp.bassMono=true;sp.monoBass=120;stereo.process(l.data(),r.data(),n,sp);require(rms(l)<rms(original)*.1f,"Mono bass did not suppress low side energy");
 hem::EqCore eq;hem::EqParameters ep;eq.prepare(sr);tone(300,.2f);checking=true;eq.process(l.data(),r.data(),n,ep);checking=false;require(std::abs(rms(l)/rms(original)-1)<1e-5f,"Neutral EQ changed level");
 eq.prepare(sr);tone(300,.2f);ep.db[0]=-6;eq.process(l.data(),r.data(),n,ep);require(std::abs(hem::decibels(rms(l)/rms(original))+6)<.03f,"EQ notch gain inaccurate");
 require(std::abs(hem::EqCore::response(300,sr,ep)+6)<.01f,"EQ graph response inaccurate");
 hem::CompressorCore comp;hem::CompressorParameters cp;comp.prepare(sr);std::fill(l.begin(),l.end(),.4f);r=l;checking=true;comp.process(l.data(),r.data(),n,cp);checking=false;require(std::abs(l.back()-.4f)<1e-6f,"Neutral compressor changed gain");
 comp.prepare(sr);std::fill(l.begin(),l.end(),.4f);r=l;cp.threshold=-18;cp.ratio=4;cp.attack=1;comp.process(l.data(),r.data(),n,cp);
 const float expected=(hem::decibels(.4f)+18)*.75f;require(std::abs(-hem::decibels(l.back()/.4f)-expected)<.03f,"Compressor ratio/threshold inaccurate");
 // Random wide-band transients + a swept near-Nyquist tone. Check a separate
 // convolution of emitted output, not merely the processor's displayed meter.
 hem::Reconstruction filter;hem::LimiterCore limiter;hem::LimiterParameters lp;
 for(double rate:{44100.0,48000.0,96000.0}){
  limiter.prepare(rate);std::mt19937 rng(83);std::uniform_real_distribution<float> random(-2,2);
  for(int i=0;i<n;++i){l[size_t(i)]=random(rng);r[size_t(i)]=1.7f*float(std::sin(2*hem::pi*(.4+.09*i/n)*i));}
  checking=true;limiter.process(l.data(),r.data(),n,lp);checking=false;
  double truePeak=0;for(int i=hem::Reconstruction::taps;i<n;++i)for(const auto& h:filter.h){double a=0,b=0;for(int k=0;k<hem::Reconstruction::taps;++k){a+=h[size_t(k)]*l[size_t(i-k)];b+=h[size_t(k)]*r[size_t(i-k)];}truePeak=std::max({truePeak,std::abs(a),std::abs(b)});}
  require(truePeak<=hem::gain(-1)+1e-6,"Limiter exceeded its 8x reconstruction ceiling");
  std::cout<<"Limiter "<<rate<<" Hz / measured "<<hem::decibels(float(truePeak))<<" dBTP\n";
 }
 limiter.prepare(sr);tone(997,.05f);limiter.process(l.data(),r.data(),n,lp);for(int i=limiter.latency();i<n;++i)require(std::abs(l[size_t(i)]-original[size_t(i-limiter.latency())])<1e-6f,"Limiter altered quiet signal beyond delay");
 limiterAccuracy();
 // Stress live controls: no NaN/Inf and no allocations in any DSP block.
 eq.prepare(sr);for(int block=0;block<100;++block){ep.hz[0]=block%2?150.f:600.f;ep.db[0]=block%2?-12.f:6.f;ep.q[0]=block%2?.5f:10.f;tone(300,.2f);checking=true;eq.process(l.data(),r.data(),256,ep);checking=false;for(int i=0;i<256;++i)require(std::isfinite(l[size_t(i)]),"EQ automation became unstable");}
 require(allocations==0,"Allocation occurred inside a DSP kernel");
 hem::HostDelay delay;delay.prepare(13);std::fill(l.begin(),l.end(),0);r=l;l[0]=r[0]=1;delay.process(l.data(),r.data(),n);require(l[13]==1&&l[0]==0,"Delay latency mismatch");
 std::cout<<"PASS: neutral paths, M/S, mono bass, EQ response, compression law, 8x ceiling, limiter latency, automation, zero kernel allocations.\n";
 return 0;
 }catch(const std::exception& e){checking=false;std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}}
