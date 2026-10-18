#pragma once
// Test-only oracle for approved faceplates 26/27/28. Every lane is explicitly
// transcribed in 1672x941 artwork coordinates. Do not call the production meter
// layout, normalization, segment, mark, colour, or editor helpers here.
struct NonlinearMeterLayout {
    const char* id;
    std::array<juce::Rectangle<float>,4> lanes;
};
const NonlinearMeterLayout* nonlinearMeterLayout(const ModuleProcessor& p) {
    static const std::array<NonlinearMeterLayout,3> layouts {{
        {"parallel-distortion", {{{103,238,28.5f,245},{139.5f,238,28.5f,245},
                                  {1505,238,27,245},{1540,238,27,245}}}},
        {"exciter", {{{84,201,33,140},{125,201,33,140},
                      {1513,201,33,140},{1554,201,33,140}}}},
        {"tape", {{{105,225,31,259},{144,225,31,259},
                   {1496,225,31,259},{1535,225,31,259}}}}
    }};
    const auto id=PluginRegistry::idFor(p);
    for(const auto& layout:layouts)if(id==layout.id)return &layout;
    return nullptr;
}
juce::Rectangle<float> nonlinearMeterPixels(const juce::Image& image,juce::Rectangle<float> area) {
    return {area.getX()*image.getWidth()/1672.f,area.getY()*image.getHeight()/941.f,
            area.getWidth()*image.getWidth()/1672.f,area.getHeight()*image.getHeight()/941.f};
}
// Calibrated raster oracle: 24 bins of 2.5 dB, -60..0 dBFS, top-down pixel
// positions for bottom-up level growth. RMS is an independently placed 1.5-unit
// white central stripe, clamped inside the lane. GUI primitives only rasterize
// this test-owned contract; expected amplitudes come from actual sample arrays.
juce::Image nonlinearMeterReference(const juce::Image& image,const NonlinearMeterLayout& layout,
                                    const PluginTelemetry::Levels& expected,bool live,bool rms=true) {
    juce::Image reference(juce::Image::ARGB,image.getWidth(),image.getHeight(),true);
    juce::Graphics g(reference);
    g.addTransform(juce::AffineTransform::scale(image.getWidth()/1672.f,image.getHeight()/941.f));
    for(size_t channel=0;channel<4;++channel){
        const auto lane=layout.lanes[channel];
        g.setColour(juce::Colour(0xff020609));g.fillRect(lane);
        const float peakDb=expected.peak[channel]>0?20.f*std::log10(expected.peak[channel]):-160.f;
        for(int row=0;row<24;++row){
            const float threshold=-60.f+2.5f*(24-row);
            const bool lit=live&&peakDb>=threshold;
            g.setColour(!lit?juce::Colour(0xff10232b):threshold>=-3.f?juce::Colour(0xffff655b):
                        threshold>=-12.f?juce::Colour(0xffffbd59):juce::Colour(0xff41d7c5));
            g.fillRect(juce::Rectangle<float>(lane.getX(),lane.getY()+row*(lane.getHeight()/24.f)+.5f,
                                             lane.getWidth(),lane.getHeight()/24.f-1.f));
        }
        const float rmsDb=expected.rms[channel]>0?20.f*std::log10(expected.rms[channel]):-160.f;
        if(live&&rms&&rmsDb>-60.f){
            const float position=std::clamp((rmsDb+60.f)/60.f,0.f,1.f);
            const float y=std::clamp(lane.getBottom()-position*lane.getHeight()-.75f,
                                     lane.getY(),lane.getBottom()-1.5f);
            g.setColour(juce::Colours::white);
            g.fillRect(juce::Rectangle<float>(lane.getX()+lane.getWidth()*.3f,y,lane.getWidth()*.4f,1.5f));
            // The production contract includes an opaque narrow RMS highlight.
            g.fillRect(juce::Rectangle<float>(lane.getX()+lane.getWidth()*.45f,y,lane.getWidth()*.1f,1.5f));
        }
    }
    return reference;
}
bool verifyNonlinearMeters(const juce::Image& image,const ModuleProcessor& p,
                           const PluginTelemetry::Levels& expected,bool live,juce::String* failure=nullptr) {
    const auto* layout=nonlinearMeterLayout(p);require(layout,"Missing independent nonlinear meter profile");
    auto fail=[&](const juce::String& detail){if(failure)*failure=juce::String(layout->id)+": "+detail;return false;};
    if(!actualMeterRasterSize(image))return fail("expected 1280x720 editor at 1x or 2x");
    if(live)for(size_t ch=0;ch<4;++ch)
        if(!std::isfinite(expected.peak[ch])||!std::isfinite(expected.rms[ch])||expected.peak[ch]<0||expected.rms[ch]<0)
            return fail("nonfinite or negative expected sample measurement");
    const auto reference=nonlinearMeterReference(image,*layout,expected,live);
    for(size_t channel=0;channel<4;++channel){
        const auto pixels=nonlinearMeterPixels(image,layout->lanes[channel]);
        int checked=0;
        // Interior pixels include every segment, inter-segment gap and the RMS
        // stripe, including its anti-aliased top/bottom. Only the lane border is
        // excluded, where the faceplate background contributes fractional alpha.
        for(int y=int(std::ceil(pixels.getY()))+1;y<int(std::floor(pixels.getBottom()))-1;++y)
            for(int x=int(std::ceil(pixels.getX()))+1;x<int(std::floor(pixels.getRight()))-1;++x){
                ++checked;const auto a=image.getPixelAt(x,y),b=reference.getPixelAt(x,y);
                if(std::abs(int(a.getRed())-b.getRed())>4||std::abs(int(a.getGreen())-b.getGreen())>4||std::abs(int(a.getBlue())-b.getBlue())>4)
                    return fail("lane "+juce::String(int(channel))+" pixel ("+juce::String(x)+","+juce::String(y)+") expected "+b.toDisplayString(true)+", got "+a.toDisplayString(true));
            }
        if(checked<1000)return fail("insufficient whole-lane peak/RMS raster evidence");
    }
    if(failure)failure->clear();return true;
}
void assertNonlinearMeterMutants(const juce::Image& image,const ModuleProcessor& p,
                                 const PluginTelemetry::Levels& expected,bool live) {
    juce::String failure;require(verifyNonlinearMeters(image,p,expected,live,&failure),failure);
    const auto& layout=*nonlinearMeterLayout(p);
    auto fabricated=image.createCopy();
    {juce::Graphics g(fabricated);g.setColour(juce::Colours::cyan);g.fillRect(nonlinearMeterPixels(image,layout.lanes[0]));}
    require(!verifyNonlinearMeters(fabricated,p,expected,live),"Nonlinear meter gate accepted fabricated full fill");
    if(!live)return;
    for(size_t channel=0;channel<4;++channel){
        const float db=expected.peak[channel]>0?20.f*std::log10(expected.peak[channel]):-160.f;
        if(db<-57.5f)continue;
        const auto lanePixels=nonlinearMeterPixels(image,layout.lanes[channel]);
        auto erased=image.createCopy();
        {juce::Graphics g(erased);g.setColour(juce::Colours::black);g.fillRect(lanePixels);}
        require(!verifyNonlinearMeters(erased,p,expected,true),"Nonlinear meter gate accepted an erased lit lane");
        auto moved=image.createCopy();const auto source=lanePixels.getSmallestIntegerContainer();
        const auto old=image.getClippedImage(source).createCopy();
        {juce::Graphics g(moved);g.setColour(juce::Colours::black);g.fillRect(source);g.drawImageAt(old,source.getX()+5,source.getY());}
        require(!verifyNonlinearMeters(moved,p,expected,true),"Nonlinear meter gate accepted a displaced lane");
        auto corrupt=image.createCopy();
        const auto point=nonlinearMeterPixels(image,{layout.lanes[channel].getX()+layout.lanes[channel].getWidth()*.15f,
                                                    layout.lanes[channel].getBottom()-layout.lanes[channel].getHeight()/48.f,1,1});
        corrupt.setPixelAt(int(std::floor(point.getX())),int(std::floor(point.getY())),juce::Colours::magenta);
        require(!verifyNonlinearMeters(corrupt,p,expected,true),"Nonlinear meter gate accepted a corrupted segment");
        auto wrongPeak=expected;wrongPeak.peak[channel]*=.01f;
        require(!verifyNonlinearMeters(image,p,wrongPeak,true),"Nonlinear meter gate accepted a 40 dB peak calibration error");
    }
    bool visibleRms=false;
    for(size_t channel=0;channel<4;++channel)if(expected.rms[channel]>.001f){
        visibleRms=true;auto wrongRms=expected;wrongRms.rms[channel]*=.1f;
        require(!verifyNonlinearMeters(image,p,wrongRms,true),"Nonlinear meter gate accepted a 20 dB RMS displacement");
    }
    if(visibleRms){
        auto absentRms=image.createCopy();const auto peaksOnly=nonlinearMeterReference(image,layout,expected,true,false);
        for(const auto lane:layout.lanes){const auto area=nonlinearMeterPixels(image,lane).getSmallestIntegerContainer();
            for(int y=area.getY();y<area.getBottom();++y)for(int x=area.getX();x<area.getRight();++x)absentRms.setPixelAt(x,y,peaksOnly.getPixelAt(x,y));}
        require(!verifyNonlinearMeters(absentRms,p,expected,true),"Nonlinear meter gate accepted missing white RMS markers");
    }
    if(std::abs(expected.peak[0]-expected.peak[1])>.002f){
        auto swapped=expected;std::swap(swapped.peak[0],swapped.peak[1]);std::swap(swapped.rms[0],swapped.rms[1]);
        require(!verifyNonlinearMeters(image,p,swapped,true),"Nonlinear meter gate accepted swapped input stereo lanes");
    }
    if(std::abs(expected.peak[2]-expected.peak[3])>.002f){
        auto swapped=expected;std::swap(swapped.peak[2],swapped.peak[3]);std::swap(swapped.rms[2],swapped.rms[3]);
        require(!verifyNonlinearMeters(image,p,swapped,true),"Nonlinear meter gate accepted swapped output stereo lanes");
    }
}
bool nonlinearNoBar(ModuleProcessor& p,const juce::String& name){
    if(!nonlinearMeterLayout(p))return false;
    // A newly opened editor must not revive a publication that preceded it.
    std::unique_ptr<juce::AudioProcessorEditor> editor(p.createEditor());
    for(int scale:{1,2}){const auto image=rasterEditor(*editor,scale);juce::String failure;
        require(verifyNonlinearMeters(image,p,{},false,&failure),name+": "+failure);
        assertNonlinearMeterMutants(image,p,{},false);save(image,name+"-"+juce::String(scale)+"x");}
    return true;
}
void testNonlinearMeterCalibration(){
    int checked=0;
    for(const auto& entry:PluginRegistry::entries()){
        auto p=entry.create();if(!nonlinearMeterLayout(*p))continue;
        prepare(*p,48000,512);parameter(*p,"output",-6);
        auto* telemetry=p->pluginTelemetry();require(telemetry,"Missing nonlinear shared stereo telemetry");
        const bool workerExpected=juce::String(entry.id)=="tape";
        require(telemetry->analysis.operational()==workerExpected,"Nonlinear worker/levels-only contract changed");
        std::unique_ptr<juce::AudioProcessorEditor> editor(p->createEditor());
        PluginTelemetry::Levels expected;std::array<double,4> energy{};
        const double decay=std::exp(-1.0/(.3*48000));
        juce::AudioBuffer<float> buffer(2,512);
        auto feed=[&](float left,float right){
            expected.peak={};
            for(int i=0;i<512;++i){const float phase=float(2*pi*i/64);
                buffer.setSample(0,i,left*std::sin(phase));buffer.setSample(1,i,right*std::cos(phase));}
            for(int ch=0;ch<2;++ch)for(int i=0;i<512;++i){const float value=buffer.getSample(ch,i);
                expected.peak[size_t(ch)]=std::max(expected.peak[size_t(ch)],std::abs(value));
                energy[size_t(ch)]=decay*energy[size_t(ch)]+(1-decay)*double(value)*value;}
            freshProcess(*p,buffer);
            for(int ch=0;ch<2;++ch)for(int i=0;i<512;++i){const float value=buffer.getSample(ch,i);
                expected.peak[size_t(ch+2)]=std::max(expected.peak[size_t(ch+2)],std::abs(value));
                energy[size_t(ch+2)]=decay*energy[size_t(ch+2)]+(1-decay)*double(value)*value;}
            PluginTelemetry::Levels captured;require(telemetry->read(captured)&&captured.valid&&captured.blocks&&captured.samples==512&&captured.rate==48000&&captured.epoch==telemetry->analysis.epoch(),"Nonlinear level block metadata is incoherent");
            for(size_t ch=0;ch<4;++ch){expected.rms[ch]=float(std::sqrt(energy[ch]));
                near(captured.peak[ch],expected.peak[ch],1.e-6f,"Nonlinear peak disagrees with actual input/output samples");
                near(captured.rms[ch],expected.rms[ch],2.e-6f,"Nonlinear RMS disagrees with independent 300 ms sample-energy recurrence");}
        };
        auto verify=[&](const juce::String& state,bool live){std::array<juce::Image,2> images;for(int scale:{1,2}){
            const auto image=rasterEditor(*editor,scale);images[size_t(scale-1)]=image;juce::String failure;
            require(verifyNonlinearMeters(image,*p,expected,live,&failure),state+": "+failure);
            assertNonlinearMeterMutants(image,*p,expected,live);
        }
        // Disk encoding can outlast the 750 ms stopped-audio window. Keep it
        // outside both live scale assertions, without extending that window.
        for(int scale:{1,2})save(images[size_t(scale-1)],juce::String(entry.id)+"-nonlinear-"+state+"-"+juce::String(scale)+"x");};
        verify("waiting",false);
        for(const auto amplitudes:std::array<std::array<float,2>,4>{{{{.5f,.25f}},{{.1f,.01f}},{{1.2f,.005f}},{{.0005f,0.f}}}}){
            for(int block=0;block<40;++block)feed(amplitudes[0],amplitudes[1]);
            verify("calibrated-"+juce::String(amplitudes[0],4),true);
        }
        // Last paint established fresh audio. Frozen publications, including
        // recent-looking worker state, must expire after the 750 ms window.
        juce::Thread::sleep(800);verify("stale",false);
        editor.reset(p->createEditor());verify("reopened-frozen",false);
        feed(.5f,.02f);verify("reopened-fresh",true);
        const auto oldEpoch=telemetry->analysis.epoch();prepare(*p,48000,512);
        require(telemetry->analysis.epoch()!=oldEpoch,"Reprepare did not advance nonlinear telemetry epoch");
        verify("reprepared-waiting",false);energy={};expected={};
        feed(0,0);verify("real-silence",true);
        for(int block=0;block<12;++block)feed(.4f,.025f);
        verify("reprepared-fresh",true);
        p->releaseResources();verify("released",false);
        std::cout<<"PASS independent nonlinear peak/RMS raster and mutants: "<<entry.id<<"\n";
        ++checked;
    }
    require(checked==3,"Nonlinear meter coverage changed; explicitly map approved 26/27/28 profiles");
}
