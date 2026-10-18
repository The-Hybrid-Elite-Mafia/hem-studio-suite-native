#pragma once
#include "FinishingCore.h"
#include "PreparedConvolution.h"

namespace hem {
// Original designs, not measured or branded hardware emulations. Units are dB
// for gain/tone/master/output and [0,1] for mic geometry. All stages default off.
struct AmpCabinetSettings {
    int ampModel=0, cabModel=0, micModel=0;
    bool ampEnabled=false, cabEnabled=false, micEnabled=false, bypass=false;
    float gain=0, bass=0, mid=0, treble=0, master=0, output=0;
    float micPosition=0, micDistance=0, micAngle=0;
};
class AmpCabinetCore {
public:
    void prepare(double rate) noexcept;
    void process(float* left,float* right,int count,const AmpCabinetSettings&) noexcept;
    int latency() const noexcept { return 32+cabDelay; }
    void setCustomIR(reverb::PreparedConvolution* p) noexcept { custom=p; }
    // Analytic magnitude of SYNTHETIC cabinet / modeled microphone only.
    static float response(double hz,double rate,const AmpCabinetSettings&,bool includeMic=true) noexcept;
    std::array<std::atomic<float>,2> inputPeak{},outputPeak{};
private:
    struct Model { OversampledSaturator pre,power; Biquad coupling,dc; };
    struct Cabinet { Biquad hp,resonance,lp; };
    std::array<Model,3> amps;
    std::array<bool,3> running{};
    std::array<Cabinet,3> cabinets;
    Biquad bass,mid,treble,micLow,micHigh;
    std::array<std::array<float,32>,2> dry{};
    unsigned position=0,phase=0;
    double sr=48000;
    Controls<21> controls;
    reverb::PreparedConvolution* custom=nullptr;
    int cabDelay=192, cabPosition=0;
    std::array<std::array<float,768>,2> aligned{},bypassDelay{};
    static AmpCabinetSettings sanitized(AmpCabinetSettings) noexcept;
    static void configureCab(Cabinet&,int,double) noexcept;
    static void configureMic(Biquad&,Biquad&,double,int,float,float,float) noexcept;
};
}


