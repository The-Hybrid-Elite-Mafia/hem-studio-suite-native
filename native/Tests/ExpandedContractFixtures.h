#pragma once
template<Kind K> static auto expandedActive(){auto result=hem::bands::expandedDefaults<K>();const auto old=active<K>();for(size_t i=0;i<old.size();++i)result[i]=old[i];
    if constexpr(K==Kind::Dynamic)for(size_t b=0;b<8;++b){result[3+b*8]=1000;result[4+b*8]=6;result[6+b*8]=-30;result[7+b*8]=12;result[67+b*3]=1;}
    if constexpr(K==Kind::MidSide){for(size_t b=0;b<8;++b){result[3+b*4]=1000;result[4+b*4]=6;}result[35]=result[39]=1000;result[36]=result[40]=6;}
    if constexpr(K==Kind::Dynamics)for(size_t base:{size_t(29),size_t(37)}){result[base]=-30;result[base+1]=4;result[base+4]=6;result[base+6]=3;}
    if constexpr(K==Kind::Imager){result[15]=140;result[18]=170;result[16]=-2;result[19]=3;}
    return result;
}
template<Kind K> static void expandedControls(double rate){
    for(size_t index=0;index<hem::bands::ExpandedCore<K>::count;++index){auto p=expandedActive<K>();const auto& spec=hem::bands::expandedSpecs<K>()[index];
        const std::string key(spec.id);
        if constexpr(K==Kind::Dynamics||K==Kind::Imager)if(key.rfind("mid",0)==0||key=="legacyMidOn")p.back()=0;
        auto q=p;p[index]=spec.minimum;q[index]=spec.maximum;hem::bands::ExpandedCore<K> a,b;a.prepare(rate,p);b.prepare(rate,q);double difference=0,meterDifference=0;
        for(int block=0;block<100;++block){std::array<float,257> al{},ar{},bl{},br{};
            for(size_t n=0;n<257;++n){const double t=(block*257+int(n))/rate,envelope=block%20<10?.25:.001;
                al[n]=bl[n]=float(envelope*(std::sin(2*hem::pi*1000*t)+.4*std::sin(2*hem::pi*83*t)+.3*std::sin(2*hem::pi*6500*t)));
                ar[n]=br[n]=float(envelope*.8*std::cos(2*hem::pi*1000*t));}
            allocationProbe::enabled=true;a.process(al.data(),ar.data(),257,p);b.process(bl.data(),br.data(),257,q);allocationProbe::enabled=false;
            for(size_t n=0;n<257;++n)difference=std::max({difference,std::abs(double(al[n])-bl[n]),std::abs(double(ar[n])-br[n])});
            const auto am=a.telemetry.read(),bm=b.telemetry.read();for(size_t c=0;c<8;++c)meterDifference=std::max(meterDifference,std::abs(double(am.cuts[c])-bm.cuts[c]));
        }
        if(difference<=1e-7&&meterDifference<=1e-5){std::cerr<<"expanded inactive control kind="<<int(K)<<" id="<<spec.id<<'\n';throw std::runtime_error("expanded control has no measured effect");}
    }
}
template<Kind K> static void expandedPartitions(double rate){
    const int size=int(hem::bands::ExpandedCore<K>::count)*1006;std::vector<float> left(size),right(size),referenceLeft,referenceRight;unsigned seed=34;
    for(int n=0;n<size;++n){left[size_t(n)]=randomSample(seed);right[size_t(n)]=randomSample(seed);}
    for(int block:{1,17,64,128,257,1024,0}){auto settings=expandedActive<K>();hem::bands::ExpandedCore<K> core;core.prepare(rate,settings);auto a=left,b=right;
        int offset=0,turn=0;const int irregular[]{3,311,1,1024,19,71};
        while(offset<size){if(offset%503==0){const size_t index=size_t(offset/503)%settings.size();const auto& spec=hem::bands::expandedSpecs<K>()[index];settings[index]=settings[index]==spec.maximum?spec.minimum:spec.maximum;}
            const int count=std::min({size-offset,503-offset%503,block?block:irregular[(turn++)%6]});allocationProbe::enabled=true;core.process(a.data()+offset,b.data()+offset,count,settings);allocationProbe::enabled=false;offset+=count;}
        if(block==1){referenceLeft=a;referenceRight=b;}else for(int n=0;n<size;++n){if(std::abs(a[size_t(n)]-referenceLeft[size_t(n)])>2e-6||std::abs(b[size_t(n)]-referenceRight[size_t(n)])>2e-6){
                std::cerr<<"expanded partition kind="<<int(K)<<" rate="<<rate<<" block="<<block<<" sample="<<n<<'\n';throw std::runtime_error("expanded callback partition dependence");}}
    }
    if constexpr(K==Kind::Dynamics||K==Kind::Imager){
        // Rapid reversals during a 10 ms transition expose any accidental
        // dependence on the next callback boundary, especially at 96 kHz.
        constexpr int nSamples=10000;std::vector<float> reference;
        for(int block:{1,17,257,1024}){auto p=expandedActive<K>();hem::bands::ExpandedCore<K> core;core.prepare(rate,p);std::vector<float> l(nSamples,.13f),r(nSamples,-.04f);
            for(int offset=0;offset<nSamples;){if(offset%137==0)p.back()=float((offset/137)%2);const int n=std::min({nSamples-offset,137-offset%137,block});core.process(l.data()+offset,r.data()+offset,n,p);offset+=n;}
            if(block==1)reference=l;else for(int n=0;n<nSamples;++n)require(std::abs(l[size_t(n)]-reference[size_t(n)])<=2e-6,"queued topology partition dependence");}
    }
}
