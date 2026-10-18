#pragma once
#include "BandBankReference.h"
namespace moduleReference {
// Compensated arithmetic for the independently derived bilinear polynomials.
// At 20 Hz, single-rounded coefficients can contribute a measurable phase
// error after eight +12 dB sections. This keeps the oracle's intermediate
// rounding below the production double-coefficient precision.
struct Wide {
    double hi=0,lo=0;
    Wide(double value=0):hi(value){}
    Wide(double h,double l):hi(h),lo(l){}
    double value() const {return hi+lo;}
};
inline Wide operator+(Wide a,Wide b){const double s=a.hi+b.hi,v=s-a.hi;
    const double e=(a.hi-(s-v))+(b.hi-v)+a.lo+b.lo;return {s+e,e-((s+e)-s)};}
inline Wide operator-(Wide a,Wide b){return a+Wide(-b.hi,-b.lo);}
inline Wide operator*(Wide a,Wide b){const double p=a.hi*b.hi,e=std::fma(a.hi,b.hi,-p)+a.hi*b.lo+a.lo*b.hi+a.lo*b.lo;
    return {p+e,e-((p+e)-p)};}
inline Wide operator/(Wide a,Wide b){const double q=a.hi/b.hi;const auto r=a-b*Wide(q);
    return Wide(q)+Wide((r.hi+r.lo)/b.hi);}
inline bandReference::Filter bell(double fs,double hz,double q,double db){
    const Wide t(std::tan(bandReference::pi*std::clamp(hz,1.,fs*.45)/fs)),t2=t*t,a(std::pow(10.,db/40.));
    const Wide d=Wide(1)+t/(Wide(q)*a)+t2;
    bandReference::Filter f;f.a={1,(Wide(2)*(t2-Wide(1))/d).value(),((Wide(1)-t/(Wide(q)*a)+t2)/d).value()};
    f.b={((Wide(1)+t*a/Wide(q)+t2)/d).value(),f.a[1],((Wide(1)-t*a/Wide(q)+t2)/d).value()};return f;
}
inline bandReference::Filter bandpass(double fs,double hz,double q){
    const double t=std::tan(bandReference::pi*std::clamp(hz,1.,fs*.45)/fs),t2=t*t,d=1+t/q+t2;
    bandReference::Filter f;f.a={1,2*(t2-1)/d,(1-t/q+t2)/d};f.b={t/q/d,0,-t/q/d};return f;
}
inline double compression(double level,double threshold,double ratio,double knee,double range){
    const double x=level-threshold;
    double result=0;
    if(knee==0){if(x>0)result=x*(ratio-1)/ratio;}
    else if(x>knee/2)result=x*(ratio-1)/ratio;
    else if(x>-knee/2)result=(ratio-1)/ratio*(x+knee/2)*(x+knee/2)/(2*knee);
    return std::clamp(result,0.,range);
}
struct Engine {
    int kind,count;double rate;unsigned cadence=0;
    std::array<bandReference::Smooth,66> ramps;std::array<bandReference::Smooth,3> selection;
    std::array<bandReference::Filter,8> filters,detectorFilters;
    std::array<double,8> energy{},cut{};bandReference::Bank bank;
    template<class Parameters> Engine(int k,double fs,const Parameters& p):kind(k),count(int(p.size())),rate(fs),bank(fs,k>=2?p[1]:200,k>=2?p[2]:2500){
        for(int i=0;i<count;++i)ramps[size_t(i)].reset(rate,p[size_t(i)]);
        if(kind>=2)for(int b=0;b<3;++b)selection[size_t(b)].reset(rate,selected(p,b));
    }
    template<class P> float selected(const P& p,int b){const int first=kind==2?11:7,step=kind==2?8:3;
        const bool any=p[size_t(first)]>.5f||p[size_t(first+step)]>.5f||p[size_t(first+2*step)]>.5f;
        return !any||p[size_t(first+b*step)]>.5f?1.f:0.f;
    }
    template<class P> void set(const P& p){for(int i=0;i<count;++i)ramps[size_t(i)].set(p[size_t(i)]);
        if(kind>=2){bank.targets(p[1],p[2]);for(int b=0;b<3;++b)selection[size_t(b)].set(selected(p,b));}}
    void configure(bandReference::Filter& dest,const bandReference::Filter& source){dest.a=source.a;dest.b=source.b;}
    double detector(size_t b,double l,double r){energy[b]=std::max({std::abs(l),std::abs(r),energy[b]*std::exp(-1/(rate*.020))});return 20*std::log10(std::max(1e-12,energy[b]));}
    double envelope(size_t b,double request,double attack,double release){const double ms=request>cut[b]?attack:release;
        cut[b]+= (request-cut[b])*(-std::expm1(-1/(rate*ms*.001)));return cut[b];}
    std::array<float,2> tick(float left,float right){
        std::array<float,66> p{};for(int i=0;i<count;++i)p[size_t(i)]=ramps[size_t(i)].tick();
        std::array<double,2> out{left,right};
        if(kind==0){for(size_t b=0;b<8;++b){const size_t i=2+b*8;
            if(cadence==0)configure(detectorFilters[b],bandpass(rate,p[i+1],p[i+3]));
            const double level=detector(b,detectorFilters[b].tick(left,0),detectorFilters[b].tick(right,1));
            const double request=std::clamp(level-p[i+4],0.,double(p[i+5]));
            const double reduction=envelope(b,request,p[i+6],p[i+7]);
            if(cadence==0)configure(filters[b],bell(rate,p[i+1],p[i+3],p[i]*(p[i+2]-reduction)));
            for(size_t c=0;c<2;++c)out[c]=filters[b].tick(out[c],c);
        }}else if(kind==1){out={(double(left)+right)/2,(double(left)-right)/2};
            for(size_t b=0;b<8;++b){const size_t i=2+b*4;if(cadence==0)configure(filters[b],bell(rate,p[i+1],p[i+3],p[i]*double(p[i+2])));out[b/4]=filters[b].tick(out[b/4],0);}
            out={out[0]+out[1],out[0]-out[1]};
        }else{const auto split=bank.tick(left,right);out={0,0};
            for(size_t b=0;b<3;++b){auto x=split[b];
                if(kind==2){const size_t i=4+b*8;const double level=detector(b,x[0],x[1]);
                    const double reduction=envelope(b,compression(level,p[i],p[i+1],p[i+4],p[i+5]),p[i+2],p[i+3]);
                    const double gain=std::pow(10.,(p[i+6]-reduction)/20.);x[0]*=gain;x[1]*=gain;
                }else{const size_t i=5+b*3;const double m=(x[0]+x[1])/2,s=(x[0]-x[1])/2;
                    const double w=p[i]/100.*(b==0?1-p[4]:1),gain=std::pow(10.,p[i+1]/20.);x={(m+w*s)*gain,(m-w*s)*gain};}
                const double participation=selection[b].tick();out[0]+=x[0]*participation;out[1]+=x[1]*participation;
            }
        }
        cadence=(cadence+1)%16;const double gain=std::pow(10.,p[size_t(kind<2?1:3)]/20.);
        for(size_t c=0;c<2;++c){const double dry=c==0?left:right;out[c]=p[0]==0?dry:dry+p[0]*(gain*out[c]-dry);}
        return {float(out[0]),float(out[1])};
    }
};
}
