#include "../Source/DSP/BandModulesCore.h"
#include "BandModulesReference.h"
#include "BandTestNoDenormals.h"
#include "FoundationAllocationProbe.h"
#include "FoundationPlatformAllocations.h"
#include <iostream>
#include <stdexcept>
#include <vector>
#include <thread>
#include <chrono>
#include <fstream>
using hem::bands::Kind;
static void require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
static float randomSample(unsigned& s){s=s*1664525u+1013904223u;return float((double(s)/4294967296.*2-1)*.2);}
static double worst=0;
template<Kind K> static auto active(){auto p=hem::bands::defaults<K>();p[0]=1;
    if constexpr(K==Kind::Dynamic)for(size_t b=0;b<8;++b){const size_t i=2+b*8;p[i+2]=b%2?3:-3;p[i+4]=-30;p[i+5]=6;}
    if constexpr(K==Kind::MidSide)for(size_t b=0;b<8;++b)p[4+b*4]=b%2?4:-4;
    if constexpr(K==Kind::Dynamics)for(size_t b=0;b<3;++b){p[4+b*8]=-30;p[5+b*8]=4;p[8+b*8]=6;p[10+b*8]=3;}
    if constexpr(K==Kind::Imager){p[5]=20;p[8]=130;p[11]=180;p[6]=-2;p[9]=3;}
    return p;
}
template<Kind K> static void stream(double rate){
    constexpr int size=int(hem::bands::Core<K>::count)*1006;std::vector<float> l(size),r(size);unsigned seed=12;for(int i=0;i<size;++i){l[size_t(i)]=randomSample(seed);r[size_t(i)]=randomSample(seed);}
    hem::bands::Core<K> core;auto p=hem::bands::defaults<K>();core.prepare(rate,p);auto a=l,b=r;
    core.process(a.data(),b.data(),size,p);require(a==l&&b==r,"global off is not exact dry");
    p=active<K>();core.prepare(rate,p);moduleReference::Engine reference(static_cast<int>(K),rate,p);a=l;b=r;
    allocationProbe::enabled=true;core.process(a.data(),b.data(),size,p);allocationProbe::enabled=false;
    for(int i=0;i<size;++i){const auto e=reference.tick(l[size_t(i)],r[size_t(i)]);for(size_t c=0;c<2;++c){const double actual=c==0?a[size_t(i)]:b[size_t(i)];const double error=std::abs(actual-e[c]);worst=std::max(worst,error);require(error<=2e-5,"independent stream reference");}}
    for(int block:{1,17,64,128,257,1024,0}){core.prepare(rate,p);auto x=l,y=r;int offset=0,turn=0;const int irregular[]{3,311,1,1024,19,71};
        while(offset<size){const int n=std::min(size-offset,block?block:irregular[(turn++)%6]);allocationProbe::enabled=true;core.process(x.data()+offset,y.data()+offset,n,p);allocationProbe::enabled=false;offset+=n;}
        for(int i=0;i<size;++i)require(std::abs(x[size_t(i)]-a[size_t(i)])<=2e-6&&std::abs(y[size_t(i)]-b[size_t(i)])<=2e-6,"partition dependence");}
    core.prepare(rate,p);moduleReference::Engine automatic(static_cast<int>(K),rate,p);std::vector<float> automationLeft(size),automationRight(size);
    for(int i=0;i<size;++i){if(i%503==0){const size_t index=size_t(i/503)%p.size();const auto& s=hem::bands::specs<K>()[index];p[index]=p[index]==s.maximum?s.minimum:s.maximum;automatic.set(p);}
        float x=l[size_t(i)],y=r[size_t(i)];allocationProbe::enabled=true;core.process(&x,&y,1,p);allocationProbe::enabled=false;
        const auto e=automatic.tick(l[size_t(i)],r[size_t(i)]);require(std::abs(x-e[0])<=2e-5+2e-6*std::abs(e[0])&&std::abs(y-e[1])<=2e-5+2e-6*std::abs(e[1]),"automation reference");}
    // Repeat the same absolute events under every callback partition. Events
    // split callbacks; this does not add sample-accurate host automation.
    for(int block:{1,17,64,128,257,1024,0}){auto scripted=active<K>();core.prepare(rate,scripted);auto x=l,y=r;int offset=0,turn=0;const int irregular[]{3,311,1,1024,19,71};
        while(offset<size){if(offset%503==0){const size_t index=size_t(offset/503)%scripted.size();const auto& s=hem::bands::specs<K>()[index];scripted[index]=scripted[index]==s.maximum?s.minimum:s.maximum;}
            const int n=std::min({size-offset,503-offset%503,block?block:irregular[(turn++)%6]});allocationProbe::enabled=true;core.process(x.data()+offset,y.data()+offset,n,scripted);allocationProbe::enabled=false;offset+=n;}
        if(block==1){automationLeft=x;automationRight=y;}else for(int i=0;i<size;++i)require(std::abs(x[size_t(i)]-automationLeft[size_t(i)])<=2e-6&&std::abs(y[size_t(i)]-automationRight[size_t(i)])<=2e-6,"scripted callback partition");}
}
static void laws(){
    using namespace hem::bands;require(dynamicRequest(-12,-24,18)==12&&dynamicRequest(-12,-24,6)==6&&dynamicRequest(-30,-24,18)==0,"dynamic law");
    require(compressionRequest(-12,-24,4,0,24)==9&&compressionRequest(-12,-24,4,0,6)==6&&compressionRequest(-12,-24,1,0,24)==0,"compression law");
    require(std::abs(compressionRequest(-24,-24,4,12,24)-1.125)<1e-12,"soft knee center");
    for(double boundary:{-6.,6.}){const double h=1e-5;
        const double derivative=(compressionRequest(boundary+h,0,4,12,24)-compressionRequest(boundary-h,0,4,12,24))/(2*h);
        require(std::abs(derivative-(boundary<0?0:.75))<1e-6,"knee first derivative");}
    for(double rate:{44100.,48000.,96000.})for(double attack:{.1,10.,100.})for(double release:{10.,100.,1000.}){
        Detector d;double oracle=0;for(int i=0;i<int(rate*.4);++i){const double target=i<int(rate*.2)?9:0;
            oracle=target+(oracle-target)*std::exp(-1/(rate*(target>oracle?attack:release)*.001));
            require(std::abs(d.follow(target,attack,release,rate)-oracle)<=.03,"envelope recurrence");}}
}
template<Kind K> static void controls(double rate){
    // Every parameter must affect audio or actual detector telemetry in an
    // active fixture. Conditional inactive modes are covered separately.
    for(size_t index=0;index<hem::bands::Core<K>::count;++index){auto p=active<K>(),q=p;const auto& s=hem::bands::specs<K>()[index];
        if constexpr(K==Kind::Dynamic){for(size_t b=0;b<8;++b){p[3+b*8]=1000;p[4+b*8]=6;p[6+b*8]=-30;p[7+b*8]=12;}}
        if constexpr(K==Kind::MidSide)for(size_t b=0;b<8;++b){p[3+b*4]=1000;p[4+b*4]=6;}
        q=p;p[index]=s.minimum;q[index]=s.maximum;
        hem::bands::Core<K> a,b;a.prepare(rate,p);b.prepare(rate,q);double difference=0,meterDifference=0;
        for(int block=0;block<100;++block){std::array<float,257> al{},ar{},bl{},br{};
            for(size_t n=0;n<257;++n){const double t=(block*257+int(n))/rate;const double envelope=block%20<10?.25:.001;
                al[n]=bl[n]=float(envelope*(std::sin(2*hem::pi*1000*t)+.4*std::sin(2*hem::pi*83*t)+.3*std::sin(2*hem::pi*6500*t)));
                ar[n]=br[n]=float(envelope*.8*std::cos(2*hem::pi*1000*t));}
            a.process(al.data(),ar.data(),257,p);b.process(bl.data(),br.data(),257,q);
            for(size_t n=0;n<257;++n)difference=std::max({difference,std::abs(double(al[n])-bl[n]),std::abs(double(ar[n])-br[n])});
            const auto am=a.telemetry.read(),bm=b.telemetry.read();for(size_t c=0;c<8;++c)meterDifference=std::max(meterDifference,std::abs(double(am.cuts[c])-bm.cuts[c]));
        }
        if(difference<=1e-7&&meterDifference<=1e-5){std::cerr<<"inactive proof kind="<<int(K)<<" id="<<s.id<<'\n';throw std::runtime_error("control has no measured effect");}
    }
}
template<Kind K> static void safety(double rate){
    auto p=active<K>();hem::bands::Core<K> core;core.prepare(rate,p);
    for(size_t i=0;i<p.size();++i){auto invalid=p;invalid[i]=std::numeric_limits<float>::quiet_NaN();float x=.1f,y=-.2f;
        allocationProbe::enabled=true;core.process(&x,&y,1,invalid);allocationProbe::enabled=false;require(std::isfinite(x)&&std::isfinite(y),"invalid target");}
    for(float scale:{8.f,1.e6f,std::numeric_limits<float>::max()}){for(int i=0;i<8192;++i){float x=i%2?scale:-scale,y=.1f;
        allocationProbe::enabled=true;core.process(&x,&y,1,p);allocationProbe::enabled=false;require(std::isfinite(x)&&std::isfinite(y),"overflow escaped");}}
    for(float v:{std::numeric_limits<float>::infinity(),-std::numeric_limits<float>::infinity(),std::numeric_limits<float>::quiet_NaN()}){float x=v,y=.1f;core.process(&x,&y,1,p);require(std::isfinite(x)&&std::isfinite(y),"nonfinite input escaped");}
    for(double invalid:{0.,7999.,384001.,std::numeric_limits<double>::infinity()})require(!core.prepare(invalid,p),"invalid rate accepted");
}
template<Kind K> static void tail(double rate){
 for(int variant=0;variant<(K==Kind::Dynamic?2:(K==Kind::Dynamics||K==Kind::Imager?5:1));++variant){auto p=hem::bands::defaults<K>();p[0]=1;
    if constexpr(K==Kind::Dynamic)for(size_t b=0;b<8;++b){p[3+b*8]=20;p[4+b*8]=12;p[5+b*8]=8;p[7+b*8]=variant?18.f:0.f;}
    if constexpr(K==Kind::MidSide)for(size_t b=0;b<8;++b){p[3+b*4]=20;p[4+b*4]=12;p[5+b*4]=8;}
    if constexpr(K==Kind::Dynamics||K==Kind::Imager){const float low[]{80,80,800,800,200},high[]{1200,8000,1200,8000,2500};p[1]=low[variant];p[2]=high[variant];}
    hem::bands::Core<K> core;core.prepare(rate,p);const int excite=int(5*rate);const double seconds=K==Kind::Dynamic||K==Kind::MidSide?12:.5;
    moduleReference::Engine reference(static_cast<int>(K),rate,p);double referenceError=0;
    double lastPeak=0;for(int i=0;i<excite+int(rate*seconds);++i){float x=i<excite?float(.25*std::sin(2*hem::pi*20*i/rate)):0,y=-x*.7f;
        const auto expected=reference.tick(x,y);core.process(&x,&y,1,p);require(std::isfinite(x)&&std::isfinite(y),"tail nonfinite");
        referenceError=std::max({referenceError,std::abs(double(x)-expected[0]),std::abs(double(y)-expected[1])});
        if(!(std::abs(x-expected[0])<=2e-5+2e-6*std::abs(expected[0])&&std::abs(y-expected[1])<=2e-5+2e-6*std::abs(expected[1])))std::cerr<<"tail mismatch kind="<<int(K)<<" rate="<<rate<<" variant="<<variant<<" sample="<<i<<" actual="<<x<<','<<y<<" reference="<<expected[0]<<','<<expected[1]<<'\n';
        require(std::abs(x-expected[0])<=2e-5+2e-6*std::abs(expected[0])&&std::abs(y-expected[1])<=2e-5+2e-6*std::abs(expected[1]),"high gain/tail reference");
        if(i>=excite+int(rate*(seconds-(seconds==12?1:.1))))lastPeak=std::max({lastPeak,std::abs(double(x)),std::abs(double(y))});}
    std::cout<<"tail kind="<<int(K)<<" rate="<<rate<<" variant="<<variant<<" final_peak="<<lastPeak<<" reference_error="<<referenceError<<'\n';require(lastPeak<1e-6,"declared tail insufficient");
 }
}
#include "BandModulePhysics.h"
#include "ExpandedBandTests.h"
#include "ExpandedContractFixtures.h"
#include "ExpandedReviewFixtures.h"
#include "ExpandedTailFixtures.h"
#include "EqPrecisionTiming.h"
#include "FmaResidualProof.h"
int main(int argc,char** argv){try{const bool expandedOnly=argc==2&&std::string(argv[1])=="--expanded-only";BandTestNoDenormals guard;allocationProbe::testThread=true;const int hooks=platformProbe::install();
    allocationProbe::enabled=true;void* probe=::operator new(37);::operator delete(probe);allocationProbe::enabled=false;require(allocationProbe::count>0,"probe calibration");
    std::thread background([]{allocationProbe::enabled=true;void* p=::operator new(37);::operator delete(p);allocationProbe::enabled=false;});background.join();require(allocationProbe::otherThreadCount>0,"background calibration");
    allocationProbe::count=0;platformProbe::count=0;
    if(argc==2&&std::string(argv[1])=="--fma-proof-only"){writeFmaResidualProof("fma-dsp");return 0;}
    if(argc==2&&std::string(argv[1])=="--precision-timing-only"){eqPrecisionTiming();require(allocationProbe::count==0&&platformProbe::count==0,"precision timing allocated");std::cout<<"PASS RECURRENCE KERNEL TIMING SUBSET; no full-chain capacity claim\n";return 0;}
    if(argc==2&&std::string(argv[1])=="--tail-only"){expandedTailReview();require(allocationProbe::count==0&&platformProbe::count==0,"tail callback allocated");std::cout<<"PASS EXPANDED EQ TAIL SUBSET; other gates not run\n";return 0;}
    laws();settlingRegression();expandedInitialProof();
    bool dynamicReviewPassed=true;
    for(double rate:{44100.,48000.,96000.}){
        expandedBankProcessingReview<Kind::Dynamics>(rate);expandedBankProcessingReview<Kind::Imager>(rate);
        try{expandedDynamicReview(rate);}catch(const std::exception& e){dynamicReviewPassed=false;
            std::cerr<<"FAIL dynamic review rate="<<rate<<": "<<e.what()<<"; see per-case collection log, other gates continue\n";}
#define EXPANDED(K) expandedControls<Kind::K>(rate);expandedPartitions<Kind::K>(rate);
        EXPANDED(Dynamic) EXPANDED(MidSide) EXPANDED(Dynamics) EXPANDED(Imager)
#undef EXPANDED
        std::cout<<"expanded all control/partition fixtures rate="<<rate<<'\n';
    }
    std::ofstream timing("band-core-timings.csv");timing<<"kind,rate,block,iteration,seconds,deadline,miss\n";
    if(!expandedOnly){expandedTailReview();eqPrecisionTiming();}
    if(!expandedOnly)for(double rate:{44100.,48000.,96000.}){
#define RUN(K) stream<Kind::K>(rate);controls<Kind::K>(rate);safety<Kind::K>(rate);tail<Kind::K>(rate);
        RUN(MidSide) RUN(Imager) RUN(Dynamics) RUN(Dynamic)
#undef RUN
        staticEq<Kind::Dynamic>(rate);staticEq<Kind::MidSide>(rate);dynamicQuantitative(rate);midSideQuantitative(rate);imaging(rate);
        solos<Kind::Dynamics>(rate);solos<Kind::Imager>(rate);
        for(int block:{1,17,64,128,257}){benchmark<Kind::Dynamic>(rate,block,timing);benchmark<Kind::MidSide>(rate,block,timing);benchmark<Kind::Dynamics>(rate,block,timing);benchmark<Kind::Imager>(rate,block,timing);}
    }
    require(allocationProbe::count==0&&platformProbe::count==0,"callback allocated");
    require(dynamicReviewPassed,"dynamic review failed; see individual failures (tolerances unchanged)");
    std::cout<<(expandedOnly?"EXPANDED SUBSET ":"FULL ")<<"max_reference_error="<<worst<<" import_hooks="<<hooks<<" allocations=0\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
