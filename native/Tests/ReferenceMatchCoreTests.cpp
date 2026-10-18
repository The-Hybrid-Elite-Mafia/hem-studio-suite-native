#include "../Source/DSP/ReferenceMatchCore.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <random>
#include <vector>
using namespace hem::reference_match;
void require(bool b,const char* message){if(!b){std::fprintf(stderr,"FAIL %s\n",message);std::exit(1);}}
// Independent one-pole lowpass plus dry creates a smooth shelving mismatch.
std::vector<float> signal(unsigned seed,size_t n,double rate,bool color){
 std::mt19937 rng(seed);std::uniform_real_distribution<float>d(-.25f,.25f);std::vector<float>x(n);double z=0,a=std::exp(-2*3.141592653589793*1200/rate);
 for(auto& v:x){v=d(rng);z=(1-a)*v+a*z;if(color)v=float(.4*v+1.5*z);}return x;
}
Profile profile(const std::vector<float>& x,double rate){const float* channels[]={x.data()};return analyze(channels,1,x.size(),rate);}
// Independent time-domain oracle: residual power against target signal, allowing
// a single scalar level alignment. Neither analyzer nor fitter enters this metric.
double error(const std::vector<float>& x,const std::vector<float>& target){double xx=0,xy=0,yy=0;for(size_t i=16384;i<x.size();++i){xx+=x[i]*x[i];xy+=x[i]*target[i];yy+=target[i]*target[i];}return 1-xy*xy/(xx*yy);}
int main(){
 for(double rate:{22050.,44100.,48000.,96000.,192000.}){
 auto source=signal(1,size_t(rate),rate,false),reference=signal(2,size_t(rate),rate,true);
 auto s=profile(source,rate),r=profile(reference,rate);require(s.status==Status::ready&&r.status==Status::ready,"broadband valid");
 auto match=fit(s,r,rate);require(match.valid,"fit ready");for(int k=0;k<=20000;++k)require(std::abs(match.responseDb(rate*.5*k/20000))<=12.001,"actual response bound");
 require(std::isfinite(match.tailSeconds)&&match.tailSeconds>0&&match.tailSeconds<120,"bounded pole tail");std::printf("pole-derived tail %.6fs\n",match.tailSeconds);
 {Processor tail;tail.publish(match);std::vector<float> warm(512);tail.process(warm.data(),nullptr,warm.size());std::vector<float> impulse(size_t(std::ceil((match.tailSeconds+.1)*rate)),0);impulse[0]=1;tail.process(impulse.data(),nullptr,impulse.size());double residual=0;for(size_t i=size_t(std::ceil(match.tailSeconds*rate));i<impulse.size();++i)residual+=std::abs(impulse[i]);require(residual<1e-9,"pole bound exceeds measured impulse tail");}
 auto identity=fit(s,s,rate);require(std::abs(identity.responseDb(1000))<1e-7,"identity");auto zero=fit(s,r,rate,0);require(std::abs(zero.responseDb(1000))<1e-7,"zero amount");
 auto half=fit(s,r,rate,.5);require(std::abs(half.responseDb(200)-match.responseDb(200)*.5)<.3,"dB amount");
 auto smooth=fit(s,r,rate,1,2);require(std::abs(smooth.responseDb(200)-match.responseDb(200))>.01,"log smoothing changes fit");
 // Independent settled sinusoidal gain measurement validates plotted response.
 std::vector<float> tone(static_cast<size_t>(rate), 0.0f);for(size_t i=0;i<tone.size();++i)tone[i]=float(.1*std::sin(2*3.141592653589793*1000*i/rate));auto original=tone;Processor meter;meter.publish(match);meter.process(tone.data(),nullptr,tone.size(),3);
 double inputEnergy=0,outputEnergy=0;for(size_t i=tone.size()/2;i<tone.size();++i){inputEnergy+=original[i]*original[i];outputEnergy+=tone[i]*tone[i];}require(std::abs(10*std::log10(outputEnergy/inputEnergy)-match.responseDb(1000)-3)<.03,"physical transfer and output trim");
 for(auto& v:reference)v*=.1f;auto quiet=fit(s,profile(reference,rate),rate);require(std::abs(quiet.responseDb(500)-match.responseDb(500))<1e-4,"independent normalization");
 auto held=signal(91,size_t(rate*2),rate,false),target=signal(91,size_t(rate*2),rate,true),processed=held;Processor p;p.publish(match);p.process(processed.data(),nullptr,processed.size());
 double before=error(held,target),after=error(processed,target);std::printf("rate %.0f held-out residual before %.6f after %.6f\n",rate,before,after);require(after<before*.8,"held-out improvement");
 const float* anti[]={source.data(),reference.data()};auto stereo=analyze(anti,2,source.size(),rate);require(stereo.status==Status::ready,"stereo");
 std::vector<float> negative=source;for(auto& v:negative)v=-v;const float* phase[]={source.data(),negative.data()};auto antiphase=analyze(phase,2,source.size(),rate);require(antiphase.status==Status::ready&&std::abs(antiphase.power[40]-s.power[40])<1e-10,"anti-phase powers");
 }
 std::vector<float> zeros(48000);require(profile(zeros,48000).status!=Status::ready,"silence");zeros[0]=std::numeric_limits<float>::quiet_NaN();require(profile(zeros,48000).status==Status::invalidInput,"NaN");
 auto x=signal(3,48000,48000,false);const float* c[]={x.data()};require(analyze(c,1,x.size(),48000,0,1).status==Status::discontinuity,"gap");require(analyze(c,1,100,48000).status!=Status::ready,"short");require(analyze(c,1,x.size(),8000).status==Status::invalidInput,"rate");require(analyze(c,1,1440001,48000).status==Status::invalidInput,"cap before read");
 auto good=profile(x,48000);auto high=signal(4,96000,96000,true);require(fit(good,profile(high,96000),44100).valid,"different rates physical Hz");auto bad=good;bad.power[3]=std::numeric_limits<double>::infinity();require(!fit(bad,good,48000).valid,"invalid persisted profile");require(!fit(good,good,48000,-1).valid,"amount range");const float* missing[]={nullptr};require(analyze(missing,1,48000,48000).status==Status::invalidInput,"missing channel");
 for(size_t i=0;i<x.size();++i)x[i]=float(.2*std::sin(2*3.141592653589793*1000*i/48000));require(profile(x,48000).status!=Status::ready,"narrowband");
 bad=good;bad.power.fill(1e308);require(!fit(bad,good,48000).valid,"finite hostile power rejected");bad=good;bad.meanSquare=1e308;require(!fit(bad,good,48000).valid,"finite hostile loudness rejected");bad=good;bad.hz.fill(1e-300);require(!fit(bad,good,48000).valid,"hostile frequency grid rejected");
 std::puts("PASS ReferenceMatchCoreTests");
}
