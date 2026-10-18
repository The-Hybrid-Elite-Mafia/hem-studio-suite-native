#pragma once
// Practical workload measurements complement, never replace, the retained
// dense eight-EQ-per-track stress fixture. No capacity assertion is inferred.
static void realisticBandTiming(const juce::File& dir){
    std::ofstream csv(dir.getChildFile("realistic-timings.csv").getFullPathName().toStdString());csv<<"scope,rate,block,iteration,seconds,deadline,miss\n";
    for(double rate:{48000.,96000.})for(int block:{128,257})for(int scenario=0;scenario<5;++scenario){
        const int tracks=scenario<2?1:scenario==2?4:13,slots=scenario==1||scenario==4?4:1;
        const juce::String label=scenario==0?"one_track_one_dynamic_four_active_bands":scenario==1?"one_track_four_distinct_modules":scenario==2?"four_tracks_one_distinct_module_each":scenario==3?"13_tracks_one_mixed_module_each":"13_tracks_four_distinct_modules_each";
        juce::AudioBuffer<float> source(2,block*8),sum(2,block);fill(source,0,rate);source.applyGain(.05f);juce::TemporaryFile fixture(".wav");writeBandWave(fixture.getFile(),source,rate);juce::AudioFormatManager formats;formats.registerBasicFormats();juce::TimeSliceThread disk("Band realistic WAV read-ahead");disk.startThread();hem::TrackBank bank;
        std::vector<float> inputGains(size_t(block),1.f);hem::CoreMasterStage master;master.prepare(rate,block);
        for(int t=0;t<tracks;++t){auto& track=bank.tracks[size_t(t)];track.prepare(rate,block);track.playback->prepareFile(fixture.getFile(),formats,disk,rate,block,true);track.loaded=true;track.transport().start();
            for(int slot=0;slot<slots;++slot){const int kind=slots==4?slot:t%4;auto p=hem::PluginRegistry::create(ids[size_t(kind)]);activate(*p);
                if(kind==0){for(int band=1;band<=8;++band){set(*p,"b"+juce::String(band)+"On",band<=4?1.f:0.f);set(*p,"b"+juce::String(band)+"DynamicOn",band<=4?1.f:0.f);}set(*p,"b1Threshold",-48);}
                if(kind==1){set(*p,"width",120);for(const char* id:{"m4On","m5On","s4On","s5On"})set(*p,id,0);}
                if(kind==2)for(const char* prefix:{"low","lowMid","highMid","high"})set(*p,juce::String(prefix)+"Threshold",-48);
                track.chain.installInitial(slot,track.chain.stageNode(std::move(p),rate,block,track.chain.slotLatency()),false);
            }
        }
        std::array<double,300> times{};int misses=0;for(size_t iteration=0;iteration<times.size();++iteration){awaitBandStreams(bank,block);sum.clear();const auto start=std::chrono::steady_clock::now();allocationProbe::enabled=true;
            for(int t=0;t<tracks;++t)bank.tracks[size_t(t)].render(sum,block,false,inputGains.data());master.process(sum.getWritePointer(0),sum.getWritePointer(1),block);allocationProbe::enabled=false;
            const double elapsed=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();times[iteration]=elapsed;misses+=elapsed>block/rate;csv<<label<<','<<rate<<','<<block<<','<<iteration<<','<<elapsed<<','<<block/rate<<','<<(elapsed>block/rate)<<'\n';
        }
        for(int t=0;t<tracks;++t)bank.tracks[size_t(t)].release();const auto cold=times[0];std::sort(times.begin(),times.end());
        std::cout<<"REALISTIC scope="<<label<<" rate="<<rate<<" block="<<block<<" cold="<<cold<<" p50="<<times[150]<<" p95="<<times[285]<<" p99="<<times[297]<<" max="<<times.back()<<" deadline="<<block/rate<<" misses="<<misses<<"/300\n";
    }
}

