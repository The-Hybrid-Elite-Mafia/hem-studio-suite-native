#pragma once
#include "Editors.h"
#include "RestorationProcessors.h"
#include "../UI/ApprovedFaceplates.h"
#include "../UI/SegmentedMeter.h"

namespace hem {
// Authored chassis and dial rings remain immutable. Only parameter bodies,
// authored readout apertures, and actual measurement regions are repainted.
class RestorationEditor final : public ModuleEditor {
public:
    RestorationEditor(RestorationProcessor& p,int catalog)
        : ModuleEditor(p,false),processor(p),number(catalog),art(ui::approvedFaceplate(catalog)) {
        setLookAndFeel(&look);
        configureFactoryPresets();
        if(number==2) {
            header("presets","PRESETS",{91,63,168,55});header("close","CLOSE",{1414,63,168,55});
            choice("profile",0,"BROAD",{240,555,110,46});choice("profile",1,"VOCAL",{371,555,103,46});choice("profile",2,"HIGH",{494,555,107,46});
            toggle("soloDelta","SOLO DELTA",{1074,555,180,47});toggle("bypass","BYPASS",{1274,555,159,47});
            const char* ids[]{"sensitivity","low","high","selectivity","maxReduction","attack","release","mix","output"};
            const float x[]{312,474,625,779,931,1082,1231,1379,1542};
            for(int i=0;i<9;++i)dial(ids[i],x[i],742,50,{x[i]-57,812,114,35});
            measured("spectrum",{291,228,844,242},"32 constant-Q band measurements, input cyan and output orange, actual band centres 80 Hz to min(18 kHz, 0.42 times sample rate), logarithmic, -120 to 0 dBFS. This is not an FFT.");
            measured("spectrogram",{1203,228,222,133},"Measured input band energy. Each column is 20 ms; newest at right. Low frequencies at bottom. Full history 2.56 seconds; color scale -100 to 0 dBFS. Frequency centres 80 Hz to min(18 kHz, 0.42 times sample rate).");
            measured("delta",{1202,413,223,91},"Actual removed signal before output gain, measured by 32 constant-Q bands; -120 to 0 dBFS, 80 Hz to min(18 kHz, 0.42 times sample rate), logarithmic left to right.");
            measured("reduction",{654,565,362,35},"Sum of current filter cuts in dB, bounded by Max Reduction. It is not a broadband RMS attenuation measurement.");
        } else if(number==6) {
            header("presets","PRESETS",{91,58,162,52});header("previous","<",{269,59,57,51});header("next",">",{340,59,56,51});header("close","CLOSE",{1453,59,130,51});
            toggle("bypass","BYPASS",{1302,59,136,51});toggle("bypass","BYPASS",{1151,799,103,41},"bypass_1");
            toggle("listenBreath","LISTEN",{120,638,119,39});toggle("listenMouth","LISTEN",{938,638,120,39});
            dial("breathDetection",400,579,49,{346,645,110,29});dial("breathReduction",565,579,49,{509,645,110,29});dial("breathLength",728,579,49,{672,645,111,29});
            dial("mouthDetection",1228,579,49,{1166,645,111,29});dial("mouthReduction",1389,579,49,{1329,645,109,29});dial("mouthLength",1548,579,49,{1492,645,110,29});
            dial("low",671,816,44,{481,846,113,31});dial("high",946,816,44,{767,846,106,31});
            measured("history",{230,198,1216,179},"Nominal four-second history (exact sampled span shown): cyan breath detector score, orange mouth detector score, green max(breath applied reduction, mouth applied reduction) divided by 24 dB. All channels are stereo-linked, not separate L/R meters.");
            measured("breathDetectionMeter",{64,531,94,84},"Stereo-linked breath detection score, 0 to 100 percent. One measured lane replaces the illustrative paired bars.");
            measured("breathReductionMeter",{201,531,92,84},"Stereo-linked breath attenuation, 0 to 24 dB. Observed gain reduction, not the Reduction setting.");
            measured("mouthDetectionMeter",{880,531,94,84},"Stereo-linked mouth detection score, 0 to 100 percent. One measured lane replaces the illustrative paired bars.");
            measured("mouthReductionMeter",{1018,531,92,84},"Stereo-linked mouth attenuation, 0 to 24 dB. Observed gain reduction, not the Reduction setting.");
            measured("outputReadout",{1014,847,97,30},"Measured maximum L/R output sample peak in dBFS. This is a read-only output level; no output-gain control is pictured.");
        } else {
            choice("algorithm",0,"FAST",{551,548,148,48});choice("algorithm",1,"PRECISE",{713,548,149,48});
            toggle("listenRepair","LISTEN REPAIR",{1169,538,193,51});toggle("bypass","BYPASS",{1390,538,193,51});
            dial("threshold",460,751,69,{360,835,194,41});dial("length",793,751,69,{700,835,180,41});dial("amount",1124,751,69,{1030,835,186,41});dial("output",1460,751,69,{1361,835,190,41});
            measured("repair",{298,255,1101,201},"Nominal 100 ms history (exact sampled span shown), left channel: orange latency-aligned dry, cyan aligned normal output even during Listen Repair, green applied repair contribution excluding gain-only changes. Display range +/-1 FS; excursions outside the range are clipped visually only.");
            measured("clipLamp",{260,548,45,47},"Lights for 250 ms when the detected clipped-run counter increases, including at zero Repair Amount. Reset and preparation changes clear old activity. This is a read-only event lamp.");
            measured("events",{325,559,139,30},"Number of detected clipped runs since prepare/reset.");
            measured("repaired",{934,557,162,32},"Number of samples actually repaired since prepare/reset. Skipped runs are shown in the repair-view status line.");
        }
        measured("inputMeter",meterBounds(false),"Input sample peaks: independent left lane L, right lane R, -60 to 0 dBFS; white marks show measured 300 ms RMS. No processed audio, invalid input, and stale data leave all segments unlit.");
        measured("outputMeter",meterBounds(true),"Output sample peaks: independent left lane L, right lane R, -60 to 0 dBFS; white marks show measured 300 ms RMS. No processed audio, invalid input, and stale data leave all segments unlit.");
        setResizable(true,true);setResizeLimits(1000,563,3344,1882);
        if(auto* c=getConstrainer())c->setFixedAspectRatio(1672./941.);
        // Seed from the current publisher: opening an editor cannot make frozen audio fresh.
        PluginTelemetry::Levels initial;
        if(auto* source=processor.pluginTelemetry();source&&source->read(initial)){lastBlocks=initial.blocks;lastEpoch=initial.epoch;}
        const auto initialDisplay=processor.restorationDisplay();lastEvents=initialDisplay.events;lastFrames=initialDisplay.frames;
        setSize(1280,720);resized();refreshParameterActivity();
    }
    ~RestorationEditor() override {controls.clear();setLookAndFeel(nullptr);}

