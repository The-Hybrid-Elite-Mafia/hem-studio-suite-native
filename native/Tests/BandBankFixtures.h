#pragma once
// Included only by BandBankTests.cpp; file I/O and vectors are test-side only.
static double maximumRenderedDbError=0,maximumRenderedPhaseError=0,maximumRenderedSampleError=0;
static double maximumRecoveryError=0,maximumTransitionError=0,maximumTransitionPeak=0;
static void compareTransfer(std::complex<double> actual,std::complex<double> expected,double dbTolerance){
    if(std::abs(expected)>1e-4){
        const double db=std::abs(20*std::log10(std::abs(actual/expected)));
        const double phase=std::abs(std::arg(actual/expected))*180/bandReference::pi;
        maximumRenderedDbError=std::max(maximumRenderedDbError,db);
        maximumRenderedPhaseError=std::max(maximumRenderedPhaseError,phase);
        check(db<=dbTolerance&&phase<=.5,"rendered sine magnitude/phase");
    }else check(std::abs(actual-expected)<=1e-5,"rendered sine stopband absolute error");
}
static void renderedSweep(double rate,double low,double high,std::ofstream& vectors){
    // Quadrature L/R permits exact complex demodulation without requiring an
    // integer-cycle window. Every measured output comes from actual tick().
    const auto l1=bandReference::design(rate,low,0),h1=bandReference::design(rate,low,1);
    const auto l2=bandReference::design(rate,high,0),h2=bandReference::design(rate,high,1);
    const auto a1=bandReference::design(rate,low,2),a2=bandReference::design(rate,high,2);
    for(int point=0;point<2048;++point){
        const double hz=20*std::pow(.45*rate/20,double(point)/2047);
        const double w=2*bandReference::pi*hz/rate,amplitude=.125;
        const auto p=l1.response(hz,rate),q=h1.response(hz,rate);
        const auto r=l2.response(hz,rate),s=h2.response(hz,rate),a=a2.response(hz,rate);
        const std::array<std::complex<double>,4> expected{a*p*p,r*r*q*q,s*s*q*q,a*a1.response(hz,rate)};
        hem::bands::ThreeBandBank bank;bank.prepare(rate,low,high);
        const int settle=int(std::ceil(.2*rate));
        const int measure=std::max(64,int(std::ceil(2*rate/hz)));
        std::array<std::complex<double>,4> measured{};
        double worstSample=0;
        for(int n=0;n<settle+measure;++n){
            const auto oscillator=std::polar(1.,w*n);
            allocationProbe::enabled=true;
            const auto y=bank.tick(amplitude*oscillator.real(),amplitude*oscillator.imag());
            allocationProbe::enabled=false;
            if(n<settle)continue;
            std::complex<double> sum{};
            for(size_t band=0;band<3;++band){
                const std::complex<double> output(y[band][0],y[band][1]);sum+=output;
                measured[band]+=output*std::conj(oscillator)/amplitude;
                const auto residual=output-amplitude*expected[band]*oscillator;
                worstSample=std::max({worstSample,std::abs(residual.real()),std::abs(residual.imag())});
            }
            measured[3]+=sum*std::conj(oscillator)/amplitude;
            const auto error=sum-amplitude*expected[3]*oscillator;
            worstSample=std::max({worstSample,std::abs(error.real()),std::abs(error.imag())});
        }
        maximumRenderedSampleError=std::max(maximumRenderedSampleError,worstSample);
        for(size_t band=0;band<4;++band){
            measured[band]/=double(measure);
            vectors<<rate<<','<<low<<','<<high<<','<<hz<<','<<band<<','<<measured[band].real()<<','<<measured[band].imag()
                   <<','<<expected[band].real()<<','<<expected[band].imag()<<','<<worstSample<<'\n';
        }
        try{
            check(worstSample<=2e-5,"rendered settled sample reference");
            for(size_t band=0;band<4;++band)compareTransfer(measured[band],expected[band],band==3?.02:.03);
        }catch(...){vectors.flush();std::cerr<<"sweep failure rate="<<rate<<" low="<<low<<" high="<<high<<" hz="<<hz<<'\n';throw;}
        // Uniform scaling is applied independently to each emitted branch.
        for(double db:{-6.,6.}){
            const double gain=std::pow(10.,db/20);
            const auto scaled=gain*measured[0]+gain*measured[1]+gain*measured[2];
            compareTransfer(scaled,gain*expected[3],.03);
        }
    }
    vectors.flush();check(bool(vectors),"sweep evidence write");
}
static void transitionFixture(double rate,std::ofstream& samples,std::ofstream& spectra){
    const int count=int(rate*.3),first=int(rate*.08),second=int(rate*.15),third=int(rate*.22);
    std::vector<double> actual(size_t(count),0),expected(size_t(count),0),dry(size_t(count),0);
    hem::bands::ThreeBandBank bank;bank.prepare(rate,80,1200);bandReference::Bank ref(rate,80,1200);
    for(int i=0;i<count;++i){
        if(i==first||i==second||i==third){const bool up=i!=second;const double low=up?800:80,high=up?8000:1200;
            allocationProbe::enabled=true;bank.targets(low,high);allocationProbe::enabled=false;ref.targets(low,high);}
        const double t=i/rate,l=.08*(std::sin(2*bandReference::pi*97*t)+std::sin(2*bandReference::pi*997*t)+std::sin(2*bandReference::pi*6011*t));
        const double r=.17*std::cos(2*bandReference::pi*311*t);dry[size_t(i)]=l;
        allocationProbe::enabled=true;const auto a=bank.tick(l,r);allocationProbe::enabled=false;const auto e=ref.tick(l,r);
        for(size_t band=0;band<3;++band)for(size_t ch=0;ch<2;++ch){
            const double error=std::abs(a[band][ch]-e[band][ch]);maximumTransitionError=std::max(maximumTransitionError,error);
            check(error<=2e-5,"transition sample reference");
        }
        for(size_t b=0;b<3;++b){actual[size_t(i)]+=a[b][0];expected[size_t(i)]+=e[b][0];}
        maximumTransitionPeak=std::max(maximumTransitionPeak,std::abs(actual[size_t(i)]));
        samples<<rate<<','<<i<<','<<l<<','<<r;
        for(size_t b=0;b<3;++b)for(size_t c=0;c<2;++c)samples<<','<<a[b][c]<<','<<e[b][c];
        samples<<'\n';
    }
    // Hann-windowed absolute amplitude spectra around every cutoff event.
    // These are measured artifacts, not a claim of time-invariant flatness.
    const int width=int(rate*.04);
    for(int event:{first,second,third})for(int offset:{-width,0,width}){
        const int start=event+offset;if(start<0||start+width>count)continue;
        for(int k=0;k<256;++k){const double hz=20*std::pow(.45*rate/20,double(k)/255);
            std::complex<double> aa{},ee{},dd{};double weightSum=0;
            for(int n=0;n<width;++n){const double window=.5-.5*std::cos(2*bandReference::pi*n/(width-1));weightSum+=window;
                const auto z=window*std::polar(1.,-2*bandReference::pi*hz*n/rate);const auto index=size_t(start+n);
                aa+=actual[index]*z;ee+=expected[index]*z;dd+=dry[index]*z;}
            spectra<<rate<<','<<event<<','<<start<<','<<width<<','<<hz<<','<<2*std::abs(aa)/weightSum<<','
                   <<2*std::abs(ee)/weightSum<<','<<2*std::abs(dd)/weightSum<<'\n';
        }
    }
    samples.flush();spectra.flush();check(bool(samples)&&bool(spectra),"transition evidence write");
}
static void recoveryFixtures(double rate){
    for(double invalid:{0.,-1.,7999.,384001.,std::numeric_limits<double>::quiet_NaN(),std::numeric_limits<double>::infinity()}){
        hem::bands::ThreeBandBank bank;bank.prepare(rate);check(!bank.prepare(invalid),"invalid reprepare accepted");
        const auto silent=bank.tick(.2,-.2);for(auto b:silent)for(auto x:b)check(x==0,"failed prepare emitted");
        check(bank.prepare(rate),"valid reprepare failed");
    }
    const double nan=std::numeric_limits<double>::quiet_NaN(),inf=std::numeric_limits<double>::infinity();
    {hem::bands::ThreeBandBank bank;bank.prepare(rate,80,1200);bandReference::Bank ref(rate,80,1200);
        allocationProbe::enabled=true;bank.targets(800,8000);bank.targets(nan,-inf);allocationProbe::enabled=false;ref.targets(800,8000);
        for(int i=0;i<int(rate*.02);++i){allocationProbe::enabled=true;const auto a=bank.tick(.2,-.1);allocationProbe::enabled=false;const auto e=ref.tick(.2,-.1);
            for(size_t b=0;b<3;++b)for(size_t c=0;c<2;++c)check(std::abs(a[b][c]-e[b][c])<=2e-5,"nonfinite target lost last valid nondefault target");}}
    for(double low:{-1e12,80.,800.,1e12,nan,inf,-inf})for(double high:{-1e12,1200.,8000.,1e12,nan,inf,-inf}){
        hem::bands::ThreeBandBank bank;bank.prepare(rate,200,2500);bandReference::Bank ref(rate,200,2500);
        allocationProbe::enabled=true;bank.targets(low,high);allocationProbe::enabled=false;
        ref.targets(std::isfinite(low)?std::clamp(low,80.,800.):200.,std::isfinite(high)?std::clamp(high,1200.,8000.):2500.);
        unsigned seed=90;for(int n=0;n<int(rate*.02);++n){const double x=noise(seed),y=noise(seed);
            allocationProbe::enabled=true;const auto a=bank.tick(x,y);allocationProbe::enabled=false;const auto e=ref.tick(x,y);
            for(size_t b=0;b<3;++b)for(size_t c=0;c<2;++c){const double error=std::abs(a[b][c]-e[b][c]);
                maximumRecoveryError=std::max(maximumRecoveryError,error);check(error<=2e-5,"bad setting clamp/retention");}}
    }
    for(size_t damaged=0;damaged<2;++damaged)for(double bad:{nan,inf,-inf}){
        hem::bands::ThreeBandBank bank,control;bank.prepare(rate,80,8000);control.prepare(rate,80,8000);
        for(int i=0;i<100;++i){bank.tick(.2,-.1);control.tick(.2,-.1);}
        allocationProbe::enabled=true;const auto a=bank.tick(damaged==0?bad:.2,damaged==1?bad:-.1);allocationProbe::enabled=false;
        const auto e=control.tick(.2,-.1);
        for(size_t b=0;b<3;++b){check(a[b][damaged]==0,"bad input did not reset affected channel");
            check(a[b][1-damaged]==e[b][1-damaged],"recovery altered opposite channel");}
        hem::bands::ThreeBandBank fresh;fresh.prepare(rate,80,8000);
        for(int i=0;i<2048;++i){allocationProbe::enabled=true;const auto y=bank.tick(.1,-.17);allocationProbe::enabled=false;const auto z=fresh.tick(.1,-.17);
            for(size_t b=0;b<3;++b)check(std::abs(y[b][damaged]-z[b][damaged])<=2e-5,"history remained poisoned");}
    }
    for(double scale:{8.,1e6}){hem::bands::ThreeBandBank bank;bank.prepare(rate,80,8000);bandReference::Bank ref(rate,80,8000);unsigned seed=74;
        for(int n=0;n<8192;++n){const double x=4*scale*noise(seed),y=4*scale*noise(seed);
            allocationProbe::enabled=true;const auto a=bank.tick(x,y);allocationProbe::enabled=false;const auto e=ref.tick(x,y);
            for(size_t b=0;b<3;++b)for(size_t c=0;c<2;++c)check(std::isfinite(a[b][c])&&std::abs(a[b][c]-e[b][c])<=2e-5+2e-6*std::abs(e[b][c]),"finite stress/reference");}
    }
    // Deliberate double overflow is a safe-failure test, not a clipping policy.
    hem::bands::ThreeBandBank bank;bank.prepare(rate,800,8000);
    for(int n=0;n<512;++n){allocationProbe::enabled=true;const auto a=bank.tick(n%2?std::numeric_limits<double>::max():-std::numeric_limits<double>::max(),.1);allocationProbe::enabled=false;
        for(auto b:a)for(auto x:b)check(std::isfinite(x),"overflow escaped bank");}
    for(int n=0;n<int(rate);++n){allocationProbe::enabled=true;const auto a=bank.tick(.1,-.2);allocationProbe::enabled=false;
        for(auto b:a)for(auto x:b)check(std::isfinite(x),"overflow poisoned later blocks");}
}
