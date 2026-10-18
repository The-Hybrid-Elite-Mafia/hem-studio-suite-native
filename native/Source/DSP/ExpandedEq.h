#pragma once
#include "BandModulesCore.h"
#include "AccurateFma.h"
namespace hem::bands {
// Error-free product/sum residuals retain DF-I output history through extreme
// resonant cascades. Coefficients are doubles; histories and interstage samples
// carry normalized high/low double pairs through the complete EQ cascade. This
// changes numerical precision, not topology, coefficient cadence or gain laws.
struct EqAccumulator {
    double high=0,low=0;
    static EqAccumulator product(double a,double high,double low=0) noexcept {
        const double p=a*high,error=accurate::multiplyAdd(a,high,-p)+a*low;
        const double sum=p+error;return {sum,error-(sum-p)};
    }
    void add(EqAccumulator value) noexcept {
        const double sum=high+value.high,part=sum-high;
        const double error=(high-(sum-part))+(value.high-part)+low+value.low;
        high=sum+error;low=error-(high-sum);
    }
    double value() const noexcept {return high+low;}
};
inline EqAccumulator operator+(EqAccumulator a,EqAccumulator b) noexcept {a.add(b);return a;}
inline EqAccumulator operator-(EqAccumulator a,EqAccumulator b) noexcept {a.add({-b.high,-b.low});return a;}
inline EqAccumulator operator*(EqAccumulator a,EqAccumulator b) noexcept {
    auto result=EqAccumulator::product(a.high,b.high,b.low);
    result.add(EqAccumulator::product(a.low,b.high,b.low));return result;
}
inline EqAccumulator operator/(EqAccumulator a,EqAccumulator b) noexcept {
    const double first=a.high/b.high;const auto residual=a-b*EqAccumulator{first,0};
    return EqAccumulator{first,0}+EqAccumulator{residual.value()/b.high,0};
}
inline Coefficients designEqualizer(double rate,double hz,double q,double db,int type) noexcept {
    if(type==0)return designBell(rate,hz,q,db);
    const double w=2*pi*std::clamp(hz,1.,rate*.45)/rate,a=std::pow(10.,db/40.);
    // Q is resonant damping, not RBJ shelf slope S:
    // alpha=sin(w)/(2*Q), beta=2*sqrt(A)*alpha. High Q may overshoot.
    using W=EqAccumulator;const W one{1,0},two{2,0},amplitude{a,0};
    const W halfSin{std::sin(w*.5),0},halfCos{std::cos(w*.5),0};
    const W cosine=one-two*halfSin*halfSin;
    const W initialRoot{std::sqrt(a),0};
    const W root=initialRoot+(amplitude-initialRoot*initialRoot)/(two*initialRoot);
    const W beta=root*two*halfSin*halfCos/W{q,0},plus=amplitude+one,minus=amplitude-one;
    if(type==1){const W denominator=plus+minus*cosine+beta;
        return {(amplitude*(plus-minus*cosine+beta)/denominator).value(),(two*amplitude*(minus-plus*cosine)/denominator).value(),
                (amplitude*(plus-minus*cosine-beta)/denominator).value(),(W{-2,0}*(minus+plus*cosine)/denominator).value(),((plus+minus*cosine-beta)/denominator).value()};}
    const W denominator=plus-minus*cosine+beta;
    return {(amplitude*(plus+minus*cosine+beta)/denominator).value(),(W{-2,0}*amplitude*(minus+plus*cosine)/denominator).value(),
            (amplitude*(plus+minus*cosine-beta)/denominator).value(),(two*(minus-plus*cosine)/denominator).value(),((plus-minus*cosine-beta)/denominator).value()};
}
struct TypedSection {
    Section section;std::array<double,2> firstResidual{},secondResidual{},firstInputResidual{},secondInputResidual{};
    double frequency=-1,q=-1,gain=999;int type=-1;bool identityClean=true;
    void configure(double rate,double hz,double width,double db,int shape) noexcept {
        if(frequency!=hz||q!=width||gain!=db||type!=shape){section.c=designEqualizer(rate,hz,width,db,shape);frequency=hz;q=width;gain=db;type=shape;}
        if(db!=0)identityClean=false;
    }
    EqAccumulator tickAccurate(EqAccumulator input,size_t channel) noexcept {
        if(gain==0&&identityClean){section.x2[channel]=section.y2[channel]=section.x1[channel];section.x1[channel]=section.y1[channel]=input.high;
            secondInputResidual[channel]=secondResidual[channel]=firstInputResidual[channel];firstInputResidual[channel]=firstResidual[channel]=input.low;return input;}
        const auto& c=section.c;
        auto output=EqAccumulator::product(c.b0,input.high,input.low);
        output.add(EqAccumulator::product(c.b1,section.x1[channel],firstInputResidual[channel]));
        output.add(EqAccumulator::product(c.b2,section.x2[channel],secondInputResidual[channel]));
        output.add(EqAccumulator::product(-c.a1,section.y1[channel],firstResidual[channel]));
        output.add(EqAccumulator::product(-c.a2,section.y2[channel],secondResidual[channel]));
        section.x2[channel]=section.x1[channel];section.x1[channel]=input.high;
        secondInputResidual[channel]=firstInputResidual[channel];firstInputResidual[channel]=input.low;
        section.y2[channel]=section.y1[channel];secondResidual[channel]=firstResidual[channel];
        section.y1[channel]=output.high;firstResidual[channel]=output.low;
        return output;
    }
    double tick(double input,size_t channel) noexcept {return tickAccurate({input,0},channel).value();}
    void reset(size_t channel) noexcept {section.reset(channel);firstResidual[channel]=secondResidual[channel]=firstInputResidual[channel]=secondInputResidual[channel]=0;}
};
// Choice changes finish their current transition before accepting the latest
// queued choice. A rapid third choice cannot abruptly discard an audible path.
struct MorphingEq {
    TypedSection primary,secondary;Ramp fade;double rate=48000;bool transitioning=false;int selected=0;
    void prepare(double sr,int type) noexcept {accurate::prepareFusedMultiplyAdd();rate=sr;primary={};secondary={};selected=type;transitioning=false;fade.reset(sr,0);}
    void configure(double hz,double q,double gain,int desired) noexcept {
        desired=std::clamp(desired,0,2);
        if(!transitioning&&desired!=selected){secondary={};secondary.configure(rate,hz,q,gain,desired);fade.reset(rate,0);fade.set(1);transitioning=true;}
        primary.configure(rate,hz,q,gain,selected);
        if(transitioning)secondary.configure(rate,hz,q,gain,secondary.type);
    }
    double mono(double input) noexcept {
        double out=primary.tick(input,0);if(transitioning){const double blend=fade.next(),other=secondary.tick(input,0);out+=blend*(other-out);finish();}return out;
    }
    std::array<double,2> stereo(std::array<double,2> input) noexcept {
        std::array<double,2> out{primary.tick(input[0],0),primary.tick(input[1],1)};
        if(transitioning){const double blend=fade.next();for(size_t c=0;c<2;++c)out[c]+=blend*(secondary.tick(input[c],c)-out[c]);finish();}return out;
    }
    EqAccumulator monoAccurate(EqAccumulator input) noexcept {
        auto out=primary.tickAccurate(input,0);if(transitioning){const EqAccumulator blend{fade.next(),0};const auto other=secondary.tickAccurate(input,0);out=out+blend*(other-out);finish();}return out;
    }
    std::array<EqAccumulator,2> stereoAccurate(std::array<EqAccumulator,2> input) noexcept {
        std::array<EqAccumulator,2> out{primary.tickAccurate(input[0],0),primary.tickAccurate(input[1],1)};
        if(transitioning){const EqAccumulator blend{fade.next(),0};for(size_t c=0;c<2;++c)out[c]=out[c]+blend*(secondary.tickAccurate(input[c],c)-out[c]);finish();}return out;
    }
    void finish() noexcept {if(fade.remaining==0){std::swap(primary,secondary);selected=primary.type;transitioning=false;fade.reset(rate,0);}}
    void reset(size_t channel) noexcept {primary.reset(channel);secondary.reset(channel);}
    std::array<double,11> coefficients() const noexcept {
        const auto a=primary.section.c,b=secondary.section.c;
        return {a.b0,a.b1,a.b2,a.a1,a.a2,b.b0,b.b1,b.b2,b.a1,b.a2,transitioning?double(fade.value):0.};
    }
};
}

