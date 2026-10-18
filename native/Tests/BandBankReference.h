#pragma once
#include <array>
#include <cmath>
#include <complex>
#include <algorithm>

// Independent bilinear polynomial design and direct-form-I recurrence.
// Deliberately does not include or call the production bank, Ramp or designers.
namespace bandReference {
constexpr double pi=3.1415926535897932384626433832795;
struct Filter {
    std::array<double,3> b{1,0,0},a{1,0,0};
    std::array<std::array<double,2>,2> x{},y{};
    double tick(double input,size_t ch){
        const double output=b[0]*input+b[1]*x[ch][0]+b[2]*x[ch][1]-a[1]*y[ch][0]-a[2]*y[ch][1];
        x[ch][1]=x[ch][0];x[ch][0]=input;y[ch][1]=y[ch][0];y[ch][0]=output;return output;
    }
    std::complex<double> response(double hz,double sr) const {
        const auto z=std::exp(std::complex<double>(0,-2*pi*hz/sr));
        return (b[0]+b[1]*z+b[2]*z*z)/(1.+a[1]*z+a[2]*z*z);
    }
};
inline Filter design(double sr,double hz,int kind){
    const double k=std::tan(pi*std::clamp(hz,1.,.45*sr)/sr),k2=k*k;
    const double denominator=1+std::sqrt(2.)*k+k2;
    Filter f;f.a={1,2*(k2-1)/denominator,(1-std::sqrt(2.)*k+k2)/denominator};
    if(kind==0)f.b={k2/denominator,2*k2/denominator,k2/denominator};
    else if(kind==1)f.b={1/denominator,-2/denominator,1/denominator};
    else f.b={f.a[2],f.a[1],1};
    return f;
}
struct Smooth {
    float value=0,target=0,step=0;int left=0,length=1;
    void reset(double rate,float v){value=target=v;left=0;length=std::max(1,int(rate*.01));}
    void set(float v){if(target==v)return;target=v;left=length;step=(v-value)/float(length);}
    float tick(){if(left){value+=step;--left;if(!left)value=target;}return value;}
};
struct Bank {
    std::array<Filter,9> filters;Smooth low,high;double sr;unsigned phase=0;
    Bank(double rate,double f1,double f2):sr(rate){low.reset(rate,float(f1));high.reset(rate,float(f2));configure(f1,f2);}
    void configure(double f1,double f2){
        const int kinds[]{0,0,1,1,2,0,0,1,1};
        for(int i=0;i<9;++i){const auto next=design(sr,i<4?f1:f2,kinds[i]);filters[size_t(i)].a=next.a;filters[size_t(i)].b=next.b;}
    }
    void targets(double f1,double f2){low.set(float(f1));high.set(float(f2));}
    std::array<std::array<double,2>,3> tick(double l,double r){
        const auto f1=low.tick(),f2=high.tick();if(phase==0)configure(f1,f2);phase=(phase+1)%16;
        std::array<std::array<double,2>,3> out{};const double in[]{l,r};
        for(size_t c=0;c<2;++c){const auto bottom=filters[1].tick(filters[0].tick(in[c],c),c);
            const auto top=filters[3].tick(filters[2].tick(in[c],c),c);
            out[0][c]=filters[4].tick(bottom,c);out[1][c]=filters[6].tick(filters[5].tick(top,c),c);
            out[2][c]=filters[8].tick(filters[7].tick(top,c),c);}
        return out;
    }
};
}
