#pragma once
// Isolated recurrence cost, not a plugin/track/104-EQ capacity measurement.
static void eqPrecisionTiming(){
    std::ofstream csv("eq-precision-kernel-timings.csv");csv<<"rate,block,compensated,iteration,milliseconds\n";
    double checksum=0;
    for(double rate:{44100.,48000.,96000.})for(int block:{1,17,64,128,257})for(int precise:{0,1}){
        std::array<hem::bands::Section,8> old;std::array<hem::bands::TypedSection,8> current;
        for(size_t b=0;b<8;++b){const double frequency=80*std::pow(2.,double(b));old[b].c=hem::bands::designBell(rate,frequency,1,6);current[b].configure(rate,frequency,1,6,0);}
        std::array<double,201> times{};std::array<double,257> left{},right{};
        for(size_t iteration=0;iteration<times.size();++iteration){for(int n=0;n<block;++n){left[size_t(n)]=.2*std::sin((iteration*size_t(block)+size_t(n))*.071);right[size_t(n)]=.13*std::cos((iteration*size_t(block)+size_t(n))*.039);}
            const auto begin=std::chrono::steady_clock::now();allocationProbe::enabled=true;
            for(int n=0;n<block;++n)for(size_t b=0;b<8;++b){if(precise){left[size_t(n)]=current[b].tick(left[size_t(n)],0);right[size_t(n)]=current[b].tick(right[size_t(n)],1);}
                else{left[size_t(n)]=old[b].tick(left[size_t(n)],0);right[size_t(n)]=old[b].tick(right[size_t(n)],1);}}
            allocationProbe::enabled=false;times[iteration]=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count();checksum+=left[size_t(block-1)]+right[size_t(block-1)];
            csv<<rate<<','<<block<<','<<precise<<','<<iteration<<','<<times[iteration]<<'\n';}
        const double cold=times[0];std::sort(times.begin(),times.end());
        std::cout<<"eq_recurrence_kernel rate="<<rate<<" block="<<block<<" compensated="<<precise<<" cold_ms="<<cold<<" p50_ms="<<times[100]<<" p95_ms="<<times[190]<<" p99_ms="<<times[198]<<" max_ms="<<times[200]<<'\n';
    }
    require(std::isfinite(checksum),"precision timing nonfinite result");
}
