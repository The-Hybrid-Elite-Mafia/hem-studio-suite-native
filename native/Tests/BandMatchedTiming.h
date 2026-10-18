#pragma once
#include <bit>
// Same source samples, controls, two-second prefix and measured sample count
// across block sizes. Earlier block*8 sources and 300 cold callbacks were NOT
// equal input/warmup conditions. Keep those measurements as separate evidence.
static void matchedBandTiming(const juce::File& dir){
    std::ofstream raw(dir.getChildFile("matched-timings.csv").getFullPathName().toStdString());
    raw<<"scope,rate,block,pass,iteration,samples,seconds,deadline,miss\n";
    std::ofstream conditions(dir.getChildFile("matched-conditions.csv").getFullPathName().toStdString());
    conditions<<"scope,rate,block,pass,source_frames,warm_samples,measured_samples,source_hash,control_hash,output_hash,output_energy,cold_seconds\n";
    auto hashWord=[](uint64_t& hash,uint32_t value){for(int i=0;i<4;++i){hash^=(value>>(8*i))&255u;hash*=1099511628211ULL;}};
    auto configure=[](hem::ModuleProcessor& p,int kind){activate(p);
        if(kind==0){for(int band=1;band<=8;++band){set(p,"b"+juce::String(band)+"On",band<=4?1.f:0.f);set(p,"b"+juce::String(band)+"DynamicOn",band<=4?1.f:0.f);}set(p,"b1Threshold",-48);}
        if(kind==1){set(p,"width",120);for(const char* id:{"m4On","m5On","s4On","s5On"})set(p,id,0);}
        if(kind==2)for(const char* prefix:{"low","lowMid","highMid","high"})set(p,juce::String(prefix)+"Threshold",-48);
    };
    auto run=[&](double rate,int block,int pass,int scenario,bool legacy){
        const bool host=scenario>=4;const int slots=scenario==5?4:1;
        const juce::String scope=(legacy?"OLD_BLOCK_DEPENDENT_":"MATCHED_")+juce::String(host?(slots==4?"13_tracks_four_modules":"13_tracks_one_mixed_module"):ids[size_t(scenario)]);
        const int frames=legacy?block*8:int(rate),warm=legacy?0:int(rate*2),measure=legacy?block*300:131584;
        juce::AudioBuffer<float> source(2,frames),sum(2,block);fill(source,0,rate);source.applyGain(.05f);uint64_t sourceHash=1469598103934665603ULL,controlHash=sourceHash,outputHash=sourceHash;
        for(int n=0;n<frames;++n)for(int c=0;c<2;++c)hashWord(sourceHash,std::bit_cast<uint32_t>(source.getSample(c,n)));
        juce::TemporaryFile fixture(".wav");juce::AudioFormatManager formats;formats.registerBasicFormats();juce::TimeSliceThread disk("Band matched WAV read-ahead");if(host){writeBandWave(fixture.getFile(),source,rate);disk.startThread();}hem::TrackBank bank;std::unique_ptr<hem::ModuleProcessor> single;hem::CoreMasterStage master;std::vector<float> inputGains(size_t(block),1.f);juce::MidiBuffer midi;
        auto describe=[&](hem::ModuleProcessor& p,int kind){hashWord(controlHash,uint32_t(kind));for(const auto& spec:p.parameterSpecs())hashWord(controlHash,std::bit_cast<uint32_t>(p.state.getRawParameterValue(spec.id)->load()));};
        if(host){master.prepare(rate,block);for(size_t t=0;t<13;++t){auto& track=bank.tracks[t];track.prepare(rate,block);track.playback->prepareFile(fixture.getFile(),formats,disk,rate,block,true);track.loaded=true;track.transport().start();
                for(int slot=0;slot<slots;++slot){const int kind=slots==4?slot:int(t%4);auto p=hem::PluginRegistry::create(ids[size_t(kind)]);configure(*p,kind);describe(*p,kind);track.chain.installInitial(slot,track.chain.stageNode(std::move(p),rate,block,track.chain.slotLatency()),false);}}}
        else{single=hem::PluginRegistry::create(ids[size_t(scenario)]);configure(*single,scenario);describe(*single,scenario);single->prepareToPlay(rate,block);}
        int position=0;
        auto process=[&](int n){if(host)awaitBandStreams(bank,n);sum.clear();if(!host)for(int i=0;i<n;++i)for(int c=0;c<2;++c)sum.setSample(c,i,source.getSample(c,(position+i)%frames));
            float* channels[]{sum.getWritePointer(0),sum.getWritePointer(1)};juce::AudioBuffer<float> view(channels,2,n);
            const auto start=std::chrono::steady_clock::now();allocationProbe::enabled=true;
            if(host){for(auto& track:bank.tracks)track.render(sum,n,false,inputGains.data());master.process(channels[0],channels[1],n);}else single->processBlock(view,midi);
            allocationProbe::enabled=false;const double seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();position+=n;return seconds;
        };
        std::vector<double> times;times.reserve(size_t((measure+block-1)/block));double energy=0;int misses=0,completed=0;const double cold=process(block);
        auto record=[&](double seconds,int n){const size_t iteration=times.size();times.push_back(seconds);misses+=seconds>n/rate;
            raw<<scope<<','<<rate<<','<<block<<','<<pass<<','<<iteration<<','<<n<<','<<seconds<<','<<n/rate<<','<<(seconds>n/rate)<<'\n';
            for(int i=0;i<n;++i)for(int c=0;c<2;++c){const float value=sum.getSample(c,i);hashWord(outputHash,std::bit_cast<uint32_t>(value));energy+=double(value)*value;}completed+=n;};
        if(legacy)record(cold,block);else while(position<warm)process(std::min(block,warm-position));
        while(completed<measure){const int n=std::min(block,measure-completed);record(process(n),n);}
        if(host)for(auto& track:bank.tracks)track.release();
        conditions<<scope<<','<<rate<<','<<block<<','<<pass<<','<<frames<<','<<warm<<','<<measure<<','<<sourceHash<<','<<controlHash<<','<<outputHash<<','<<std::setprecision(17)<<energy<<','<<cold<<'\n';
        std::sort(times.begin(),times.end());auto percentile=[&](double p){return times[size_t(std::floor(p*double(times.size()-1)))];};
        std::cout<<std::setprecision(9)<<"MATCHED_TIMING scope="<<scope<<" rate="<<rate<<" block="<<block<<" pass="<<pass<<" warm_samples="<<warm<<" measured_samples="<<measure<<" cold="<<cold<<" p50="<<percentile(.5)<<" p95="<<percentile(.95)<<" p99="<<percentile(.99)<<" max="<<times.back()<<" misses="<<misses<<'/'<<times.size()<<" source_hash="<<sourceHash<<" control_hash="<<controlHash<<" output_hash="<<outputHash<<'\n';
    };
    for(int pass=0;pass<2;++pass){const std::array<int,3> blocks=pass==0?std::array<int,3>{128,256,512}:std::array<int,3>{512,256,128};
        for(int block:blocks)for(int scenario=0;scenario<6;++scenario)run(48000,block,pass,scenario,false);
        const std::array<int,2> comparison=pass==0?std::array<int,2>{128,257}:std::array<int,2>{257,128};
        for(bool legacy:{true,false})for(int block:comparison)run(96000,block,pass,5,legacy);
    }
}

