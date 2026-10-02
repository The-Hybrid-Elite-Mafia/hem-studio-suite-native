#pragma once
// Dependency-free DSP kernels. JUCE processors and numerical tests use these
// SAME implementations. All storage is reserved in prepare(), never process().
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <vector>

namespace hem {
constexpr double pi = 3.14159265358979323846;
inline float gain(float db) noexcept { return std::pow(10.0f, db / 20.0f); }
inline float decibels(float x) noexcept { return 20.0f * std::log10(std::max(x, 1.0e-12f)); }
struct Ramp {
    float value=0, target=0, step=0; int remaining=0, length=960;
    void reset(double rate, float initial) noexcept { length=std::max(1,int(rate*.01));value=target=initial;remaining=0; }
    void set(float v) noexcept { if(v!=target){target=v;remaining=length;step=(target-value)/float(length);} }
    float next() noexcept { if(remaining>0){value+=step;if(--remaining==0)value=target;}return value; }
};
struct Biquad {
    enum Type { HighPass, LowPass, Bell, HighShelf };
    double b0=1,b1=0,b2=0,a1=0,a2=0;
    std::array<double,2> z1{},z2{};
    void reset() noexcept { z1={};z2={}; }
    void configure(Type,double rate,float hz,float q,float db=0) noexcept;
    float tick(float x,int channel) noexcept {
        const auto i=static_cast<size_t>(channel);
        const double y=b0*x+z1[i];z1[i]=b1*x-a1*y+z2[i];z2[i]=b2*x-a2*y;
        return static_cast<float>(y);
    }
    float magnitude(double hz,double rate) const noexcept;
};
class HostDelay {
public:
    void prepare(int samples) { delay=std::max(0,samples);for(auto& c:ring)c.assign(static_cast<size_t>(delay+1),0);pos=0; }
    void process(float* l,float* r,int n) noexcept;
private:
    std::array<std::vector<float>,2> ring; int pos=0,delay=0;
};
struct StereoParameters { float width=100,monoBass=120,balance=0,output=0; bool bassMono=false,mono=false,invertL=false,invertR=false,swap=false; };
class StereoCore {
public:
    void prepare(double rate); void process(float*,float*,int,const StereoParameters&) noexcept;
    std::atomic<float> correlation{0},peak{0};
private:
    double sr=96000;Biquad sideHighPass;Ramp width,cutoff,balance,output,bassMix,monoMix,inversionL,inversionR,swapMix;
};
struct EqParameters {
    float cutoff=30;bool highPass=false;int slope=0;
    std::array<float,4> hz{300,1500,6500,12000},db{0,0,0,0},q{1,1,2,.7071f};float output=0;
};
class EqCore {
public:
    void prepare(double); void process(float*,float*,int,const EqParameters&) noexcept;
    static float response(double hz,double rate,const EqParameters&) noexcept;
    std::atomic<float> peak{0};
private:
    double sr=96000; std::array<Biquad,4> hp,eq;
    std::array<Ramp,4> frequencies,gains,qs,hpMix;
    Ramp cutoff,output; int controlPhase=0;
};
struct CompressorParameters { float threshold=0,ratio=1,attack=30,release=100,knee=0,makeup=0,mix=100,sidechain=80;bool scEnabled=false,autoRelease=false; };
class CompressorCore {
public:
    void prepare(double);void process(float*,float*,int,const CompressorParameters&) noexcept;
    std::atomic<float> reduction{0},peak{0};
private:
    double sr=96000;Biquad detector;float envelopeDb=0,programme=0;
    Ramp threshold,ratio,attack,release,knee,makeup,mix,cutoff,scMix;
};
// The meter's explicitly defined 8x, 32-tap, windowed-sinc reconstruction.
// Coefficients sum to one for each fractional phase.
struct Reconstruction {
    static constexpr int taps=32,phases=8;
    std::array<std::array<double,taps>,phases> h{};
    Reconstruction();
};
struct LimiterParameters { float input=0,ceiling=-1,release=100; };
class LimiterCore {
public:
    void prepare(double);void process(float*,float*,int,const LimiterParameters&) noexcept;
    int latency() const noexcept { return delay; }
    std::atomic<float> reduction{0},truePeak{0};
private:
    Reconstruction filter;
    double sr=96000; int delay=480,size=0,position=0;
    std::array<std::vector<float>,2> audio;
    std::vector<float> budgets;
    std::array<std::array<float,Reconstruction::taps>,2> history{},meterHistory{};
    int histPos=0,meterPos=0;
    Ramp input,ceiling,release;
    float previousGain=1;
};
} // namespace hem
