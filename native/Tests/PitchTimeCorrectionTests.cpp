#include "../Source/DSP/PitchTimeBackend.h"
#include <vector>
#include <cmath>
#include <iostream>
#include <random>
#include <stdexcept>
#include <algorithm>
using namespace hem::pitchtime;
constexpr double pi=3.141592653589793;
void check(bool c,const char* m){if(!c)throw std::runtime_error(m);}
std::vector<float> tone(double sr,double hz,int frames){std::vector<float> x(frames);for(int i=0;i<frames;++i)x[i]=float(.3*std::sin(2*pi*hz*i/sr));return x;}
PitchEstimate measure(const std::vector<float>& x,double sr){const float* p=x.data();MonophonicDetector detector;return detector.analyse(&p,1,int(x.size()),sr);}
int main(){try{
 for(double sr:{8000.,44100.,48000.,96000.,192000.})for(double hz:{65.,110.,220.,440.,880.,1100.}){auto x=tone(sr,hz,int(std::ceil(sr*.08)));auto e=measure(x,sr);check(e.voiced,"steady voice detection");double cents=1200*std::log2(e.hz/hz);std::cout<<sr<<" Hz input "<<hz<<" estimate "<<e.hz<<" cents "<<cents<<"\n";check(std::abs(cents)<15,"pitch cents tolerance");}
 std::vector<float> silence(3840);check(!measure(silence,48000).voiced,"silence unvoiced");std::mt19937 random(123);std::uniform_real_distribution<float> noise(-.2f,.2f);for(auto& v:silence)v=noise(random);check(!measure(silence,48000).voiced,"noise unvoiced");
 auto left=tone(48000,233.08188,3840),right=left;for(auto& v:right)v=-v;const float* stereo[]={left.data(),right.data()};MonophonicDetector detector;auto e=detector.analyse(stereo,2,3840,48000);check(e.voiced&&std::abs(e.hz-233.08188)<1,"antiphase detection");
 std::fill(left.begin(),left.end(),0);e=detector.analyse(stereo,2,3840,48000);check(e.voiced&&e.detectorChannel==1,"hard right detector policy");
 double phase=0;int voiced=0;for(int block=0;block<20;++block){std::vector<float> v(3840);for(int i=0;i<3840;++i){double time=(block*3840+i)/48000.;phase+=2*pi*440*std::exp2(.3*std::sin(2*pi*5*time)/12)/48000;v[i]=float(.3*std::sin(phase));}if(measure(v,48000).voiced)++voiced;}check(voiced>=18,"vibrato voiced confidence");
 CorrectionSettings settings;settings.enabled=true;settings.speedMs=0;MonophonicCorrection correction;PitchEstimate p;p.voiced=true;p.confidence=1;p.hz=440*std::exp2(.3/12);auto decision=correction.update(p,settings,.01);check(decision.targetValid&&decision.targetMidi==69&&std::abs(decision.correctionSemitones+.3)<1e-9,"chromatic correction");
 settings.amount=.5;correction.reset();decision=correction.update(p,settings,.01);check(std::abs(decision.correctionSemitones+.15)<1e-9,"amount cents");settings.amount=1;settings.speedMs=100;correction.reset();decision=correction.update(p,settings,.1);check(std::abs(decision.correctionSemitones+.3*(1-std::exp(-1.)))<1e-9,"speed time constant");
 p.voiced=false;decision=correction.update(p,settings,.01);check(!decision.targetValid&&decision.correctionSemitones==0,"unvoiced clears stale");
 settings.speedMs=0;settings.scale=Scale::major;settings.key=0;p.voiced=true;p.hz=440*std::exp2((61.1-69)/12);decision=correction.update(p,settings,.01);check(decision.targetMidi==62,"C major nearest note");settings.scale=Scale::naturalMinor;settings.key=1;correction.reset();decision=correction.update(p,settings,.01);check(decision.targetMidi==61,"key and minor note set");
 settings.scale=Scale::chromatic;correction.reset();p.hz=440*std::exp2(.49/12);check(correction.update(p,settings,.01).targetMidi==69,"boundary lower");p.hz=440*std::exp2(.51/12);check(correction.update(p,settings,.01).targetMidi==69,"boundary hysteresis");p.hz=440*std::exp2(.61/12);check(correction.update(p,settings,.01).targetMidi==70,"boundary transition");
 Settings backend;backend.channels=1;backend.correction.enabled=true;backend.correction.speedMs=20;backend.strategy=Strategy::tonal;auto source=tone(48000,448,96000);std::vector<float> output;int valid=0;auto status=render(backend,source.size(),[&](auto offset,float* const* dest,int count){std::copy_n(source.data()+offset,count,dest[0]);return true;},[&](const float* const* data,int count){output.insert(output.end(),data[0],data[0]+count);return true;},{},[&](auto,const CorrectionFrame& frame){if(frame.targetValid)++valid;});check(status.status==Result::Status::complete&&output.size()==source.size(),"corrected render duration");check(valid>100,"actual measured telemetry");std::vector<float> centre(output.begin()+48000,output.begin()+51840);auto corrected=measure(centre,48000);std::cout<<"corrected 448 -> "<<corrected.hz<<" Hz\n";check(corrected.voiced&&std::abs(corrected.hz-440)<3,"production corrected output");
  // More than one noise draw: confidence gating cannot be a fixture accident.
 for(int trial=0;trial<50;++trial){for(auto& v:silence)v=noise(random);check(!measure(silence,48000).voiced,"independent noise windows unvoiced");}
 // A saw-like harmonic source still reports the fundamental, not a crossing octave.
 auto harmonic=tone(48000,170,3840);for(size_t i=0;i<harmonic.size();++i)for(int h=2;h<8;++h)harmonic[i]+=float(.15/h*std::sin(2*pi*h*170*i/48000));auto fundamental=measure(harmonic,48000);check(fundamental.voiced&&std::abs(fundamental.hz-170)<2,"harmonic fundamental");
 // Corrected render observes silence as unavailable, then cancels without a completed result.
 std::fill(source.begin(),source.end(),0);int observed=0, reads=0;status=render(backend,source.size(),[&](auto offset,float* const* dest,int count){++reads;std::copy_n(source.data()+offset,count,dest[0]);return true;},[](auto,int){return true;},[&]{return observed>=5;},[&](auto,const CorrectionFrame& frame){++observed;check(!frame.targetValid&&!frame.input.voiced&&frame.correctionSemitones==0,"silence telemetry unavailable");});check(status.status==Result::Status::cancelled&&observed==5&&reads<20,"cancel correction render bounded");
 std::cout<<"PASS monophonic correction\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}}

