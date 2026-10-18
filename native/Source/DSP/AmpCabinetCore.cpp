#include "AmpCabinetCore.h"
namespace hem {
namespace { float bounded(float x,float lo,float hi,float neutral=0) noexcept {return std::isfinite(x)?std::clamp(x,lo,hi):neutral;} }
AmpCabinetSettings AmpCabinetCore::sanitized(AmpCabinetSettings p) noexcept {
    p.ampModel=std::clamp(p.ampModel,0,2);p.cabModel=std::clamp(p.cabModel,0,3);p.micModel=std::clamp(p.micModel,0,2);
    p.gain=bounded(p.gain,0,36);p.master=bounded(p.master,-24,12);p.output=bounded(p.output,-60,12);
    p.bass=bounded(p.bass,-12,12);p.mid=bounded(p.mid,-12,12);p.treble=bounded(p.treble,-12,12);
    p.micPosition=bounded(p.micPosition,0,1);p.micDistance=bounded(p.micDistance,0,1);p.micAngle=bounded(p.micAngle,0,1);return p;
}
void AmpCabinetCore::configureCab(Cabinet& c,int model,double rate) noexcept {
    // Synthetic Open, Closed and Compact: distinct resonances and bandwidths.
    const float low[]={65,80,115},res[]={120,155,210},high[]={6500,4800,3800},db[]={2,5,3};
    c.hp.configure(Biquad::HighPass,rate,low[model],.7071f);
    c.resonance.configure(Biquad::Bell,rate,res[model],1.1f,db[model]);
    c.lp.configure(Biquad::LowPass,rate,high[model],.7071f);
}
void AmpCabinetCore::configureMic(Biquad& low,Biquad& high,double rate,int model,float pos,float dist,float angle) noexcept {
    // Modeled Neutral, Presence and Dark. Geometry is coloration, not distance
    // propagation or a measured microphone. No artificial acoustic-delay claim.
    const float presence[]={0,3,-4};
    low.configure(Biquad::Bell,rate,150,.65f,4*(1-dist));
    high.configure(Biquad::HighShelf,rate,3500,.7071f,presence[model]-6*pos-8*angle-3*dist);
}
void AmpCabinetCore::prepare(double rate) noexcept {
    sr=std::isfinite(rate)?std::clamp(rate,8000.,192000.):48000.;position=phase=0;dry={};running={};cabDelay=int(std::ceil(sr*.004));cabPosition=0;aligned={};bypassDelay={};
    for(int m=0;m<3;++m){auto& a=amps[size_t(m)];a.pre.prepare();a.power.prepare();a.coupling.reset();a.dc.reset();
        a.coupling.configure(Biquad::HighPass,sr,m==2?160.f:35.f,.7071f);a.dc.configure(Biquad::HighPass,sr,8,.7071f);
        auto& c=cabinets[size_t(m)];c.hp.reset();c.resonance.reset();c.lp.reset();configureCab(c,m,sr);}
    bass.reset();mid.reset();treble.reset();micLow.reset();micHigh.reset();
    controls.reset(sr,{1,1,1,0,0,0,0,0,0,1,0,0,1,0,0,0,0,0,0,0,0});
    for(auto& v:inputPeak)v.store(0);for(auto& v:outputPeak)v.store(0);
}
void AmpCabinetCore::process(float* l,float* r,int n,const AmpCabinetSettings& raw) noexcept {
    if(!l||!r||n<=0)return;
    const auto p=sanitized(raw);
    controls.target({hem::gain(p.gain),hem::gain(p.master),hem::gain(p.output),p.bass,p.mid,p.treble,
        p.ampEnabled?1.f:0.f,p.cabEnabled?1.f:0.f,p.micEnabled?1.f:0.f,
        p.ampModel==0?1.f:0.f,p.ampModel==1?1.f:0.f,p.ampModel==2?1.f:0.f,
        p.cabModel==0?1.f:0.f,p.cabModel==1?1.f:0.f,p.cabModel==2?1.f:0.f,p.bypass?1.f:0.f,(p.micModel==1?3.f:(p.micModel==2?-4.f:0.f)),p.micPosition,p.micDistance,p.micAngle,p.cabModel==3?1.f:0.f});
    // All filter geometry and model gains ramp on the audio thread.

    float in[2]={},out[2]={};
    for(int i=0;i<n;++i){const auto v=controls.next();
        for(int m=0;m<3;++m){const bool active=v[6]>0&&v[size_t(9+m)]>0;if(active&&!running[size_t(m)]){auto& a=amps[size_t(m)];a.pre.resetHistory();a.power.resetHistory();a.coupling.reset();a.dc.reset();}running[size_t(m)]=active;}
        if((phase++%32)==0){bass.configure(Biquad::Bell,sr,130,.65f,v[3]);mid.configure(Biquad::Bell,sr,850,.8f,v[4]);treble.configure(Biquad::HighShelf,sr,3200,.7071f,v[5]);micLow.configure(Biquad::Bell,sr,150,.65f,4*(1-v[18]));micHigh.configure(Biquad::HighShelf,sr,3500,.7071f,v[16]-6*v[17]-8*v[19]-3*v[18]);}
        float stage[2]{},dryOut[2]{};
        for(int ch=0;ch<2;++ch){float& sample=ch?r[i]:l[i];const float x=bounded(sample,-64,64);in[ch]=std::max(in[ch],std::abs(x));
            const float delayed=dry[size_t(ch)][position];dry[size_t(ch)][position]=x;
            float amplified=0;
            for(int m=0;m<3;++m){if(v[6]==0||v[size_t(9+m)]==0)continue;auto& a=amps[size_t(m)];const float d=m==0?.6f:(m==1?2.f:6.f);
                // Clean: symmetric mild preamp. Edge: asymmetric preamp followed
                // by symmetric power stage. Lead: high-gain asymmetric preamp,
                // interstage bass cut and hotter cascaded power saturation.
                float y=a.pre.tick(x*v[0],ch,d,m==1?.22f:(m==2?.1f:0.f),1);
                y=a.coupling.tick(y,ch);y=a.power.tick(y*v[1],ch,m==2?3.f:.85f,0,1);
                amplified+=v[size_t(9+m)]*a.dc.tick(y,ch);
            }
            const float toned=treble.tick(mid.tick(bass.tick(amplified,ch),ch),ch);
            stage[ch]=delayed+v[6]*(toned-delayed);
            dryOut[ch]=bypassDelay[size_t(ch)][size_t(cabPosition)];bypassDelay[size_t(ch)][size_t(cabPosition)]=delayed;
        }
        float convolved[2]{stage[0],stage[1]};if(custom)custom->process(&convolved[0],&convolved[1],1);
        for(int ch=0;ch<2;++ch){float& sample=ch?r[i]:l[i];
            const float delayed=aligned[size_t(ch)][size_t(cabPosition)];aligned[size_t(ch)][size_t(cabPosition)]=stage[ch];
            float cab=0;
            for(int m=0;m<3;++m){auto& c=cabinets[size_t(m)];cab+=v[size_t(12+m)]*c.lp.tick(c.resonance.tick(c.hp.tick(delayed,ch),ch),ch);}
            cab+=v[20]*(custom?convolved[ch]:delayed); // unavailable custom remains dry-safe
            float y=delayed+v[7]*(cab-delayed);const float mic=micHigh.tick(micLow.tick(y,ch),ch);y+=v[8]*(mic-y);
            sample=(y*v[2])*(1-v[15])+dryOut[ch]*v[15];out[ch]=std::max(out[ch],std::abs(sample));
        }position=(position+1)%32;cabPosition=(cabPosition+1)%cabDelay;
    }
    for(int ch=0;ch<2;++ch){inputPeak[size_t(ch)].store(in[ch],std::memory_order_relaxed);outputPeak[size_t(ch)].store(out[ch],std::memory_order_relaxed);}
}
float AmpCabinetCore::response(double hz,double rate,const AmpCabinetSettings& raw,bool includeMic) noexcept {
    const auto p=sanitized(raw);rate=std::isfinite(rate)?std::clamp(rate,8000.,192000.):48000.;
    if(!std::isfinite(hz)||hz<0||hz>rate*.5)return 0;
    Cabinet c;configureCab(c,std::min(p.cabModel,2),rate);Biquad low,high;configureMic(low,high,rate,p.micModel,p.micPosition,p.micDistance,p.micAngle);
    float value=p.cabEnabled&&p.cabModel!=3?c.hp.magnitude(hz,rate)*c.resonance.magnitude(hz,rate)*c.lp.magnitude(hz,rate):1;
    if(includeMic&&p.micEnabled)value*=low.magnitude(hz,rate)*high.magnitude(hz,rate);return value;
}
}




