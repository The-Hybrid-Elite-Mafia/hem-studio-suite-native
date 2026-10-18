#include "../Source/DSP/PreparedConvolution.h"
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmismatched-new-delete"
#endif
#include "FoundationAllocationProbe.h"
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif
#include <chrono>
#include <cstdint>
#include <iostream>
#include <random>
#include <stdexcept>
#include <string>
#include <ctime>

namespace {
using Core=hem::reverb::PreparedConvolution;
using FFT=hem::reverb::PreparedFFT;
std::uint64_t assertions=0;
void require(bool ok,const char* text) { ++assertions; if(!ok) throw std::runtime_error(text); }
struct Count {
    Count(){allocationProbe::testThread=true;allocationProbe::enabled.store(true);}
    ~Count(){allocationProbe::enabled.store(false);allocationProbe::testThread=false;}
};
struct Stereo {
    std::vector<float> l,r;
    explicit Stereo(std::size_t length):l(length),r(length){}
};
void process(Core& c,Stereo& signal,int block) {
    for(std::size_t first=0;first<signal.l.size();first+=std::size_t(block)) {
        const int count=int(std::min(std::size_t(block),signal.l.size()-first));
        Count allocations;
        c.process(signal.l.data()+first,signal.r.data()+first,count);
    }
}
float noise(std::uint32_t& state) { state=state*1664525u+1013904223u;return float(double(state)/4294967296.-.5)*.02f; }

void fftTests() {
    for(std::size_t size:{128u,512u,2048u,8192u}) {
        FFT fft(size);
        std::uint32_t seed=31;
        std::vector<std::complex<float>> input(size);
        for(auto& x:input) x={noise(seed),noise(seed)};
        for(bool inverse:{false,true}) {
            auto expected=input;
            fft.transform(expected.data(),inverse);
            for(std::size_t quota:{1u,7u,33u,201u,4096u}) {
                auto actual=input;
                auto job=FFT::begin(inverse);
                std::size_t consumed=0;
                {
                    Count allocations;
                    while(!job.finished) {
                        const auto work=fft.advance(actual.data(),job,quota);
                        require(work>0&&work<=quota,"Incremental FFT exceeded its work quota");
                        consumed+=work;
                    }
                }
                require(consumed==fft.workUnits(),"FFT exact work count mismatch");
                for(std::size_t i=0;i<size;++i) {
                    if(inverse)actual[i]/=float(size);
                    require(std::abs(actual[i]-expected[i])<1e-7f,"Incremental and full FFT disagree");
                }
            }
        }
    }
    std::cout<<"PASS every incremental FFT phase, forward/inverse, quotas 1..4096, exact work counts\n";
}

constexpr std::array<std::size_t,10> impulses{0,1,63,64,1023,4095,4096,8191,8192,12345};
constexpr std::array<float,10> amplitudes{1.f,-.37f,.19f,-.2f,.51f,-.17f,.3f,-.11f,.13f,.21f};

void sparseReference(std::size_t length,int block,bool resetAgain=false) {
    Stereo ir(length);
    std::uint32_t rng=91;
    for(std::size_t i=0;i<length;++i){ir.l[i]=noise(rng);ir.r[i]=noise(rng);}
    ir.l[0]=.75f;ir.r[0]=-.5f;
    Stereo input(length+impulses.back()+1);
    for(std::size_t i=0;i<impulses.size();++i){input.l[impulses[i]]=amplitudes[i];input.r[impulses[i]]=amplitudes[i]*-.7f;}
    Core core;core.prepare(ir.l.data(),ir.r.data(),length);
    auto actual=input;
    process(core,actual,block);
    double maximumError=0;
    for(std::size_t n=0;n<actual.l.size();++n) {
        double l=0,r=0;
        for(std::size_t i=0;i<impulses.size();++i)if(n>=impulses[i]&&n-impulses[i]<length) {
            l+=double(ir.l[n-impulses[i]])*amplitudes[i];
            r+=double(ir.r[n-impulses[i]])*input.r[impulses[i]];
        }
        const double error=std::max(std::abs(double(actual.l[n])-l),std::abs(double(actual.r[n])-r));
        maximumError=std::max(maximumError,error);
        require(error<2e-5,"Scheduled convolution differs from independent sparse-input FIR reference");
    }
    require(actual.l[0]==.75f&&actual.r[0]==.35f,"Direct head gained algorithmic latency");
    if(resetAgain) {
        {Count allocations;core.reset();}
        auto repeated=input;process(core,repeated,block);
        require(repeated.l==actual.l&&repeated.r==actual.r,"Scheduled state reset is not deterministic");
        std::cout<<"PASS "<<length<<" dense IR taps, block "<<block<<", full support reference; maximum error "<<maximumError<<'\n';
    }
}

void boundaryAndBlockTests() {
    for(std::size_t length:{1u,63u,64u,65u,255u,256u,257u,1023u,1024u,1025u,2047u,2048u,2049u,4095u,4096u,4097u,8191u,8192u,8193u,12287u,12288u,12289u})
        for(int block:{1,7,17,31,63,64,127,256,511,1024,4096,8192}) sparseReference(length,block);
    for(int block:{1,7,64,127,511,4096,8192}) sparseReference(100003,block,block==127);
    std::cout<<"PASS all head/tier/scheduler boundaries across 12 host-block sizes, through 100003 taps\n";
    // A single coefficient on every side of the scheduler boundary must land
    // on precisely its own sample, with no additional FFT latency or dropout.
    for(std::size_t coefficient:{2047u,2048u,2049u,4095u,4096u,8191u,8192u,8193u,12287u,12288u,16384u,32769u}) {
        Stereo ir(coefficient+1);ir.l[coefficient]=1;ir.r[coefficient]=-.5f;
        Core core;core.prepare(ir.l.data(),ir.r.data(),ir.l.size());
        Stereo signal(coefficient+8193);signal.l[0]=signal.r[0]=1;
        process(core,signal,37);
        for(std::size_t i=0;i<signal.l.size();++i)
            require(std::abs(signal.l[i]-(i==coefficient?1.f:0.f))<2e-6f&&std::abs(signal.r[i]-(i==coefficient?-.5f:0.f))<2e-6f,"Scheduled coefficient missed its exact deadline");
    }
}

void denseReferenceAndPartitionTests() {
    Stereo ir(8333),input(10017);
    std::uint32_t state=7;
    for(std::size_t i=0;i<ir.l.size();++i){ir.l[i]=noise(state);ir.r[i]=noise(state);}
    for(std::size_t i=0;i<777;++i){input.l[i]=noise(state);input.r[i]=noise(state);}
    Core core;core.prepare(ir.l.data(),ir.r.data(),ir.l.size());
    auto result=input;process(core,result,1);
    for(std::size_t i=0;i<result.l.size();++i) {
        double l=0,r=0;
        for(std::size_t j=0;j<777&&j<=i;++j)if(i-j<ir.l.size()) {
            l+=double(input.l[j])*ir.l[i-j];r+=double(input.r[j])*ir.r[i-j];
        }
        require(std::abs(result.l[i]-l)<2e-5&&std::abs(result.r[i]-r)<2e-5,"Dense-input scheduled reference mismatch");
    }
    for(int block:{3,17,64,255,511,1024,8192}) {
        core.reset();auto partitioned=input;process(core,partitioned,block);
        require(partitioned.l==result.l&&partitioned.r==result.r,"Scheduler depends on host block partitioning");
    }
    // Reprepare to no IR, then to a short IR, must discard pending job state.
    core.prepare(nullptr,nullptr,0);
    auto empty=input;process(core,empty,127);
    for(float x:empty.l)require(x==0,"Empty prepare retained scheduled output");
    std::array<float,1>identity{1};core.prepare(identity.data(),identity.data(),1);
    auto unchanged=input;process(core,unchanged,77);
    require(unchanged.l==input.l&&unchanged.r==input.r,"Short reprepare retained scheduled output");
    std::cout<<"PASS dense direct FIR, bit-exact block invariance, reset and mid-job reprepare\n";
}

void maximumLengthTest() { sparseReference(3841536,113,true); }

double threadTimeMicros() {
#if defined(__linux__)
    timespec time{};clock_gettime(CLOCK_THREAD_CPUTIME_ID,&time);
    return double(time.tv_sec)*1e6+double(time.tv_nsec)*1e-3;
#else
    return double(std::clock())*1e6/double(CLOCKS_PER_SEC);
#endif
}
void benchmark() {
    for(std::size_t length:{48000u,480000u,3841536u}) {
        Stereo ir(length);std::fill(ir.l.begin(),ir.l.end(),.00001f);ir.r=ir.l;ir.l[0]=ir.r[0]=1;
        Core core;core.prepare(ir.l.data(),ir.r.data(),length);
        std::array<float,64> left{},right{};
        std::vector<double> cpu,wall;cpu.reserve(2048);wall.reserve(2048);
        for(int block=0;block<2176;++block) {
            left.fill(.1f);right.fill(.1f);
            const auto start=std::chrono::steady_clock::now();const double begin=threadTimeMicros();
            core.process(left.data(),right.data(),64);
            const double used=threadTimeMicros()-begin;
            const double elapsed=std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-start).count();
            if(block>=128){cpu.push_back(used);wall.push_back(elapsed);}
        }
        double cpuTotal=0,wallTotal=0;for(double x:cpu)cpuTotal+=x;for(double x:wall)wallTotal+=x;
        std::sort(cpu.begin(),cpu.end());std::sort(wall.begin(),wall.end());
        std::cout<<"BENCH "<<length<<" taps / 64 frames: CPU mean "<<cpuTotal/cpu.size()<<" us, p99 "<<cpu[cpu.size()*99/100]<<" us, max "<<cpu.back()<<" us; wall mean "<<wallTotal/wall.size()<<" us, p99 "<<wall[wall.size()*99/100]<<" us, max "<<wall.back()<<" us\n";
    }
    std::cout<<"Timing is observational Linux cloud evidence, not a Windows realtime guarantee\n";
}
}
int main(int argc,char**argv) {
    try {
        if(argc>1&&std::string(argv[1])=="--benchmark"){benchmark();return 0;}
        fftTests();boundaryAndBlockTests();denseReferenceAndPartitionTests();maximumLengthTest();
        require(allocationProbe::count.load()==0,"Scheduler allocated during processing/reset");
        std::cout<<"PASS scheduler: "<<assertions<<" assertions, zero processing/reset allocations\n";
        return 0;
    }catch(const std::exception&e){allocationProbe::enabled.store(false);std::cerr<<"FAIL scheduler: "<<e.what()<<'\n';return 1;}
}
