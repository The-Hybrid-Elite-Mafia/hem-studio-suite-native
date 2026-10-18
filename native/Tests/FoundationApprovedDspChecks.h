#pragma once
#include "../Source/DSP/FoundationModes.h"
#include "../Source/DSP/HumAnalysis.h"
#include <memory>
namespace approvedDsp {
inline void check(bool good,const char* message){if(!good)throw std::runtime_error(message);}
inline void gate(){
 for(double rate:{44100.,48000.,88200.,96000.,384000.})for(float ahead:{0.f,3.f,6.f}){
    const int delay=int(std::ceil(rate*.006)),look=int(std::round(rate*ahead*.001)),n=delay*5+1000;
    hem::SmartGateCore legacy,actual;auto p=hem::foundationDefaults(hem::FoundationKind::gate);p[3]=24;p[5]=0;p[9]=-6;
    legacy.prepare(rate,p);actual.prepare(rate,p);auto modes=std::make_unique<hem::FoundationModes<hem::FoundationKind::gate>>();modes->prepare(rate,ahead,false);
    check(modes->latency()==delay,"Gate fixed delay");
    struct Sample {std::array<double,2> dry{},normal{};double gain=1,mix=1,output=1;};std::vector<Sample> oracle(size_t(n),Sample{});
    for(int i=0;i<n;++i){if(i==delay+17){p[8]=37;p[9]=3;p[0]=-20;}if(i==delay*2+29)p[3]=12;
        const float x=i%1200<250?.4f:.0001f;float a=x,b=-x*.3f,c=a,d=b;
        legacy.processTransformed(&a,&b,1,p,[](const auto&,const auto&,float,bool){},[&](const auto& dry,const auto& normal,const auto& controls,double env){oracle[size_t(i)]={dry,normal,hem::foundation::dbGain(-env),controls[8]*.01,hem::foundation::dbGain(controls[9])};return normal;});
        actual.processTransformed(&c,&d,1,p,[](const auto&,const auto&,float,bool){},[&](const auto& dry,const auto& normal,const auto& controls,double env){return modes->process(dry,normal,controls,env);});
        if(i>=delay){const auto& old=oracle[size_t(i-delay)];const double expected=-20*std::log10(1-old.mix+old.mix*oracle[size_t(i-delay+look)].gain);check(std::abs(modes->reduction()-expected)<2e-6,"Gate meter is not aligned to rendered gain trajectory");}
        for(int ch=0;ch<2;++ch){double expected=0;if(i>=delay){const auto& old=oracle[size_t(i-delay)];expected=ahead==0?old.normal[size_t(ch)]:old.output*(old.dry[size_t(ch)]+old.mix*(old.dry[size_t(ch)]*oracle[size_t(i-delay+look)].gain-old.dry[size_t(ch)]));}
            check((ch?d:c)==float(expected),"Gate complete legacy/control trajectory mismatch");}
    }
 }
 // Guard/reset timing remains on the undelayed stream even for an overflow or
 // nonfinite input, rather than being postponed until a bad sample is emitted.
 {hem::SmartGateCore legacy,actual;auto p=hem::foundationDefaults(hem::FoundationKind::gate);p[3]=24;p[9]=6;legacy.prepare(48000,p);actual.prepare(48000,p);auto mode=std::make_unique<hem::FoundationModes<hem::FoundationKind::gate>>();mode->prepare(48000);std::vector<float> expected;
  for(int i=0;i<1600;++i){float x=i==0?std::numeric_limits<float>::max():i==17?std::numeric_limits<float>::quiet_NaN():.0001f,y=x,a=x,b=x;legacy.process(&x,&y,1,p);expected.push_back(x);actual.processTransformed(&a,&b,1,p,[](const auto&,const auto&,float,bool){},[&](const auto& dry,const auto& normal,const auto& controls,double env){return mode->process(dry,normal,controls,env);});check(a==(i<288?0:expected[size_t(i-288)]),"Gate exceptional reset delayed with audio");}}
 // Key excludes trim and delay remains exact; final impulse is drained once.
 auto mode=std::make_unique<hem::FoundationModes<hem::FoundationKind::gate>>();mode->prepare(48000,6,true);auto p=hem::foundationDefaults(hem::FoundationKind::gate);p[9]=-24;
 for(int i=0;i<600;++i){const double x=i==17?1:0;const auto y=mode->process({x,-x},{.01*x,-.01*x},p,24);check(y[0]==(i==305?1:0)&&y[1]==(i==305?-1:0),"Key delayed unity stereo");}
 // Repeated retargets remain continuous and independent of block partition.
 std::vector<double> reference;
 for(int block:{1,17,257}){auto m=std::make_unique<hem::FoundationModes<hem::FoundationKind::gate>>();m->prepare(48000);std::vector<double> result;
    for(int start=0;start<3000;){const int event=start<1000?1000:start<1600?1600:3000;const int end=std::min({start+block,event,3000});m->targets(start>=1600?0.f:start>=1000?6.f:0.f,start>=1600);
        for(int i=start;i<end;++i)result.push_back(m->process({.2,-.1},{.02,-.01},p,20+.001*i)[0]);start=end;}
    if(reference.empty())reference=result;else check(reference==result,"Gate variable-block mode smoothing");
 }
}
template<hem::FoundationKind Kind> void removed(){
 for(float mix:{0.f,37.f,100.f})for(float output:{-12.f,0.f,6.f}){
    hem::FoundationCore<Kind> a,b;auto p=hem::foundationDefaults(Kind);p[hem::FoundationCore<Kind>::count-2]=mix;p[hem::FoundationCore<Kind>::count-1]=output;p[3]=24;a.prepare(48000,p);b.prepare(48000,p);
    hem::FoundationModes<Kind> mode;mode.prepare(48000,0,true);
    for(int i=0;i<5000;++i){const float x=float(.2*std::sin(2*hem::pi*50*i/48000));float l=x,r=-x,u=l,v=r;a.process(&l,&r,1,p);b.processTransformed(&u,&v,1,p,[](const auto&,const auto&,float,bool){},[&](const auto& dry,const auto& normal,const auto& controls,double env){return mode.process(dry,normal,controls,env);});check(std::abs(double(l)+u-x)<2e-7&&std::abs(double(r)+v+x)<2e-7,"Normal + removed != sanitized aligned input");}
 }
 hem::FoundationModes<Kind> m;m.prepare(48000);auto p=hem::foundationDefaults(Kind);m.targets(0,true);
 for(int i=1;i<=480;++i){const auto y=m.process({.2,-.2},{.15,-.15},p,0);check(std::abs(y[0]-(.15+double(i)/480*(-.1)))<1e-12,"Audition exact ten millisecond ramp");}
 hem::FoundationCore<Kind> exceptional;auto settings=hem::foundationDefaults(Kind);settings[hem::FoundationCore<Kind>::count-2]=0;settings[hem::FoundationCore<Kind>::count-1]=6;exceptional.prepare(48000,settings);m.prepare(48000,0,true);float huge=std::numeric_limits<float>::max(),zero=0;
 exceptional.processTransformed(&huge,&zero,1,settings,[](const auto&,const auto&,float,bool){},[&](const auto& dry,const auto& normal,const auto& controls,double env){return m.process(dry,normal,controls,env);});check(huge==std::numeric_limits<float>::max()&&zero==0,"Delta did not subtract guarded normal output");
}
inline void hum(){
 for(double rate:{8000.,44100.,48000.,96000.,384000.})for(int fixture=0;fixture<8;++fixture){
    auto analysis=std::make_unique<hem::HumAnalysis>();analysis->prepare(rate);analysis->settings(50,fixture==7?16:8,fixture==3?0:100);
    const int stride=std::max(1,int(std::floor(rate/4000.))),samples=stride*hem::HumAnalysis::size;uint32_t random=123;
    for(int i=0;i<samples;++i){random=random*1664525u+1013904223u;const double noise=(double(random)/4294967296.-.5)*.02;
        const double t=i/rate,cycles=fixture==5?49.8*t+.2*t*t/(samples/rate):(fixture==6?73.:fixture==7?800.:50.)*t;
        const double x=fixture==0?0:fixture==1?noise:.003*std::sin(2*hem::pi*cycles);analysis->observe({x,fixture==4?-x:0});}
    check(analysis->consume(),"Hum capture missing");hem::HumAnalysisFrame frame;check(analysis->read(frame),"Hum snapshot missing");
    check(frame.detected==(fixture==2||fixture==4||fixture==5||fixture==7),"Hum silence/noise/sensitivity/stereo/drift/harmonic qualification");check(frame.endSample==uint64_t(samples)&&frame.fundamental==50,"Hum sample clock or hidden frequency steering");
    if(fixture==2||fixture==4)check(std::abs(frame.strongestDb-20*std::log10(.003/std::sqrt(2.)))<.05,"Measured Hum RMS calibration");
    analysis->settings(60,8,100);check(!analysis->read(frame),"Hum stale manual-frequency measurement retained");
    analysis->prepare(rate);check(!analysis->read(frame),"Hum stale reprepare measurement retained");
 }
 // Saturation is bounded; stale worker windows never overwrite new settings.
 auto a=std::make_unique<hem::HumAnalysis>();a->prepare(8000);a->settings(50,8,50);for(int i=0;i<hem::HumAnalysis::size*2*5;++i)a->observe({0,0});a->settings(60,8,50);while(a->consume()){}hem::HumAnalysisFrame frame;check(!a->read(frame),"Stale queue published");
 auto concurrent=std::make_unique<hem::HumAnalysis>();concurrent->prepare(8000);concurrent->settings(50,8,50);std::atomic<bool> done{false},workerDone{false},coherent{true};std::atomic<int> reads{0};
 std::thread producer([&]{for(int i=0;i<hem::HumAnalysis::size*16;++i)concurrent->observe({.01*std::sin(2*hem::pi*50*i/8000),0});done=true;});
 std::thread worker([&]{for(;;){const bool consumed=concurrent->consume();if(consumed)while(reads==0)std::this_thread::yield();if(done&&!consumed)break;std::this_thread::yield();}workerDone=true;});
 while(!workerDone){hem::HumAnalysisFrame f;if(concurrent->read(f)){++reads;if(f.fundamental!=50||f.harmonics!=8||f.thresholdDb!=-60||f.endSample%(hem::HumAnalysis::size*2)!=0||!f.detected)coherent=false;for(float value:f.spectrum)if(!std::isfinite(value))coherent=false;}std::this_thread::yield();}
 producer.join();worker.join();check(coherent&&reads>0,"Concurrent measured Hum snapshot incoherent");
 auto extreme=std::make_unique<hem::HumAnalysis>();extreme->prepare(8000);extreme->settings(50,8,50);for(int i=0;i<hem::HumAnalysis::size*2;++i)extreme->observe({i==0?double(std::numeric_limits<float>::max()):0,0});check(extreme->consume()&&extreme->read(frame),"Extreme measurement absent");for(float value:frame.spectrum)check(std::isfinite(value),"Extreme input overflowed measured spectrum");check(std::isfinite(frame.strongestDb),"Extreme input overflowed hum strength");
}
inline void run(){gate();removed<hem::FoundationKind::plosive>();removed<hem::FoundationKind::hum>();hum();std::cout<<"PASS approved gate/control-delay, key, delta, exact transition, measured Hum and stale-window contracts\n";}
}
