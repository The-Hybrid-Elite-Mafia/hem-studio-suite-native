#include "../Source/DSP/FoundationCore.h"
#include "../Source/DSP/FoundationTelemetry.h"
#include "../Source/UI/FoundationMappings.h"
#include <complex>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <cstring>
#include <vector>
#include <limits>
namespace {
void require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
void near(float actual,float expected,float tolerance,const char* message){require(std::abs(actual-expected)<=tolerance,message);}
void calibration(){
    hem::FoundationTelemetry telemetry;hem::FoundationDisplayFrame f;
    require(!telemetry.read(telemetry.latestSerial(),f),"No audio must not fabricate a frame");
    telemetry.prepare(48000);
    for(int i=0;i<23999;++i)telemetry.observe({.25,-.5},{.125f,-.25f},-3,true);
    require(telemetry.read(telemetry.latestSerial(),f),"Missing measured frame");
    near(f.values[0],.5f,0,"Input peak calibration");near(f.values[1],.25f,0,"Output peak calibration");
    require(f.values[12]==0,"DC estimate must wait for complete 500 ms window");
    telemetry.observe({.25,-.5},{.125f,-.25f},-3,true);
    require(telemetry.read(telemetry.latestSerial(),f),"Missing DC window boundary");
    require(f.samples==400&&f.endSample==24000&&f.values[12]==1,"Sample-timed publication or DC validity");
    near(f.values[2],.25f,0,"Signed input L DC");near(f.values[3],-.5f,0,"Signed input R DC");
    near(f.values[4],.125f,0,"Signed output L DC");near(f.values[5],-.25f,0,"Signed output R DC");
    near(f.values[6],.125f,0,"Waveform min L");near(f.values[9],-.25f,0,"Waveform max R");
    near(f.values[10],-3,0,"Signed instantaneous envelope");near(f.values[11],1,0,"Real activity flag");
    const auto oldSerial=f.serial;telemetry.prepare(96000);
    require(!telemetry.read(oldSerial,f),"Reprepare exposes stale generation");
    for(int i=0;i<800;++i)telemetry.observe({0,0},{0,0},0,false);
    require(telemetry.read(telemetry.latestSerial(),f)&&f.samples==800&&f.endSample==800,"Reprepared rate cadence");
    require(f.values[0]==0&&f.values[1]==0&&f.values[11]==0&&f.values[12]==0,"Silence fabricates signal or prior DC");
}
template<hem::FoundationKind K> void equivalence(){
    hem::FoundationCore<K> plain,observed;hem::FoundationTelemetry telemetry;
    auto p=hem::foundationDefaults(K);
    if constexpr(K==hem::FoundationKind::cleaner){p[0]=1;p[1]=1;p[3]=1;}
    if constexpr(K==hem::FoundationKind::gate)p[3]=18;
    if constexpr(K==hem::FoundationKind::rider){p[1]=6;p[2]=6;}
    if constexpr(K==hem::FoundationKind::plosive)p[3]=18;
    if constexpr(K==hem::FoundationKind::hum)p[3]=24;
    for(const double rate:{8000.,44100.,96000.,384000.}){
        plain.prepare(rate,p);observed.prepare(rate,p);telemetry.prepare(rate);
        uint32_t random=17;std::array<float,1024> a,b,c,d;
        for(int block=0;block<70;++block){const int n=block%3==0?17:block%3==1?257:1024;
            for(int i=0;i<n;++i){random=1664525*random+1013904223;a[i]=c[i]=float(int32_t(random))/2147483648.f*.25f;b[i]=d[i]=float(.3*std::sin((block*1024+i)*.07));}
            plain.process(a.data(),b.data(),n,p);
            observed.processObserved(c.data(),d.data(),n,p,[&](const auto& input,const auto& output,float gain,bool active) noexcept {telemetry.observe(input,output,gain,active);});
            require(std::memcmp(a.data(),c.data(),size_t(n)*sizeof(float))==0&&std::memcmp(b.data(),d.data(),size_t(n)*sizeof(float))==0,"Telemetry changes sample output");
            require(plain.measurement.load()==observed.measurement.load(),"Telemetry changes existing meter");
        }
    }
}
void independentWindowOracle(){
    hem::FoundationTelemetry telemetry;telemetry.prepare(8000);
    std::array<float,2> minimum{100,100},maximum{-100,-100};float inputPeak=0,outputPeak=0;
    for(int i=0;i<67;++i){const float left=float(i-33)/100.f,right=.2f*std::cos(i*.37f);
        const std::array<double,2> input{left*.5,right*2};const std::array<float,2> output{left,right};
        for(int c=0;c<2;++c){minimum[c]=std::min(minimum[c],output[c]);maximum[c]=std::max(maximum[c],output[c]);inputPeak=std::max(inputPeak,float(std::abs(input[c])));outputPeak=std::max(outputPeak,std::abs(output[c]));}
        telemetry.observe(input,output,float(i),i%2!=0);
    }
    hem::FoundationDisplayFrame f;require(telemetry.read(telemetry.latestSerial(),f),"Oracle frame missing");
    near(f.values[0],inputPeak,0,"Independent input peak oracle");near(f.values[1],outputPeak,0,"Independent output peak oracle");
    near(f.values[6],minimum[0],0,"Independent L min oracle");near(f.values[7],maximum[0],0,"Independent L max oracle");
    near(f.values[8],minimum[1],0,"Independent R min oracle");near(f.values[9],maximum[1],0,"Independent R max oracle");
    require(f.values[10]==66&&f.values[11]==0,"Must publish actual endpoint, not max gain or any-activity accumulation");
    telemetry.prepare(std::numeric_limits<double>::quiet_NaN());telemetry.observe({1,1},{1,1},1,true);
    require(!telemetry.read(telemetry.latestSerial(),f),"Invalid preparation must not publish old or fabricated frames");
}
void mappingOracles(){
    using namespace hem::ui::foundationMapping;
    for(double sensitivity:{0.,12.5,50.,66.66666666666667,100.}){
        require(std::abs(riderSensitivity(riderFloor(sensitivity))-sensitivity)<1e-10,"Rider sensitivity inverse");
        require(std::abs(plosiveSensitivity(plosiveThreshold(sensitivity))-sensitivity)<1e-10,"Plosive sensitivity inverse");
    }
    require(riderFloor(0)==-20&&riderFloor(100)==-80&&plosiveThreshold(0)==-6&&plosiveThreshold(100)==-60,"Sensitivity endpoints changed");
    require(riderSpeed(400,150)==RiderSpeed::custom&&riderSpeed(800,300)==RiderSpeed::slow&&riderSpeed(200,75)==RiderSpeed::fast,"Speed preset classification");
    for(double rate:{8000.,11025.,16000.,22050.,32000.,44100.,48000.,88200.,96000.,176400.,192000.,352800.,384000.})for(double frequency:{40.,50.,60.,70.})for(double q:{10.,10.1,17.3,30.,49.9,60.}){
        const double width=notchWidth(frequency,rate,q);
        require(std::abs(notchQ(frequency,rate,width)-q)<1e-10,"Digital width inverse");
        // Independent bisection of the actual complex response. Neither the
        // production width helper nor analytic edge formula selects the roots.
        hem::foundation::Section section;section.configure(2,rate,frequency,q);
        auto power=[&](double omega){const auto z=std::exp(std::complex<double>(0,-omega));return std::norm((section.b0+section.b1*z+section.b2*z*z)/(1.+section.a1*z+section.a2*z*z));};
        const double w0=2*pi*frequency/rate;double a=0,b=w0;
        for(int n=0;n<80;++n){const double m=(a+b)*.5;if(power(m)>.5)a=m;else b=m;}const double lower=(a+b)*.5;
        a=w0;b=pi;for(int n=0;n<80;++n){const double m=(a+b)*.5;if(power(m)<.5)a=m;else b=m;}const double upper=(a+b)*.5;
        require(std::abs(power(lower)-.5)<1e-6&&std::abs(power(upper)-.5)<1e-6,"Independent half-power roots");
        require(std::abs(width-rate*(upper-lower)/(2*pi))<1e-5,"Width differs from independently solved Hz edges");
    }
}
void concurrent(){
    hem::FoundationTelemetry telemetry;telemetry.prepare(8000);
    std::atomic<bool> done{false},coherent{true};std::atomic<unsigned> reads{0};
    std::thread writer([&]{for(int i=0;i<500000;++i)telemetry.observe({.25,-.5},{.125f,-.25f},7,true);done.store(true);});
    std::thread reader([&]{while(!done.load()){hem::FoundationDisplayFrame f;if(telemetry.read(telemetry.latestSerial(),f)){
        ++reads;if(f.values[0]!=.5f||f.values[1]!=.25f||f.values[6]!=.125f||f.values[9]!=-.25f||f.values[10]!=7||f.values[11]!=1)coherent=false;
    }}});
    writer.join();reader.join();require(coherent.load()&&reads.load()>0,"Torn concurrent snapshot");
    hem::FoundationDisplayFrame f;const auto end=telemetry.latestSerial();require(!telemetry.read(end-uint32_t(hem::FoundationTelemetry::capacity),f),"Overwritten serial accepted");
}
}
#include "FoundationApprovedDspChecks.h"
int main(){try{
    approvedDsp::run();
    calibration();independentWindowOracle();mappingOracles();equivalence<hem::FoundationKind::cleaner>();equivalence<hem::FoundationKind::gate>();equivalence<hem::FoundationKind::rider>();equivalence<hem::FoundationKind::plosive>();equivalence<hem::FoundationKind::hum>();concurrent();
    std::cout<<"PASS telemetry: independent calibration, 500ms signed stereo DC, sample cadence, generation reset, concurrent coherence, ring overwrite, bitwise DSP observation parity at 4 rates / 5 modules\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
