#pragma once
// Numerical fixtures are independent of native editors/APVTS and use production
// kernels against the separately written bilinear reference.
static void settlingRegression(){
    // Independently computed with Python Decimal at 60 decimal digits, using
    // the sine/cosine Taylor series (see high-precision-bell-diagnostic.json).
    // Require exact rounding to double; do not relax the rendered tolerance.
    const auto precise=moduleReference::bell(48000,20,8,12);
    require(precise.b[0]==1.000244447138006&&precise.b[1]==-1.999829147171692
        &&precise.b[2]==0.9995915533596993&&precise.a[1]==-1.999829147171692
        &&precise.a[2]==0.9998360004977054,"independent coefficient precision calibration");
    constexpr double rate=44100,hz=20;auto p=hem::bands::defaults<Kind::Dynamic>();p[0]=1;p[3]=20;p[4]=6;p[5]=8;
    hem::bands::Core<Kind::Dynamic> core;core.prepare(rate,p);moduleReference::Engine reference(0,rate,p);
    double ss=0,cc=0,sc=0,ys=0,yc=0,maximumDifference=0;const int settle=int(1.5*rate),measure=int(8*rate/hz);
    for(int n=0;n<settle+measure;++n){const double angle=2*bandReference::pi*hz*n/rate,s=std::sin(angle),c=std::cos(angle);float l=float(.1*c),r=l;
        const auto expected=reference.tick(l,r);core.process(&l,&r,1,p);maximumDifference=std::max(maximumDifference,std::abs(double(l)-expected[0]));
        if(n>=settle){ss+=s*s;cc+=c*c;sc+=s*c;ys+=expected[0]*s;yc+=expected[0]*c;}}
    const double denominator=ss*cc-sc*sc;const std::complex<double> measured((yc*ss-ys*sc)/denominator/.1,-(ys*cc-yc*sc)/denominator/.1);
    const auto steady=moduleReference::bell(rate,hz,8,6).response(hz,rate);const double transientBias=std::abs(measured-steady);
    require(maximumDifference<=2e-5,"settling regression production/reference mismatch");
    require(transientBias>2e-5+2e-6*std::abs(steady),"old fixed window unexpectedly settled");
    std::cout<<"independent_old_1.5s_window_bias="<<transientBias<<" production_reference_sample_error="<<maximumDifference<<'\n';
}
static void dynamicQuantitative(double rate){
    for(size_t band=0;band<8;++band){auto p=hem::bands::defaults<Kind::Dynamic>();p[0]=1;const size_t i=2+band*8;
        p[i+1]=1000;p[i+4]=-30;p[i+5]=6;p[i+6]=1;p[i+7]=100;
        hem::bands::Core<Kind::Dynamic> core;core.prepare(rate,p);double inputPower=0,outputPower=0;
        const int total=int(rate);for(int n=0;n<total;++n){const float original=float(.25*std::sin(2*hem::pi*1000*n/rate));float l=original,r=original;
            core.process(&l,&r,1,p);if(n>total/2){inputPower+=double(original)*original;outputPower+=double(l)*l;}}
        require(std::abs(core.telemetry.read().cuts[band]-6)<=.02,"per-slot dynamic cut calibration");
        require(std::abs(10*std::log10(outputPower/inputPower)+6)<=.08,"per-slot dynamic center gain");
    }
    auto a=hem::bands::defaults<Kind::Dynamic>();a[0]=1;a[11]=1000;a[14]=-30;a[15]=12;auto b=a;
    hem::bands::Core<Kind::Dynamic> first,second;first.prepare(rate,a);second.prepare(rate,b);
    for(int n=0;n<int(rate*.3);++n){if(n%503==0){b[4]=b[4]==12?-12:12;b[3]=b[3]==20?18000:20;b[2]=1-b[2];}
        float l=float(.25*std::sin(2*hem::pi*1000*n/rate)),r=-l*.7f,x=l,y=r;first.process(&l,&r,1,a);second.process(&x,&y,1,b);
        require(std::abs(first.telemetry.read().cuts[1]-second.telemetry.read().cuts[1])<=1e-6,"preceding bell changed feed-forward detector");}
}
template<Kind K> static void staticEq(double rate){
    constexpr size_t stride=K==Kind::Dynamic?8:4;
    for(size_t band=0;band<8;++band)for(double db:{-12.,-6.,6.,12.})for(double q:{.25,1.,8.})for(double hz:{20.,1000.,18000.}){
        auto p=hem::bands::defaults<K>();p[0]=1;const size_t i=2+band*stride;p[i+1]=float(hz);p[i+2]=float(db);p[i+3]=float(q);
        hem::bands::Core<K> core;core.prepare(rate,p);const auto independent=moduleReference::bell(rate,hz,q,db);const auto expected=independent.response(hz,rate);
        // A fixed 1.5 s was insufficient for the legal 20 Hz / Q8 boosts.
        // Derive settling from independent digital poles, never relax error.
        const auto root=std::sqrt(std::complex<double>(independent.a[1]*independent.a[1]-4*independent.a[2],0));
        const double radius=std::max(std::abs((-independent.a[1]+root)*.5),std::abs((-independent.a[1]-root)*.5));
        require(radius>0&&radius<1,"reference pole radius");
        double ss=0,cc=0,sc=0,ys=0,yc=0;const int settle=std::max(64,int(std::ceil(std::log(1e-13)/std::log(radius)))),measure=std::max(64,int(std::ceil(8*rate/hz)));
        std::array<float,257> left{},right{};
        for(int position=0;position<settle+measure;){const int length=std::min(257,settle+measure-position);
            for(int j=0;j<length;++j){left[size_t(j)]=float(.1*std::cos(2*hem::pi*hz*(position+j)/rate));right[size_t(j)]=K==Kind::MidSide&&band>=4?-left[size_t(j)]:left[size_t(j)];}
            core.process(left.data(),right.data(),length,p);
            for(int j=0;j<length;++j){const int n=position+j;if(n<settle)continue;const double phase=2*hem::pi*hz*n/rate,s=std::sin(phase),c=std::cos(phase);
                const double l=left[size_t(j)],r=right[size_t(j)],y=K==Kind::MidSide?(band>=4?(l-r)*.5:(l+r)*.5):l;
                ss+=s*s;cc+=c*c;sc+=s*c;ys+=y*s;yc+=y*c;}
            position+=length;
        }
        const double denominator=ss*cc-sc*sc;const std::complex<double> measured((yc*ss-ys*sc)/denominator/.1,-(ys*cc-yc*sc)/denominator/.1);
        require(std::abs(20*std::log10(std::abs(measured))-db)<=.05,"EQ center gain calibration");
        if(std::abs(measured-expected)>2e-5+2e-6*std::abs(expected))std::cerr<<"complex fixture kind="<<int(K)<<" rate="<<rate<<" band="<<band<<" db="<<db<<" q="<<q<<" hz="<<hz<<" measured="<<measured<<" expected="<<expected<<" error="<<std::abs(measured-expected)<<'\n';
        require(std::abs(measured-expected)<=2e-5+2e-6*std::abs(expected),"EQ measured complex transfer");
        // Full independent logarithmic transfer sweep for each calibrated case.
        const auto production=hem::bands::designBell(rate,hz,q,db);const auto reference=moduleReference::bell(rate,hz,q,db);
        for(int point=0;point<2048;++point){const double f=20*std::pow(.45*rate/20,double(point)/2047);
            const auto ratio=production.response(2*hem::pi*f/rate)/reference.response(f,rate);
            require(std::abs(20*std::log10(std::abs(ratio)))<=.05,"EQ independent full sweep");}
    }
    auto p=hem::bands::defaults<K>();p[0]=1;const size_t second=2+stride;
    p[3]=p[second+1]=1000;p[4]=12;p[second+2]=-12;p[5]=p[second+3]=8;
    hem::bands::Core<K> core;core.prepare(rate,p);unsigned seed=901;
    for(int n=0;n<int(rate);++n){const float input=randomSample(seed);float l=input,r=input;core.process(&l,&r,1,p);
        require(std::abs(l-input)<=2e-5&&std::abs(r-input)<=2e-5,"reciprocal bells");}
}
static void midSideQuantitative(double rate){
    for(size_t band=0;band<4;++band)for(bool anti:{false,true}){auto p=hem::bands::defaults<Kind::MidSide>();p[0]=1;
        const size_t i=2+(anti?band:band+4)*4;p[i+1]=1000;p[i+2]=12;
        hem::bands::Core<Kind::MidSide> core;core.prepare(rate,p);
        for(int n=0;n<int(rate*.1);++n){float l=float(.2*std::sin(2*hem::pi*1000*n/rate)),r=anti?-l:l;const auto a=l,b=r;
            core.process(&l,&r,1,p);require(std::abs(l-a)<=2e-7&&std::abs(r-b)<=2e-7,"M/S opposite lane leakage");}
    }
    auto p=active<Kind::MidSide>();hem::bands::Core<Kind::MidSide> a,b;a.prepare(rate,p);b.prepare(rate,p);unsigned seed=96;
    for(int n=0;n<16001;++n){float l=randomSample(seed),r=randomSample(seed),x=r,y=l;a.process(&l,&r,1,p);b.process(&x,&y,1,p);
        require(std::abs(l-y)<=2e-6&&std::abs(r-x)<=2e-6,"M/S channel swap");}
}
template<Kind K> static void solos(double rate){for(int mask=0;mask<8;++mask){auto p=active<K>();for(size_t b=0;b<3;++b)p[(K==Kind::Dynamics?11:7)+b*(K==Kind::Dynamics?8:3)]=(mask&(1<<b))?1.f:0.f;
    hem::bands::Core<K> core;core.prepare(rate,p);moduleReference::Engine ref(static_cast<int>(K),rate,p);unsigned seed=907;
    for(int n=0;n<8192;++n){float l=randomSample(seed),r=randomSample(seed);const auto e=ref.tick(l,r);core.process(&l,&r,1,p);
        require(std::abs(l-e[0])<=2e-5&&std::abs(r-e[1])<=2e-5,"solo selected wrong band sum");}
}}
static void imaging(double rate){for(float width:{0.f,100.f,200.f}){
    auto p=hem::bands::defaults<Kind::Imager>();p[0]=1;for(size_t b=0;b<3;++b)p[5+b*3]=width;
    hem::bands::Core<Kind::Imager> core;core.prepare(rate,p);auto m1=bandReference::design(rate,200,2),m2=bandReference::design(rate,2500,2);unsigned seed=86;
    for(int n=0;n<16001;++n){float l=randomSample(seed),r=randomSample(seed);const double mid=(double(l)+r)/2,side=(double(l)-r)/2;
        const double expectedMid=m2.tick(m1.tick(mid,0),0),expectedSide=width*.01*m2.tick(m1.tick(side,1),1);core.process(&l,&r,1,p);
        require(std::abs((double(l)+r)/2-expectedMid)<=2e-5&&std::abs((double(l)-r)/2-expectedSide)<=2e-5,"uniform image algebra");}
    for(bool anti:{false,true}){core.prepare(rate,p);for(int n=0;n<4096;++n){float l=float(.2*std::sin(2*hem::pi*997*n/rate)),r=anti?-l:l;core.process(&l,&r,1,p);
        if(!anti)require(std::abs(l-r)<=2e-6,"mono created side");else if(width==0)require(std::abs(l)<=2e-6&&std::abs(r)<=2e-6,"zero width retained side");}
        if(!anti)require(std::abs(core.telemetry.read().correlation-1)<=1e-5,"mono correlation");else if(width>0)require(std::abs(core.telemetry.read().correlation+1)<=1e-5,"opposite correlation");}
    }
    auto p=active<Kind::Imager>(),q=p;p[4]=q[4]=1;p[5]=0;q[5]=200;hem::bands::Core<Kind::Imager> a,b;a.prepare(rate,p);b.prepare(rate,q);unsigned seed=21;
    for(int n=0;n<8192;++n){float l=randomSample(seed),r=randomSample(seed),x=l,y=r;a.process(&l,&r,1,p);b.process(&x,&y,1,q);require(std::abs(l-x)<=2e-6&&std::abs(r-y)<=2e-6,"low lock width leaked");}
    q[4]=p[4]=0;a.prepare(rate,p);b.prepare(rate,q);double sideDifference=0;
    for(int n=0;n<8192;++n){float l=randomSample(seed),r=randomSample(seed),x=l,y=r;a.process(&l,&r,1,p);b.process(&x,&y,1,q);
        require(std::abs((double(l)+r)-(double(x)+y))*.5<=2e-5,"width changed mono sum");sideDifference=std::max(sideDifference,std::abs(double(l)-x));}
    require(sideDifference>1e-4,"unlock did not restore saved width");
}
template<Kind K> static void benchmark(double rate,int block,std::ofstream& csv){
    auto p=active<K>();hem::bands::Core<K> core;core.prepare(rate,p);std::vector<float> l(size_t(block),.1f),r(size_t(block),-.03f);std::array<double,500> times{};
    for(size_t i=0;i<times.size();++i){std::fill(l.begin(),l.end(),.1f);std::fill(r.begin(),r.end(),-.03f);const auto start=std::chrono::steady_clock::now();
        allocationProbe::enabled=true;core.process(l.data(),r.data(),block,p);allocationProbe::enabled=false;times[i]=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
        csv<<int(K)<<','<<rate<<','<<block<<','<<i<<','<<times[i]<<','<<block/rate<<','<<(times[i]>block/rate)<<'\n';}
    const auto cold=times[0];std::sort(times.begin(),times.end());std::cout<<"core="<<int(K)<<" rate="<<rate<<" block="<<block<<" cold="<<cold<<" p50="<<times[250]<<" p95="<<times[475]<<" p99="<<times[495]<<" max="<<times.back()<<" deadline="<<block/rate<<'\n';
}
