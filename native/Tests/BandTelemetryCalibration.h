#pragma once
static void bandTelemetryCalibration(){
    for(int mode=0;mode<4;++mode){auto p=hem::PluginRegistry::create(ids[3]);p->prepareToPlay(48000,257);set(*p,"enabled",1);auto* band=dynamic_cast<hem::BandModuleProcessor*>(p.get());band->subscribeVisuals(true);
        juce::AudioBuffer<float> buffer(2,257);juce::MidiBuffer midi;int offset=0;
        for(int block=0;block<400;++block){for(int n=0;n<257;++n){const double angle=2*hem::pi*1000*(offset+n)/48000.;const float left=float(.1*std::sin(angle));
                buffer.setSample(0,n,left);buffer.setSample(1,n,mode==0?left:mode==1?-left:mode==2?0.f:float(.1*std::cos(angle)));}
            allocationProbe::enabled=true;p->processBlock(buffer,midi);allocationProbe::enabled=false;offset+=257;}
        hem::bands::VisualFrame frame;require(band->visualSnapshot(frame)&&frame.processed&&frame.scopeCount==32,"imager real bounded scope frame");
        for(size_t b=0;b<4;++b){if(mode==2)require(!frame.correlationValid[b],"one-channel band correlation must be undefined");else{require(frame.correlationValid[b],"known-energy band correlation unavailable");
                const double expected=mode==0?1:mode==1?-1:0;require(std::abs(frame.correlations[b]-expected)<1e-5,"actual band correlation calibration");}
            if(mode<3)for(unsigned n=0;n<frame.scopeCount;++n){const auto& point=frame.scopes[b][n];const float expected=mode==0?point[0]:mode==1?-point[0]:0;require(std::abs(point[1]-expected)<1e-7f,"actual scope sample stereo relationship");}}
        band->subscribeVisuals(false);
    }
    for(int direction:{0,1}){auto p=hem::PluginRegistry::create(ids[0]);p->prepareToPlay(48000,257);set(*p,"enabled",1);set(*p,"b1Freq",1000);set(*p,"b1Q",1);set(*p,"b1Threshold",-30);set(*p,"b1Range",6);set(*p,"b1DynamicOn",1);set(*p,"b1Up",float(direction));
        auto* band=dynamic_cast<hem::BandModuleProcessor*>(p.get());band->subscribeVisuals(true);juce::AudioBuffer<float> buffer(2,257);juce::MidiBuffer midi;
        for(int block=0;block<400;++block){for(int n=0;n<257;++n){const float value=float(.25*std::sin(2*hem::pi*1000*(block*257+n)/48000.));buffer.setSample(0,n,value);buffer.setSample(1,n,value);}allocationProbe::enabled=true;p->processBlock(buffer,midi);allocationProbe::enabled=false;}
        hem::bands::VisualFrame frame;require(band->visualSnapshot(frame)&&frame.processed,"dynamic signed visual frame");require(std::abs(frame.signedDynamics[0]-(direction?6.:-6.))<.01,"applied signed dynamic meter direction/range calibration");
        band->subscribeVisuals(false);
    }
    std::cout<<"PASS visual telemetry calibration: four-band +1/-1/quadrature correlations and undefined one-channel cases; actual 32-sample scope relationships; Dynamic DOWN -6 / UP +6 dB\n";
}
