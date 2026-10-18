#include "../Source/DSP/FinishingCore.h"
#include <iostream>
#include <chrono>
#include <cstdlib>
#include <new>
#include <stdexcept>
static bool countAllocations=false;static size_t allocations=0;
void* operator new(std::size_t n){if(countAllocations)++allocations;if(auto* p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void* operator new[](std::size_t n){return ::operator new(n);}
void operator delete(void* p) noexcept{std::free(p);}void operator delete(void* p,std::size_t) noexcept{std::free(p);}
void operator delete[](void* p) noexcept{std::free(p);}void operator delete[](void* p,std::size_t) noexcept{std::free(p);}
namespace reference { using hem::pi;
// Frozen original scalar kernel: independently preserves original ring indexing,
// summation order and all four downsampling FIR evaluations for comparison.
class ScalarReference {
public:
    static constexpr int taps=65,latencySamples=16;
    void prepare() noexcept {
        constexpr double cutoff=.1125;double sum=0;
        for(int i=0;i<taps;++i){const double x=i-32.;
            const double sinc=std::abs(x)<1.e-12?2*cutoff:std::sin(2*pi*cutoff*x)/(pi*x);
            const double window=.42-.5*std::cos(2*pi*i/64)+.08*std::cos(4*pi*i/64);
            coefficients[size_t(i)]=sinc*window;sum+=coefficients[size_t(i)];}
        for(auto& v:coefficients)v/=sum;
        up={};down={};dry={};position={};dryPosition={};
    }
    void resetHistory() noexcept {up={};down={};dry={};position={};dryPosition={};}
    float tick(float x,int channel,float drive,float bias,float amount,bool residual=false) noexcept {
        const auto ch=size_t(channel);auto& dp=dryPosition[ch];
        const float delayed=dry[ch][size_t(dp)];dry[ch][size_t(dp)]=x;dp=(dp+1)%latencySamples;
        auto& pos=position[ch];float result=0;
        const double zero=std::tanh(double(bias));
        const double slope=std::max(.1,1-zero*zero);
        for(int phase=0;phase<4;++phase){
            up[ch][size_t(pos)]=phase==0?x*4:0;
            double interpolated=0;
            for(int k=0;k<taps;++k)interpolated+=coefficients[size_t(k)]*up[ch][size_t((pos-k+taps)%taps)];
            const double shaped=(std::tanh(interpolated*drive+bias)-zero)/(drive*slope);
            down[ch][size_t(pos)]=residual?shaped-interpolated:shaped;
            double filtered=0;
            for(int k=0;k<taps;++k)filtered+=coefficients[size_t(k)]*down[ch][size_t((pos-k+taps)%taps)];
            if(phase==0)result=float(filtered);
            pos=(pos+1)%taps;
        }
        return residual?amount*result:delayed+amount*(result-delayed);
    }
private:
    std::array<double,taps> coefficients{};
    std::array<std::array<double,taps>,2> up{},down{};
    std::array<std::array<float,latencySamples>,2> dry{};
    std::array<int,2> position{},dryPosition{};
};
}
static void require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
static volatile double sink=0;
template<class Core> double benchmark(){Core core;core.prepare();double checksum=0;constexpr int samples=250000;
 auto start=std::chrono::steady_clock::now();
 for(int i=0;i<samples;++i){const float x=float((i*17)%1024-512)/512.f;
 checksum+=core.tick(x,0,8,.25f,1,false);checksum+=core.tick(-x*.73f,1,8,.25f,1,false);}
 sink=checksum;return std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();}
int main(int argc,char**){try{
 double maximumError=0;size_t comparisons=0;unsigned random=3245234;
 for(bool residual:{false,true})for(int scenario=0;scenario<4;++scenario){
  hem::OversampledSaturator actual;reference::ScalarReference expected;actual.prepare();expected.prepare();
  for(int n=0;n<40000;++n){if(n==21713){actual.resetHistory();expected.resetHistory();}
   const float drive=scenario==0?1.f:scenario==1?15.848932f:1.f+float(n%101)*.14848932f;
   const float bias=scenario==0?0.f:scenario==1?-.5f:float(n%51)*.02f-.5f;
   const float amount=scenario==0?0.f:scenario==1?1.f:float(n%27)/26.f;
   for(int ch=0;ch<2;++ch){random=1664525u*random+1013904223u;
    const float x=n>39000?0.f:scenario==0?(n==0?1.f:0.f):scenario==1?float(std::sin(n*2*hem::pi*.431+ch)):
     scenario==2?(float(random>>8)/8388608.f-1.f)*2.f:float(std::sin(n*2*hem::pi*.017+ch))*.5f;
    countAllocations=true;const float got=actual.tick(x,ch,drive,bias,amount,residual);countAllocations=false;
    const float ref=expected.tick(x,ch,drive,bias,amount,residual);
    const double error=std::abs(double(got)-ref);maximumError=std::max(maximumError,error);++comparisons;
    require(std::isfinite(got)&&error<=2.e-6*std::max(1.,std::abs(double(ref))),"Scalar/SIMD mismatch");
    if(scenario==0&&!residual&&n<100)require(got==(n==16?1.f:0.f),"Dry impulse latency changed");
   }
  }
 }
 require(allocations==0,"Tick allocated");
 std::cout<<"PASS "<<comparisons<<" scalar comparisons; max absolute error "<<maximumError<<"; reset, tail, channel isolation, 16-sample dry latency; allocations "<<allocations<<"\n";
 if(argc>1){for(int trial=0;trial<3;++trial){double scalar=benchmark<reference::ScalarReference>(),simd=benchmark<hem::OversampledSaturator>();
 std::cout<<"BENCH stereo 250000 frames scalar_ms="<<scalar<<" optimized_ms="<<simd<<" speedup="<<scalar/simd<<" checksum="<<sink<<"\n";}}
 return 0;
 }catch(const std::exception& e){countAllocations=false;std::cerr<<e.what()<<"\n";return 1;}}
