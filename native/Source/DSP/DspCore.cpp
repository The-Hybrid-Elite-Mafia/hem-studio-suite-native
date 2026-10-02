#include "DspCore.h"
#include <complex>
namespace hem {
void Biquad::configure(Type type,double rate,float hz,float q,float db) noexcept {
    const double w=2*pi*std::clamp(double(hz),1.0,rate*.45)/rate;
    const double c=std::cos(w),s=std::sin(w),alpha=s/(2*std::max(.1f,q));
    const double A=std::pow(10.0,double(db)/40);double a0=1;
    if(type==HighPass){b0=(1+c)/2;b1=-(1+c);b2=b0;a0=1+alpha;a1=-2*c;a2=1-alpha;}
    else if(type==LowPass){b0=(1-c)/2;b1=1-c;b2=b0;a0=1+alpha;a1=-2*c;a2=1-alpha;}
    else if(type==Bell){b0=1+alpha*A;b1=-2*c;b2=1-alpha*A;a0=1+alpha/A;a1=-2*c;a2=1-alpha/A;}
    else {const double v=2*std::sqrt(A)*s/std::sqrt(2.0);
        b0=A*((A+1)+(A-1)*c+v);b1=-2*A*((A-1)+(A+1)*c);b2=A*((A+1)+(A-1)*c-v);
        a0=(A+1)-(A-1)*c+v;a1=2*((A-1)-(A+1)*c);a2=(A+1)-(A-1)*c-v;}
    b0/=a0;b1/=a0;b2/=a0;a1/=a0;a2/=a0;
}
float Biquad::magnitude(double hz,double rate) const noexcept {
    const std::complex<double> z=std::polar(1.0,-2*pi*hz/rate);
    return float(std::abs((b0+b1*z+b2*z*z)/(1.0+a1*z+a2*z*z)));
}
void HostDelay::process(float* l,float* r,int n) noexcept {
    if(delay==0)return;
    for(int i=0;i<n;++i){const int read=(pos+1)%(delay+1);const auto p=size_t(pos),j=size_t(read);
        ring[0][p]=l[i];ring[1][p]=r[i];l[i]=ring[0][j];r[i]=ring[1][j];pos=read;}
}
void StereoCore::prepare(double rate){sr=rate;sideHighPass.reset();width.reset(sr,1);cutoff.reset(sr,120);balance.reset(sr,0);output.reset(sr,1);bassMix.reset(sr,0);monoMix.reset(sr,0);inversionL.reset(sr,1);inversionR.reset(sr,1);swapMix.reset(sr,0);}
void StereoCore::process(float* l,float* r,int n,const StereoParameters& p) noexcept {
    width.set(p.width/100);cutoff.set(p.monoBass);balance.set(p.balance/100);output.set(gain(p.output));bassMix.set(p.bassMono?1.f:0.f);monoMix.set(p.mono?1.f:0.f);inversionL.set(p.invertL?-1.f:1.f);inversionR.set(p.invertR?-1.f:1.f);swapMix.set(p.swap?1.f:0.f);
    double ll=0,rr=0,lr=0;float pk=0;
    for(int i=0;i<n;++i){sideHighPass.configure(Biquad::HighPass,sr,cutoff.next(),.7071068f);
        const float mid=(l[i]+r[i])*.5f,side=(l[i]-r[i])*.5f,hi=sideHighPass.tick(side,0),wet=bassMix.next();
        const float s=(side+(hi-side)*wet)*width.next()*(1-monoMix.next());
        const float b=balance.next(),g=output.next(),left=(mid+s)*(1-std::max(0.f,b))*inversionL.next(),right=(mid-s)*(1+std::min(0.f,b))*inversionR.next(),sw=swapMix.next();
        l[i]=(left+(right-left)*sw)*g;r[i]=(right+(left-right)*sw)*g;
        ll+=l[i]*l[i];rr+=r[i]*r[i];lr+=l[i]*r[i];pk=std::max({pk,std::abs(l[i]),std::abs(r[i])});}
    correlation.store(ll*rr>1e-20?float(lr/std::sqrt(ll*rr)):0,std::memory_order_relaxed);peak.store(pk,std::memory_order_relaxed);
}
void EqCore::prepare(double rate){sr=rate;controlPhase=0;cutoff.reset(sr,30);output.reset(sr,1);const EqParameters p;
    for(size_t i=0;i<4;++i){hp[i].reset();eq[i].reset();frequencies[i].reset(sr,p.hz[i]);gains[i].reset(sr,0);qs[i].reset(sr,p.q[i]);hpMix[i].reset(sr,0);}}
void EqCore::process(float* l,float* r,int n,const EqParameters& p) noexcept {
    cutoff.set(p.cutoff);output.set(gain(p.output));for(size_t j=0;j<4;++j){frequencies[j].set(p.hz[j]);gains[j].set(p.db[j]);qs[j].set(p.q[j]);hpMix[j].set(p.highPass&&int(j)<=p.slope?1.f:0.f);}
    float pk=0;
    for(int i=0;i<n;++i){const float f=cutoff.next();std::array<float,4> mixes{};
        for(size_t j=0;j<4;++j){const float hz=frequencies[j].next(),d=gains[j].next(),q=qs[j].next();mixes[j]=hpMix[j].next();
            // Recompute stable RBJ coefficients at a bounded control cadence.
            if(controlPhase==0){hp[j].configure(Biquad::HighPass,sr,f,.7071068f);eq[j].configure(j==3?Biquad::HighShelf:Biquad::Bell,sr,hz,q,d);}}
        controlPhase=(controlPhase+1)&15;const float g=output.next();
        for(int ch=0;ch<2;++ch){float& sample=ch==0?l[i]:r[i];float x=sample;
            for(size_t j=0;j<4;++j){const float filtered=hp[j].tick(x,ch);x+=(filtered-x)*mixes[j];}
            for(auto& filter:eq)x=filter.tick(x,ch);sample=x*g;pk=std::max(pk,std::abs(sample));}}
    peak.store(pk,std::memory_order_relaxed);
}
float EqCore::response(double hz,double rate,const EqParameters& p) noexcept {
    Biquad f;float magnitude=gain(p.output);if(p.highPass){f.configure(Biquad::HighPass,rate,p.cutoff,.7071068f);magnitude*=std::pow(f.magnitude(hz,rate),p.slope+1);}
    for(size_t i=0;i<4;++i){f.configure(i==3?Biquad::HighShelf:Biquad::Bell,rate,p.hz[i],p.q[i],p.db[i]);magnitude*=f.magnitude(hz,rate);}return decibels(magnitude);
}
void CompressorCore::prepare(double rate){sr=rate;envelopeDb=programme=0;detector.reset();threshold.reset(sr,0);ratio.reset(sr,1);attack.reset(sr,30);release.reset(sr,100);knee.reset(sr,0);makeup.reset(sr,1);mix.reset(sr,1);cutoff.reset(sr,80);scMix.reset(sr,0);}
void CompressorCore::process(float* l,float* r,int n,const CompressorParameters& p) noexcept {
    threshold.set(p.threshold);ratio.set(p.ratio);attack.set(p.attack);release.set(p.release);knee.set(p.knee);makeup.set(gain(p.makeup));mix.set(p.mix/100);cutoff.set(p.sidechain);scMix.set(p.scEnabled?1.f:0.f);float maxReduction=0,pk=0;
    for(int i=0;i<n;++i){detector.configure(Biquad::HighPass,sr,cutoff.next(),.7071068f);const float sc=scMix.next(),a=detector.tick(l[i],0),b=detector.tick(r[i],1);
        const float detected=std::max(std::abs(l[i]+sc*(a-l[i])),std::abs(r[i]+sc*(b-r[i])));
        const float over=decibels(detected)-threshold.next(),k=knee.next(),slope=1-1/std::max(1.f,ratio.next());float gr=0;
        if(k>0&&over>-k*.5f&&over<k*.5f)gr=slope*(over+k*.5f)*(over+k*.5f)/(2*k);else if(over>=k*.5f)gr=slope*over;
        programme+=float(1-std::exp(-1/(sr*.25)))*(gr-programme);
        const float at=attack.next(),rel=release.next()*(p.autoRelease?1+std::min(2.f,programme/6):1);
        const float c=float(std::exp(-1/(sr*.001*std::max(.1f,gr>envelopeDb?at:rel))));envelopeDb=c*envelopeDb+(1-c)*gr;
        const float g=gain(-envelopeDb)*makeup.next(),wet=mix.next(),combined=1+wet*(g-1);l[i]*=combined;r[i]*=combined;
        maxReduction=std::max(maxReduction,envelopeDb);pk=std::max({pk,std::abs(l[i]),std::abs(r[i])});}
    reduction.store(maxReduction,std::memory_order_relaxed);peak.store(pk,std::memory_order_relaxed);
}
Reconstruction::Reconstruction(){for(int p=0;p<phases;++p){double sum=0;for(int i=0;i<taps;++i){const double x=i-(taps/2-1)-double(p)/phases;
    const double sinc=std::abs(x)<1e-12?1:std::sin(pi*x)/(pi*x);const double window=.42-.5*std::cos(2*pi*i/(taps-1))+.08*std::cos(4*pi*i/(taps-1));h[size_t(p)][size_t(i)]=sinc*window;sum+=sinc*window;}
    for(auto& c:h[size_t(p)])c/=sum;}}
void LimiterCore::prepare(double rate){sr=rate;delay=std::max(Reconstruction::taps,int(std::ceil(sr*.005)));size=delay+Reconstruction::taps+1;
    for(auto& c:audio)c.assign(size_t(size),0);budgets.assign(size_t(size),1);history={};meterHistory={};position=histPos=meterPos=0;previousGain=1;input.reset(sr,1);ceiling.reset(sr,-1);release.reset(sr,100);}
void LimiterCore::process(float* l,float* r,int n,const LimiterParameters& p) noexcept {
    input.set(gain(p.input));ceiling.set(p.ceiling);release.set(p.release);float maxGR=0,maxTP=0;
    for(int sample=0;sample<n;++sample){const float drive=input.next(),limit=gain(ceiling.next())*.99999f;
        audio[0][size_t(position)]=l[sample]*drive;audio[1][size_t(position)]=r[sample]*drive;budgets[size_t(position)]=1;
        history[0][size_t(histPos)]=l[sample]*drive;history[1][size_t(histPos)]=r[sample]*drive;
        // A conservative bound, not just sample clipping: every reconstructed
        // window constrains ALL contributing future output samples. Triangle
        // inequality then bounds each 8x FIR phase by the ceiling. Budgets are
        // final before the 5 ms delayed sample is emitted. Stereo is linked.
        double bound=std::max(std::abs(l[sample]*drive),std::abs(r[sample]*drive));
        for(int ch=0;ch<2;++ch)for(const auto& phase:filter.h){double b=0;for(int k=0;k<Reconstruction::taps;++k)b+=std::abs(phase[size_t(k)]*history[size_t(ch)][size_t((histPos-k+Reconstruction::taps)%Reconstruction::taps)]);bound=std::max(bound,b);}
        const float budget=bound>limit?float(limit/bound):1.f;
        for(int k=0;k<Reconstruction::taps;++k){auto& v=budgets[size_t((position-k+size)%size)];v=std::min(v,budget);}
        const int read=(position-delay+size)%size;const float releaseCoeff=float(std::exp(-1/(sr*.001*release.next())));
        previousGain=std::min(budgets[size_t(read)],releaseCoeff*previousGain+(1-releaseCoeff));
        l[sample]=audio[0][size_t(read)]*previousGain;r[sample]=audio[1][size_t(read)]*previousGain;
        meterHistory[0][size_t(meterPos)]=l[sample];meterHistory[1][size_t(meterPos)]=r[sample];
        for(int ch=0;ch<2;++ch)for(const auto& phase:filter.h){double v=0;for(int k=0;k<Reconstruction::taps;++k)v+=phase[size_t(k)]*meterHistory[size_t(ch)][size_t((meterPos-k+Reconstruction::taps)%Reconstruction::taps)];maxTP=std::max(maxTP,float(std::abs(v)));}
        maxGR=std::max(maxGR,-decibels(previousGain));position=(position+1)%size;histPos=(histPos+1)%Reconstruction::taps;meterPos=(meterPos+1)%Reconstruction::taps;}
    reduction.store(maxGR,std::memory_order_relaxed);truePeak.store(maxTP,std::memory_order_relaxed);
}
} // namespace hem
