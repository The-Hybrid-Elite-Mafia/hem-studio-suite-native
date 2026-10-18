#pragma once
#include <iomanip>
#include <bit>
namespace hem::bands {
struct ExpandedCoreReviewAccess {
    static double stage(const ExpandedCore<Kind::Dynamic>& core,size_t band){return core.filters[band].primary.section.y1[0];}
    static std::array<double,2> stagePair(const ExpandedCore<Kind::Dynamic>& core,size_t band){return {core.filters[band].primary.section.y1[0],core.filters[band].primary.firstResidual[0]};}
    static double envelope(const ExpandedCore<Kind::Dynamic>& core,size_t band){return core.detectors[band].cut;}
    static double gain(const ExpandedCore<Kind::Dynamic>& core,size_t band){return core.filters[band].primary.gain;}
    static Coefficients coefficients(const ExpandedCore<Kind::Dynamic>& core,size_t band){return core.filters[band].primary.section.c;}
    static float parameter(const ExpandedCore<Kind::Dynamic>& core,size_t index){return core.current[index];}
    static std::array<double,4> history(const ExpandedCore<Kind::Dynamic>& core,size_t index){const auto& s=core.filters[index].primary.section;return {s.x1[0],s.x2[0],s.y1[0],s.y2[0]};}
};
}
// Independent oracle: compensated products and histories evaluated with the
// bilinear reference's Wide arithmetic. No production accumulator is used.
struct ReviewPreciseFilter {
    std::array<moduleReference::Wide,2> x{},y{};
    moduleReference::Wide tick(moduleReference::Wide input,const bandReference::Filter& c){
        using moduleReference::Wide;
        const auto output=Wide(c.b[0])*input+Wide(c.b[1])*x[0]+Wide(c.b[2])*x[1]-Wide(c.a[1])*y[0]-Wide(c.a[2])*y[1];
        x[1]=x[0];x[0]=input;y[1]=y[0];y[0]=output;return output;
    }
};
// This isolates per-band processing and selection against the separately
// qualified crossover outputs; it is not a second independent crossover proof.
template<Kind K> static void expandedBankProcessingReview(double rate){
    using namespace hem::bands;double error=0,cutError=0;
    for(int onMask:{0,5,10,15})for(int soloMask=0;soloMask<16;++soloMask)for(int mixIndex=0;mixIndex<(K==Kind::Dynamics?3:1);++mixIndex){
        auto p=expandedDefaults<K>();p[0]=1;const double mix=K==Kind::Dynamics?(mixIndex==0?0:mixIndex==1?.37:1):1;
        const std::array<size_t,4> bases=K==Kind::Dynamics?std::array<size_t,4>{4,29,37,20}:std::array<size_t,4>{5,15,18,11};
        const std::array<float,4> makeup{3,-2,4,1};
        for(size_t b=0;b<4;++b){const size_t i=bases[b];p[(K==Kind::Dynamics?45:21)+b]=float((onMask>>b)&1);
            p[i+(K==Kind::Dynamics?7:2)]=float((soloMask>>b)&1);
            if constexpr(K==Kind::Dynamics){p[i]=-32;p[i+1]=4;p[i+4]=6;p[i+6]=makeup[b];p[50]=float(mix*100);}
            else{p[i]=float(30+50*b);p[i+1]=makeup[b];}}
        ExpandedCore<K> core;core.prepare(rate,p);FourBandBank split;split.prepare(rate,200,1000,2500);
        std::array<double,4> peak{},cut{};
        for(int n=0;n<4096;++n){float l=float(.2*std::sin(n*.019)+.07*std::cos(n*.73)),r=float(.17*std::cos(n*.17));
            const auto raw=split.tick(l,r);std::array<double,2> expected{};
            for(size_t b=0;b<4;++b){auto processed=raw[b];
                if constexpr(K==Kind::Dynamics){peak[b]=std::max({std::abs(raw[b][0]),std::abs(raw[b][1]),peak[b]*std::exp(-1/(rate*.020))});
                    const double request=moduleReference::compression(20*std::log10(std::max(1e-12,peak[b])),-32,4,6,24);
                    cut[b]+=(request-cut[b])*(-std::expm1(-1/(rate*(request>cut[b]?10:100)*.001)));
                    const double gain=std::pow(10.,(makeup[b]-cut[b])/20);for(auto& value:processed)value*=gain;
                }else{const double mid=(raw[b][0]+raw[b][1])*.5,side=(raw[b][0]-raw[b][1])*.5,width=(30+50*b)*.01,gain=std::pow(10.,makeup[b]/20.);
                    processed={(mid+width*side)*gain,(mid-width*side)*gain};}
                if(!(onMask&(1<<b)))processed=raw[b];
                for(size_t c=0;c<2;++c)if(soloMask==0||(soloMask&(1<<b)))expected[c]+=raw[b][c]+mix*(processed[c]-raw[b][c]);
            }
            allocationProbe::enabled=true;core.process(&l,&r,1,p);allocationProbe::enabled=false;
            error=std::max({error,std::abs(double(l)-expected[0]),std::abs(double(r)-expected[1])});
            require(std::abs(double(l)-expected[0])<2e-6&&std::abs(double(r)-expected[1])<2e-6,"four-band processed MIX/enable/solo law");
            if constexpr(K==Kind::Dynamics){const auto meter=core.telemetry.read();for(size_t b=0;b<4;++b){const double wanted=(onMask&(1<<b))?cut[b]:0;
                cutError=std::max(cutError,std::abs(meter.cuts[b]-wanted));require(std::abs(meter.cuts[b]-wanted)<1e-5,"actual pre-MIX compressor reduction meter");}}
        }
    }
    std::cout<<"bank_processing_review kind="<<int(K)<<" rate="<<rate<<" signal_error="<<error<<" cut_meter_error="<<cutError<<'\n';
}
// Independent detector, envelope, physical-value ramps and bilinear filters.
// Covers sign/enable automation separately from type/topology morphing.
static void expandedDynamicReview(double rate){
    using namespace hem::bands;
    double maximumError=0,maximumStep=0;int failedCases=0;
    for(int type:{0,1,2})for(int bands:{1,8})for(int up:{0,1}){
        auto p=expandedDefaults<Kind::Dynamic>();p[0]=1;
        for(int b=0;b<8;++b){const size_t i=2+8*size_t(b),e=66+3*size_t(b);
            p[i]=float(b<bands);p[i+1]=1000;p[i+2]=up?12.f:-12.f;p[i+3]=8;
            p[i+4]=-60;p[i+5]=18;p[i+6]=.1f;p[e]=float(type);p[e+1]=1;p[e+2]=float(up);}
        ExpandedCore<Kind::Dynamic> core;core.prepare(rate,p);core.visuals.subscribed=true;
        std::array<bandReference::Filter,8> eq,detector,sameCoefficients;
        std::array<ReviewPreciseFilter,8> preciseReference;
        std::array<double,8> peak{},envelope{};
        bandReference::Smooth direction,on;direction.reset(rate,float(up));on.reset(rate,1);
        for(auto& f:detector)f=moduleReference::bandpass(rate,1000,8);
        double previous=0,casePeak=0,caseMaxError=0;bool casePassed=true;const int warm=int(rate),duration=warm+12000;
        std::array<double,8> expectedStages{};
        double replayError=0;
        const bool record=bands==8&&up==1;
        std::ofstream trajectory,coefficientTrace;
        if(record){const auto name="numeric-diagnosis/current-"+std::to_string(int(rate))+"-type-"+std::to_string(type);
            trajectory.open(name+"-trajectory.csv");coefficientTrace.open(name+"-coefficients.csv");
            trajectory<<std::setprecision(17)<<"n,input,output,reference,input_bits,output_bits";for(int b=0;b<8;++b)trajectory<<",actual"<<b<<",reference"<<b<<",actual_hi_bits"<<b<<",actual_lo_bits"<<b<<",reference_hi_bits"<<b<<",reference_lo_bits"<<b;trajectory<<'\n';
            coefficientTrace<<std::setprecision(17)<<"n,gain,envelope,reference_envelope,b0,b1,b2,a1,a2,reference_b0,reference_b1,reference_b2,reference_a1,reference_a2,gain_bits,b0_bits,b1_bits,b2_bits,a1_bits,a2_bits\n";}
        for(int n=0;n<duration;++n){
            if(bands==1&&n>=warm&&(n-warm)%137==0){const int event=(n-warm)/137;
                p[68]=float(event%2);p[67]=float((event/3)%2);direction.set(p[68]);on.set(p[67]);}
            const double signedEnable=on.tick()*(2*direction.tick()-1);
            const float input=float(.25*std::sin(2*hem::pi*1000*n/rate));double expected=input;moduleReference::Wide precise(input);
            for(int b=0;b<bands;++b){const size_t index=size_t(b);const double detected=detector[index].tick(input,0);
                peak[index]=std::max(std::abs(detected),peak[index]*std::exp(-1/(rate*.020)));
                const double request=std::clamp(20*std::log10(std::max(1e-12,peak[index]))+60,0.,18.);
                // Match the physical float parameter, not the different double
                // literal 0.1. The independent envelope equation is unchanged.
                const double ms=request>envelope[index]?double(p[8+8*index]):double(p[9+8*index]);
                envelope[index]+=(request-envelope[index])*(-std::expm1(-1/(rate*ms*.001)));
                if(n%16==0){const double gain=(up?12:-12)+signedEnable*envelope[index];
                    const auto next=type?expandedReference::shelf(rate,1000,8,gain,type):moduleReference::bell(rate,1000,8,gain);
                    eq[index].a=next.a;eq[index].b=next.b;}
                expected=eq[index].tick(expected,0);
                precise=preciseReference[index].tick(precise,eq[index]);
                expectedStages[index]=precise.value();
            }
            expected=precise.value();
            float l=input,r=input;allocationProbe::enabled=true;core.process(&l,&r,1,p);allocationProbe::enabled=false;
            double replay=input;for(int b=0;b<bands;++b){const size_t index=size_t(b);const auto c=ExpandedCoreReviewAccess::coefficients(core,index);
                sameCoefficients[index].a={1,c.a1,c.a2};sameCoefficients[index].b={c.b0,c.b1,c.b2};replay=sameCoefficients[index].tick(replay,0);
                replayError=std::max(replayError,std::abs(replay-ExpandedCoreReviewAccess::stage(core,index)));}
            if(record){trajectory<<n<<','<<input<<','<<l<<','<<expected<<','<<std::bit_cast<uint64_t>(double(input))<<','<<std::bit_cast<uint64_t>(double(l));
                for(size_t b=0;b<8;++b){const auto pair=ExpandedCoreReviewAccess::stagePair(core,b);const auto referencePair=preciseReference[b].y[0];
                    trajectory<<','<<pair[0]<<','<<expectedStages[b]<<','<<std::bit_cast<uint64_t>(pair[0])<<','<<std::bit_cast<uint64_t>(pair[1])<<','<<std::bit_cast<uint64_t>(referencePair.hi)<<','<<std::bit_cast<uint64_t>(referencePair.lo);}trajectory<<'\n';
                if(n%16==0){const auto c=ExpandedCoreReviewAccess::coefficients(core,0);
                    coefficientTrace<<n<<','<<ExpandedCoreReviewAccess::gain(core,0)<<','<<ExpandedCoreReviewAccess::envelope(core,0)<<','<<envelope[0]<<','<<c.b0<<','<<c.b1<<','<<c.b2<<','<<c.a1<<','<<c.a2<<','<<eq[0].b[0]<<','<<eq[0].b[1]<<','<<eq[0].b[2]<<','<<eq[0].a[1]<<','<<eq[0].a[2]<<','<<std::bit_cast<uint64_t>(ExpandedCoreReviewAccess::gain(core,0));
                    for(double coefficient:{c.b0,c.b1,c.b2,c.a1,c.a2})coefficientTrace<<','<<std::bit_cast<uint64_t>(coefficient);coefficientTrace<<'\n';}}
            require(std::isfinite(l)&&std::isfinite(r),"combined dynamic extreme nonfinite");
            const double error=std::abs(double(l)-expected);maximumError=std::max(maximumError,error);
            casePeak=std::max(casePeak,std::abs(expected));caseMaxError=std::max(caseMaxError,error);
            if(error>2e-5+2e-6*std::abs(expected)){
                if(casePassed){std::cerr<<std::setprecision(17)<<"dynamic review mismatch rate="<<rate<<" type="<<type<<" bands="<<bands<<" up="<<up<<" sample="<<n<<" input="<<input<<" actual="<<l<<" expected="<<expected<<" error="<<error<<" peak_so_far="<<casePeak<<'\n';
                    const std::string filename="dynamic-divergence-"+std::to_string(int(rate))+"-"+std::to_string(type)+"-"+std::to_string(bands)+"-"+std::to_string(up)+".csv";
                    std::ofstream detail(filename);detail<<std::setprecision(17)<<"band,actual_stage,reference_stage,stage_error,actual_envelope,reference_envelope,actual_gain,actual_b0,actual_b1,actual_b2,actual_a1,actual_a2,reference_b0,reference_b1,reference_b2,reference_a1,reference_a2,actual_on,actual_up,reference_signed_enable\n";
                    std::ofstream states(filename+"-states.csv");states<<std::setprecision(17)<<"band,actual_x1,actual_x2,actual_y1,actual_y2,reference_x1,reference_x2,reference_y1,reference_y2\n";
                    for(int b=0;b<bands;++b){const size_t index=size_t(b);const auto c=ExpandedCoreReviewAccess::coefficients(core,index);const double stage=ExpandedCoreReviewAccess::stage(core,index);const auto history=ExpandedCoreReviewAccess::history(core,index);
                        states<<b;for(auto value:history)states<<','<<value;states<<','<<eq[index].x[0][0]<<','<<eq[index].x[0][1]<<','<<eq[index].y[0][0]<<','<<eq[index].y[0][1]<<'\n';
                        detail<<b<<','<<stage<<','<<expectedStages[index]<<','<<stage-expectedStages[index]<<','<<ExpandedCoreReviewAccess::envelope(core,index)<<','<<envelope[index]<<','<<ExpandedCoreReviewAccess::gain(core,index)<<','<<c.b0<<','<<c.b1<<','<<c.b2<<','<<c.a1<<','<<c.a2<<','<<eq[index].b[0]<<','<<eq[index].b[1]<<','<<eq[index].b[2]<<','<<eq[index].a[1]<<','<<eq[index].a[2]<<','<<ExpandedCoreReviewAccess::parameter(core,67+3*index)<<','<<ExpandedCoreReviewAccess::parameter(core,68+3*index)<<','<<signedEnable<<'\n';}
                }
                casePassed=false;
            }
            if(bands==1&&n>warm)maximumStep=std::max(maximumStep,std::abs(double(l)-previous));previous=l;
            if(n==warm-1){VisualFrame frame;require(core.visuals.read(frame),"extreme dynamic telemetry missing");
                require(std::abs(frame.signedDynamics[0]-(up?18:-18))<.02,"combined gain did not reach dynamic +/-18");}
        }
        std::cout<<std::setprecision(17)<<"dynamic_review_case rate="<<rate<<" type="<<type<<" bands="<<bands<<" up="<<up<<" peak="<<casePeak<<" max_error="<<caseMaxError<<" same_coeff_DFI_replay_error="<<replayError<<" result="<<(casePassed?"PASS":"FAIL")<<'\n';
        if(!casePassed)++failedCases;
    }
    require(failedCases==0,"combined dynamic extreme / sign-enable reference (all cases collected)");
    std::cout<<"dynamic_review rate="<<rate<<" independent_max_error="<<maximumError<<" observed_max_adjacent_sample="<<maximumStep<<" (signal derivative, not click threshold)\n";
}
