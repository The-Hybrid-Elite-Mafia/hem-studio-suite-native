#pragma once
// Independent branch-by-branch bilinear cascade, without shared filter stages.
// Production uses a shared tree; this reference deliberately duplicates paths.
namespace fourReference {
struct Bank {
    double rate;unsigned phase=0;std::array<bandReference::Smooth,3> ramps;
    std::array<std::array<bandReference::Filter,6>,4> branch;
    static constexpr int lengths[]{4,5,6,6};
    static constexpr int frequencyIndex[4][6]{{0,0,1,2,0,0},{0,0,1,1,2,0},{0,0,1,1,2,2},{0,0,1,1,2,2}};
    static constexpr int kinds[4][6]{{0,0,2,2,0,0},{1,1,0,0,2,0},{1,1,1,1,0,0},{1,1,1,1,1,1}};
    static std::array<double,3> legal(std::array<double,3> f){f[1]=std::max(f[0]*1.05,std::min(f[1],f[2]/1.05));return f;}
    Bank(double sr,std::array<double,3> f):rate(sr){f=legal(f);for(size_t i=0;i<3;++i)ramps[i].reset(rate,float(f[i]));configure(f);}
    void configure(std::array<double,3> f){for(size_t b=0;b<4;++b)for(int i=0;i<lengths[b];++i){auto next=bandReference::design(rate,f[size_t(frequencyIndex[b][i])],kinds[b][i]);
        branch[b][size_t(i)].a=next.a;branch[b][size_t(i)].b=next.b;}}
    void targets(std::array<double,3> f){f=legal(f);for(size_t i=0;i<3;++i)ramps[i].set(float(f[i]));}
    std::array<std::array<double,2>,4> tick(double l,double r){std::array<double,3> f{};for(size_t i=0;i<3;++i)f[i]=ramps[i].tick();
        if(phase==0)configure(legal(f));phase=(phase+1)%16;std::array<std::array<double,2>,4> out{};
        for(size_t b=0;b<4;++b)for(size_t c=0;c<2;++c){double value=c?r:l;for(int i=0;i<lengths[b];++i)value=branch[b][size_t(i)].tick(value,c);out[b][c]=value;}return out;}
    std::array<std::complex<double>,4> response(double hz){std::array<std::complex<double>,4> out{};
        for(size_t b=0;b<4;++b){out[b]=1;for(int i=0;i<lengths[b];++i)out[b]*=branch[b][size_t(i)].response(hz,rate);}return out;}
};
}
static void fourBandProof(){
    for(double rate:{8000.,16000.,384000.}){hem::bands::FourBandBank bank;check(bank.prepare(rate,800,18000,8000),"safety-rate bank prepare");
        for(int n=0;n<10000;++n){if(n%137==0)bank.targets(n%274?80:800,n%411?20:18000,n%548?1200:8000);bank.tick(.1,-.1);
            const auto applied=bank.actualCutoffs();check(applied[1]>=1.05*applied[0]-1e-9&&applied[1]<=applied[2]/1.05+1e-9&&applied[2]<=rate*.45,"safety-rate applied crossover constraints");}}
    double maximumBranchError=0,maximumSumError=0,maximumResponseError=0,maximumAutomationError=0;
    std::ofstream evidence("gate-a-evidence/four-band-proof.csv");check(bool(evidence),"four-band evidence file");
    evidence<<"rate,low,requested_mid,effective_mid,high,branch_error,sum_error\n";
    for(double rate:{44100.,48000.,96000.}){
        for(const auto pair:std::array<std::array<double,2>,5>{{{80,1200},{80,8000},{800,1200},{800,8000},{200,2500}}})for(double middle:{20.,1000.,18000.}){
            const std::array<double,3> requested{pair[0],middle,pair[1]},effective=fourReference::Bank::legal(requested);
            hem::bands::FourBandBank bank;check(bank.prepare(rate,requested[0],requested[1],requested[2]),"four bank preparation");
            fourReference::Bank reference(rate,requested);std::array<bandReference::Filter,3> allpass;
            for(size_t i=0;i<3;++i)allpass[i]=bandReference::design(rate,effective[i],2);
            for(int bin=0;bin<2048;++bin){const double hz=20*std::pow((rate*.45)/20,bin/2047.);const auto actual=bank.response(hz),expected=reference.response(hz);
                std::complex<double> sum=0,ap=1;for(const auto& f:allpass)ap*=f.response(hz,rate);
                for(size_t b=0;b<4;++b){maximumResponseError=std::max(maximumResponseError,std::abs(actual[b]-expected[b]));check(std::abs(actual[b]-expected[b])<2e-9,"four-band branch response");sum+=actual[b];}
                check(std::abs(sum-ap)<2e-9&&std::abs(std::abs(sum)-1)<2e-9,"four-band allpass reconstruction");}
            double branchError=0,sumError=0;
            for(int n=0;n<int(rate*.5);++n){const double l=n==0?1:.12*std::sin(n*.063),r=n==0?-.3:.09*std::cos(n*.037);
                allocationProbe::enabled=true;const auto actual=bank.tick(l,r);allocationProbe::enabled=false;const auto expected=reference.tick(l,r);
                for(size_t c=0;c<2;++c){double sum=0,ap=c?r:l;for(auto& filter:allpass)ap=filter.tick(ap,c);
                    for(size_t b=0;b<4;++b){sum+=actual[b][c];branchError=std::max(branchError,std::abs(actual[b][c]-expected[b][c]));}
                    sumError=std::max(sumError,std::abs(sum-ap));
                    for(int mask=0;mask<16;++mask){double selected=0,ref=0;for(size_t b=0;b<4;++b)if(mask==0||(mask&(1<<b))){selected+=actual[b][c];ref+=expected[b][c];}
                        check(std::abs(selected-ref)<2e-9,"four-bank all sixteen solo combinations");
                        for(double mix:{0.,.37,1.}){double mixed=0;for(size_t b=0;b<4;++b)if(mask==0||(mask&(1<<b)))mixed+=(1-mix)*actual[b][c]+mix*actual[b][c];
                            check(std::abs(mixed-ref)<2e-9,"phase matched unity mix");}}
                }
            }
            maximumBranchError=std::max(maximumBranchError,branchError);maximumSumError=std::max(maximumSumError,sumError);
            check(branchError<2e-9&&sumError<2e-9,"four-bank time reconstruction");
            evidence<<rate<<','<<requested[0]<<','<<requested[1]<<','<<effective[1]<<','<<requested[2]<<','<<branchError<<','<<sumError<<'\n';
        }
        hem::bands::FourBandBank moving;moving.prepare(rate);fourReference::Bank reference(rate,{200,1000,2500});
        for(int n=0;n<int(rate);++n){if(n%509==0){const int k=(n/509)%4;const std::array<double,3> next{k&1?800.:80.,k&2?18000.:20.,k&2?8000.:1200.};
                moving.targets(next[0],next[1],next[2]);reference.targets(next);}
            const double l=.17*std::sin(n*.044),r=.13*std::cos(n*.11);allocationProbe::enabled=true;const auto a=moving.tick(l,r);allocationProbe::enabled=false;const auto b=reference.tick(l,r);
            const auto applied=moving.actualCutoffs();
            check(applied[1]>=applied[0]*1.05-1e-9&&applied[1]<=applied[2]/1.05+1e-9,"applied smoothed crossover ordering");
            // Branch samples versus independently duplicated time-varying
            // branches, NOT a reduced all-pass identity under automation.
            for(size_t band=0;band<4;++band)for(size_t c=0;c<2;++c){maximumAutomationError=std::max(maximumAutomationError,std::abs(a[band][c]-b[band][c]));check(std::abs(a[band][c]-b[band][c])<2e-9,"four-bank automation");}}
        moving.targets(NAN,NAN,NAN);for(double invalid:{NAN,INFINITY,-INFINITY}){const auto a=moving.tick(invalid,.1);for(auto band:a)for(auto value:band)check(std::isfinite(value),"four-bank input recovery");}
        check(!moving.prepare(0)&&!moving.prepare(INFINITY),"four-bank invalid preparation");
    }
    std::cout<<"four_band_branch_error="<<maximumBranchError<<" four_band_sum_error="<<maximumSumError<<" four_band_response_error="<<maximumResponseError<<" four_band_automation_error="<<maximumAutomationError<<'\n';
}
