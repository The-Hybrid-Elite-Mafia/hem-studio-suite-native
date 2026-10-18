#pragma once
// Independent test contract for approved 01/05/07/08/10/11/12. These explicit
// rectangles were checked against the approved 1672x941 artwork and the final
// source-owner aperture contracts. Never call production geometry, segment,
// normalisation, colour, mark or telemetry-view helpers to build expectations.
struct ChassisMeterLayout {
    const char* id;
    std::array<juce::Rectangle<float>,4> lanes;
    juce::uint32 rmsColour;
};
const ChassisMeterLayout* chassisMeterLayout(const ModuleProcessor& p) {
    static const std::array<ChassisMeterLayout,7> layouts {{
        {"transient", {{{102,230,30,179},{176,230,30,179},{1471,230,30,179},{1545,230,30,179}}},0xffffffff},
        {"sipa", {{{110,276,30,350},{174,276,30,350},{1470,276,30,350},{1534,276,30,350}}},0xffffffff},
        {"vocal-rider", {{{127,218,53,164},{186,218,53,164},{1434,218,52,164},{1492,218,52,164}}},0xffe0f9ff},
        {"plosive-control", {{{92,231,34,173},{132,231,34,173},{1509,231,33,173},{1548,231,33,173}}},0xffe0f9ff},
        {"smart-gate", {{{102,236,35,187},{143,236,35,187},{1495,236,34,187},{1535,236,34,187}}},0xffe0f9ff},
        {"hum-buzz", {{{120,230,42.5f,206},{168.5f,230,42.5f,206},{1454,230,44,188},{1504,230,44,188}}},0xffe0f9ff},
        {"dc-subsonic", {{{90,230,53,241},{149,230,53,241},{1467,230,53,241},{1526,230,53,241}}},0xffe0f9ff}
    }};
    const auto id=PluginRegistry::idFor(p);
    for(const auto& layout:layouts)if(id==layout.id)return &layout;
    return nullptr;
}
juce::Rectangle<float> chassisMeterPixels(const juce::Image& image,juce::Rectangle<float> area) {
    return {area.getX()*image.getWidth()/1672.f,area.getY()*image.getHeight()/941.f,
            area.getWidth()*image.getWidth()/1672.f,area.getHeight()*image.getHeight()/941.f};
}
float chassisDb(float amplitude) {
    return amplitude>0&&std::isfinite(amplitude)?float(20.0*std::log10(double(amplitude))):-160.f;
}
// A segment represents a completed 2.5 dB bin, rather than a percentage of
// pixels or a first-LED-at-silence exception. Overrange saturates the raster;
// the independent numerical readout tests retain the unclamped measurement.
int chassisLitCount(float amplitude) {
    if(!std::isfinite(amplitude)||amplitude<=0)return 0;
    const float db=chassisDb(amplitude);
    int count=0;
    for(int bin=1;bin<=24;++bin)if(db>=-60.f+2.5f*float(bin))++count;
    return count;
}
void testChassisIndependentQuantisation() {
    require(chassisLitCount(0)==0&&chassisLitCount(.001f)==0&&chassisLitCount(1)==24
        &&chassisLitCount(2)==24,"Chassis reference changed silence/floor/ceiling/overrange semantics");
    for(int bin=1;bin<=24;++bin){
        const double boundary=-60.0+2.5*bin;
        require(chassisLitCount(float(std::pow(10.0,(boundary-.02)/20.0)))==bin-1,
            "Independent chassis oracle lights a bin below its calibrated edge");
        require(chassisLitCount(float(std::pow(10.0,(boundary+.02)/20.0)))==bin,
            "Independent chassis oracle misses a completed calibrated bin");
    }
    for(float invalid:{-1.f,std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity()})
        require(chassisLitCount(invalid)==0,"Independent chassis reference fabricated invalid amplitude");
}
juce::Image chassisMeterReference(const juce::Image& image,const ChassisMeterLayout& layout,
                                  const PluginTelemetry::Levels& expected,bool live,bool rms=true) {
    juce::Image reference(juce::Image::ARGB,image.getWidth(),image.getHeight(),true);
    juce::Graphics g(reference);
    g.addTransform(juce::AffineTransform::scale(image.getWidth()/1672.f,image.getHeight()/941.f));
    for(size_t ch=0;ch<4;++ch){
        const auto lane=layout.lanes[ch];
        g.setColour(juce::Colour(0xff020609));g.fillRect(lane);
        const int lit=live?chassisLitCount(expected.peak[ch]):0;
        for(int bin=1;bin<=24;++bin){
            const float threshold=-60.f+2.5f*float(bin);
            g.setColour(bin>lit?juce::Colour(0xff10232b):threshold>=-3.f?juce::Colour(0xffff655b):
                        threshold>=-12.f?juce::Colour(0xffffbd59):juce::Colour(0xff41d7c5));
            g.fillRect(juce::Rectangle<float>(lane.getX(),lane.getY()+float(24-bin)*lane.getHeight()/24.f+.5f,
                                             lane.getWidth(),lane.getHeight()/24.f-1.f));
        }
        const float rmsDb=chassisDb(expected.rms[ch]);
        if(live&&rms&&rmsDb>-60.f){
            const float position=std::clamp((rmsDb+60.f)/60.f,0.f,1.f);
            const float y=std::clamp(lane.getBottom()-position*lane.getHeight()-.75f,
                                     lane.getY(),lane.getBottom()-1.5f);
            g.setColour(juce::Colour(layout.rmsColour));
            g.fillRect(juce::Rectangle<float>(lane.getX()+lane.getWidth()*.3f,y,lane.getWidth()*.4f,1.5f));
            g.setColour(juce::Colours::white);
            g.fillRect(juce::Rectangle<float>(lane.getX()+lane.getWidth()*.45f,y,lane.getWidth()*.1f,1.5f));
        }
    }
    return reference;
}
bool verifyChassisMeters(const juce::Image& image,const ModuleProcessor& p,
                         const PluginTelemetry::Levels& expected,bool live,juce::String* failure=nullptr) {
    const auto* layout=chassisMeterLayout(p);require(layout,"Missing independent approved chassis profile");
    const auto fail=[&](const juce::String& detail){if(failure)*failure=juce::String(layout->id)+": "+detail;return false;};
    if(!actualMeterRasterSize(image))return fail("expected actual 1280x720 native raster at 1x or 2x");
    if(live)for(size_t ch=0;ch<4;++ch)
        if(!std::isfinite(expected.peak[ch])||!std::isfinite(expected.rms[ch])||expected.peak[ch]<0||expected.rms[ch]<0)
            return fail("invalid independent peak/RMS measurement");
    const auto reference=chassisMeterReference(image,*layout,expected,live);
    for(size_t ch=0;ch<4;++ch){
        const auto pixels=chassisMeterPixels(image,layout->lanes[ch]);int inspected=0;
        // Compare the entire interior, including unlit bins, the one-unit gaps,
        // coloured RMS stripe and white highlight. Exclude only the fractional
        // outer boundary where the artwork can contribute antialias coverage.
        for(int y=int(std::ceil(pixels.getY()))+1;y<int(std::floor(pixels.getBottom()))-1;++y)
            for(int x=int(std::ceil(pixels.getX()))+1;x<int(std::floor(pixels.getRight()))-1;++x){
                if(!image.getBounds().contains(x,y))return fail("lane lies outside actual editor raster");
                ++inspected;const auto actual=image.getPixelAt(x,y),wanted=reference.getPixelAt(x,y);
                if(std::abs(int(actual.getRed())-wanted.getRed())>4||std::abs(int(actual.getGreen())-wanted.getGreen())>4
                    ||std::abs(int(actual.getBlue())-wanted.getBlue())>4)
                    return fail("lane "+juce::String(int(ch))+" pixel ("+juce::String(x)+","+juce::String(y)+") expected "
                        +wanted.toDisplayString(true)+", got "+actual.toDisplayString(true));
            }
        if(inspected<1000)return fail("insufficient whole-lane peak/gap/RMS evidence");
    }
    if(failure)failure->clear();return true;
}
juce::Image chassisLaneStrip(const juce::Image& image,const ChassisMeterLayout& layout) {
    std::array<juce::Rectangle<int>,4> rectangles;int width=0,height=0;
    for(size_t ch=0;ch<4;++ch){rectangles[ch]=chassisMeterPixels(image,layout.lanes[ch]).getSmallestIntegerContainer();
        width+=rectangles[ch].getWidth()+4;height=std::max(height,rectangles[ch].getHeight());}
    juce::Image strip(juce::Image::ARGB,width,height,true);juce::Graphics g(strip);int x=0;
    for(const auto& rectangle:rectangles){g.drawImageAt(image.getClippedImage(rectangle),x,0);x+=rectangle.getWidth()+4;}
    return strip;
}
void assertChassisMeterMutants(const juce::Image& image,const ModuleProcessor& p,
                               const PluginTelemetry::Levels& expected,bool live,const juce::String& proof={}) {
    juce::String failure;require(verifyChassisMeters(image,p,expected,live,&failure),failure);
    const auto& layout=*chassisMeterLayout(p);
    const auto record=[&](const juce::Image& mutant,const juce::String& kind){
        if(proof.isNotEmpty())save(chassisLaneStrip(mutant,layout),proof+"-mutant-"+kind);};
    const auto replaceLane=[&](juce::Image& target,const juce::Image& replacement,size_t ch){
        const auto bounds=chassisMeterPixels(image,layout.lanes[ch]).getSmallestIntegerContainer();
        for(int y=bounds.getY();y<bounds.getBottom();++y)for(int x=bounds.getX();x<bounds.getRight();++x)
            target.setPixelAt(x,y,replacement.getPixelAt(x,y));};
    for(size_t ch=0;ch<4;++ch){
        const auto pixels=chassisMeterPixels(image,layout.lanes[ch]);
        auto fabricated=image.createCopy();
        {juce::Graphics g(fabricated);g.setColour(juce::Colours::cyan);g.fillRect(pixels);}
        require(!verifyChassisMeters(fabricated,p,expected,live),"Chassis gate accepted fabricated full fill in lane "+juce::String(int(ch)));
        if(ch==0)record(fabricated,"fabricated-fill");
        if(!live)continue;
        for(float invalid:{-1.f,std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity()}){
            auto invalidPeak=expected;invalidPeak.peak[ch]=invalid;
            require(!verifyChassisMeters(image,p,invalidPeak,true),"Chassis gate accepted invalid expected peak");
            auto invalidRms=expected;invalidRms.rms[ch]=invalid;
            require(!verifyChassisMeters(image,p,invalidRms,true),"Chassis gate accepted invalid expected RMS");
        }
        if(chassisLitCount(expected.peak[ch])>0){
            auto erased=image.createCopy();replaceLane(erased,chassisMeterReference(image,layout,{},false),ch);
            require(!verifyChassisMeters(erased,p,expected,true),"Chassis gate accepted an erased live lane");
            if(ch==0)record(erased,"erased-lane");
            auto moved=image.createCopy();const auto bounds=pixels.getSmallestIntegerContainer();
            const auto old=image.getClippedImage(bounds).createCopy();
            {juce::Graphics g(moved);g.setColour(juce::Colours::black);g.fillRect(bounds);g.drawImageAt(old,bounds.getX()+5,bounds.getY());}
            require(!verifyChassisMeters(moved,p,expected,true),"Chassis gate accepted a displaced live lane");
            if(ch==0)record(moved,"moved-lane");
            auto bad=expected;bad.peak[ch]*=.01f;
            require(!verifyChassisMeters(image,p,bad,true),"Chassis gate accepted a 40 dB peak calibration error");
            auto mutant=image.createCopy();replaceLane(mutant,chassisMeterReference(image,layout,bad,true),ch);
            require(!verifyChassisMeters(mutant,p,expected,true),"Chassis gate accepted wrong-calibration peak pixels");
            if(ch==0)record(mutant,"wrong-peak");
        }
        // One pixel is enough to fail, even if it is a gap or unlit segment.
        auto corrupted=image.createCopy();corrupted.setPixelAt(int(std::floor(pixels.getCentreX())),int(std::floor(pixels.getCentreY())),juce::Colours::magenta);
        require(!verifyChassisMeters(corrupted,p,expected,true),"Chassis gate accepted one corrupted interior pixel");
        if(ch==0)record(corrupted,"single-pixel");
        if(expected.rms[ch]>.001f){
            auto wrong=expected;wrong.rms[ch]*=.1f;
            require(!verifyChassisMeters(image,p,wrong,true),"Chassis gate accepted a 20 dB RMS displacement");
            auto mutant=image.createCopy();replaceLane(mutant,chassisMeterReference(image,layout,wrong,true),ch);
            require(!verifyChassisMeters(mutant,p,expected,true),"Chassis gate accepted wrong RMS pixels");
            if(ch==0)record(mutant,"wrong-RMS");
            auto absent=image.createCopy();replaceLane(absent,chassisMeterReference(image,layout,expected,true,false),ch);
            require(!verifyChassisMeters(absent,p,expected,true),"Chassis gate accepted a missing RMS marker");
            if(ch==0)record(absent,"missing-RMS");
        }
    }
    if(!live)return;
    for(size_t offset:{size_t(0),size_t(2)})if(std::abs(expected.peak[offset]-expected.peak[offset+1])>.002f){
        auto swapped=expected;std::swap(swapped.peak[offset],swapped.peak[offset+1]);std::swap(swapped.rms[offset],swapped.rms[offset+1]);
        require(!verifyChassisMeters(image,p,swapped,true),"Chassis gate accepted swapped stereo lane expectations");
        auto mutant=image.createCopy();const auto reference=chassisMeterReference(image,layout,swapped,true);
        replaceLane(mutant,reference,offset);replaceLane(mutant,reference,offset+1);
        require(!verifyChassisMeters(mutant,p,expected,true),"Chassis gate accepted swapped stereo lane pixels");
        record(mutant,offset?"swapped-output-LR":"swapped-input-LR");
    }
}
juce::Image chassisRaster(juce::AudioProcessorEditor& editor,int scale) {
    auto* module=dynamic_cast<ModuleEditor*>(&editor);require(module,"Chassis editor lost its message-thread refresh interface");
    module->refreshParameterActivity();return rasterEditor(editor,scale);
}
void pollChassisEditorBeforeAudio(juce::AudioProcessorEditor& editor) {
    auto* module=dynamic_cast<ModuleEditor*>(&editor);require(module,"Chassis editor lost its message-thread refresh interface");
    // This is the same public observation hook used by the real 25 Hz editor
    // timer. Resume it before the NEXT real callback; it publishes no audio.
    module->refreshParameterActivity();
}
void chassisFreshProcess(ModuleProcessor& p,juce::AudioProcessorEditor& editor,juce::AudioBuffer<float>& buffer) {
    pollChassisEditorBeforeAudio(editor);freshProcess(p,buffer);
}
bool chassisNoBar(ModuleProcessor& p,const juce::String& name) {
    if(!chassisMeterLayout(p))return false;
    // An editor opened after a publication has no new block yet. Never weaken
    // this expectation to historical valid data merely to make a test pass.
    std::unique_ptr<juce::AudioProcessorEditor> editor(p.createEditor());
    const std::array<juce::Image,2> images{{chassisRaster(*editor,1),chassisRaster(*editor,2)}};
    for(int scale:{1,2}){const auto& image=images[size_t(scale-1)];juce::String failure;
        require(verifyChassisMeters(image,p,{},false,&failure),name+": "+failure);
        assertChassisMeterMutants(image,p,{},false);
        if(dynamic_cast<SipaProcessor*>(&p))require(correlationRasterMatches(image,0,false),name+" revived correlation before a post-open block");
        if(dynamic_cast<SmartGateProcessor*>(&p)||dynamic_cast<PlosiveProcessor*>(&p))
            assertFoundationReductionNegativeControl(image,p,0,false);
        save(chassisLaneStrip(image,*chassisMeterLayout(p)),name+"-"+juce::String(scale)+"x-lanes");}
    return true;
}
void testChassisMeterCalibration() {
    testChassisIndependentQuantisation();int checked=0;
    for(const auto& entry:PluginRegistry::entries()){
        auto p=entry.create();if(!chassisMeterLayout(*p))continue;
        double rate=48000;prepare(*p,rate,512);
        if(p->state.getParameter("output"))parameter(*p,"output",-6);
        auto* telemetry=p->pluginTelemetry();require(telemetry&&telemetry->analysis.operational(),"Original chassis analysis-worker contract changed");
        std::unique_ptr<juce::AudioProcessorEditor> editor(p->createEditor());
        PluginTelemetry::Levels expected;std::array<double,4> energy{};juce::AudioBuffer<float> buffer(2,512);
        auto feed=[&](float left,float right,bool resumeGui=true){
            const double decay=std::exp(-1.0/(.3*rate));expected.peak={};
            for(int i=0;i<512;++i){const float phase=float(2*pi*i/64);buffer.setSample(0,i,left*std::sin(phase));buffer.setSample(1,i,right*std::cos(phase));}
            for(int ch=0;ch<2;++ch)for(int i=0;i<512;++i){const float value=buffer.getSample(ch,i);
                expected.peak[size_t(ch)]=std::max(expected.peak[size_t(ch)],std::abs(value));
                energy[size_t(ch)]=decay*energy[size_t(ch)]+(1-decay)*double(value)*value;}
            PluginTelemetry::Levels before;telemetry->read(before);
            if(resumeGui)chassisFreshProcess(*p,*editor,buffer);else freshProcess(*p,buffer);
            for(int ch=0;ch<2;++ch)for(int i=0;i<512;++i){const float value=buffer.getSample(ch,i);require(std::isfinite(value),"Finite chassis input produced nonfinite output");
                expected.peak[size_t(ch+2)]=std::max(expected.peak[size_t(ch+2)],std::abs(value));
                energy[size_t(ch+2)]=decay*energy[size_t(ch+2)]+(1-decay)*double(value)*value;}
            PluginTelemetry::Levels captured;
            require(telemetry->read(captured)&&captured.valid&&captured.blocks!=before.blocks&&captured.samples==512
                &&captured.rate==rate&&captured.epoch==telemetry->analysis.epoch(),"Chassis level block metadata is incoherent");
            for(size_t ch=0;ch<4;++ch){expected.rms[ch]=float(std::sqrt(energy[ch]));
                near(captured.peak[ch],expected.peak[ch],1.e-6f,"Chassis peak differs from actual input/output samples");
                near(captured.rms[ch],expected.rms[ch],2.e-6f,"Chassis RMS differs from independent 300 ms sample-energy recurrence");}
        };
        auto verify=[&](const juce::String& state,bool live,bool proof=false){
            // Capture both scales before costly mutation proofs/PNG encoding.
            // Those operations are never allowed to extend GUI freshness.
            const std::array<juce::Image,2> images{{chassisRaster(*editor,1),chassisRaster(*editor,2)}};
            for(int scale:{1,2}){const auto& image=images[size_t(scale-1)];juce::String failure;
                require(verifyChassisMeters(image,*p,expected,live,&failure),state+": "+failure);
                if(dynamic_cast<SmartGateProcessor*>(p.get())||dynamic_cast<PlosiveProcessor*>(p.get())){
                    auto* source=dynamic_cast<FoundationDisplaySource*>(p.get());FoundationDisplayFrame frame;
                    const bool measured=live&&source&&source->displayTelemetry().read(source->displayTelemetry().latestSerial(),frame);
                    assertFoundationReductionNegativeControl(image,*p,measured?frame.values[10]:0,measured);
                }
                const auto stem=juce::String(entry.id)+"-chassis-"+state+"-"+juce::String(scale)+"x";
                assertChassisMeterMutants(image,*p,expected,live,proof?stem:juce::String());
                if(proof)save(image,stem);else save(chassisLaneStrip(image,*chassisMeterLayout(*p)),stem+"-lanes");
            }
            std::cout<<"CHASSIS "<<entry.id<<" state="<<state<<" live="<<live<<" rate="<<rate<<" peak=";
            for(float value:expected.peak)std::cout<<value<<",";
            std::cout<<" rms300ms=";for(float value:expected.rms)std::cout<<value<<",";
            std::cout<<" scales=1,2 all-lane-pixels-and-mutants=pass\n";
        };
        verify("prepared-before-audio",false);
        for(int block=0;block<8;++block)feed(.45f,.03f);
        editor.reset(p->createEditor());verify("new-editor-before-callback",false);
        feed(.45f,.03f);verify("new-editor-fresh",true);
        const std::array<std::array<float,2>,6> fixtures{{{{.5f,.25f}},{{.1f,.01f}},{{.35f,0.f}},{{0.f,.3f}},{{1.2f,.005f}},{{.0005f,0.f}}}};
        for(const auto amplitudes:fixtures){
            for(int block=0;block<40;++block)feed(amplitudes[0],amplitudes[1]);
            verify("calibrated-"+juce::String(amplitudes[0],4)+"-"+juce::String(amplitudes[1],4),true,amplitudes[0]==.5f);
        }
        // Audio published while GUI observation is suspended must not become
        // fresh merely because a delayed paint finally sees a different block.
        // First resumed observation is a baseline; the next genuine callback
        // is what restores live state. There is no fabricated feed in a dark
        // assertion, and neither wall-clock freshness window is extended.
        juce::Thread::sleep(800);feed(.25f,.015f,false);verify("gui-gap-first-poll-baseline",false);
        feed(.25f,.015f);verify("gui-gap-new-positive-block",true);
        juce::Thread::sleep(800);verify("stopped-stale",false);
        editor.reset(p->createEditor());verify("reopened-frozen",false);
        feed(.5f,.02f);verify("reopened-fresh",true);
        auto epoch=telemetry->analysis.epoch();rate=96000;prepare(*p,rate,512);
        require(telemetry->analysis.epoch()!=epoch&&!p->meterSnapshot().hasProcessedAudio,"Chassis reprepare retained old generation");
        verify("reprepared-waiting",false);energy={};expected={};
        feed(0,0);verify("real-silence",true);
        for(int block=0;block<12;++block)feed(.3f,.015f);verify("reprepared-fresh",true);
        // A deliberately malformed zero-sample publication is a negative UI
        // fixture, not a processed audio callback. A changed block number must
        // not make it live. The existing processor skipped-buffer gate remains
        // in testLifecycleAndSilence; this directly tests the view's validator.
        telemetry->input(nullptr,nullptr,0);telemetry->output(nullptr,nullptr,0);
        PluginTelemetry::Levels zeroFrame;require(telemetry->read(zeroFrame)&&zeroFrame.samples==0,
            "Could not establish zero-sample publication negative fixture");
        verify("malformed-zero-sample-publication",false);
        feed(.3f,.015f);verify("zero-sample-recovered-fresh",true);
        // Inject actual nonfinite audio into every processor. The shared input
        // scan must invalidate the whole publication even if DSP sanitises it.
        for(float invalid:{std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity()}){
            constant(buffer,.2f,.1f);buffer.setSample(0,7,invalid);PluginTelemetry::Levels before;telemetry->read(before);process(*p,buffer);
            PluginTelemetry::Levels bad;require(telemetry->read(bad)&&!bad.valid&&bad.blocks!=before.blocks,"Nonfinite audio did not invalidate the real chassis publication");
            verify(std::isnan(invalid)?"invalid-NaN":"invalid-Inf",false);
            prepare(*p,rate,512);energy={};expected={};verify("invalid-reprepared-waiting",false);
            feed(.3f,.015f);verify("invalid-recovered-fresh",true);
        }
        epoch=telemetry->analysis.epoch();p->releaseResources();PluginTelemetry::Levels released;
        require(telemetry->analysis.epoch()!=epoch&&(!telemetry->read(released)||!released.valid),
            "Released chassis retained a valid shared publication");verify("released",false);
        editor.reset(p->createEditor());verify("released-reopen",false);
        rate=48000;prepare(*p,rate,512);energy={};expected={};verify("release-reprepared-waiting",false);
        feed(0,0);verify("release-real-silence",true);feed(.4f,.02f);verify("release-reprepared-fresh",true);
        p->releaseResources();++checked;
    }
    require(checked==7,"Chassis meter coverage changed; explicitly map approved 01/05/07/08/10/11/12");
}
