#include "ReferenceMatchCore.h"
#include <algorithm>
#include <cmath>
#include <complex>
#include <vector>

namespace hem::reference_match {
namespace {
constexpr double pi=3.14159265358979323846;
constexpr size_t nfft=8192;
bool rateOk(double r) { return std::isfinite(r) && r>=22050 && r<=192000; }
void fft(std::vector<std::complex<double>>& x) {
    for(size_t i=1,j=0;i<x.size();++i) {size_t bit=x.size()>>1;for(;j&bit;bit>>=1)j^=bit;j^=bit;if(i<j)std::swap(x[i],x[j]);}
    for(size_t n=2;n<=x.size();n*=2) {
        auto root=std::polar(1.0,-2*pi/n);
        for(size_t i=0;i<x.size();i+=n) {std::complex<double>w=1;for(size_t j=0;j<n/2;++j){auto a=x[i+j],b=w*x[i+j+n/2];x[i+j]=a+b;x[i+j+n/2]=a-b;w*=root;}}
    }
}
Coefficients bell(double hz,double gain,double rate) {
    const double A=std::pow(10.0,gain/40),w=2*pi*hz/rate,alpha=std::sin(w)/(2*1.1),c=std::cos(w),a0=1+alpha/A;
    return {(1+alpha*A)/a0,-2*c/a0,(1-alpha*A)/a0,-2*c/a0,(1-alpha/A)/a0};
}
double response(const Coefficients& c,double f,double r) {
    auto z=std::polar(1.0,-2*pi*f/r);
    return 20*std::log10(std::max(1e-15,std::abs((c.b0+c.b1*z+c.b2*z*z)/(1.0+c.a1*z+c.a2*z*z))));
}
double interpolate(const Profile& p,double f) {
    auto it=std::lower_bound(p.hz.begin(),p.hz.end(),f);
    size_t k=static_cast<size_t>(it-p.hz.begin());
    if(k==0)return p.power.front();if(k==bins)return p.power.back();
    double t=std::log(f/p.hz[k-1])/std::log(p.hz[k]/p.hz[k-1]);
    return std::exp((1-t)*std::log(std::max(1e-30,p.power[k-1]))+t*std::log(std::max(1e-30,p.power[k])));
}
bool validProfile(const Profile& p) {
    if(p.status!=Status::ready || !rateOk(p.sampleRate) || p.frames<nfft || p.frames>p.sampleRate*30 || !std::isfinite(p.meanSquare) || p.meanSquare<=1e-12 || p.meanSquare>10000)return false;
    // The analyzer accepts |PCM| <= 100. Its normalized window power cannot
    // exceed 10000. Enforce those bounds before division/logarithms, including
    // for finite but hostile persisted doubles that would otherwise overflow.
    for(size_t i=0;i<bins;++i){
        const double expectedHz=30*std::pow(std::min(20000.0,p.sampleRate*.45)/30,double(i)/(bins-1));
        if(!std::isfinite(p.power[i])||p.power[i]<0||p.power[i]>10000||!std::isfinite(p.hz[i])||std::abs(p.hz[i]-expectedHz)>expectedHz*1e-9)return false;
    }
    return true;
}
}
Profile analyze(const float* const* channels,size_t count,size_t frames,double rate,uint64_t generation,uint64_t lost) {
    Profile p;p.generation=generation;p.sampleRate=rate;p.frames=frames;
    if(lost){p.status=Status::discontinuity;return p;}
    if(!rateOk(rate)||!channels||count<1||count>2||frames>rate*30){p.status=Status::invalidInput;return p;}
    if(frames<nfft||frames<rate*.25)return p;
    double sum=0;for(size_t c=0;c<count;++c){if(!channels[c]){p.status=Status::invalidInput;return p;}for(size_t i=0;i<frames;++i){double v=channels[c][i];if(!std::isfinite(v)||std::abs(v)>100){p.status=Status::invalidInput;return p;}sum+=v*v;}}
    p.meanSquare=sum/(frames*count);if(p.meanSquare<1e-12)return p;
    const double top=std::min(20000.0,rate*.45);
    for(size_t b=0;b<bins;++b)p.hz[b]=30*std::pow(top/30,double(b)/(bins-1));
    std::vector<std::complex<double>> x(nfft);std::array<double,nfft/2+1> spectrum{};size_t windows=0;
    for(size_t c=0;c<count;++c)for(size_t start=0;start+nfft<=frames;start+=nfft/2){
        for(size_t i=0;i<nfft;++i)x[i]=channels[c][start+i]*(.5-.5*std::cos(2*pi*i/(nfft-1)));
        fft(x);for(size_t i=1;i<=nfft/2;++i)spectrum[i]+=std::norm(x[i]);++windows;
    }
    size_t useful=0;
    for(size_t b=0;b<bins;++b){double weighted=0,weight=0;const double width=std::max(rate/nfft,p.hz[b]*.055);
        for(size_t k=1;k<=nfft/2;++k){double w=std::max(0.0,1-std::abs(k*rate/nfft-p.hz[b])/width);weighted+=w*spectrum[k];weight+=w;}
        p.power[b]=weighted/std::max(weight,1e-12)/windows/(nfft*nfft*.375);
        if(p.power[b]>p.meanSquare*1e-7)++useful;
    }
    if(useful<bins/3)return p;
    p.status=Status::ready;return p;
}
double Solution::responseDb(double f)const noexcept {
    if(!valid||!rateOk(sampleRate)||!std::isfinite(f)||f<0||f>sampleRate/2)return 0;
    double sum=0;for(const auto& c:filters)sum+=response(c,f,sampleRate);return sum;
}
Solution fit(const Profile& src,const Profile& ref,double rate,double amount,double smoothing) {
    Solution out;
    if(!validProfile(src)||!validProfile(ref)||!rateOk(rate)||!std::isfinite(amount)||!std::isfinite(smoothing)||amount<0||amount>1||smoothing<0||smoothing>3)return out;
    out.sampleRate=rate;out.amount=amount;out.smoothingOctaves=smoothing;
    const double top=std::min({src.hz.back(),ref.hz.back(),rate*.45});
    std::array<double,bins> frequencies{},raw{},weights{};
    for(size_t b=0;b<bins;++b){double f=30*std::pow(top/30,double(b)/(bins-1));frequencies[b]=f;
        double s=interpolate(src,f)/src.meanSquare,r=interpolate(ref,f)/ref.meanSquare;
        weights[b]=(s>1e-7&&r>1e-7)?1:0;raw[b]=std::clamp(10*std::log10(std::max(1e-30,r)/std::max(1e-30,s)),-12.0,12.0);
    }
    if(std::count(weights.begin(),weights.end(),1.0)<bins/3)return out;
    for(size_t b=0;b<bins;++b){double sum=0,wSum=0;
        for(size_t j=0;j<bins;++j){double d=std::log2(frequencies[j]/frequencies[b]);double w=smoothing<=.001?(j==b?weights[j]:0):weights[j]*std::exp(-.5*d*d/(smoothing*smoothing));sum+=w*raw[j];wSum+=w;}
        out.targetDb[b]=sum/std::max(wSum,1e-12);
    }
    for(size_t i=0;i<sections;++i)out.hz[i]=45*std::pow((top*.9)/45,double(i)/(sections-1));
    // Bounded Gauss-Seidel nonlinear least squares; finite-difference Jacobian
    // uses the actual cascaded biquad response, not a display approximation.
    std::array<double,bins> prediction{};
    for(int iteration=0;iteration<18;++iteration)for(size_t i=0;i<sections;++i){
        auto old=bell(out.hz[i],out.gainDb[i],rate),plus=bell(out.hz[i],out.gainDb[i]+.05,rate);
        double numerator=-.15*out.gainDb[i],denominator=.15;
        for(size_t b=0;b<bins;++b){double derivative=(response(plus,frequencies[b],rate)-response(old,frequencies[b],rate))/.05;numerator+=weights[b]*derivative*(out.targetDb[b]-prediction[b]);denominator+=weights[b]*derivative*derivative;}
        out.gainDb[i]=std::clamp(out.gainDb[i]+std::clamp(numerator/denominator,-3.0,3.0),-12.0,12.0);
        auto next=bell(out.hz[i],out.gainDb[i],rate);
        for(size_t b=0;b<bins;++b)prediction[b]+=response(next,frequencies[b],rate)-response(old,frequencies[b],rate);
    }
    out.valid=true;
    // Bound combined response too: overlapping sections can otherwise exceed
    // individual gain limits. Dense grid includes DC and Nyquist neighbourhood.
    for(int pass=0;pass<16;++pass){for(size_t i=0;i<sections;++i)out.filters[i]=bell(out.hz[i],out.gainDb[i],rate);
        double peak=0;for(size_t k=0;k<=4096;++k)peak=std::max(peak,std::abs(out.responseDb(rate*.5*k/4096)));
        if(peak<=11.99)break;for(auto& gain:out.gainDb)gain*=11.9/peak;
    }
    for(auto& target:out.targetDb)target*=amount;
    for(size_t i=0;i<sections;++i){out.gainDb[i]*=amount;out.filters[i]=bell(out.hz[i],out.gainDb[i],rate);}
    // For denominator poles bounded by r, its impulse is <=(n+1)r^n.
    // Convolve numerator absolute sum C, then use the union bound on the
    // cascade convolution tail at sum(N_i). Computed only on the worker.
    std::array<double,sections> radius{},logBound{};double totalLog=0;
    for(size_t i=0;i<sections;++i){if(std::abs(out.gainDb[i])<1e-12)continue;const auto& c=out.filters[i];auto discriminant=std::sqrt(std::complex<double>(c.a1*c.a1-4*c.a2,0));radius[i]=std::max(std::abs((-c.a1+discriminant)*.5),std::abs((-c.a1-discriminant)*.5));double C=std::abs(c.b0)+std::abs(c.b1)+std::abs(c.b2);logBound[i]=std::log(C)-2*std::log1p(-radius[i]);totalLog+=logBound[i];}
    double totalSamples=0;
    for(size_t i=0;i<sections;++i){if(radius[i]==0)continue;double r=radius[i],one=1-r;const auto& c=out.filters[i];double logC=std::log(std::abs(c.b0)+std::abs(c.b1)+std::abs(c.b2));auto safe=[&](double n){double logTail=logC+(n-2)*std::log(r)+std::log((n-1)/one+r/(one*one));return logTail+totalLog-logBound[i]<std::log(1e-9/(sections*16));};double low=2,high=rate;while(!safe(high))high*=2;for(int step=0;step<40;++step){double mid=(low+high)*.5;if(safe(mid))high=mid;else low=mid;}totalSamples+=std::ceil(high);}
    out.tailSeconds=totalSamples/rate+.01; // Includes 5 ms control smoothing margin.
    return out;
}
void Processor::reset()noexcept{current.state={};previous.state={};remaining=0;outputGain=1;bypassMix=0;}
void Processor::publish(const Solution& s)noexcept {
    if(!s.valid||!rateOk(s.sampleRate))return;
    // Defend publication even if a caller constructed rather than fitted it.
    for(const auto& c:s.filters)if(!std::isfinite(c.b0)||!std::isfinite(c.b1)||!std::isfinite(c.b2)||!std::isfinite(c.a1)||!std::isfinite(c.a2)||std::abs(c.a2)>=1||1+c.a1+c.a2<=0||1-c.a1+c.a2<=0)return;
    previous=current;current.solution=s;current.state={};remaining=256;
}
double Processor::Bank::tick(double x,size_t channel)noexcept {
    if(!solution.valid)return x;
    for(size_t i=0;i<sections;++i){const auto& c=solution.filters[i];auto& z=state[channel][i];double y=c.b0*x+z[0];z[0]=c.b1*x-c.a1*y+z[1];z[1]=c.b2*x-c.a2*y;x=y;}
    if(!std::isfinite(x)){state[channel]={};return 0;}return x;
}
void Processor::process(float* left,float* right,size_t frames,float outputDb,bool bypass)noexcept {
    if(!left)return;const double gain=std::pow(10.0,std::clamp(std::isfinite(outputDb)?double(outputDb):0.0,-24.0,24.0)/20);
    const double rate=current.solution.valid?current.solution.sampleRate:48000;const double smoothing=1-std::exp(-1/(rate*.005));
    for(size_t i=0;i<frames;++i){outputGain+=(gain-outputGain)*smoothing;bypassMix+=((bypass?1.0:0.0)-bypassMix)*smoothing;for(size_t ch=0;ch<(right?2u:1u);++ch){auto* buffer=ch?right:left;double x=std::isfinite(buffer[i])?buffer[i]:0,y=current.tick(x,ch);if(remaining){double t=double(256-remaining)/256;y=previous.tick(x,ch)*(1-t)+y*t;}buffer[i]=static_cast<float>(std::clamp(y*outputGain*(1-bypassMix)+x*bypassMix,-1e12,1e12));}if(remaining)--remaining;}
}
}
