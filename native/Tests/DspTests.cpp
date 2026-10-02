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
 // Stress live controls: no NaN/Inf and no allocations in any DSP block.
 eq.prepare(sr);for(int block=0;block<100;++block){ep.hz[0]=block%2?150.f:600.f;ep.db[0]=block%2?-12.f:6.f;ep.q[0]=block%2?.5f:10.f;tone(300,.2f);checking=true;eq.process(l.data(),r.data(),256,ep);checking=false;for(int i=0;i<256;++i)require(std::isfinite(l[size_t(i)]),"EQ automation became unstable");}
 require(allocations==0,"Allocation occurred inside a DSP kernel");
 hem::HostDelay delay;delay.prepare(13);std::fill(l.begin(),l.end(),0);r=l;l[0]=r[0]=1;delay.process(l.data(),r.data(),n);require(l[13]==1&&l[0]==0,"Delay latency mismatch");
 std::cout<<"PASS: neutral paths, M/S, mono bass, EQ response, compression law, 8x ceiling, limiter latency, automation, zero kernel allocations.\n";
 return 0;
 }catch(const std::exception& e){checking=false;std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}}
