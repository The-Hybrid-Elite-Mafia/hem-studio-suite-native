#pragma once
#include <complex>

// Independent analog-prototype response, evaluated at bilinear-prewarped
// digital frequency. Does not use the production coefficient helper.
inline std::complex<double> referenceLow(double hz,double cutoff,double sr){return 1.0/std::complex<double>(1,std::tan(hem::pi*hz/sr)/std::tan(hem::pi*cutoff/sr));}
inline void calibratedSplitFixtures(){
    for(double sr:{22050.,44100.,48000.,96000.,192000.})for(double requested:{80.,250.,1500.,2000.,4000.,12000.}){
        const double fc=std::min(requested,sr*.4),a=hem::ExciterSplit::coefficient(fc,sr);
        for(double hz:{fc*.5,fc,std::min(fc*1.5,sr*.45)}){
            hem::ExciterSplit split;std::complex<double> measured{};const auto rotation=std::polar(1.0,-2*hem::pi*hz/sr);std::complex<double> phase(1,0);
            for(int n=0;n<16384;++n){const auto bands=split.tick(n==0?1.f:0.f,0,a,a);measured+=double(bands[0])*phase;phase*=rotation;require(std::abs(bands[0]+bands[1]+bands[2]-(n==0?1.f:0.f))<1.e-7,"Split impulse fails complementary reconstruction");}
            const auto expected=referenceLow(hz,fc,sr);require(std::abs(measured-expected)<3.e-6,"Complex split response differs from independent prototype");
            if(hz==fc){require(std::abs(std::abs(measured)-std::sqrt(.5))<3.e-6,"Cutoff not calibrated to -3.0103 dB");require(std::abs(std::arg(measured)+hem::pi/4)<3.e-6,"Cutoff phase not -45 degrees");}
        }
        // Render the actual processor at a solo cutoff and compare its complex
        // gain (including the independent 16-sample delay) with the prototype.
        hem::ExciterCore core;hem::ExciterSettings p;const bool low=requested<=1500;
        if(low){p.lowMid=float(requested);p.solo={true,false,false};}else{p.midHigh=float(requested);p.solo={false,false,true};}
        core.prepare(sr);Buffer l{},r{};double xx=0,yy=0,xy=0,yx=0,yc=0;const int begin=int(sr*.25),total=begin+8192;
        for(int start=0;start<total;start+=257){const int n=std::min(257,total-start);for(int i=0;i<n;++i)l[size_t(i)]=r[size_t(i)]=float(.25*std::cos(2*hem::pi*fc*(start+i)/sr));core.process(l.data(),r.data(),n,p);
            for(int i=0;i<n;++i)if(start+i>=begin){const double angle=2*hem::pi*fc*(start+i)/sr,c=std::cos(angle),s=std::sin(angle);xx+=c*c;yy+=s*s;xy+=c*s;yx+=l[size_t(i)]*c;yc+=l[size_t(i)]*s;}}
        const double determinant=xx*yy-xy*xy;const std::complex<double> measured((yx*yy-yc*xy)/determinant/.25,-(yc*xx-yx*xy)/determinant/.25);
        const auto lp=referenceLow(fc,fc,sr),expected=(low?lp:1.0-lp)*std::polar(1.0,-2*hem::pi*fc*16/sr);
        require(std::abs(measured-expected)<2.e-5,"Rendered solo tone cutoff/phase/delay mismatch");
    }
    std::cout<<"PASS independent complex response and rendered cutoff tone fixtures\n";
}