    // The owner explicitly binds its close/back action. An unbound CLOSE is
    // disabled; the plugin never searches for or destroys a parent/DAW window.
    void setCloseRequest(std::function<void()> request) {closeRequest=std::move(request);refreshHeader();}
    int factoryPresetCount() const noexcept {return int(presets.size());}
    juce::String factoryPresetName(int index) const {return index>=0&&index<int(presets.size())?presets[size_t(index)].name:juce::String();}
    int currentFactoryPreset() const {
        for(size_t i=0;i<presets.size();++i) {
            bool matches=true;
            for(const auto& setting:presets[i].values)if(processor.state.getRawParameterValue(setting.first)->load()!=setting.second){matches=false;break;}
            if(matches)return int(i);
        }
        return -1;
    }
    void applyFactoryPreset(int index) {
        if(index<0||index>=int(presets.size()))return;
        for(const auto& setting:presets[size_t(index)].values)if(auto* p=processor.state.getParameter(setting.first)){p->beginChangeGesture();p->setValueNotifyingHost(p->convertTo0to1(setting.second));p->endChangeGesture();}
        refreshParameterActivity();
    }
    void resized() override {
        const float sx=getWidth()/1672.f,sy=getHeight()/941.f;
        const auto scale=[&](juce::Rectangle<float> b){return juce::Rectangle<float>(b.getX()*sx,b.getY()*sy,b.getWidth()*sx,b.getHeight()*sy).toNearestInt();};
        for(auto& c:controls) {
            if(c->kind==Kind::dial) {
                const auto b=c->body.getUnion(c->read);c->slider.setBounds(scale(b));auto& v=c->slider.getProperties();
                v.set("bodyRadius",c->body.getWidth()*.5f*sx);v.set("bodyX",(c->body.getCentreX()-b.getX())*sx);v.set("bodyY",(c->body.getCentreY()-b.getY())*sy);
                v.set("readX",int((c->read.getX()-b.getX())*sx));v.set("readY",int((c->read.getY()-b.getY())*sy));v.set("readW",int(c->read.getWidth()*sx));v.set("readH",int(c->read.getHeight()*sy));
                c->slider.setTextBoxStyle(juce::Slider::TextBoxBelow,false,int(c->read.getWidth()*sx),int(c->read.getHeight()*sy));c->slider.resized();
            } else c->button.setBounds(scale(c->body));
        }
        for(auto& m:measurements)m->label.setBounds(scale(m->area));
    }
    void refreshParameterActivity() override {
        levels={};auto* source=processor.pluginTelemetry();const bool read=source&&source->read(levels);
        display=processor.restorationDisplay();
        // A lifecycle transition between the two publishers must not combine
        // old family counters/traces with a new shared-level epoch.
        PluginTelemetry::Levels after;const bool sameEpoch=read&&source->read(after)&&after.epoch==levels.epoch;
        const auto meter=processor.meterSnapshot();const auto now=juce::Time::getMillisecondCounterHiRes();
        const bool eligible=read&&sameEpoch&&levels.valid&&levels.blocks&&processor.processingReady()&&meter.isPrepared&&meter.hasProcessedAudio;
        const bool epochChanged=read&&levels.epoch!=lastEpoch;
        const bool advanced=read&&(levels.blocks!=lastBlocks||epochChanged);
        if(advanced){lastBlocks=levels.blocks;lastEpoch=levels.epoch;lastAt=eligible?now:-1;}
        if(!eligible)lastAt=-1;
        fresh=eligible&&lastAt>=0&&now>=lastAt&&now-lastAt<=750;
        measuredValid=fresh&&display.valid;
        if(!measuredValid){lastEventAt=-1;lastEvents=display.events;lastFrames=display.frames;}
        else if(advanced){
            // Epoch/reset changes discard old events, but a new epoch's first
            // measured block may already contain a genuinely detected run.
            if(epochChanged||display.frames<lastFrames||display.events<lastEvents){lastEvents=0;lastEventAt=-1;}
            if(display.events>lastEvents)lastEventAt=now;
            lastEvents=display.events;lastFrames=display.frames;
        }
        clipEventActive=measuredValid&&lastEventAt>=0&&now>=lastEventAt&&now-lastEventAt<=250;
        for(auto& c:controls) {
            if(c->kind==Kind::host)continue;
            const bool active=processor.parameterActive(c->id);
            if(c->kind==Kind::dial)c->slider.setEnabled(active);else c->button.setEnabled(active);
            if(c->choice>=0)c->button.setToggleState(int(std::lround(processor.state.getRawParameterValue(c->id)->load()))==c->choice,juce::dontSendNotification);
        }
        refreshHeader();repaint();
    }
    void paint(juce::Graphics& g) override {
        g.fillAll(juce::Colours::black);
        if(art.isValid())g.drawImage(art,getLocalBounds().toFloat(),juce::RectanglePlacement::stretchToFit);
        const juce::Graphics::ScopedSaveState save(g);g.addTransform(juce::AffineTransform::scale(getWidth()/1672.f,getHeight()/941.f));
        paintMeters(g);if(number==2)paintResonance(g);else if(number==6)paintBreath(g);else paintRepair(g);
    }
private:
    enum class Kind {button,dial,host};
    struct Control {
        juce::String id;Kind kind=Kind::button;int choice=-1;juce::Rectangle<float> body,read;
        ui::PhysicalValueSlider slider;juce::ToggleButton button;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> sa;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> ba;
    };
    struct Measurement {juce::Rectangle<float> area;juce::Label label;};
    void measured(const char* id,juce::Rectangle<float> b,const char* description) {
        auto m=std::make_unique<Measurement>();m->area=b;m->label.setComponentID(juce::String("display-")+id);m->label.setName(description);m->label.setTooltip(description);
        m->label.setInterceptsMouseClicks(true,false);addAndMakeVisible(m->label);measurements.push_back(std::move(m));
    }
    void button(const char* id,const char* title,juce::Rectangle<float> b,std::function<void()> action) {
        auto c=std::make_unique<Control>();c->id=id;c->body=b;c->button.setComponentID(id);c->button.setName(title);c->button.setButtonText(title);
        c->button.setClickingTogglesState(false);c->button.onClick=std::move(action);look.registerButton(c->button,art,b,true);addAndMakeVisible(c->button);controls.push_back(std::move(c));
    }
    void toggle(const char* id,const char* title,juce::Rectangle<float> b,const char* componentId=nullptr) {
        button(id,title,b,[this]{refreshParameterActivity();});auto& c=*controls.back();if(componentId)c.button.setComponentID(componentId);
        for(const auto& s:processor.parameterSpecs())if(c.id==s.id)c.button.setName(s.name);
        c.button.setClickingTogglesState(true);c.button.setTooltip(processor.helpText());
        c.ba=std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(processor.state,id,c.button);
    }
    void choice(const char* id,int value,const char* title,juce::Rectangle<float> b) {
        button(id,title,b,[this,id,value]{setParameter(id,float(value));});auto& c=*controls.back();c.choice=value;
        c.button.setComponentID(value?juce::String(id)+"_"+juce::String(value):juce::String(id));
        c.button.getProperties().set("hemChoiceParameter",id);c.button.getProperties().set("hemChoiceValue",value);
        for(const auto& s:processor.parameterSpecs())if(c.id==s.id)c.button.setName(s.name);
        c.button.setTooltip(processor.helpText());
    }
    void dial(const char* id,float x,float y,float radius,juce::Rectangle<float> read) {
        auto c=std::make_unique<Control>();c->id=id;c->kind=Kind::dial;c->body={x-radius,y-radius,2*radius,2*radius};c->read=read;
        c->slider.getProperties().set("preserveAuthoredDialRing",true);
        c->slider.setComponentID(id);c->slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);c->slider.setRotaryParameters(juce::MathConstants<float>::pi*1.25f,juce::MathConstants<float>::pi*2.75f,true);
        c->slider.setMouseDragSensitivity(250);addAndMakeVisible(c->slider);
        // The shared material renderer clips to the original inner body; all
        // authored ring/tick pixels stay unchanged.
        for(const auto& s:processor.parameterSpecs())if(c->id==s.id) {
            c->slider.setName(s.name);c->slider.setTextValueSuffix(juce::String(" ")+s.unit);
            c->slider.setTooltip(juce::String(s.name)+": "+juce::String(s.minimum)+" to "+juce::String(s.maximum)+" "+s.unit+". Double-click restores "+juce::String(s.initial)+". "+processor.helpText());
        }
        c->sa=std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(processor.state,id,c->slider);
        for(const auto& s:processor.parameterSpecs())if(c->id==s.id)c->slider.setDoubleClickReturnValue(true,canonical(s.id,s.initial));
        controls.push_back(std::move(c));
    }
    void setParameter(const char* id,float value) {
        if(auto* p=processor.state.getParameter(id)){p->beginChangeGesture();p->setValueNotifyingHost(p->convertTo0to1(value));p->endChangeGesture();}refreshParameterActivity();
    }
    void header(const char* id,const char* title,juce::Rectangle<float> b) {
        button(id,title,b,[this,id]{headerAction(id);});controls.back()->kind=Kind::host;
        if(juce::String(id)=="previous")controls.back()->button.setName("Previous factory preset");
        else if(juce::String(id)=="next")controls.back()->button.setName("Next factory preset");
    }
    struct FactoryPreset {juce::String name;std::vector<std::pair<juce::String,float>> values;};
    float canonical(const juce::String& id,float value) const {
        if(auto* p=processor.state.getParameter(id))return p->convertFrom0to1(p->convertTo0to1(value));
        return value;
    }
    void configureFactoryPresets() {
        if(number!=2&&number!=6)return;
        FactoryPreset neutral;neutral.name="Neutral";for(const auto& s:processor.parameterSpecs())neutral.values.emplace_back(s.id,canonical(s.id,s.initial));
        presets.push_back(neutral);
        const auto make=[&](const char* name,std::initializer_list<std::pair<const char*,float>> settings) {
            auto preset=neutral;preset.name=name;
            for(const auto& setting:settings)for(auto& value:preset.values)if(value.first==setting.first)value.second=canonical(value.first,setting.second);
            presets.push_back(std::move(preset));
        };
        if(number==2) {
            make("Gentle Broad",{{"sensitivity",45},{"low",180},{"high",10000},{"selectivity",55},{"maxReduction",3},{"attack",15},{"release",180},{"profile",0}});
            make("Vocal Focus",{{"sensitivity",55},{"low",160},{"high",11000},{"selectivity",70},{"maxReduction",6},{"attack",10},{"release",180},{"profile",1}});
        } else {
            make("Gentle Cleanup",{{"breathDetection",45},{"breathReduction",4},{"breathLength",120},{"mouthDetection",50},{"mouthReduction",3},{"mouthLength",.4f}});
            make("Focused Cleanup",{{"breathDetection",60},{"breathReduction",8},{"breathLength",100},{"mouthDetection",65},{"mouthReduction",6},{"mouthLength",.6f},{"high",14000}});
        }
    }
    void headerAction(const juce::String& id) {
        if(id=="close") {auto callback=closeRequest;if(callback)callback();return;}
        if(presets.empty())return;
        if(id=="previous"||id=="next") {
            const int current=currentFactoryPreset();const int count=int(presets.size());
            applyFactoryPreset(current<0?0:(current+(id=="next"?1:count-1))%count);return;
        }
        if(id=="presets") {
            juce::PopupMenu menu;menu.addSectionHeader("Built-in module settings");
            const int selected=currentFactoryPreset();
            for(size_t i=0;i<presets.size();++i)menu.addItem(int(i)+1,presets[i].name,true,int(i)==selected);
            juce::Component* target=this;for(auto& c:controls)if(c->kind==Kind::host&&c->id=="presets")target=&c->button;
            juce::Component::SafePointer<RestorationEditor> safe(this);
            menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(target),[safe](int result){if(safe&&result>0)safe->applyFactoryPreset(result-1);});
        }
    }
    void refreshHeader() {
        const int selected=currentFactoryPreset();
        for(auto& c:controls)if(c->kind==Kind::host) {
            if(c->id=="close") {c->button.setEnabled(bool(closeRequest));c->button.setTooltip(closeRequest?"Close this module editor using its owner's bound close action":"Close unavailable: the host has not bound an editor-close action");}
            else {c->button.setEnabled(!presets.empty());c->button.setTooltip("Built-in module settings. Current: "+(selected<0?juce::String("Custom"):presets[size_t(selected)].name)+". Applies all module parameters; no user or whole-session preset is loaded.");}
        }
    }
    static void blank(juce::Graphics& g,juce::Rectangle<float> b) {g.setColour(juce::Colour(0xff030a10));g.fillRect(b);}
    static void text(juce::Graphics& g,juce::Rectangle<float> b,const juce::String& value,float size=16,juce::Colour colour=juce::Colour(0xffb6c4cc)) {
        blank(g,b);g.setColour(colour);g.setFont(FoundationLook::faceFont(size));g.drawFittedText(value,b.toNearestInt(),juce::Justification::centred,1);
    }
    static void grid(juce::Graphics& g,juce::Rectangle<float> b,int columns=16,int rows=4) {
        blank(g,b);g.setColour(juce::Colour(0xff18333c));for(int i=0;i<=columns;++i){const auto x=b.getX()+i*b.getWidth()/columns;g.drawVerticalLine(int(x),b.getY(),b.getBottom());}
        for(int i=0;i<=rows;++i){const auto y=b.getY()+i*b.getHeight()/rows;g.drawHorizontalLine(int(y),b.getX(),b.getRight());}
    }
    juce::String unavailable() const {return !processor.processingReady()?"NO PROCESSED AUDIO":levels.valid&&!fresh?"STALE / AUDIO STOPPED":"WAITING FOR VALID AUDIO";}
    juce::Rectangle<float> meterBounds(bool output) const {
        if(number==2)return {output?1497.f:79.f,239,97,260};
        if(number==6)return {output?1528.f:78.f,201,65,157};
        return {output?1504.f:82.f,208,87,276};
    }
    void paintMeters(juce::Graphics& g) {
        for(int side=0;side<2;++side) {
            const auto b=meterBounds(side!=0);blank(g,b);
            const auto lane=b.reduced(5,12);const float width=(lane.getWidth()-10)/2;
            for(int channel=0;channel<2;++channel) {
                const auto area=juce::Rectangle<float>(lane.getX()+channel*(width+10),lane.getY(),width,lane.getHeight());
                const auto index=size_t(side*2+channel);
                ui::paintSegmentedMeter(g,area,ui::meterAmplitudePosition(levels.peak[index]),fresh);
                ui::paintMeterMark(g,area,ui::meterAmplitudePosition(levels.rms[index]),juce::Colours::white,1.5f,ui::MeterMarkRole::rms,fresh);
            }
            text(g,b.withHeight(11),"L       R",10,juce::Colour(0xffa4cbd3));
            const float maximum=std::max(levels.peak[size_t(side*2)],levels.peak[size_t(side*2+1)]);
            const auto value=!fresh?juce::String("--"):maximum>0?juce::String(20*std::log10(maximum),1):juce::String("-inf");
            if(number==2)text(g,{side?1498.f:78.f,510,94,29},value+" dBFS",17);
            else if(number==6)text(g,{side?1517.f:66.f,369,90,23},value+" dBFS",15);
            else text(g,{b.getX(),b.getBottom()-12,b.getWidth(),12},fresh?value+" dBFS":"-- dBFS",11);
        }
    }
    static float dbY(juce::Rectangle<float> plot,float db) {return plot.getBottom()-std::clamp((db+120)/120,0.f,1.f)*plot.getHeight();}
    void bands(juce::Graphics& g,juce::Rectangle<float> plot,const std::array<float,restoration::Display::bands>& values,juce::Colour colour) {
        const juce::Graphics::ScopedSaveState clip(g);g.reduceClipRegion(plot.toNearestInt());
        juce::Path line;const float low=display.frequencies.front(),high=display.frequencies.back();
        if(!(low>0&&high>low))return;
        for(size_t i=0;i<values.size();++i){
            const float position=std::clamp(std::log(display.frequencies[i]/low)/std::log(high/low),0.f,1.f);
            const float x=plot.getX()+position*plot.getWidth(),y=dbY(plot,values[i]);
            if(i)line.lineTo(x,y);else line.startNewSubPath(x,y);
        }
        g.setColour(colour);g.strokePath(line,juce::PathStrokeType(1.8f));
    }
    juce::String frequencyRange() const {
        // Prefer the actual detector centres; before its first spectrum, expose
        // the exact prepared bank endpoints, never a fabricated FFT axis.
        const double rate=processor.actualRate.load();
        const float low=measuredValid&&display.spectralCount?display.frequencies.front():80.f;
        const float high=measuredValid&&display.spectralCount?display.frequencies.back():float(std::min(18000.,rate*.42));
        return rate>0&&high>low?juce::String(low,0)+" ... "+juce::String(high,0)+" Hz (log)":"80 Hz ... min(18 kHz, 0.42 Fs)";
    }
    void paintResonance(juce::Graphics& g) {
        const juce::Rectangle<float> main(291,228,844,243),spectro(1203,228,222,133),delta(1201,413,225,91);
        const auto mainTrace=main.withTrimmedTop(29).reduced(2,2);
        const auto deltaTrace=delta.withTrimmedTop(21).withTrimmedBottom(23).reduced(2,1);
        const auto heat=spectro.withTrimmedTop(21).withTrimmedBottom(22);
        // Erase the whole baked signal area, then reserve captions from traces.
        blank(g,{292,229,842,240});grid(g,mainTrace);blank(g,{1201,413,225,90});grid(g,deltaTrace,8,3);blank(g,spectro);
        if(measuredValid&&display.spectralCount>0) {
            bands(g,mainTrace,display.inputDb,juce::Colours::cyan);bands(g,mainTrace,display.outputDb,juce::Colours::orange);bands(g,deltaTrace,display.deltaDb,juce::Colours::orange);
            const juce::Graphics::ScopedSaveState clip(g);g.reduceClipRegion(heat.toNearestInt());
            for(size_t col=0;col<display.spectralCount;++col) {
                const auto source=(display.spectralCursor+restoration::Display::columns-display.spectralCount+col)%restoration::Display::columns;
                const float x=heat.getRight()-float(display.spectralCount-col)*heat.getWidth()/restoration::Display::columns;
                for(size_t band=0;band<restoration::Display::bands;++band) {
                    const float level=std::clamp((display.spectrogram[source][band]+100)/100,0.f,1.f);
                    const auto low=juce::Colour(0xff031320),middle=juce::Colour(0xff00c7f1),high=juce::Colour(0xffffae20);
                    g.setColour(level<.65f?low.interpolatedWith(middle,level/.65f):middle.interpolatedWith(high,(level-.65f)/.35f));
                    g.fillRect(x,heat.getBottom()-(float(band)+1)*heat.getHeight()/restoration::Display::bands,heat.getWidth()/restoration::Display::columns+.3f,heat.getHeight()/restoration::Display::bands+.3f);
                }
            }
        } else text(g,mainTrace.withSizeKeepingCentre(500,28),unavailable(),19);
        text(g,{302,232,820,22},"32 constant-Q bands | cyan IN / orange OUT | -120 ... 0 dBFS",16);
        text(g,{300,477,832,30},frequencyRange()+" | meters L/R peak; white = 300 ms RMS",15);
        const double rate=processor.actualRate.load();
        const auto high=rate>0?juce::String(std::min(18000.,rate*.42),0)+" Hz":"min(18k, 0.42 Fs)";
        text(g,{1206,229,215,18},high+" / -100 ... 0 dBFS",12);
        text(g,{1206,340,215,19},"80 Hz / last 2.56 s",12);
        // DELTA is amplitude versus frequency, so the baked vertical Hz label
        // is replaced while its surrounding authored bezel remains intact.
        text(g,{1170,438,29,42},"dBFS",10);
        text(g,{1206,416,215,17},"DELTA -120 ... 0 dBFS",12);
        text(g,{1204,484,220,18},frequencyRange(),11);
        const juce::Rectangle<float> bar(658,570,353,26);blank(g,{655,565,362,35});
        const float fraction=measuredValid?std::clamp(display.maximumCut/24.f,0.f,1.f):0;
        for(int i=0;i<40;++i){g.setColour(juce::Colour(0xffffad2b).withAlpha(i<int(std::ceil(fraction*40))?.95f:.09f));g.fillRect(bar.getX()+i*bar.getWidth()/40,bar.getY(),bar.getWidth()/40-2,bar.getHeight());}
        text(g,{748,573,164,20},measuredValid?juce::String(display.maximumCut,1)+" dB summed cut":"-- dB summed cut",14,juce::Colours::white);
    }
    void history(juce::Graphics& g,juce::Rectangle<float> plot,bool bipolar) {
        // Clear the entire illustrative trace through its last rightmost pixel,
        // preserving the original baseline and vertical-axis/frame pixels.
        blank(g,bipolar?juce::Rectangle<float>(299,255,1101,200):juce::Rectangle<float>(230,198,1216,178));
        plot=plot.withTrimmedTop(28).withTrimmedBottom(27).reduced(2,1);
        grid(g,plot,24,4);
        if(!measuredValid){text(g,plot.withSizeKeepingCentre(500,27),unavailable(),18);return;}
        if(display.count<2)return;
        const juce::Colour colours[]{bipolar?juce::Colours::orange:juce::Colours::cyan,bipolar?juce::Colours::cyan:juce::Colours::orange,juce::Colour(0xff50e386)};
        const juce::Graphics::ScopedSaveState clipped(g);g.reduceClipRegion(plot.toNearestInt());
        for(size_t channel=0;channel<3;++channel) {
            juce::Path line;for(size_t j=0;j<display.count;++j) {
                const auto i=(display.cursor+restoration::Display::points-display.count+j)%restoration::Display::points;
                const float x=plot.getRight()-float(display.count-1-j)*plot.getWidth()/float(restoration::Display::points-1),value=display.history[channel][i];
                const float y=bipolar?plot.getCentreY()-std::clamp(value,-1.f,1.f)*plot.getHeight()*.48f:plot.getBottom()-std::clamp(value,0.f,1.f)*plot.getHeight();
                if(j)line.lineTo(x,y);else line.startNewSubPath(x,y);
            }
            g.setColour(colours[channel]);g.strokePath(line,juce::PathStrokeType(channel==2?1.2f:1.8f));
        }
    }
    void linkedMeter(juce::Graphics& g,juce::Rectangle<float> area,float value,bool reduction) {
        blank(g,area);const float normalized=reduction?value/24.f:value;const auto lane=area.reduced(19,14);
        // This is one stereo-linked statistic, never two fictional channel reads.
        if(reduction)ui::paintSegmentedMeter(g,lane,normalized,measuredValid,true,10);
        else {g.setColour(juce::Colour(0xff051520));g.fillRect(lane);for(int i=0;i<10;++i){g.setColour(juce::Colours::cyan.withAlpha(measuredValid&&i<int(std::ceil(std::clamp(normalized,0.f,1.f)*10))?.9f:.09f));g.fillRect(lane.getX(),lane.getBottom()-(i+1)*lane.getHeight()/10,lane.getWidth(),lane.getHeight()/10-2);}}
        text(g,area.withHeight(13),measuredValid?juce::String(reduction?value:value*100,1)+(reduction?" dB":" %"):"--",12);
        text(g,area.withY(area.getBottom()-12).withHeight(12),"LINKED",11);
    }
    juce::String historySpan(bool milliseconds) const {
        const double rate=processor.actualRate.load();const double nominal=number==9?.1:4.;
        if(!(rate>0))return "span pending audio";
        const double interval=std::max(1.,std::round(rate*nominal/restoration::Display::points));
        return juce::String((restoration::Display::points-1)*interval/rate*(milliseconds?1000:1),milliseconds?2:3)+(milliseconds?" ms":" s");
    }
    void paintBreath(juce::Graphics& g) {
        history(g,{230,198,1216,179},false);
        text(g,{237,201,1202,20},"Cyan breath / orange mouth: score 0 ... 1 | green: max applied cut / 24 dB | "+historySpan(false),16);
        text(g,{237,353,1202,19},"METERS: independent L/R sample peak | white = 300 ms RMS | -60 ... 0 dBFS",14);
        linkedMeter(g,{64,531,94,84},display.detection[0],false);linkedMeter(g,{201,531,92,84},display.reduction[0],true);
        linkedMeter(g,{880,531,94,84},display.detection[1],false);linkedMeter(g,{1018,531,92,84},display.reduction[1],true);
        const float peak=std::max(levels.peak[2],levels.peak[3]);text(g,{1014,847,97,30},!fresh?"-- dBFS":peak>0?juce::String(20*std::log10(peak),1)+" dBFS":"-inf dBFS",17);
    }
    void paintRepair(juce::Graphics& g) {
        history(g,{298,255,1101,201},true);
        text(g,{361,258,1000,20},"L: orange aligned dry / cyan normal out / green repair contribution | +/-1 FS | "+historySpan(true),16);
        text(g,{306,432,550,21},"METERS: L/R peak; white = 300 ms RMS; -60 ... 0 dBFS",13);
        text(g,{865,432,522,21},measuredValid?juce::String(display.skipped)+" clipped runs skipped":"-- clipped runs skipped",15);
        // Preserve the authored bezel; replace its complete activity lens.
        g.setColour(juce::Colour(0xff050b10));g.fillEllipse(266,555,34,34);g.setColour(clipEventActive?juce::Colour(0xffffa02b):juce::Colour(0xff152129));g.fillEllipse(270,559,26,26);
        text(g,{325,559,139,30},measuredValid?juce::String(display.events)+" events":"-- events",17);
        text(g,{934,557,162,32},measuredValid?juce::String(display.repaired):"--",20);
    }
    RestorationProcessor& processor;const int number;juce::Image art;FoundationLook look;
    std::vector<std::unique_ptr<Control>> controls;std::vector<std::unique_ptr<Measurement>> measurements;
    std::vector<FactoryPreset> presets;std::function<void()> closeRequest;
    juce::TooltipWindow tooltips{this,500};restoration::Display display;PluginTelemetry::Levels levels;
    unsigned lastBlocks=0,lastEpoch=0;std::uint64_t lastEvents=0,lastFrames=0;double lastAt=-1,lastEventAt=-1;bool fresh=false,measuredValid=false,clipEventActive=false;
};
} // namespace hem
