#pragma once
// Test-only oracle for original approved faceplates 02/06/09. The 12 lane
// rectangles below are independently transcribed in the original 1672x941
// artwork coordinates. These live I/O apertures fit the retained authored glass.
// Never call production layout, normalization, segment, mark or colour helpers.
struct RestorationMeterLayout {
    const char* id;
    std::array<juce::Rectangle<float>,4> lanes;
};
const RestorationMeterLayout* restorationMeterLayout(const ModuleProcessor& p) {
    static const std::array<RestorationMeterLayout,3> layouts {{
        {"dynamic-resonance-suppressor", {{{84,251,38.5f,236},{132.5f,251,38.5f,236},
                                           {1502,251,38.5f,236},{1550.5f,251,38.5f,236}}}},
        {"breath-mouth-control", {{{83,213,22.5f,133},{115.5f,213,22.5f,133},
                                   {1533,213,22.5f,133},{1565.5f,213,22.5f,133}}}},
        {"clip-repair", {{{87,220,33.5f,252},{130.5f,220,33.5f,252},
                          {1509,220,33.5f,252},{1552.5f,220,33.5f,252}}}}
    }};
    const auto id=PluginRegistry::idFor(p);
    for(const auto& layout:layouts)if(id==layout.id)return &layout;
    return nullptr;
}
juce::Rectangle<float> restorationMeterPixels(const juce::Image& image,juce::Rectangle<float> area) {
    return {area.getX()*image.getWidth()/1672.f,area.getY()*image.getHeight()/941.f,
            area.getWidth()*image.getWidth()/1672.f,area.getHeight()*image.getHeight()/941.f};
}
// Calibrated raster oracle: 24 bins of 2.5 dB, -60..0 dBFS, top-down pixel
// positions for bottom-up level growth. RMS is an independently placed 1.5-unit
// white central stripe, clamped inside the lane. GUI primitives only rasterize
// this test-owned contract; expected amplitudes come from actual sample arrays.
juce::Image restorationMeterReference(const juce::Image& image,const RestorationMeterLayout& layout,
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
bool verifyRestorationMeters(const juce::Image& image,const ModuleProcessor& p,
                           const PluginTelemetry::Levels& expected,bool live,juce::String* failure=nullptr) {
    const auto* layout=restorationMeterLayout(p);require(layout,"Missing independent restoration meter profile");
    auto fail=[&](const juce::String& detail){if(failure)*failure=juce::String(layout->id)+": "+detail;return false;};
    if(!actualMeterRasterSize(image))return fail("expected 1280x720 editor at 1x or 2x");
    if(live)for(size_t ch=0;ch<4;++ch)
        if(!std::isfinite(expected.peak[ch])||!std::isfinite(expected.rms[ch])||expected.peak[ch]<0||expected.rms[ch]<0)
            return fail("nonfinite or negative expected sample measurement");
    const auto reference=restorationMeterReference(image,*layout,expected,live);
    for(size_t channel=0;channel<4;++channel){
        const auto pixels=restorationMeterPixels(image,layout->lanes[channel]);
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
// Compact proof preserves actual raster pixels in input L/R, output L/R order.
// A four-pixel transparent gutter separates channels; no lane is resampled.
juce::Image restorationLaneStrip(const juce::Image& image,const RestorationMeterLayout& layout){
    std::array<juce::Rectangle<int>,4> rectangles;int width=0,height=0;
    for(size_t ch=0;ch<4;++ch){rectangles[ch]=restorationMeterPixels(image,layout.lanes[ch]).getSmallestIntegerContainer();width+=rectangles[ch].getWidth()+4;height=std::max(height,rectangles[ch].getHeight());}
    juce::Image strip(juce::Image::ARGB,width,height,true);juce::Graphics g(strip);int x=0;
    for(const auto& rectangle:rectangles){g.drawImageAt(image.getClippedImage(rectangle),x,0);x+=rectangle.getWidth()+4;}
    return strip;
}
void assertRestorationMeterMutants(const juce::Image& image,const ModuleProcessor& p,
                                 const PluginTelemetry::Levels& expected,bool live,const juce::String& proof={}) {
    juce::String failure;require(verifyRestorationMeters(image,p,expected,live,&failure),failure);
    const auto& layout=*restorationMeterLayout(p);
    // Save the four measured lane crops side-by-side for compact mutant proof.
    // These are copied actual mutant pixels in input L/R, output L/R order.
    const auto record=[&](const juce::Image& mutant,const juce::String& kind){
        if(proof.isEmpty())return;
        save(restorationLaneStrip(mutant,layout),proof+"-mutant-"+kind);
    };
    const auto replaceLane=[&](juce::Image& target,const juce::Image& replacement,size_t ch){
        const auto r=restorationMeterPixels(image,layout.lanes[ch]).getSmallestIntegerContainer();
        for(int y=r.getY();y<r.getBottom();++y)for(int x=r.getX();x<r.getRight();++x)target.setPixelAt(x,y,replacement.getPixelAt(x,y));
    };
    auto fabricated=image.createCopy();
    {juce::Graphics g(fabricated);g.setColour(juce::Colours::cyan);g.fillRect(restorationMeterPixels(image,layout.lanes[0]));}
    require(!verifyRestorationMeters(fabricated,p,expected,live),"Restoration meter gate accepted fabricated full fill");
    record(fabricated,"fabricated");
    if(!live)return;
    for(size_t channel=0;channel<4;++channel){
        const float db=expected.peak[channel]>0?20.f*std::log10(expected.peak[channel]):-160.f;
        if(db<-57.5f)continue;
        const auto lanePixels=restorationMeterPixels(image,layout.lanes[channel]);
        auto erased=image.createCopy();
        {juce::Graphics g(erased);g.setColour(juce::Colours::black);g.fillRect(lanePixels);}
        require(!verifyRestorationMeters(erased,p,expected,true),"Restoration meter gate accepted an erased lit lane");
        if(channel==0)record(erased,"erased-L-input");
        auto moved=image.createCopy();const auto source=lanePixels.getSmallestIntegerContainer();
        const auto old=image.getClippedImage(source).createCopy();
        {juce::Graphics g(moved);g.setColour(juce::Colours::black);g.fillRect(source);g.drawImageAt(old,source.getX()+5,source.getY());}
        require(!verifyRestorationMeters(moved,p,expected,true),"Restoration meter gate accepted a displaced lane");
        if(channel==0)record(moved,"moved-L-input");
        auto corrupt=image.createCopy();
        const auto point=restorationMeterPixels(image,{layout.lanes[channel].getX()+layout.lanes[channel].getWidth()*.15f,
                                                    layout.lanes[channel].getBottom()-layout.lanes[channel].getHeight()/48.f,1,1});
        corrupt.setPixelAt(int(std::floor(point.getX())),int(std::floor(point.getY())),juce::Colours::magenta);
        require(!verifyRestorationMeters(corrupt,p,expected,true),"Restoration meter gate accepted a corrupted segment");
        if(channel==0)record(corrupt,"corrupt-pixel-L-input");
        auto wrongPeak=expected;wrongPeak.peak[channel]*=.01f;
        require(!verifyRestorationMeters(image,p,wrongPeak,true),"Restoration meter gate accepted a 40 dB peak calibration error");
        auto peakMutant=image.createCopy();replaceLane(peakMutant,restorationMeterReference(image,layout,wrongPeak,true),channel);
        require(!verifyRestorationMeters(peakMutant,p,expected,true),"Restoration meter gate accepted wrong peak pixels");
        if(channel==0)record(peakMutant,"wrong-peak");
    }
    bool visibleRms=false;
    for(size_t channel=0;channel<4;++channel)if(expected.rms[channel]>.001f){
        visibleRms=true;auto wrongRms=expected;wrongRms.rms[channel]*=.1f;
        require(!verifyRestorationMeters(image,p,wrongRms,true),"Restoration meter gate accepted a 20 dB RMS displacement");
        auto rmsMutant=image.createCopy();replaceLane(rmsMutant,restorationMeterReference(image,layout,wrongRms,true),channel);
        require(!verifyRestorationMeters(rmsMutant,p,expected,true),"Restoration meter gate accepted wrong RMS pixels");
        if(channel==0)record(rmsMutant,"wrong-RMS");
    }
    if(visibleRms){
        auto absentRms=image.createCopy();const auto peaksOnly=restorationMeterReference(image,layout,expected,true,false);
        for(const auto lane:layout.lanes){const auto area=restorationMeterPixels(image,lane).getSmallestIntegerContainer();
            for(int y=area.getY();y<area.getBottom();++y)for(int x=area.getX();x<area.getRight();++x)absentRms.setPixelAt(x,y,peaksOnly.getPixelAt(x,y));}
        require(!verifyRestorationMeters(absentRms,p,expected,true),"Restoration meter gate accepted missing white RMS markers");
        record(absentRms,"missing-RMS");
    }
    if(std::abs(expected.peak[0]-expected.peak[1])>.002f){
        auto swapped=expected;std::swap(swapped.peak[0],swapped.peak[1]);std::swap(swapped.rms[0],swapped.rms[1]);
        require(!verifyRestorationMeters(image,p,swapped,true),"Restoration meter gate accepted swapped input stereo lanes");
        auto mutant=image.createCopy();const auto reference=restorationMeterReference(image,layout,swapped,true);replaceLane(mutant,reference,0);replaceLane(mutant,reference,1);
        require(!verifyRestorationMeters(mutant,p,expected,true),"Restoration meter gate accepted input L/R-swapped pixels");record(mutant,"swapped-input-LR");
    }
    if(std::abs(expected.peak[2]-expected.peak[3])>.002f){
        auto swapped=expected;std::swap(swapped.peak[2],swapped.peak[3]);std::swap(swapped.rms[2],swapped.rms[3]);
        require(!verifyRestorationMeters(image,p,swapped,true),"Restoration meter gate accepted swapped output stereo lanes");
        auto mutant=image.createCopy();const auto reference=restorationMeterReference(image,layout,swapped,true);replaceLane(mutant,reference,2);replaceLane(mutant,reference,3);
        require(!verifyRestorationMeters(mutant,p,expected,true),"Restoration meter gate accepted output L/R-swapped pixels");record(mutant,"swapped-output-LR");
    }
}
// Exercise the same public refresh entry point used by the message-thread timer.
// Painting deliberately has no publication/availability side effects.
juce::Image restorationRaster(juce::AudioProcessorEditor& editor,int scale){
    auto* module=dynamic_cast<ModuleEditor*>(&editor);
    require(module,"Restoration editor lost its ModuleEditor refresh interface");
    module->refreshParameterActivity();return rasterEditor(editor,scale);
}
bool restorationNoBar(ModuleProcessor& p,const juce::String& name){
    if(!restorationMeterLayout(p))return false;
    std::unique_ptr<juce::AudioProcessorEditor> editor(p.createEditor());
    const std::array<juce::Image,2> images{{restorationRaster(*editor,1),restorationRaster(*editor,2)}};
    for(int scale:{1,2}){const auto& image=images[size_t(scale-1)];juce::String failure;
        require(verifyRestorationMeters(image,p,{},false,&failure),name+": "+failure);
        assertRestorationMeterMutants(image,p,{},false);save(image,name+"-"+juce::String(scale)+"x");}
    return true;
}
void testRestorationMeterCoveragePartition(){
    // Every registry entry belongs to a real asserted meter family. Extending
    // the registry must never silently turn the original-layout skip into
    // unmeasured coverage. Existing family assertions remain unchanged.
    std::array<int,7> counts{};
    juce::StringArray identities;
    for(const auto& entry:PluginRegistry::entries()){
        auto p=entry.create();int category=-1,matches=0;
        require(p&&PluginRegistry::idFor(*p)==entry.id&&!identities.contains(entry.id),
            "Meter coverage encountered an unmapped or duplicate registry identity");
        identities.add(entry.id);
        const std::array<bool,6> special{{!recoveredFamilyLanes(*p).empty(),
            dynamic_cast<BandModuleProcessor*>(p.get())!=nullptr,
            nonlinearMeterLayout(*p)!=nullptr,
            recoveredFamilyLanes(*p).empty()&&!analysisMeterLanes(*p).empty(),
            restorationMeterLayout(*p)!=nullptr,
            chassisMeterLayout(*p)!=nullptr}};
        for(size_t i=0;i<special.size();++i)if(special[i]){category=int(i)+1;++matches;}
        require(matches<=1,"A processor ambiguously belongs to multiple meter families");
        if(!matches){(void)actualLevelLayout(*p);category=0;}
        ++counts[size_t(category)];
    }
    require(counts==std::array<int,7>{{9,6,4,3,3,3,7}},
        "Meter family partition must retain 9 legacy, 6 recovered, 4 band, 3 nonlinear, 3 analysis, 3 restoration and 7 approved chassis profiles");
}
void testRestorationMeterCalibration(){
    testRestorationMeterCoveragePartition();
    int checked=0;
    for(const auto& entry:PluginRegistry::entries()){
        auto p=entry.create();if(!restorationMeterLayout(*p))continue;
        double rate=48000;prepare(*p,rate,512);
        // 06 has an output readout, deliberately no invented output parameter.
        if(p->state.getParameter("output"))parameter(*p,"output",-6);
        auto* telemetry=p->pluginTelemetry();
        require(telemetry&&!telemetry->analysis.operational(),"Restoration must publish shared levels without a generic FFT worker");
        std::unique_ptr<juce::AudioProcessorEditor> editor(p->createEditor());
        PluginTelemetry::Levels expected;std::array<double,4> energy{};
        juce::AudioBuffer<float> buffer(2,512);
        auto feed=[&](float left,float right){
            const double decay=std::exp(-1.0/(.3*rate));expected.peak={};
            for(int i=0;i<512;++i){const float phase=float(2*pi*i/64);
                buffer.setSample(0,i,left*std::sin(phase));buffer.setSample(1,i,right*std::cos(phase));}
            for(int ch=0;ch<2;++ch)for(int i=0;i<512;++i){const float v=buffer.getSample(ch,i);
                expected.peak[size_t(ch)]=std::max(expected.peak[size_t(ch)],std::abs(v));
                energy[size_t(ch)]=decay*energy[size_t(ch)]+(1-decay)*double(v)*v;}
            freshProcess(*p,buffer);
            for(int ch=0;ch<2;++ch)for(int i=0;i<512;++i){const float v=buffer.getSample(ch,i);
                expected.peak[size_t(ch+2)]=std::max(expected.peak[size_t(ch+2)],std::abs(v));
                energy[size_t(ch+2)]=decay*energy[size_t(ch+2)]+(1-decay)*double(v)*v;}
            PluginTelemetry::Levels captured;
            require(telemetry->read(captured)&&captured.valid&&captured.blocks&&captured.samples==512&&captured.rate==rate&&captured.epoch==telemetry->analysis.epoch(),"Restoration level metadata is incoherent");
            for(size_t ch=0;ch<4;++ch){expected.rms[ch]=float(std::sqrt(energy[ch]));
                near(captured.peak[ch],expected.peak[ch],1.e-6f,"Restoration peak differs from real input/output samples");
                near(captured.rms[ch],expected.rms[ch],2.e-6f,"Restoration RMS differs from independent 300 ms sample-energy recurrence");}
        };
        auto verify=[&](const juce::String& state,bool live,bool proof=false){
            // Capture every freshness-sensitive raster before mutation tests or
            // PNG writes. Disk work must never extend any production window.
            const std::array<juce::Image,2> images{{restorationRaster(*editor,1),restorationRaster(*editor,2)}};
            for(int scale:{1,2}){const auto& image=images[size_t(scale-1)];juce::String failure;
                require(verifyRestorationMeters(image,*p,expected,live,&failure),state+": "+failure);
                const auto stem=juce::String(entry.id)+"-restoration-"+state+"-"+juce::String(scale)+"x";
                assertRestorationMeterMutants(image,*p,expected,live,proof?stem:juce::String());
                // Full measured artwork for the representative asymmetric
                // fixture; all other lifecycle states still run pixel gates.
                if(proof)save(image,stem);
                else if(artifacts!=juce::File())save(restorationLaneStrip(image,*restorationMeterLayout(*p)),stem+"-lanes");}
            if(state.startsWith("calibrated-")){
                std::cout<<"MEASURE "<<entry.id<<" state="<<state<<" rate="<<rate<<" peak=";
                for(const auto value:expected.peak)std::cout<<value<<",";
                std::cout<<" rms300ms=";for(const auto value:expected.rms)std::cout<<value<<",";
                std::cout<<" scales=1,2 all-lane-pixels-and-mutants=pass\n";
            }
        };
        auto waiting=[&](const juce::String& state,bool prepared){
            const auto snapshot=p->meterSnapshot();
            require(snapshot.isPrepared==prepared&&!snapshot.hasProcessedAudio&&!snapshot.valueAvailable&&snapshot.processedBlocks==0&&snapshot.blockSamples==0,state+": stale core meter readiness");
            if(prepared)require(snapshot.sampleRate==rate&&snapshot.latencySamples==p->getLatencySamples(),state+": stale rate or latency metadata");
            else require(snapshot.sampleRate==0&&snapshot.latencySamples==0,state+": released rate or latency metadata remained available");
            PluginTelemetry::Levels levels;require(!telemetry->read(levels)||!levels.valid,state+": stale shared levels remained valid");
            verify(state,false);
        };
        waiting("prepared-before-audio",true);
        // Real audio published before constructing a replacement editor must
        // not be promoted to fresh merely because that editor has just opened.
        for(int i=0;i<8;++i)feed(.45f,.03f);
        editor.reset(p->createEditor());verify("new-editor-before-callback",false);
        feed(.45f,.03f);verify("new-editor-fresh",true);
        for(const auto amplitudes:std::array<std::array<float,2>,4>{{{{.5f,.25f}},{{.1f,.01f}},{{1.2f,.005f}},{{.0005f,0.f}}}}){
            for(int block=0;block<40;++block)feed(amplitudes[0],amplitudes[1]);
            verify("calibrated-"+juce::String(amplitudes[0],4),true,amplitudes[0]==.5f);
        }
        // The last real publication was observed above. Refreshing a frozen
        // publication must not rejuvenate it, even with the editor still open.
        juce::Thread::sleep(800);verify("stale",false);
        editor.reset(p->createEditor());verify("reopened-frozen",false);
        feed(.5f,.02f);verify("reopened-fresh",true);
        auto epoch=telemetry->analysis.epoch();p->reset();
        require(telemetry->analysis.epoch()!=epoch,"Restoration reset did not advance publication epoch");
        waiting("reset-waiting",true);energy={};expected={};
        feed(0,0);verify("reset-real-silence",true);
        for(int block=0;block<12;++block)feed(.4f,.025f);verify("reset-fresh",true);
        epoch=telemetry->analysis.epoch();rate=96000;prepare(*p,rate,512);
        require(telemetry->analysis.epoch()!=epoch,"Restoration reprepare did not advance publication epoch");
        waiting("reprepared-waiting",true);energy={};expected={};
        feed(0,0);verify("reprepared-real-silence",true);
        for(int block=0;block<12;++block)feed(.3f,.015f);verify("reprepared-fresh",true);
        p->releaseResources();waiting("released",false);
        editor.reset(p->createEditor());verify("released-reopen",false);
        // Reset after release is not an implicit prepare or stale-value revival.
        p->reset();waiting("released-reset",false);
        rate=48000;prepare(*p,rate,512);waiting("release-reprepare-waiting",true);energy={};expected={};
        feed(0,0);verify("release-reprepare-silence",true);
        for(int block=0;block<12;++block)feed(.4f,.02f);verify("release-reprepare-fresh",true);
        p->releaseResources();
        std::cout<<"PASS independent restoration peak/RMS raster, all-lane mutants and lifecycle: "<<entry.id<<"\n";
        ++checked;
    }
    require(checked==3,"Restoration meter coverage changed; explicitly map original 02/06/09 profiles");
}
