#include "../Source/DSP/ModulationCore.h"
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmismatched-new-delete"
#endif
#include "FoundationAllocationProbe.h"
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif
#include <iostream>
#include <limits>
#include <string>
#include <bit>
using namespace hem;
namespace {
int checks=0;
void check(bool b,const char*s){++checks;if(!b)throw std::runtime_error(s);}
struct Counting{Counting(){allocationProbe::testThread=true;allocationProbe::enabled=true;}~Counting(){allocationProbe::enabled=false;allocationProbe::testThread=false;}};
struct Signal{std::vector<float>l,r;explicit Signal(int n):l(size_t(n)),r(size_t(n)){} };
template<class Core,class Settings>void process(Core&c,Signal&b,const Settings&p,int block=127,modulation::Transport t={}){for(int i=0;i<int(b.l.size());i+=block){const int n=std::min(block,int(b.l.size())-i);Counting guard;c.process(b.l.data()+i,b.r.data()+i,n,p,t);}}
Signal tone(int n,double rate=48000){Signal s(n);for(int i=0;i<n;++i){s.l[size_t(i)]=float(.2*std::sin(2*modulation::pi*733*i/rate));s.r[size_t(i)]=float(.17*std::sin(2*modulation::pi*317*i/rate+.3));}return s;}
double diff(const Signal&a,const Signal&b){double d=0;for(size_t i=0;i<a.l.size();++i){d+=std::pow(double(a.l[i])-b.l[i],2)+std::pow(double(a.r[i])-b.r[i],2);}return d;}
void bounded(const Signal&s){for(size_t i=0;i<s.l.size();++i)check(std::isfinite(s.l[i])&&std::isfinite(s.r[i])&&std::abs(s.l[i])<=32&&std::abs(s.r[i])<=32,"non-finite or unbounded output");}
template<class Core,class Settings>void neutral(){for(double rate:{8000.,11025.,22050.,44100.,48000.,96000.,192000.}){Core c;c.prepare(rate);auto source=tone(4096,rate),s=source;Settings p;process(c,s,p);check(diff(s,source)==0,"default must be exact dry");check(c.latency()==0,"latency changed");p.mix=100;p.bypass=true;c.reset();s=source;process(c,s,p);check(diff(s,source)==0,"initial bypass must be exact dry");std::vector<float>mono(513,.2f);{Counting guard;c.process(mono.data(),nullptr,int(mono.size()),p);}for(float x:mono)check(x==.2f,"mono bypass not exact");}}
template<class Core,class Settings>void partition(Settings p){Core a,b;a.prepare(48000);b.prepare(48000);auto x=tone(50000),y=x;process(a,x,p,1);process(b,y,p,997);check(diff(x,y)==0,"block partition changed samples");a.reset();auto z=tone(50000);process(a,z,p,31);check(diff(x,z)==0,"reset not reproducible");}
void chorusTests(){ChorusFlangerPhaserSettings p;p.mix=100;p.depth=87;p.feedback=40;
    for(int mode=0;mode<3;++mode)for(int shape=0;shape<3;++shape){p.mode=mode;p.shape=shape;partition<ChorusFlangerPhaserCore>(p);}
    ChorusFlangerPhaserCore c;c.prepare(48000);p.mode=0;p.shape=0;p.rate=2;p.depth=75;auto s=tone(48000);process(c,s,p);auto d=c.display();check(d.valid&&d.count==200&&d.frames==48000,"actual history clock invalid");check(std::abs(d.rateHz-2)<1.e-6f,"free LFO frequency wrong");float maximum=0;double quadrature=0;int rising=0;for(size_t i=0;i<d.count;++i){maximum=std::max(maximum,std::abs(d.left[i]));quadrature+=d.left[i]*d.right[i];if(i&&d.left[i-1]<0&&d.left[i]>=0)++rising;}check(maximum>.749&&maximum<=.7501,"depth not measured correctly");check(std::abs(quadrature)<1.e-4,"stereo LFO not quadrature");check(rising==1||rising==2,"measured LFO cycle rate wrong");
    c.reset();s=tone(48000);p.sync=true;p.rate=1;process(c,s,p,127,{180,true});d=c.display();check(d.syncActive&&d.bpm==180&&std::abs(d.rateHz-3)<1.e-6,"real BPM sync failed");c.reset();s=tone(48000);process(c,s,p,127,{});d=c.display();check(!d.tempoAvailable&&!d.syncActive&&d.bpm==0&&d.rateHz==1,"missing tempo fabricated BPM or changed free rate");
    p.sync=false;p.depth=0;p.delay=10;p.feedback=0;p.mode=1;c.reset();Signal impulse(2000);impulse.l[0]=1;process(c,impulse,p);check(std::abs(impulse.l[480]-1)<1.e-6f,"flanger actual base delay wrong");for(int i=0;i<2000;++i)if(i!=480)check(impulse.l[size_t(i)]==0,"flanger impulse extra samples");
    // The phaser is genuinely an allpass cascade: all-wet static magnitude is unity.
    for(int stages=0;stages<4;++stages){c.reset();p.mode=2;p.center=733;p.stages=stages;auto input=tone(48000),out=input;process(c,out,p);double eIn=0,eOut=0;for(size_t i=10000;i<input.l.size();++i){eIn+=input.l[i]*input.l[i];eOut+=out.l[i]*out.l[i];}check(std::abs(eOut/eIn-1)<.002,"phaser allpass magnitude response wrong");}
    std::array<Signal,3>outputs{tone(12000),tone(12000),tone(12000)};for(int mode=0;mode<3;++mode){c.reset();p.mode=mode;p.depth=65;process(c,outputs[size_t(mode)],p);}check(diff(outputs[0],outputs[1])>.1&&diff(outputs[1],outputs[2])>.1,"effect modes not distinct");
    for(double rate:{8000.,44100.,96000.,192000.})for(int mode=0;mode<3;++mode)for(float feedback:{-85.f,85.f}){c.prepare(rate);p.mode=mode;p.feedback=feedback;p.depth=100;p.rate=10;p.center=8000;p.delay=.1f;p.stages=3;p.output=6;Signal impulse{int(rate)};impulse.l[0]=1;impulse.r[0]=-1;process(c,impulse,p,337);bounded(impulse);}
    std::cout<<"PASS chorus/flanger/phaser: rates/depth/quadrature, actual tempo/no-tempo, base delay, allpass response, modes, extreme feedback stability\n";
}
void rotaryTests(){RotaryTremoloAutopanSettings p;p.depth=75;p.mix=100;p.speed=3;
    for(int mode=0;mode<3;++mode)for(int shape=0;shape<3;++shape){p.mode=mode;p.shape=shape;partition<RotaryTremoloAutopanCore>(p);}
    RotaryTremoloAutopanCore c;c.prepare(48000);p.mode=1;p.shape=0;p.speed=2;p.depth=100;p.width=0;Signal dc(48000);std::fill(dc.l.begin(),dc.l.end(),1);std::fill(dc.r.begin(),dc.r.end(),1);process(c,dc,p);check(std::abs(dc.l[6000]-1)<1.e-6&&std::abs(dc.l[18000])<1.e-6,"tremolo rate/depth wrong");check(dc.l==dc.r,"zero-width tremolo channels differ");p.width=100;c.reset();Signal anti(24000);std::fill(anti.l.begin(),anti.l.end(),1);std::fill(anti.r.begin(),anti.r.end(),1);process(c,anti,p);for(size_t i=0;i<anti.l.size();++i)check(std::abs(anti.l[i]+anti.r[i]-1)<1.e-6,"tremolo stereo phase incorrect");
    p.mode=2;c.reset();Signal pan(24000);std::fill(pan.l.begin(),pan.l.end(),1);std::fill(pan.r.begin(),pan.r.end(),1);process(c,pan,p);for(size_t i=0;i<pan.l.size();++i)check(std::abs(pan.l[i]*pan.l[i]+pan.r[i]*pan.r[i]-2)<1.e-5,"autopan is not equal power");check(pan.l[6000]<1.e-6&&pan.r[6000]>1.4141,"autopan actual excursion wrong");
    for(int mode=0;mode<3;++mode){p.mode=mode;p.depth=0;c.reset();auto input=tone(48000),out=input;process(c,out,p);check(diff(input,out)==0,"zero-depth motion must be neutral");}
    p.mode=0;p.depth=100;p.speed=4;p.rotary=0;p.ramp=.25f;c.reset();auto input=tone(24000);process(c,input,p);check(std::abs(c.display().rateHz-.6f)<1.e-6,"rotary slow ratio incorrect");p.rotary=1;input=tone(12000);process(c,input,p);const float fast=c.display().rateHz;check(fast>2.7&&fast<2.8,"rotary acceleration time constant wrong");p.rotary=2;input=tone(12000);process(c,input,p);check(c.display().rateHz>1&&c.display().rateHz<1.03,"rotary brake not decelerating");input=tone(96000);process(c,input,p);check(c.display().rateHz<.001,"rotary brake not approaching zero");
    p.mode=1;p.sync=true;p.speed=.5f;c.reset();input=tone(24000);process(c,input,p,127,{150,true});check(c.display().syncActive&&std::abs(c.display().rateHz-1.25)<1.e-6,"motion tempo sync wrong");
    for(double rate:{8000.,44100.,96000.,192000.})for(int mode=0;mode<3;++mode){c.prepare(rate);p.mode=mode;p.depth=100;p.speed=10;p.output=6;p.ramp=.05f;p.rotary=1;auto s=tone(int(rate/4),rate);process(c,s,p,337,{400,true});bounded(s);}
    std::cout<<"PASS rotary/tremolo/autopan: AM rate/depth/stereo phase, equal-power pan, depth-zero neutral, slow/fast/brake inertia, sync/extremes\n";
}
template<class Core,class Settings>void safety(Settings p){Core c;c.prepare(48000);p.mix=100;p.depth=100;auto s=tone(8192);s.l[3]=std::numeric_limits<float>::infinity();s.r[9]=std::numeric_limits<float>::quiet_NaN();process(c,s,p);bounded(s);p.bypass=true;auto dry=tone(8192),out=dry;process(c,out,p);for(size_t i=1000;i<dry.l.size();++i)check(out.l[i]==dry.l[i]&&out.r[i]==dry.r[i],"bypass did not settle to exact dry");p.mix=std::numeric_limits<float>::quiet_NaN();p.depth=std::numeric_limits<float>::infinity();p.output=-std::numeric_limits<float>::infinity();out=dry;process(c,out,p);bounded(out);}
void automationAndIsolation(){
    ChorusFlangerPhaserCore a;RotaryTremoloAutopanCore b;a.prepare(48000);b.prepare(48000);ChorusFlangerPhaserSettings p;RotaryTremoloAutopanSettings q;p.mix=q.mix=100;
    unsigned seed=17;auto random=[&](){seed=1664525u*seed+1013904223u;return float(seed&65535)/65535;};
    std::array<float,127>l{},r{};
    for(int block=0;block<1000;++block){p.mode=block%3;p.shape=(block/3)%3;p.stages=block%4;p.depth=random()*100;p.rate=.02f+random()*9.98f;p.delay=.1f+random()*39.9f;p.center=20+random()*7980;p.feedback=random()*170-85;p.output=random()*30-24;p.bypass=block%7==0;
        q.mode=block%3;q.shape=(block/5)%3;q.rotary=block%3;q.depth=random()*100;q.speed=.02f+random()*9.98f;q.ramp=.05f+random()*9.95f;q.width=random()*100;q.output=random()*30-24;q.bypass=block%11==0;
        for(int effect=0;effect<2;++effect){for(size_t i=0;i<l.size();++i){l[i]=.25f*std::sin(float((block*127+int(i))*.1));r[i]=0;}{Counting guard;if(effect==0)a.process(l.data(),r.data(),127,p);else b.process(l.data(),r.data(),127,q);}for(size_t i=0;i<l.size();++i){check(std::isfinite(l[i])&&std::abs(l[i])<=32,"rapid automation nonfinite or unbounded");check(r[i]==0,"modulation crossed independent stereo channels");}}
    }
    a.reset();p={};p.output=6;for(int warm=0;warm<10;++warm){l.fill(.2f);r.fill(.2f);a.process(l.data(),r.data(),127,p);}p.bypass=true;float previous=l.back();for(int block=0;block<10;++block){l.fill(.2f);r.fill(.2f);a.process(l.data(),r.data(),127,p);for(float x:l){check(std::abs(x-previous)<.001,"bypass output change was not sample-ramped");previous=x;}}check(l.back()==.2f,"ramped bypass endpoint not exact");
    for(double rate:{0.,7999.,192001.,std::numeric_limits<double>::quiet_NaN()}){bool rejectedA=false,rejectedB=false;try{a.prepare(rate);}catch(const std::invalid_argument&){rejectedA=true;}try{b.prepare(rate);}catch(const std::invalid_argument&){rejectedB=true;}check(rejectedA&&rejectedB,"invalid preparation rate accepted");}
    std::cout<<"PASS 2000 rapid-automation callbacks, channel isolation, sample-ramped bypass, invalid-rate rejection\n";
}

}
int main(){try{neutral<ChorusFlangerPhaserCore,ChorusFlangerPhaserSettings>();neutral<RotaryTremoloAutopanCore,RotaryTremoloAutopanSettings>();chorusTests();rotaryTests();automationAndIsolation();safety<ChorusFlangerPhaserCore>(ChorusFlangerPhaserSettings{});safety<RotaryTremoloAutopanCore>(RotaryTremoloAutopanSettings{});check(allocationProbe::count==0,"audio callback allocated");std::cout<<"PASS "<<checks<<" assertions; mono/stereo and 8-192 kHz; reset/partition identical; zero callback allocations\n";return 0;}catch(const std::exception&e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
