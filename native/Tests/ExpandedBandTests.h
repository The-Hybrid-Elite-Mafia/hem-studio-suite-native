#pragma once
#include "../Source/DSP/ExpandedBandCore.h"
namespace expandedReference {
inline bandReference::Filter shelf(double fs,double hz,double q,double db,int type){
    using moduleReference::Wide;const Wide t(std::tan(bandReference::pi*std::clamp(hz,1.,fs*.45)/fs));
    const Wide t2=t*t,a(std::pow(10.,db/40.)),initialRoot(std::sqrt(a.value()));
    // Use sqrt(A) from the same A, retaining its residual. A separate pow(db/80)
    // introduces a second independently rounded amplitude into one filter.
    const Wide root=initialRoot+(a-initialRoot*initialRoot)/(Wide(2)*initialRoot),damping=root*t/Wide(q);
    bandReference::Filter f;
    if(type==1){const Wide denominator=a+t2+damping;
        f.b={(a*(Wide(1)+a*t2+damping)/denominator).value(),(Wide(2)*a*(a*t2-Wide(1))/denominator).value(),(a*(Wide(1)+a*t2-damping)/denominator).value()};
        f.a={1,(Wide(2)*(t2-a)/denominator).value(),((a+t2-damping)/denominator).value()};}
    else{const Wide denominator=Wide(1)+a*t2+damping;
        f.b={(a*(a+t2+damping)/denominator).value(),(Wide(2)*a*(t2-a)/denominator).value(),(a*(a+t2-damping)/denominator).value()};
        f.a={1,(Wide(2)*(a*t2-Wide(1))/denominator).value(),((Wide(1)+a*t2-damping)/denominator).value()};}return f;
}
}
template<Kind K> static void expandedBasics(double rate){
    auto settings=hem::bands::expandedDefaults<K>();hem::bands::ExpandedCore<K> core;require(core.prepare(rate,settings),"expanded prepare");
    for(int n=0;n<4096;++n){float l=float(.12*std::sin(n*.031)),r=float(.09*std::cos(n*.017));const auto a=l,b=r;
        allocationProbe::enabled=true;core.process(&l,&r,1,settings);allocationProbe::enabled=false;require(l==a&&r==b,"expanded factory dry");}
    auto old=active<K>();for(size_t i=0;i<old.size();++i)settings[i]=old[i];
    if constexpr(K==Kind::Dynamic)for(size_t b=0;b<8;++b)settings[67+b*3]=1;
    if constexpr(K==Kind::MidSide){settings[34]=settings[38]=0;}
    if constexpr(K==Kind::Dynamics||K==Kind::Imager)settings.back()=0;
    hem::bands::Core<K> legacy;legacy.prepare(rate,old);core.prepare(rate,settings);double error=0;
    for(int block:{1,17,64,128,257}){std::vector<float> l(size_t(block),0),r(size_t(block),0),a(size_t(block),0),b(size_t(block),0);
        for(int turn=0;turn<23;++turn){for(int i=0;i<block;++i){l[size_t(i)]=a[size_t(i)]=float(.2*std::sin((turn*block+i)*.029));r[size_t(i)]=b[size_t(i)]=float(.17*std::cos((turn*block+i)*.071));}
            allocationProbe::enabled=true;legacy.process(l.data(),r.data(),block,old);core.process(a.data(),b.data(),block,settings);allocationProbe::enabled=false;
            for(int i=0;i<block;++i)error=std::max({error,std::abs(double(l[size_t(i)])-a[size_t(i)]),std::abs(double(r[size_t(i)])-b[size_t(i)])});}}
    require(error<=2e-6,"expanded legacy sound compatibility");
    hem::bands::ExpandedCore<K> plain,observed;settings=hem::bands::expandedDefaults<K>();settings[0]=1;
    plain.prepare(rate,settings);observed.prepare(rate,settings);observed.visuals.subscribed=true;
    for(int n=0;n<int(rate*.2);++n){float l=float(.1*std::sin(n*.037)),r=float(.08*std::cos(n*.091)),a=l,b=r;
        allocationProbe::enabled=true;plain.process(&l,&r,1,settings);observed.process(&a,&b,1,settings);allocationProbe::enabled=false;
        require(l==a&&r==b,"visual subscription changed sound");}
    hem::bands::VisualFrame frame;require(observed.visuals.read(frame)&&frame.processed&&frame.samples>0&&frame.rate==rate,"missing actual visual telemetry");
    require(frame.inputPeak>0&&frame.outputPeak>0,"visual peak telemetry");
    std::cout<<"expanded basics kind="<<int(K)<<" rate="<<rate<<" legacy_error="<<error<<'\n';
}
static void expandedEqLaws(double rate){
    double maxError=0;for(int type:{0,1,2})for(double hz:{20.,1000.,18000.})for(double q:{.25,1.,8.})for(double db:{-30.,-12.,0.,12.,30.}){
        const auto actual=hem::bands::designEqualizer(rate,hz,q,db,type);const auto expected=type==0?moduleReference::bell(rate,hz,q,db):expandedReference::shelf(rate,hz,q,db,type);
        for(int bin=0;bin<256;++bin){const double f=20*std::pow((rate*.45)/20,bin/255.);const auto a=actual.response(2*hem::pi*f/rate),b=expected.response(f,rate);
            maxError=std::max(maxError,std::abs(a-b));require(std::abs(a-b)<=2e-5+2e-6*std::abs(b),"expanded EQ type transfer response");}}
    using namespace hem::bands;
    for(int type:{0,1,2})for(int up:{0,1}){auto p=expandedDefaults<Kind::Dynamic>();p[0]=1;p[3]=1000;p[6]=-30;p[7]=6;p[66]=float(type);p[67]=1;p[68]=float(up);
        ExpandedCore<Kind::Dynamic> core;core.prepare(rate,p);core.visuals.subscribed=true;double inPower=0,outPower=0;
        for(int n=0;n<int(rate);++n){const float x=float(.25*std::sin(2*hem::pi*1000*n/rate));float l=x,r=x;
            allocationProbe::enabled=true;core.process(&l,&r,1,p);allocationProbe::enabled=false;
            if(n>int(rate*.75)){inPower+=double(x)*x;outPower+=double(l)*l;}}
        VisualFrame frame;require(core.visuals.read(frame),"signed dynamics frame");const double expected=up?6:-6;
        require(std::abs(frame.signedDynamics[0]-expected)<.02,"signed UP/DOWN detector law");
        const double gain=10*std::log10(outPower/inPower);require(std::abs(gain-(type==0?expected:expected/2))<.08,"UP/DOWN type center audio gain");}
    std::cout<<"expanded EQ independent response error="<<maxError<<" rate="<<rate<<'\n';
}
static void expandedInitialProof(){for(double rate:{44100.,48000.,96000.}){
    expandedBasics<Kind::Dynamic>(rate);expandedBasics<Kind::MidSide>(rate);expandedBasics<Kind::Dynamics>(rate);expandedBasics<Kind::Imager>(rate);expandedEqLaws(rate);}}
