#pragma once
#include "DspCore.h"
#include <complex>
#if defined(_M_X64) || defined(__SSE2__)
#include <emmintrin.h>
#define HEM_BAND_STEREO_SSE2 1
#endif

namespace hem::bands {
// Audio-thread-owned scalar storage. No JUCE coefficient objects or heap work.
struct Coefficients {
    double b0=1,b1=0,b2=0,a1=0,a2=0;
    std::complex<double> response(double radians) const noexcept {
        const auto z=std::polar(1.,-radians);
        return (b0+b1*z+b2*z*z)/(1.+a1*z+a2*z*z);
    }
};
struct Section {
    Coefficients c;
    // Direct form I is part of the time-varying contract: coefficient updates
    // retain literal past inputs/outputs, not coefficient-dependent DF2T states.
    std::array<double,2> x1{},x2{},y1{},y2{};
    double tick(double x,size_t channel) noexcept {
        const double y=c.b0*x+c.b1*x1[channel]+c.b2*x2[channel]
                      -c.a1*y1[channel]-c.a2*y2[channel];
        x2[channel]=x1[channel];x1[channel]=x;
        y2[channel]=y1[channel];y1[channel]=y;
        return y;
    }
    void reset(size_t channel) noexcept {x1[channel]=x2[channel]=y1[channel]=y2[channel]=0;}
#if HEM_BAND_STEREO_SSE2
    // Independent L/R lanes, never adjacent time samples. Keep DF1 operation
    // order and double precision identical to tick(), including subtraction.
    __m128d tickStereo(__m128d x) noexcept {
        auto y=_mm_add_pd(_mm_mul_pd(_mm_set1_pd(c.b0),x),_mm_mul_pd(_mm_set1_pd(c.b1),_mm_loadu_pd(x1.data())));
        y=_mm_add_pd(y,_mm_mul_pd(_mm_set1_pd(c.b2),_mm_loadu_pd(x2.data())));
        y=_mm_sub_pd(y,_mm_mul_pd(_mm_set1_pd(c.a1),_mm_loadu_pd(y1.data())));
        y=_mm_sub_pd(y,_mm_mul_pd(_mm_set1_pd(c.a2),_mm_loadu_pd(y2.data())));
        x2=x1;_mm_storeu_pd(x1.data(),x);y2=y1;_mm_storeu_pd(y1.data(),y);return y;
    }
#endif
};
struct SplitCoefficients {Coefficients low,high,allpass;};
inline SplitCoefficients designSplit(double rate,double frequency) noexcept {
    const double w=2*pi*std::clamp(frequency,1.,rate*.45)/rate;
    const double cs=std::cos(w),alpha=std::sin(w)/std::sqrt(2.),d=1+alpha;
    const double a1=-2*cs/d,a2=(1-alpha)/d;
    return {{(1-cs)/(2*d),(1-cs)/d,(1-cs)/(2*d),a1,a2},
            {(1+cs)/(2*d),-(1+cs)/d,(1+cs)/(2*d),a1,a2},
            {a2,a1,1,a1,a2}};
}
// Four-band extension. The separate three-band class below is intentionally
// retained for exact legacy-session reconstruction and migration qualification.
class FourBandBank {
public:
    using Frame=std::array<std::array<double,2>,4>;
    bool prepare(double rate,double low=200,double middle=1000,double high=2500) noexcept {
        ready=false;sections={};phase=0;cached={-1,-1,-1};
        if(!std::isfinite(rate)||rate<8000||rate>384000)return false;
        sampleRate=rate;requested={200,1000,2500};targets(low,middle,high);
        const auto effective=constrained(requested);
        for(size_t i=0;i<3;++i)ramps[i].reset(rate,float(effective[i]));
        configure(effective);ready=true;return true;
    }
    void targets(double low,double middle,double high) noexcept {
        if(std::isfinite(low))requested[0]=std::clamp(low,80.,800.);
        if(std::isfinite(middle))requested[1]=std::clamp(middle,20.,18000.);
        if(std::isfinite(high))requested[2]=std::clamp(high,1200.,8000.);
        const auto effective=constrained(requested);
        for(size_t i=0;i<3;++i)ramps[i].set(float(effective[i]));
    }
    std::array<double,3> actualCutoffs() const noexcept {
        return {std::min(cached[0],sampleRate*.45),std::min(cached[1],sampleRate*.45),std::min(cached[2],sampleRate*.45)};
    }
    void resetChannel(size_t channel) noexcept {for(auto& s:sections)s.reset(channel);}
    template<bool StereoSimd=false> Frame tick(double left,double right) noexcept {
        Frame result{};if(!ready)return result;
        std::array<double,3> current{};for(size_t i=0;i<3;++i)current[i]=ramps[i].next();
        current=constrained(current);if(phase==0)configure(current);phase=(phase+1)&15;
        const std::array<double,2> input{left,right};
#if HEM_BAND_STEREO_SSE2
        if constexpr(StereoSimd){
            auto safe=input;for(size_t ch=0;ch<2;++ch)if(!std::isfinite(safe[ch])){resetChannel(ch);safe[ch]=0;}
            const auto x=_mm_loadu_pd(safe.data());
            const auto low=sections[1].tickStereo(sections[0].tickStereo(x));
            const auto upper=sections[3].tickStereo(sections[2].tickStereo(x));
            const auto lowMid=sections[7].tickStereo(sections[6].tickStereo(upper));
            const auto upperTwo=sections[9].tickStereo(sections[8].tickStereo(upper));
            _mm_storeu_pd(result[0].data(),sections[5].tickStereo(sections[4].tickStereo(low)));
            _mm_storeu_pd(result[1].data(),sections[10].tickStereo(lowMid));
            _mm_storeu_pd(result[2].data(),sections[12].tickStereo(sections[11].tickStereo(upperTwo)));
            _mm_storeu_pd(result[3].data(),sections[14].tickStereo(sections[13].tickStereo(upperTwo)));
            for(size_t ch=0;ch<2;++ch){bool finite=true;for(const auto& band:result)finite=finite&&std::isfinite(band[ch]);
                if(!finite){resetChannel(ch);for(auto& band:result)band[ch]=0;}}
            return result;
        }
#endif
        for(size_t ch=0;ch<2;++ch){double x=input[ch];if(!std::isfinite(x)){resetChannel(ch);x=0;}
            const auto low=sections[1].tick(sections[0].tick(x,ch),ch);
            const auto upper=sections[3].tick(sections[2].tick(x,ch),ch);
            const auto lowMid=sections[7].tick(sections[6].tick(upper,ch),ch);
            const auto upperTwo=sections[9].tick(sections[8].tick(upper,ch),ch);
            result[0][ch]=sections[5].tick(sections[4].tick(low,ch),ch);
            result[1][ch]=sections[10].tick(lowMid,ch);
            result[2][ch]=sections[12].tick(sections[11].tick(upperTwo,ch),ch);
            result[3][ch]=sections[14].tick(sections[13].tick(upperTwo,ch),ch);
            bool finite=true;for(const auto& band:result)finite=finite&&std::isfinite(band[ch]);
            if(!finite){resetChannel(ch);for(auto& band:result)band[ch]=0;}
        }return result;
    }
    std::array<std::complex<double>,4> response(double hz) const noexcept {
        const double w=2*pi*hz/sampleRate;
        const auto l1=sections[0].c.response(w),h1=sections[2].c.response(w);
        const auto l2=sections[6].c.response(w),h2=sections[8].c.response(w);
        const auto l3=sections[11].c.response(w),h3=sections[13].c.response(w);
        const auto a2=sections[4].c.response(w),a3=sections[5].c.response(w);
        return {l1*l1*a2*a3,h1*h1*l2*l2*a3,h1*h1*h2*h2*l3*l3,h1*h1*h2*h2*h3*h3};
    }
private:
    std::array<double,3> constrained(std::array<double,3> values) const noexcept {
        // Constrain against the endpoints actually used by the coefficient
        // designer, including the safety-rate domain below 44.1 kHz.
        values[0]=std::min(values[0],sampleRate*.45);
        values[2]=std::min(values[2],sampleRate*.45);
        values[1]=std::clamp(values[1],values[0]*1.05,values[2]/1.05);return values;
    }
    void configure(const std::array<double,3>& values) noexcept {
        if(values[0]!=cached[0]){const auto c=designSplit(sampleRate,values[0]);sections[0].c=sections[1].c=c.low;sections[2].c=sections[3].c=c.high;}
        if(values[1]!=cached[1]){const auto c=designSplit(sampleRate,values[1]);sections[4].c=c.allpass;sections[6].c=sections[7].c=c.low;sections[8].c=sections[9].c=c.high;}
        if(values[2]!=cached[2]){const auto c=designSplit(sampleRate,values[2]);sections[5].c=sections[10].c=c.allpass;sections[11].c=sections[12].c=c.low;sections[13].c=sections[14].c=c.high;}
        cached=values;
    }
    std::array<Section,15> sections{};std::array<Ramp,3> ramps;
    std::array<double,3> requested{200,1000,2500},cached{-1,-1,-1};
    double sampleRate=0;unsigned phase=0;bool ready=false;
};
class ThreeBandBank {
public:
    // Caller must establish a block-level no-denormals scope, matching the
    // existing JUCE processBlock convention. Do not change FP state per sample.
    using Frame=std::array<std::array<double,2>,3>;
    bool prepare(double rate,double low=200,double high=2500) noexcept {
        ready=false;sections={};phase=0;cachedLow=cachedHigh=-1;
        if(!std::isfinite(rate)||rate<8000||rate>384000)return false;
        sampleRate=rate;
        lowTarget=std::isfinite(low)?std::clamp(low,80.,800.):200.;
        highTarget=std::isfinite(high)?std::clamp(high,1200.,8000.):2500.;
        lowRamp.reset(rate,float(lowTarget));highRamp.reset(rate,float(highTarget));
        configure(lowTarget,highTarget);ready=true;return true;
    }
    void targets(double low,double high) noexcept {
        if(std::isfinite(low))lowTarget=std::clamp(low,80.,800.);
        if(std::isfinite(high))highTarget=std::clamp(high,1200.,8000.);
        lowRamp.set(float(lowTarget));highRamp.set(float(highTarget));
    }
    void resetChannel(size_t channel) noexcept {for(auto& section:sections)section.reset(channel);}
    template<bool StereoSimd=false> Frame tick(double left,double right) noexcept {
        Frame result{};if(!ready)return result;
        const double low=lowRamp.next(),high=highRamp.next();
        if(phase==0)configure(low,high);phase=(phase+1)&15;
        const std::array<double,2> input{left,right};
#if HEM_BAND_STEREO_SSE2
        if constexpr(StereoSimd){
            auto safe=input;for(size_t ch=0;ch<2;++ch)if(!std::isfinite(safe[ch])){resetChannel(ch);safe[ch]=0;}
            const auto x=_mm_loadu_pd(safe.data());
            const auto lower=sections[1].tickStereo(sections[0].tickStereo(x));
            const auto upper=sections[3].tickStereo(sections[2].tickStereo(x));
            _mm_storeu_pd(result[0].data(),sections[4].tickStereo(lower));
            _mm_storeu_pd(result[1].data(),sections[6].tickStereo(sections[5].tickStereo(upper)));
            _mm_storeu_pd(result[2].data(),sections[8].tickStereo(sections[7].tickStereo(upper)));
            for(size_t ch=0;ch<2;++ch)if(!std::isfinite(result[0][ch])||!std::isfinite(result[1][ch])||!std::isfinite(result[2][ch])){
                resetChannel(ch);for(auto& band:result)band[ch]=0;}
            return result;
        }
#endif
        for(size_t ch=0;ch<2;++ch){
            double x=input[ch];if(!std::isfinite(x)){resetChannel(ch);x=0;}
            const double lower=sections[1].tick(sections[0].tick(x,ch),ch);
            const double upper=sections[3].tick(sections[2].tick(x,ch),ch);
            result[0][ch]=sections[4].tick(lower,ch);
            result[1][ch]=sections[6].tick(sections[5].tick(upper,ch),ch);
            result[2][ch]=sections[8].tick(sections[7].tick(upper,ch),ch);
            if(!std::isfinite(result[0][ch])||!std::isfinite(result[1][ch])||!std::isfinite(result[2][ch])){
                resetChannel(ch);for(auto& band:result)band[ch]=0;
            }
        }
        return result;
    }
    std::array<std::complex<double>,3> response(double hz) const noexcept {
        const double w=2*pi*hz/sampleRate;
        const auto lp1=sections[0].c.response(w),hp1=sections[2].c.response(w);
        const auto ap2=sections[4].c.response(w),lp2=sections[5].c.response(w),hp2=sections[7].c.response(w);
        return {ap2*lp1*lp1,lp2*lp2*hp1*hp1,hp2*hp2*hp1*hp1};
    }
private:
    void configure(double low,double high) noexcept {
        if(low!=cachedLow){const auto c=designSplit(sampleRate,low);sections[0].c=sections[1].c=c.low;
            sections[2].c=sections[3].c=c.high;cachedLow=low;}
        if(high!=cachedHigh){const auto c=designSplit(sampleRate,high);sections[4].c=c.allpass;
            sections[5].c=sections[6].c=c.low;sections[7].c=sections[8].c=c.high;cachedHigh=high;}
    }
    std::array<Section,9> sections{};
    Ramp lowRamp,highRamp;
    double sampleRate=0,lowTarget=200,highTarget=2500,cachedLow=-1,cachedHigh=-1;
    unsigned phase=0;bool ready=false;
};
}
