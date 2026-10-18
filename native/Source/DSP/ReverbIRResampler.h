#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <vector>
#include <cmath>
namespace hem::reverb {
// Preparation-only, finite-support resampling of FIR coefficients. Coefficients
// are treated as weighted impulses. Complete Blackman-windowed sinc support is
// retained at BOTH boundaries, including a one-sample IR. Every polyphase kernel
// has unit sum, preserving the DC gain rather than losing onset/tail energy.
// A fixed 4 ms causal offset fits the worst support radius for supported rates
// and size ranges; the processor delays dry by and reports that same amount.
inline int convolutionLatency(double sampleRate)noexcept{return int(std::ceil(sampleRate*.004));}
inline int resampledIRFrames(int sourceFrames,double effectiveSourceRate,double targetRate)noexcept{const int latency=convolutionLatency(targetRate);if(juce::approximatelyEqual(effectiveSourceRate,targetRate))return sourceFrames+latency;const double ratio=targetRate/effectiveSourceRate;return int(std::ceil((sourceFrames-1)*ratio+latency+16*std::max(1.,ratio)))+1;}
inline juce::AudioBuffer<float> resampleIR(const juce::AudioBuffer<float>&source,double effectiveSourceRate,double targetRate){
    const int latency=convolutionLatency(targetRate);
    if(juce::approximatelyEqual(effectiveSourceRate,targetRate)){
        juce::AudioBuffer<float>out(source.getNumChannels(),source.getNumSamples()+latency);out.clear();
        for(int c=0;c<source.getNumChannels();++c)out.copyFrom(c,latency,source,c,0,source.getNumSamples());return out;
    }
    constexpr int phases=1024;
    constexpr double pi=3.1415926535897932384626433832795;
    const double ratio=targetRate/effectiveSourceRate,band=1/std::max(1.,ratio),radius=16/band;
    const int reach=int(std::ceil(radius))+1,width=2*reach+1;
    const int length=resampledIRFrames(source.getNumSamples(),effectiveSourceRate,targetRate);
    std::vector<float>kernels(size_t(phases+1)*size_t(width));
    for(int phase=0;phase<=phases;++phase){const double fraction=double(phase)/phases;double sum=0;auto*kernel=kernels.data()+size_t(phase)*size_t(width);
        for(int tap=-reach;tap<=reach;++tap){const double d=tap-fraction,normalized=d/radius;double value=0;if(std::abs(normalized)<1){const double sinc=std::abs(d)<1.e-14?1:std::sin(pi*d*band)/(pi*d*band);const double window=.42+.5*std::cos(pi*normalized)+.08*std::cos(2*pi*normalized);value=band*sinc*window;}kernel[tap+reach]=float(value);sum+=value;}
        for(int tap=0;tap<width;++tap)kernel[tap]=float(kernel[tap]/sum);
    }
    juce::AudioBuffer<float>out(source.getNumChannels(),length);out.clear();
    for(int n=0;n<source.getNumSamples();++n){const double position=n*ratio+latency;const int centre=int(std::floor(position));const double phasePosition=(position-centre)*phases;const int phase=std::min(phases-1,int(phasePosition));const float blend=float(phasePosition-phase);const auto*a=kernels.data()+size_t(phase)*size_t(width);const auto*b=a+width;
        const int begin=std::max(-reach,-centre),end=std::min(reach,length-centre-1);
        for(int ch=0;ch<source.getNumChannels();++ch){const float coefficient=source.getSample(ch,n);if(coefficient==0)continue;auto*destination=out.getWritePointer(ch)+centre;for(int tap=begin;tap<=end;++tap){const int k=tap+reach;destination[tap]+=coefficient*(a[k]+blend*(b[k]-a[k]));}}
    }
    return out;
}
}
