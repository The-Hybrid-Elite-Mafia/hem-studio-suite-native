#include "../Source/DSP/BandBank.h"
#include "BandBankReference.h"
#include "FoundationAllocationProbe.h"
#include "FoundationPlatformAllocations.h"
#include "BandTestNoDenormals.h"
#include <iostream>
#include <stdexcept>
#include <limits>
#include <vector>
#include <thread>
#include <filesystem>
#include <fstream>
#include <iomanip>
static void check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
static double noise(unsigned& s){s=s*1664525u+1013904223u;return (double(s)/4294967296.*2-1)*.25;}
static double maximumTimeError=0,maximumDbError=0,maximumPhaseError=0,maximumAutomationError=0;
static void spectral(double rate,double low,double high){
    hem::bands::ThreeBandBank bank;check(bank.prepare(rate,low,high),"prepare");
    const auto l1=bandReference::design(rate,low,0),h1=bandReference::design(rate,low,1),a1=bandReference::design(rate,low,2);
    const auto l2=bandReference::design(rate,high,0),h2=bandReference::design(rate,high,1),a2=bandReference::design(rate,high,2);
    for(double cutoff:{low,high}){const auto pair=hem::bands::designSplit(rate,cutoff);
        for(auto c:{pair.low,pair.high})check(std::abs(40*std::log10(std::abs(c.response(2*hem::pi*cutoff/rate)))+6.02059991328)<.02,"LR cutoff calibration");}
    for(int i=0;i<2048;++i){const double hz=20*std::pow(.45*rate/20,double(i)/2047);
        const auto actual=bank.response(hz);const auto lp=l1.response(hz,rate),hp=h1.response(hz,rate),ap=a2.response(hz,rate);
        const auto lp2=l2.response(hz,rate),hp2=h2.response(hz,rate);
        const std::array<std::complex<double>,3> expected{ap*lp*lp,lp2*lp2*hp*hp,hp2*hp2*hp*hp};
        for(size_t b=0;b<3;++b){if(std::abs(expected[b])>1e-4){
            const double db=std::abs(20*std::log10(std::abs(actual[b]/expected[b])));
            const double phase=std::abs(std::arg(actual[b]/expected[b]))*180/hem::pi;
            maximumDbError=std::max(maximumDbError,db);maximumPhaseError=std::max(maximumPhaseError,phase);
            check(db<.03&&phase<.5,"individual branch complex response");
        }else check(std::abs(actual[b]-expected[b])<1e-5,"stopband absolute error");}
        const auto sum=actual[0]+actual[1]+actual[2],expectedSum=ap*a1.response(hz,rate);
        check(std::abs(20*std::log10(std::abs(sum)))<.02,"neutral magnitude");
        check(std::abs(std::arg(sum/expectedSum))*180/hem::pi<.5,"neutral phase");
    }
}
static void temporal(double rate,double low,double high){
    for(int fixture=0;fixture<3;++fixture){hem::bands::ThreeBandBank bank;bank.prepare(rate,low,high);
        auto ap1=bandReference::design(rate,low,2),ap2=bandReference::design(rate,high,2);unsigned seed=83;
        for(int i=0;i<int(rate);++i){const double l=fixture==0?(i==0?.25:0):fixture==1?.125:noise(seed);
            const double r=fixture==0?(i==1?-.2:0):fixture==1?-.2:noise(seed);
            allocationProbe::enabled=true;const auto bands=bank.tick(l,r);allocationProbe::enabled=false;
            const double input[]{l,r};for(size_t ch=0;ch<2;++ch){const double reference=ap2.tick(ap1.tick(input[ch],ch),ch);
                const double sum=bands[0][ch]+bands[1][ch]+bands[2][ch];
                maximumTimeError=std::max(maximumTimeError,std::abs(sum-reference));check(std::abs(sum-reference)<=2e-5,"allpass time reconstruction");
                for(double db:{-6.,0.,6.})check(std::abs(std::pow(10.,db/20)*sum-std::pow(10.,db/20)*reference)<=2e-5,"uniform gain");}}
    }
}
static void automation(double rate){
    constexpr int total=24001;std::vector<std::array<double,2>> input(total);unsigned seed=42;
    for(auto& x:input)x={noise(seed),noise(seed)};
    std::vector<hem::bands::ThreeBandBank::Frame> baseline(total);
    const int irregular[]{3,311,1,1024,19,71};
    for(int block:{1,17,64,128,257,1024,0}){
        hem::bands::ThreeBandBank bank;bank.prepare(rate,200,2500);bandReference::Bank reference(rate,200,2500);
        int offset=0,turn=0;
        while(offset<total){const bool high=(offset/503)%2!=0;
            const double low=high?800:80,upper=high?8000:1200;
            allocationProbe::enabled=true;bank.targets(low,upper);allocationProbe::enabled=false;
            reference.targets(low,upper);
            const int count=std::min({total-offset,block?block:irregular[(turn++)%6],503-offset%503});
            for(int j=0;j<count;++j){const int i=offset+j;
                allocationProbe::enabled=true;const auto actual=bank.tick(input[size_t(i)][0],input[size_t(i)][1]);allocationProbe::enabled=false;
                const auto expected=reference.tick(input[size_t(i)][0],input[size_t(i)][1]);
                for(size_t b=0;b<3;++b)for(size_t c=0;c<2;++c){const auto error=std::abs(actual[b][c]-expected[b][c]);
                    maximumAutomationError=std::max(maximumAutomationError,error);check(error<=2e-5,"time varying independent recurrence");
                    if(block!=1)check(std::abs(actual[b][c]-baseline[size_t(i)][b][c])<=2e-6,"callback partition");}
                if(block==1)baseline[size_t(i)]=actual;
            }offset+=count;
        }
    }
}
#include "BandBankFixtures.h"
#include "FourBandFixtures.h"
int main(){try{
    // Same FTZ/DAZ policy as the future JUCE callback. Both production and
    // independent reference run under this scope; prior thread FP state returns.
    BandTestNoDenormals noDenormals;
    allocationProbe::testThread=true;const int hooks=platformProbe::install();
    allocationProbe::enabled=true;void* calibration=::operator new(64);::operator delete(calibration);allocationProbe::enabled=false;
    check(allocationProbe::count.load()>0,"allocation positive calibration");
    check(hooks==0||platformProbe::count.load()>0,"platform import positive calibration");
    const auto foregroundBefore=allocationProbe::count.load();
    std::thread background([]{allocationProbe::enabled=true;void* p=::operator new(64);::operator delete(p);allocationProbe::enabled=false;});background.join();
    check(allocationProbe::otherThreadCount.load()>0,"allocation background calibration");
    check(allocationProbe::count.load()==foregroundBefore,"background incorrectly attributed to callback thread");
    allocationProbe::count=0;platformProbe::count=0;
    std::filesystem::create_directories("gate-a-evidence");
    std::ofstream sweep("gate-a-evidence/rendered-sweep.csv"),transition("gate-a-evidence/transition-samples.csv"),spectra("gate-a-evidence/transition-spectra.csv");
    check(bool(sweep)&&bool(transition)&&bool(spectra),"open evidence files");
    sweep<<std::setprecision(17)<<"rate,low,high,hz,branch,actual_real,actual_imag,reference_real,reference_imag,worst_sample_error\n";
    transition<<std::setprecision(17)<<"rate,sample,input_l,input_r,low_l,ref_low_l,low_r,ref_low_r,mid_l,ref_mid_l,mid_r,ref_mid_r,high_l,ref_high_l,high_r,ref_high_r\n";
    spectra<<std::setprecision(17)<<"rate,event_sample,window_start,window_samples,hz,actual_amplitude,reference_amplitude,dry_amplitude\n";
    for(double rate:{44100.,48000.,96000.}){for(auto pair:std::array<std::array<double,2>,5>{{{80,1200},{80,8000},{800,1200},{800,8000},{200,2500}}}){
        spectral(rate,pair[0],pair[1]);temporal(rate,pair[0],pair[1]);
        renderedSweep(rate,pair[0],pair[1],sweep);
        std::cout<<"completed sweep rate="<<rate<<" low="<<pair[0]<<" high="<<pair[1]<<std::endl;
    }automation(rate);transitionFixture(rate,transition,spectra);recoveryFixtures(rate);}
    hem::bands::ThreeBandBank bank;check(!bank.prepare(std::numeric_limits<double>::quiet_NaN()),"bad rate");bank.prepare(48000);
    bank.targets(std::numeric_limits<double>::infinity(),-1e12);
    for(double bad:{std::numeric_limits<double>::quiet_NaN(),std::numeric_limits<double>::infinity(),1e6,-1e6}){
        const auto y=bank.tick(bad,.1);for(auto b:y)for(auto x:b)check(std::isfinite(x),"sample recovery");}
    std::cout<<"max_time_error="<<maximumTimeError<<" max_db_error="<<maximumDbError<<" max_phase_degrees="<<maximumPhaseError
             <<" max_automation_error="<<maximumAutomationError<<"\n";
    std::cout<<"max_rendered_sample_error="<<maximumRenderedSampleError<<" max_rendered_db_error="<<maximumRenderedDbError
             <<" max_rendered_phase_degrees="<<maximumRenderedPhaseError<<" max_recovery_error="<<maximumRecoveryError
             <<" max_transition_error="<<maximumTransitionError<<" max_transition_peak="<<maximumTransitionPeak<<"\n";
    fourBandProof();
    check(allocationProbe::count.load()==0&&platformProbe::count.load()==0,"callback allocated");
    std::cout<<"allocator_import_hooks="<<hooks<<" callback_new_count="<<allocationProbe::count.load()
             <<" callback_import_count="<<platformProbe::count.load()<<"; coverage excludes allocator internals in system DLLs\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}}
