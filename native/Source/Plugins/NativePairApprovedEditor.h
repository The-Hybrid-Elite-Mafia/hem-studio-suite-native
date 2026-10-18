#pragma once
#include "../UI/ButtonLight.h"
#include "Editors.h"
#include "PluginMeterReadout.h"
#include "../UI/FoundationMaterialLook.h"
#include "../UI/SegmentedMeter.h"
#include <ApprovedArt.h>

namespace hem {
// Message-thread presentation state. A newly opened editor must witness a NEW
// producer publication; reading a recent historical block never revives it.
// This does not change PluginTelemetryView or any audio callback.
// The bounded reader can fail under contention. A first coherent snapshot then
// establishes a baseline only; it is never evidence of a new audio callback.
class NativePairPublicationClock {
public:
    void seed(unsigned generation,std::optional<unsigned> serial,double now) {
        epoch=generation;known=serial.has_value();blocks=serial.value_or(0);lastAudioAt=-1;lastPollAt=now;
    }
    bool observe(unsigned generation,bool coherent,unsigned serial,unsigned samples,bool eligible,double now) {
        const bool delayed=lastPollAt>=0&&(now<lastPollAt||now-lastPollAt>750.);
        lastPollAt=now;
        if(generation!=epoch){epoch=generation;known=true;blocks=0;lastAudioAt=-1;}
        if(!coherent)return false;
        if(!known||delayed){known=true;blocks=serial;lastAudioAt=-1;return false;}
        if(serial!=blocks){blocks=serial;lastAudioAt=eligible&&samples>0?now:-1;}
        if(!eligible||samples==0)lastAudioAt=-1;
        return eligible&&samples>0&&lastAudioAt>=0&&now>=lastAudioAt&&now-lastAudioAt<=750.;
    }
    bool witnessedAudio() const noexcept{return lastAudioAt>=0;}
    void invalidate() noexcept{lastAudioAt=-1;}
private:
    unsigned epoch=0,blocks=0;bool known=false;double lastAudioAt=-1,lastPollAt=-1;
};
// A failed try-lock cannot prove that no previous analysis frame exists.
// Baseline the first successful read conservatively, then require a newer frame.
class NativePairFrameBaseline {
public:
    void seed(std::optional<std::uint64_t> sequence){known=sequence.has_value();minimum=sequence.value_or(0);}
    bool newer(std::uint64_t sequence){if(!known){known=true;minimum=sequence;return false;}return sequence>minimum;}
private:
    bool known=false;std::uint64_t minimum=0;
};
class NativePairTelemetry {
public:
    explicit NativePairTelemetry(ModuleProcessor& p) {
        if(auto* source=p.pluginTelemetry()){
            epoch=source->analysis.epoch();PluginTelemetry::Levels current;
            const bool read=source->read(current);const auto initialMeter=p.meterSnapshot();
            // Before any block in a prepared generation, zero is independently
            // proven by the existing coherent processor meter snapshot.
            clock.seed(epoch,read?std::optional<unsigned>(current.blocks):initialMeter.isPrepared&&!initialMeter.hasProcessedAudio?std::optional<unsigned>(0):std::nullopt,juce::Time::getMillisecondCounterHiRes());
            if(source->analysis.copyLatest(frame))frameBaseline.seed(frame.sequence);
            else if(initialMeter.isPrepared&&!initialMeter.hasProcessedAudio)frameBaseline.seed(0);
            else frameBaseline.seed(std::nullopt);
            frame={};
        }
    }
    void poll(ModuleProcessor& p) {
        auto* source=p.pluginTelemetry();meter=p.meterSnapshot();live=false;analysisLive=false;
        if(!source||!meter.isPrepared){status="UNAVAILABLE";clock.invalidate();return;}
        const auto generation=source->analysis.epoch();
        if(generation!=epoch){epoch=generation;frame={};frameBaseline.seed(0);}
        PluginTelemetry::Levels next;const bool coherent=source->read(next)&&next.epoch==epoch;
        const double now=juce::Time::getMillisecondCounterHiRes();
        const bool eligible=coherent&&next.valid&&next.blocks&&next.samples>0&&meter.hasProcessedAudio;
        live=clock.observe(generation,coherent,next.blocks,next.samples,eligible,now);
        if(!coherent){status=meter.hasProcessedAudio?"UNAVAILABLE":"WAITING";return;}
        levels=next;
        if(!eligible){status=next.blocks?"INVALID":"WAITING";return;}
        status=live?"LIVE":clock.witnessedAudio()?"STALE":"WAITING FOR NEW AUDIO";
        if(source->analysis.copyLatest(frame)){
            const bool newer=frameBaseline.newer(frame.sequence);
            analysisLive=live&&frame.epoch==epoch&&newer&&now>=frame.completedAt&&now-frame.completedAt<=750.;
        }
    }
    PluginTelemetry::Levels levels;
    PluginMeterSnapshot meter;
    PluginAnalysis::Snapshot frame;
    bool live=false,analysisLive=false;
    juce::String status="WAITING";
private:
    NativePairPublicationClock clock;NativePairFrameBaseline frameBaseline;unsigned epoch=0;
};

// Approved01/12 are an immutable chassis. Only the independently documented
// glass interiors, dial disks, readouts and inset button indicators are live.
class NativePairApprovedEditor : public ModuleEditor {
    struct Look final : juce::LookAndFeel_V4 {
        FoundationLook material;
        juce::Slider::SliderLayout getSliderLayout(juce::Slider& s) override{return material.getSliderLayout(s);}
        juce::Label* createSliderTextBox(juce::Slider& s) override {
            auto* label=material.createSliderTextBox(s);
            label->setColour(juce::Label::textColourId,juce::Colour(0xffffc374));
            label->setColour(juce::Label::backgroundColourId,juce::Colour(0xff020709));
            label->setColour(juce::Label::outlineColourId,juce::Colours::transparentBlack);
            return label;
        }
        juce::Font getLabelFont(juce::Label& label) override{return material.getLabelFont(label);}
        void drawRotarySlider(juce::Graphics& g,int x,int y,int w,int h,float position,float start,float end,juce::Slider& s) override {
            material.drawRotarySlider(g,x,y,w,h,position,start,end,s);
        }
        void drawLabel(juce::Graphics& g,juce::Label& label) override {
            if(dynamic_cast<juce::Slider*>(label.getParentComponent())){
                g.fillAll(juce::Colour(0xff020709));
                if(!label.isBeingEdited()){
                    g.setColour(juce::Colour(0xffffc374).withAlpha(label.isEnabled()?1.f:.45f));
                    g.setFont(getLabelFont(label));g.drawFittedText(label.getText(),label.getLocalBounds().reduced(2),juce::Justification::centred,1);
                }
            }else juce::LookAndFeel_V4::drawLabel(g,label);
        }
        void drawToggleButton(juce::Graphics& g,juce::ToggleButton& b,bool hover,bool down) override {
            // Keep the approved caption and metal bevel. A narrow inset rail is
            // the actual Boolean indicator, never a replacement generic button.
            const auto box=b.getLocalBounds().toFloat();
            const bool active=b.isEnabled()&&(b.getToggleState()||down);
            hem::ui::drawButtonLight(g,box.reduced(5),b.getToggleState(),down,hover,b.isEnabled());
            g.setColour(active?juce::Colour(0xffffe2b0):juce::Colour(0xff152b32));
            g.fillRect(box.reduced(box.getWidth()*.15f,0).withY(box.getHeight()*.1f).withHeight(std::max(2.f,box.getHeight()*.055f)));
        }
    } look;
    struct Dial final : ui::PhysicalValueSlider {
        bool keyPressed(const juce::KeyPress& key) override {
            if(!isEnabled())return false;
            const bool arrow=key==juce::KeyPress::leftKey||key==juce::KeyPress::rightKey||key==juce::KeyPress::upKey||key==juce::KeyPress::downKey;
            if(arrow&&!key.getModifiers().isAnyModifierKeyDown()){
                // JUCE8's arrow implementation writes the value synchronously
                // but does not emit the drag notifications its APVTS attachment
                // needs. Keep this correction editor-local.
                juce::Slider::ScopedDragNotification gesture(*this);
                return ui::PhysicalValueSlider::keyPressed(key);
            }
            return ui::PhysicalValueSlider::keyPressed(key);
        }
    };
    struct Control {
        juce::String id;bool toggle=false;juce::Rectangle<float> body,read;
        Dial slider;juce::ToggleButton button;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> buttonAttachment;
    };
public:
    NativePairApprovedEditor(ModuleProcessor& p,bool stereo):ModuleEditor(p,false),processor(p),isStereo(stereo),telemetry(p) {
        art=stereo?juce::ImageCache::getFromMemory(ApprovedArt::approved_12_png,ApprovedArt::approved_12_pngSize)
                  :juce::ImageCache::getFromMemory(ApprovedArt::approved_01_png,ApprovedArt::approved_01_pngSize);
        setLookAndFeel(&look);
        for(const auto& spec:p.parameterSpecs()){
            auto c=std::make_unique<Control>();c->id=spec.id;c->toggle=spec.toggle;
            if(c->toggle){auto& b=c->button;b.setComponentID(spec.id);b.setName(spec.name);b.setButtonText(spec.name);b.setWantsKeyboardFocus(true);b.setTooltip(spec.name);addAndMakeVisible(b);
                c->buttonAttachment=std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(p.state,spec.id,b);}
            else {auto& s=c->slider;s.setComponentID(spec.id);s.setName(spec.name);s.setWantsKeyboardFocus(true);
                s.setTooltip(juce::String(spec.name)+": "+juce::String(spec.minimum)+" to "+juce::String(spec.maximum)+" "+spec.unit+". Double-click restores the existing parameter default.");
                s.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);s.setTextBoxStyle(juce::Slider::TextBoxBelow,false,120,32);
                s.setTextValueSuffix(juce::String(" ")+spec.unit);s.setRotaryParameters(juce::MathConstants<float>::pi*1.25f,juce::MathConstants<float>::pi*2.75f,true);
                s.setDoubleClickReturnValue(true,spec.initial);s.setMouseDragSensitivity(250);s.getProperties().set("preserveAuthoredDialRing",true);addAndMakeVisible(s);
                c->attachment=std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(p.state,spec.id,s);}
            controls.push_back(std::move(c));
        }
        // Measured from the approved1672x941 raster; no later layout overrides.
        if(stereo){
            dial("width",431,610,68,{355,699,153,31});dial("bass",712,610,68,{635,699,151,31});
            dial("balance",991,610,68,{915,699,150,31});dial("output",1268,610,68,{1192,699,151,31});
            button("bassOn",{450,810,171,54});button("mono",{713,810,148,54});button("invertL",{947,810,148,54});
            button("invertR",{1180,810,148,54});button("swap",{1416,810,149,54});
        }else{
            dial("attack",335,686,67,{281,779,110,29});dial("sustain",568,686,67,{514,779,109,29});
            dial("speed",800,686,67,{744,779,111,29});dial("recovery",1034,686,67,{980,779,109,29});
            dial("mix",1275,686,67,{1220,779,109,29});dial("output",1516,686,67,{1461,779,110,29});
        }
        setResizable(true,false);setResizeLimits(1000,563,3344,1882);if(auto* c=getConstrainer())c->setFixedAspectRatio(1672.0/941.0);
        setSize(1280,720);resized();
    }
    ~NativePairApprovedEditor() override {controls.clear();setLookAndFeel(nullptr);}
    void refreshParameterActivity() override {telemetry.poll(processor);repaint();}
    void resized() override {
        const float sx=getWidth()/1672.f,sy=getHeight()/941.f;
        auto scaled=[&](juce::Rectangle<float> r){return juce::Rectangle<float>(r.getX()*sx,r.getY()*sy,r.getWidth()*sx,r.getHeight()*sy).toNearestInt();};
        for(auto& c:controls){if(c->toggle){c->button.setBounds(scaled(c->body));continue;}
            // Give the child a generous hit target but constrain its actual ink
            // to the disk/readout, including fractional and odd-size scaling.
            auto& s=c->slider;const auto bounds=scaled(c->body.expanded(8).getUnion(c->read));s.setBounds(bounds);auto& props=s.getProperties();
            props.set("bodyRadius",c->body.getWidth()*.5f*std::min(sx,sy));
            props.set("bodyX",c->body.getCentreX()*sx-bounds.getX());props.set("bodyY",c->body.getCentreY()*sy-bounds.getY());
            const auto read=scaled(c->read).translated(-bounds.getX(),-bounds.getY());
            props.set("readX",read.getX());props.set("readY",read.getY());props.set("readW",read.getWidth());props.set("readH",read.getHeight());
            s.setTextBoxStyle(juce::Slider::TextBoxBelow,false,read.getWidth(),read.getHeight());s.resized();
        }
    }
    // Stable presentation geometry for diagnostics. Tests also keep their own
    // independently authored aperture and lane oracle, not this list as truth.
    struct MeterGeometry {juce::Rectangle<float> aperture;std::array<juce::Rectangle<float>,2> lanes;};
    std::array<MeterGeometry,2> meterGeometry() const {
        if(isStereo)return {{{{98,249,116,416},{{{110,276,30,350},{174,276,30,350}}}},
                             {{1458,249,116,416},{{{1470,276,30,350},{1534,276,30,350}}}}}};
        return {{{{79,207,148,233},{{{102,230,30,179},{176,230,30,179}}}},
                 {{1450,207,148,233},{{{1471,230,30,179},{1545,230,30,179}}}}}};
    }
    std::vector<juce::Rectangle<int>> liveApertures() const {
        std::vector<juce::Rectangle<int>> result;
        for(const auto& c:controls){result.push_back(c->body.getSmallestIntegerContainer());if(!c->toggle)result.push_back(c->read.getSmallestIntegerContainer());}
        for(const auto& m:meterGeometry())result.push_back(m.aperture.toNearestInt());
        if(isStereo){result.push_back({340,262,436,193});result.push_back({858,303,489,80});result.push_back({1007,406,192,34});}
        else {result.push_back({308,234,1046,270});result.push_back({76,451,150,23});result.push_back({1447,451,151,23});result.push_back({83,482,136,31});result.push_back({1454,482,135,31});}
        return result;
    }
    const NativePairTelemetry& displayState() const noexcept{return telemetry;}
    void paint(juce::Graphics& g) override {
        g.fillAll(juce::Colours::black);g.setImageResamplingQuality(juce::Graphics::highResamplingQuality);g.drawImage(art,getLocalBounds().toFloat(),juce::RectanglePlacement::stretchToFit);
        const juce::Graphics::ScopedSaveState save(g);g.addTransform(juce::AffineTransform::scale(getWidth()/1672.f,getHeight()/941.f));
        telemetry.poll(processor);
        for(int side=0;side<2;++side)paintMeter(g,meterGeometry()[size_t(side)],side*2);
        if(isStereo){paintStereo(g);paintCorrelation(g);}else {paintTransient(g);paintGain(g,0);paintGain(g,1);}
    }
private:
    static void blank(juce::Graphics& g,juce::Rectangle<float> r){g.setColour(juce::Colour(0xff020709));g.fillRect(r);}
    static void label(juce::Graphics& g,juce::Rectangle<float> r,const juce::String& s,float size,juce::Colour colour=juce::Colour(0xffb8dce5)){
        g.setColour(colour);g.setFont(FoundationLook::faceFont(size));g.drawFittedText(s,r.toNearestInt(),juce::Justification::centred,2);
    }
    static juce::String signedDb(float value){return(value>0?juce::String("+"):juce::String())+juce::String(value,2);}
    static void grid(juce::Graphics& g,juce::Rectangle<float> r){
        g.setColour(juce::Colour(0xff12313b));for(int i=0;i<=8;++i)g.drawVerticalLine(juce::roundToInt(r.getX()+i*r.getWidth()/8),r.getY(),r.getBottom());
        for(int i=0;i<=4;++i)g.drawHorizontalLine(juce::roundToInt(r.getY()+i*r.getHeight()/4),r.getX(),r.getRight());
    }
    void paintMeter(juce::Graphics& g,const MeterGeometry& m,int offset){
        const juce::Graphics::ScopedSaveState clip(g);g.reduceClipRegion(m.aperture.toNearestInt());blank(g,m.aperture);
        for(int ch=0;ch<2;++ch){const auto lane=m.lanes[size_t(ch)];
            ui::paintSegmentedMeter(g,lane,ui::meterAmplitudePosition(telemetry.levels.peak[size_t(offset+ch)]),telemetry.live);
            ui::paintMeterMark(g,lane,ui::meterAmplitudePosition(telemetry.levels.rms[size_t(offset+ch)]),juce::Colours::white,1.5f,ui::MeterMarkRole::rms,telemetry.live);
        }
        label(g,m.aperture.withHeight(19),"L    -60..0    R",12);
        label(g,m.aperture.withY(m.aperture.getBottom()-28).withHeight(26),telemetry.live?"PK / WHITE RMS\n300 ms":telemetry.status,11);
    }
    void waveform(juce::Graphics& g,juce::Rectangle<float> plot){
        grid(g,plot);if(!telemetry.analysisLive)return;
        const juce::Graphics::ScopedSaveState clip(g);g.reduceClipRegion(plot.toNearestInt());
        for(int ch=0;ch<2;++ch){juce::Path path;
            auto x=[&](int i){return plot.getX()+float(i)*plot.getWidth()/255;};
            auto y=[&](float value){return plot.getCentreY()-std::clamp(value,-1.f,1.f)*plot.getHeight()*.5f;};
            for(int i=0;i<256;++i){const auto& p=telemetry.frame.wave[size_t(i)];if(i==0)path.startNewSubPath(x(i),y(ch?p.hiR:p.hiL));else path.lineTo(x(i),y(ch?p.hiR:p.hiL));}
            for(int i=255;i>=0;--i){const auto& p=telemetry.frame.wave[size_t(i)];path.lineTo(x(i),y(ch?p.loR:p.loL));}path.closeSubPath();
            const juce::Colour c(ch?0xffffac32:0xff05dfff);g.setColour(c.withAlpha(.16f));g.fillPath(path);g.setColour(c);g.strokePath(path,juce::PathStrokeType(1.15f));
        }
    }
    juce::String analysisStatus() const {
        if(!telemetry.live)return telemetry.status;
        if(!telemetry.analysisLive)return "WAITING FOR NEW OUTPUT WINDOW";
        if(telemetry.frame.silent)return "SILENCE";
        return juce::String(1000.*PluginAnalysis::size/telemetry.frame.sampleRate,1)+" ms / 32-SAMPLE MIN-MAX"+(telemetry.frame.overRange?" / OVER 1 FS":"")+(telemetry.frame.droppedFrames?" / DROPPED DATA":"");
    }
    void paintTransient(juce::Graphics& g){
        const juce::Rectangle<float> aperture(308,234,1046,270);const juce::Graphics::ScopedSaveState clip(g);g.reduceClipRegion(aperture.toNearestInt());blank(g,aperture);
        label(g,{313,237,1035,21},"POST-FX OUTPUT WAVEFORM / L CYAN / R AMBER / +/-1 FS",17);
        const juce::Rectangle<float> plot(344,266,995,185);waveform(g,plot);
        for(int i=0;i<3;++i)label(g,{310,plot.getY()+i*plot.getHeight()*.5f-8,31,18},i==0?"+1":i==1?"0":"-1",13);
        label(g,{345,454,190,17},telemetry.analysisLive?"-"+juce::String(double(PluginAnalysis::size)/telemetry.frame.sampleRate,3)+" s":"",12);
        label(g,{1195,454,140,17},"0 s / latest",12);
        label(g,{314,477,1034,21},analysisStatus()+" / GAIN: PRE/POST MIX, SIGNED ABS-MAX dB / BEFORE OUTPUT TRIM",14);
    }
    void paintGain(juce::Graphics& g,int branch){
        const juce::Rectangle<float> heading=branch?juce::Rectangle<float>(1447,451,151,23):juce::Rectangle<float>(76,451,150,23);
        const juce::Rectangle<float> read=branch?juce::Rectangle<float>(1454,482,135,31):juce::Rectangle<float>(83,482,136,31);
        blank(g,heading);label(g,heading,branch?"SUSTAIN GAIN dB":"ATTACK GAIN dB",17,juce::Colours::whitesmoke);blank(g,read);
        label(g,read,telemetry.live?"PRE "+signedDb(telemetry.levels.gains[size_t(branch)])+"\nPOST "+signedDb(telemetry.levels.gains[size_t(branch+2)]):telemetry.status,13,juce::Colour(0xffffc374));
    }
    void paintStereo(juce::Graphics& g){
        const juce::Rectangle<float> aperture(340,262,436,193);const juce::Graphics::ScopedSaveState clip(g);g.reduceClipRegion(aperture.toNearestInt());blank(g,aperture);
        label(g,{344,264,428,21},"OUTPUT: M UP / S RIGHT     |     OUTPUT L/R",13);
        const juce::Rectangle<float> phase(364,288,143,143),wave(549,298,212,120);grid(g,phase);waveform(g,wave);
        label(g,{342,348,21,22},"0",11);label(g,{407,431,60,19},"S +/-1",11);label(g,{365,285,45,17},"M +1",11);
        label(g,{550,286,210,16},"L CYAN / R AMBER / +/-1",11);
        if(telemetry.analysisLive){const juce::Graphics::ScopedSaveState phaseClip(g);g.reduceClipRegion(phase.toNearestInt());
            g.setColour(juce::Colour(0xff23eaff).withAlpha(.7f));for(size_t i=0;i<1024;++i){const float l=telemetry.frame.phaseL[i],r=telemetry.frame.phaseR[i];
                const float x=phase.getCentreX()+std::clamp((l-r)*.5f,-1.f,1.f)*phase.getWidth()*.5f;
                const float y=phase.getCentreY()-std::clamp((l+r)*.5f,-1.f,1.f)*phase.getHeight()*.5f;g.fillEllipse(x-1,y-1,2,2);}}
        label(g,{520,423,252,29},analysisStatus(),11);
    }
    void paintCorrelation(juce::Graphics& g){
        // Includes the whole illustrated cyan bar AND bloom, not only its core.
        const juce::Rectangle<float> aperture(858,303,489,80),read(1007,406,192,34);
        {const juce::Graphics::ScopedSaveState clip(g);g.reduceClipRegion(aperture.toNearestInt());blank(g,aperture);
            const float left=869,right=1337,centre=1103,y=348;
            g.setColour(juce::Colour(0xff71838b));g.drawLine(left,y,right,y,1);g.drawLine(centre,312,centre,371,1);
            const bool defined=telemetry.live&&telemetry.meter.valueAvailable;
            if(defined){const float value=std::clamp(telemetry.meter.value,-1.f,1.f),x=centre+value*234;
                g.setColour(juce::Colour(value<0?0xffffb36b:value>0?0xff00cddd:0xffd9e5ea));
                if(value!=0)g.fillRect(std::min(x,centre),342.f,std::abs(x-centre),12.f);else g.fillRect(centre-1,341.f,2.f,14.f);}
            label(g,{860,361,65,20},"-1",12);label(g,{1078,361,50,20},"0",12);label(g,{1280,361,65,20},"+1",12);
        }
        blank(g,read);label(g,read,telemetry.live?(telemetry.meter.valueAvailable?signedCorrelation(telemetry.meter.value):"UNDEFINED"):telemetry.status,20,juce::Colour(0xffffc374));
    }
    static juce::String signedCorrelation(float v){return(v>0?juce::String("+"):juce::String())+juce::String(v,3);}
    void dial(const char* id,float x,float y,float r,juce::Rectangle<float> read){for(auto& c:controls)if(c->id==id){c->body={x-r,y-r,2*r,2*r};c->read=read;}}
    void button(const char* id,juce::Rectangle<float> body){for(auto& c:controls)if(c->id==id)c->body=body;}
    ModuleProcessor& processor;const bool isStereo;juce::Image art;NativePairTelemetry telemetry;std::vector<std::unique_ptr<Control>> controls;
};
}
