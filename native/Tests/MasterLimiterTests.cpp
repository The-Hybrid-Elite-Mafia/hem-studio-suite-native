#include "../Source/DSP/MasterLimiter.h"
#include <iostream>
#include <random>
#include <cstdlib>
#include <new>
static bool inProcess=false;static int allocations=0;
void* operator new(std::size_t n){if(inProcess)++allocations;if(auto* p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void* operator new[](std::size_t n){return ::operator new(n);}
void operator delete(void* p) noexcept{std::free(p);}void operator delete[](void* p) noexcept{std::free(p);}
void operator delete(void* p,std::size_t) noexcept{std::free(p);}void operator delete[](void* p,std::size_t) noexcept{std::free(p);}
void require(bool b,const char* text){if(!b)throw std::runtime_error(text);}
int main(){try{
 hem::Reconstruction reconstruction;
 for(double rate:{44100.,48000.,96000.}){
    hem::MasterLimiter limiter;limiter.prepare(rate);
    const int n=24000;std::vector<float> left(n),right(n);std::mt19937 generator(71);std::uniform_real_distribution<float> random(-1,1);
    for(int i=0;i<n/2;++i){
        // Thirteen coincident full-level stems followed by a +12 dB monitor
        // boost, including independent stereo noise and high-frequency peaks.
        left[size_t(i)]=13*hem::gain(12)*random(generator);
        right[size_t(i)]=13*hem::gain(12)*float(std::sin(2*hem::pi*.477*i));
    }
    left[100]=std::numeric_limits<float>::quiet_NaN();right[500]=std::numeric_limits<float>::infinity();
    for(int start=0;start<n;start+=137){inProcess=true;limiter.process(left.data()+start,right.data()+start,std::min(137,n-start));inProcess=false;}
    require(limiter.invalidSampleCount()==2,"Non-finite input not contained");
    double maximum=0;
    for(int i=0;i<n;++i){require(std::isfinite(left[size_t(i)])&&std::isfinite(right[size_t(i)]),"Non-finite output");
        maximum=std::max({maximum,double(std::abs(left[size_t(i)])),double(std::abs(right[size_t(i)]))});
        for(const auto& h:reconstruction.h){double l=0,r=0;for(int k=0;k<32;++k)if(i>=k){l+=h[size_t(k)]*left[size_t(i-k)];r+=h[size_t(k)]*right[size_t(i-k)];}maximum=std::max({maximum,std::abs(l),std::abs(r)});}
    }
    require(maximum<=hem::gain(-1)+1.e-6,"Master ceiling exceeded");
    std::cout<<rate<<" Hz: 13-stem +12 dB overload <= "<<hem::decibels(float(maximum))<<" dBTP\n";
    limiter.prepare(rate);std::fill(left.begin(),left.end(),0);std::fill(right.begin(),right.end(),0);left[0]=right[0]=.05f;
    limiter.process(left.data(),right.data(),n);require(std::abs(left[size_t(limiter.latencySamples())]-.05f)<1.e-6,"Quiet impulse/latency failure");
 }
 require(allocations==0,"Audio callback allocated memory");std::cout<<"PASS: linked final master, 8x ceiling, non-finite containment, latency, zero process allocations\n";
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}return 0;}