// Analytic sinusoidal band response feeds an independent unfiltered shaper.
// Frequency-bin projections are coherent: 8192 samples, 4096 warmup.
inline void expandedAliasFixtures(){
    constexpr int length=8192,warm=4096;const int bins[]{1409,1559,1733,1889,2111};
    for(double sr:{44100.,48000.,96000.})for(int inputBin:bins){const double hz=sr*inputBin/length;
        for(int mode=0;mode<5;++mode){ // symmetric, asymmetric, LOW, MID, HIGH
            hem::ParallelCore parallel;hem::ExciterCore exciter;parallel.prepare(sr);exciter.prepare(sr);
            hem::ParallelSettings p;p.drive=18;p.blend=100;p.type=mode==1?1:0;p.quality=1;
            hem::ExciterSettings q;q.lowMid=1500;q.midHigh=4000;if(mode>=2){const size_t band=size_t(mode-2);q.on[band]=true;q.amount[band]=q.mix[band]=100;}
            std::array<double,3> re{},im{},rawRe{},rawIm{};const int harmonics[]{2,3,5};Buffer l{},r{};
            std::complex<double> bandResponse(1,0);if(mode>=2){const auto lo=referenceLow(hz,q.lowMid,sr),hi=referenceLow(hz,q.midHigh,sr);bandResponse=mode==2?lo:mode==3?hi-lo:1.0-hi;}
            const double bias=mode==0?0:mode==1?.3:.25,d=mode<2?hem::gain(18):8,z=std::tanh(bias),slope=1-z*z;
            for(int start=0;start<warm+length;start+=257){const int n=std::min(257,warm+length-start);for(int i=0;i<n;++i)l[size_t(i)]=r[size_t(i)]=float(.5*std::cos(2*hem::pi*hz*(start+i)/sr));
                counting=true;if(mode<2)parallel.process(l.data(),r.data(),n,p);else exciter.process(l.data(),r.data(),n,q);counting=false;
                for(int i=0;i<n;++i)if(start+i>=warm){const int sample=start+i;const auto phasor=std::polar(1.0,2*hem::pi*hz*sample/sr);const double band=.5*std::real(bandResponse*phasor),shaped=(std::tanh(band*d+bias)-z)/(d*slope),raw=mode<2?shaped:.5*phasor.real()+shaped-band;
                    for(size_t h=0;h<3;++h){const int wrapped=(inputBin*harmonics[h])%length,folded=std::min(wrapped,length-wrapped);const double phase=2*hem::pi*folded*sample/length;re[h]+=l[size_t(i)]*std::cos(phase);im[h]+=l[size_t(i)]*std::sin(phase);rawRe[h]+=raw*std::cos(phase);rawIm[h]+=raw*std::sin(phase);}}
            }
            int checked=0;for(size_t h=0;h<3;++h)if(inputBin*harmonics[h]>length/2){const double raw=2*std::hypot(rawRe[h],rawIm[h])/length,filtered=2*std::hypot(re[h],im[h])/length;
                // Above-floor references require >=6 dB improvement. Tiny
                // references retain an explicit absolute output floor bound.
                require(filtered<raw*.5+2.e-6,"Alias sweep exceeds relative/absolute foldback bound");if(raw>1.e-5)++checked;
                std::cout<<"ALIAS fs="<<sr<<" bin="<<inputBin<<" mode="<<mode<<" H="<<harmonics[h]<<" raw="<<raw<<" 4x="<<filtered<<'\n';}
            require(checked>0,"Alias fixture has no measurable foldback reference");
        }
    }
}

inline void transitionFixtures(){
    for(double sr:{44100.,48000.,96000.}){hem::ParallelCore core;core.prepare(sr);hem::ParallelSettings p;p.drive=18;p.blend=0;Buffer l{},r{};
        for(int start=0;start<12000;start+=257){const int n=std::min(257,12000-start);p.quality=(start/257)%2;p.type=(start/514)%2;
            for(int i=0;i<n;++i)l[size_t(i)]=r[size_t(i)]=.2f*std::sin(float((start+i)*.077));counting=true;core.process(l.data(),r.data(),n,p);counting=false;
            for(int i=0;i<n;++i){const int sample=start+i-16;require(std::abs(l[size_t(i)]-(sample<0?0:.2f*std::sin(float(sample*.077))))<3.e-6,"Quality/type switching disturbs aligned dry path");}}
        // Drive zero makes both quality branches exact delayed identities even
        // with 100% wet and changing quality; rejects rate-dependent delay.
        core.prepare(sr);p.drive=0;p.blend=100;
        for(int start=0;start<12000;start+=257){const int n=std::min(257,12000-start);p.quality=(start/257)%2;p.bypass=(start/514)%2;
            for(int i=0;i<n;++i)l[size_t(i)]=r[size_t(i)]=.2f*std::sin(float((start+i)*.041));core.process(l.data(),r.data(),n,p);
            for(int i=0;i<n;++i){const int sample=start+i-16;require(std::abs(l[size_t(i)]-(sample<0?0:.2f*std::sin(float(sample*.041))))<3.e-6,"Neutral wet quality/bypass transition misaligned");}}
    }
}
