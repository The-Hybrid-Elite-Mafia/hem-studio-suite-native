#include "../Source/DSP/BandBank.h"
#include "BandTestNoDenormals.h"
#include <chrono>
#include <cstring>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>
static void check(bool b,const char* m){if(!b)throw std::runtime_error(m);}
static double noise(unsigned& s){s=s*1664525u+1013904223u;return (double(s)/4294967296.*2-1)*.7;}
template<class Bank> void equivalence(double rate){
    Bank scalar,simd;check(scalar.prepare(rate)&&simd.prepare(rate),"prepare");unsigned seed=57;
    for(int n=0;n<48000;++n){
        if(n%113==0){double lo=80+(n%719),hi=1200+(n%6701);scalar.targets(lo,hi);simd.targets(lo,hi);}
        double l=noise(seed),r=noise(seed);
        if(n==11003)l=std::numeric_limits<double>::quiet_NaN();
        if(n==18001)r=std::numeric_limits<double>::infinity();
        if(n==26001)l=std::numeric_limits<double>::max();
        if(n>35000)l=r=0;
        auto a=scalar.template tick<false>(l,r),b=simd.template tick<true>(l,r);
        for(size_t band=0;band<a.size();++band)for(size_t c=0;c<2;++c){
            check(std::isfinite(b[band][c]),"finite recovery");
            check(a[band][c]==b[band][c],"scalar/SIMD sample mismatch");
        }
    }
}
// Four-band targets have a separate middle argument; exercise its independent
// automated crossover through a derived wrapper without changing DSP storage.
struct Four:hem::bands::FourBandBank{void targets(double lo,double hi){FourBandBank::targets(lo,(lo+hi)*.5,hi);}};
volatile double sink=0;
template<class Bank,bool Simd> double bench(){
    Bank bank;bank.prepare(96000);unsigned seed=1;std::array<std::array<double,2>,4096> input{};
    for(auto& f:input)f={noise(seed),noise(seed)};
    double sum=0;const auto begin=std::chrono::steady_clock::now();
    for(int repeat=0;repeat<128;++repeat)for(const auto& f:input){const auto y=bank.template tick<Simd>(f[0],f[1]);sum+=y[0][0]+y.back()[1];}
    const double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count();sink=sum;return ms;
}
int main(int argc,char**){try{BandTestNoDenormals noDenormals;
    for(double rate:{8000.,44100.,48000.,96000.,192000.,384000.}){equivalence<hem::bands::ThreeBandBank>(rate);equivalence<Four>(rate);}
    std::cout<<"PASS: 576000 stereo bank frames exactly equal; 3/4-band, six sample rates, crossover automation, NaN/Inf/overflow recovery, silence tails.\n";
    if(argc>1){for(int run=0;run<5;++run){double s3=bench<hem::bands::ThreeBandBank,false>(),v3=bench<hem::bands::ThreeBandBank,true>();
        double s4=bench<Four,false>(),v4=bench<Four,true>();std::cout<<"run="<<run<<" three scalar_ms="<<s3<<" SIMD_ms="<<v3<<" ratio="<<s3/v3<<" four scalar_ms="<<s4<<" SIMD_ms="<<v4<<" ratio="<<s4/v4<<'\n';}}
    return 0;}catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}}
