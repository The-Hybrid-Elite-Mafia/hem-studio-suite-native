#include "../Source/DSP/PitchTimeBackend.h"
#include <vector>
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <iostream>
#include <limits>
using namespace hem::pitchtime;
void require(bool x,const char* msg){if(!x)throw std::runtime_error(msg);}
struct Run { Result result; std::vector<float> output; };
Run go(const Settings& s,const std::vector<float>& in){
 Run o; o.result=render(s,in.size(),[&](auto off,float* const* p,int n){for(int c=0;c<s.channels;++c)for(int i=0;i<n;++i)p[c][i]=in[size_t(off)+i]*(c?-1.f:1.f);return true;},[&](const float* const* p,int n){for(int i=0;i<n;++i){if(s.channels==2)if(std::abs(p[0][i]+p[1][i])>=1e-4){std::cerr<<p[0][i]<<","<<p[1][i]<<"\n";require(false,"stereo coherence");};o.output.push_back(p[0][i]);}return true;});return o;
}
double frequency(const std::vector<float>& x,double sr){double best=0,fbest=0;size_t a=x.size()/4,n=std::min<size_t>(16384,x.size()/2);for(int f=850;f<=910;++f){double re=0,im=0;for(size_t i=0;i<n;++i){double phase=2*3.141592653589793*f*i/sr,w=.5-.5*std::cos(2*3.141592653589793*i/(n-1));re+=x[a+i]*w*std::cos(phase);im+=x[a+i]*w*std::sin(phase);}double e=re*re+im*im;if(e>best){best=e;fbest=f;}}return fbest;}int main(){try{
for(double sr:{44100.,48000.,96000.,192000.})for(auto strategy:{Strategy::tonal,Strategy::rhythmic,Strategy::mixed}){
 Settings s;s.sampleRate=sr;s.strategy=strategy;std::vector<float> x(static_cast<size_t>(sr), 0.f);for(size_t i=0;i<x.size();++i)x[i]=float(.3*std::sin(2*3.141592653589793*440*i/sr));
 for(double ratio:{.5,1.,2.}){s.durationRatio=ratio;s.pitchSemitones=12;auto y=go(s,x);require(y.result.status==Result::Status::complete,"render complete");require(y.output.size()==size_t(std::llround(x.size()*ratio)),"frame count");std::cout<<"frequency "<<frequency(y.output,sr)<<" ratio "<<ratio<<" strategy "<<int(strategy)<<"\n";require(std::abs(frequency(y.output,sr)-880)<8,"independent pitch");require(!y.result.realtimeQualified,"offline admission");}
 s.durationRatio=1;s.pitchSemitones=0;s.preserveFormants=false;auto y=go(s,x);double err=0,energy=0;for(size_t i=0;i<x.size();++i){err+=std::pow(y.output[i]-x[i],2);energy+=x[i]*x[i];}require(err/energy<.005,"neutral alignment");
 std::cout<<"rate "<<sr<<" strategy "<<int(strategy)<<" latency "<<y.result.inputLatency<<"+"<<y.result.outputLatency<<" neutral relative error "<<err/energy<<"\n";
}
Settings s;for(int n:{1,17,257,513,1057})for(double ratio:{.5,1.,2.}){s.durationRatio=ratio;std::vector<float>x(n);x.back()=.5f;auto y=go(s,x);require(y.result.status==Result::Status::complete,"short drain");require(y.output.size()==size_t(std::llround(n*ratio)),"short size");}
s.durationRatio=1;std::vector<float>x(48000);auto y=go(s,x);for(float a:y.output)require(a==0,"silence");x[100]=std::numeric_limits<float>::quiet_NaN();require(go(s,x).result.status==Result::Status::nonFinite,"nan rejected");
int calls=0;auto r=render(s,48000,[&](auto,float* const*,int){++calls;return true;},[](auto,int){return true;},[]{return true;});require(r.status==Result::Status::cancelled&&calls==0,"cancel before IO");
s.durationRatio=3;require(go(s,x).result.status==Result::Status::invalid,"invalid ratio");
Settings form;form.channels=1;form.strategy=Strategy::tonal;form.formantBaseHz=100;
std::vector<float> vowel(48000);for(size_t i=0;i<vowel.size();++i)for(int h=1;h<=40;++h)vowel[i]+=float(.03*(.05+std::exp(-.5*std::pow((h*100.-800)/160,2)))*std::sin(2*3.141592653589793*h*100*i/48000));
auto centroid=[](const std::vector<float>& v){double sum=0,weight=0;for(int h=1;h<=40;++h){double re=0,im=0;for(int i=0;i<12000;++i){double p=2*3.141592653589793*h*100*i/48000;re+=v[i+18000]*std::cos(p);im+=v[i+18000]*std::sin(p);}double e=re*re+im*im;sum+=h*100*e;weight+=e;}return sum/weight;};
auto original=go(form,vowel);form.formantSemitones=12;auto raised=go(form,vowel);double a=centroid(original.output),b=centroid(raised.output);std::cout<<"formant centroid "<<a<<" -> "<<b<<" Hz\n";require(b>a*1.5,"formant movement independent of pitch");
form.formantSemitones=0;std::vector<float> impulse(10007);impulse.back()=.5f;auto drained=go(form,impulse);auto peak=std::max_element(drained.output.begin(),drained.output.end());require(size_t(peak-drained.output.begin())==impulse.size()-1,"final transient alignment");std::cout<<"final peak "<<*peak<<"\n";require(*peak>.4,"final transient preserved");
auto failRead=render(form,5000,[](auto,auto,int){return false;},[](auto,int){return true;});require(failRead.status==Result::Status::readFailed,"reader failure");
auto failWrite=render(form,5000,[](auto,float* const* p,int n){std::fill_n(p[0],n,0.f);return true;},[](auto,int){return false;});require(failWrite.status==Result::Status::writeFailed,"writer failure");
form.durationRatio=1.37;form.pitchSemitones=-7;auto first=go(form,vowel),second=go(form,vowel);require(first.output==second.output,"deterministic repeated render");
form.strategy=Strategy::rhythmic;auto rhythmic=go(form,vowel);double delta=0;for(size_t i=0;i<first.output.size();++i)delta+=std::abs(first.output[i]-rhythmic.output[i]);require(delta>1,"strategies not aliases");
Settings mix;mix.channels=1;mix.pitchSemitones=7;auto wet=go(mix,vowel);mix.wetMix=0;mix.outputDb=-6;auto dry=go(mix,vowel);for(size_t i=0;i<vowel.size();++i)require(std::abs(dry.output[i]-vowel[i]*std::pow(10.,-.3))<1e-7,"dry exact alignment and single gain");mix.wetMix=.25;auto blend=go(mix,vowel);for(size_t i=0;i<vowel.size();++i)require(std::abs(blend.output[i]-(.25*wet.output[i]+.75*vowel[i])*std::pow(10.,-.3))<1e-7,"linear aligned mix");mix.durationRatio=1.1;require(go(mix,vowel).result.status==Result::Status::invalid,"duration changing dry rejected");
std::cout<<"PASS pitch/time backend initial qualification\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}}









