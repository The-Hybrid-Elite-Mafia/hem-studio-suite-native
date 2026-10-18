#pragma once
template<Kind K> static bool expandedTailCase(double rate,int type,int sign,int nodes){
    using namespace hem::bands;auto p=expandedDefaults<K>();p[0]=1;
    constexpr bool dynamic=K==Kind::Dynamic;
    const double seconds=dynamic?60:24;
    if constexpr(dynamic){for(size_t b=0;b<8;++b){const size_t i=2+8*b,e=66+3*b;
        p[i+1]=20;p[i+2]=float(sign*12);p[i+3]=8;p[i+4]=-60;
        p[i+5]=nodes==1?18.f:0.f;p[i+6]=.1f;p[i+7]=1000;
        p[e]=float(type);p[e+1]=1;p[e+2]=float(sign>0);}}
    else{for(size_t b=0;b<10;++b){const size_t i=b==4?34:b==9?38:b<4?2+4*b:18+4*(b-5);
        p[i+1]=20;p[i+2]=float(sign*12);p[i+3]=8;p[42+b]=float(type);}}
    std::vector<ExpandedCore<K>> core(static_cast<size_t>(nodes));for(auto& c:core)require(c.prepare(rate,p),"tail preparation");
    constexpr int block=257;std::array<float,block> l{},r{};
    const double excitation=nodes==1?.25:1e-6;
    const int64_t excite=int64_t(rate*5),end=excite+int64_t(rate*seconds*nodes),window=end-int64_t(rate);
    double finalPeak=0,peak=0;bool finite=true;
    for(int64_t offset=0;offset<end;){const int n=int(std::min(int64_t(block),end-offset));
        for(int i=0;i<n;++i){const int64_t sample=offset+i;l[size_t(i)]=sample<excite?float(excitation*std::sin(2*hem::pi*20*double(sample)/rate)):0;r[size_t(i)]=-.7f*l[size_t(i)];}
        allocationProbe::enabled=true;for(auto& c:core)c.process(l.data(),r.data(),n,p);allocationProbe::enabled=false;
        for(int i=0;i<n;++i){finite=finite&&std::isfinite(l[size_t(i)])&&std::isfinite(r[size_t(i)]);
            const double value=std::max(std::abs(double(l[size_t(i)])),std::abs(double(r[size_t(i)])));peak=std::max(peak,value);
            if(offset+i>=window)finalPeak=std::max(finalPeak,value);}
        offset+=n;
    }
    // Serial fixture uses a small excitation to keep its stated static-gain
    // chain in float range; normalize decay back to a 0.25-amplitude input.
    const double normalized=finalPeak*(.25/excitation);
    const bool pass=finite&&normalized<1e-6;
    std::cout<<std::setprecision(17)<<"expanded_tail kind="<<int(K)<<" rate="<<rate<<" type="<<type<<" sign="<<sign<<" nodes="<<nodes<<" seconds="<<seconds*nodes<<" excitation="<<excitation<<" peak="<<peak<<" final_peak="<<finalPeak<<" normalized_final_peak="<<normalized<<" dynamic_range="<<(dynamic?p[7]:0)<<" result="<<(pass?"PASS":"FAIL")<<std::endl;
    return pass;
}
static void expandedTailReview(){bool pass=true;
    for(double rate:{44100.,48000.,96000.})for(int type:{0,1,2})for(int sign:{-1,1}){
        pass=expandedTailCase<Kind::Dynamic>(rate,type,sign,1)&&pass;
        pass=expandedTailCase<Kind::MidSide>(rate,type,sign,1)&&pass;}
    // Separately labelled serial static-gain states; not a claim that an
    // eight-node, 64-band +30 dB chain stays within float dynamic range.
    pass=expandedTailCase<Kind::Dynamic>(48000,0,1,8)&&pass;
    pass=expandedTailCase<Kind::MidSide>(48000,0,1,8)&&pass;
    require(pass,"expanded tail bound failed (all tail cases collected)");
}
