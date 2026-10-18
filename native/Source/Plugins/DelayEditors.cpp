#include "DelayEditors.h"
#include "../UI/PhysicalValueSlider.h"
#include "../UI/FoundationMaterialLook.h"
#include "../UI/SegmentedMeter.h"
#include "DelayPresentation.h"
#include <ApprovedArt.h>

namespace hem {
namespace {
using Rect=juce::Rectangle<float>;
constexpr float artW=1672,artH=941;
const std::array<const char*,21> divisions{"1/64","1/64 D","1/64 T","1/32","1/32 D","1/32 T","1/16","1/16 D","1/16 T","1/8","1/8 D","1/8 T","1/4","1/4 D","1/4 T","1/2","1/2 D","1/2 T","1/1","1/1 D","1/1 T"};
struct PointerSlider:ui::PhysicalValueSlider {
    bool division=false;DelayProcessor* processor=nullptr;
    double snapValue(double proposed,DragMode)override {
        if(!division||!processor)return proposed;
        const int id=juce::jlimit(0,20,int(std::round(proposed)));
        for(int delta=0;delta<=20;++delta)for(int sign:{1,-1}){const int candidate=id+sign*delta;if(candidate<0||candidate>20)continue;
            delay::DelayTimeSetting s;s.sync=true;s.division=delay::BeatDivision(candidate);
            if(delay::resolveTime(s,{processor->bpm()}).resolved())return candidate;
        }return getValue();
    }
};
struct FixedLighting:juce::LookAndFeel_V4 {
    SuiteMaterialLook dialLook;
    void drawRotarySlider(juce::Graphics& g,int x,int y,int w,int h,float proportion,float,float,juce::Slider& slider)override {
        // Cover every authored pointer with an opaque fixed-lighting inner body.
        // The approved outer ring/ticks/chassis stay untouched.
        slider.getProperties().set("preserveAuthoredDialRing",true);
        slider.getProperties().set("bodyRadius",std::min(w,h)*.48f);
        slider.getProperties().set("bodyX",x+w*.5f);
        slider.getProperties().set("bodyY",y+h*.5f);
        dialLook.drawRotarySlider(g,x,y,w,h,proportion,
            juce::MathConstants<float>::pi*1.5f,juce::MathConstants<float>::pi*2.5f,slider);
    }
    void drawToggleButton(juce::Graphics& g,juce::ToggleButton& b,bool hover,bool down)override {
        auto area=b.getLocalBounds().toFloat().reduced(2);
        if(b.getToggleState()){const auto colour=bool(b.getProperties()["unavailable"])?juce::Colours::orange:juce::Colours::cyan;g.setColour(colour.withAlpha(.13f));g.fillRoundedRectangle(area,3);g.setColour(colour);g.drawRoundedRectangle(area,3,2);}
        else if(hover||down||b.hasKeyboardFocus(false)){g.setColour(juce::Colours::white.withAlpha(.65f));g.drawRoundedRectangle(area,3,1);}
    }
    void drawButtonBackground(juce::Graphics& g,juce::Button& b,const juce::Colour&,bool hover,bool down)override {
        if(hover||down||b.hasKeyboardFocus(false)){g.setColour(juce::Colours::cyan.withAlpha(.55f));g.drawRoundedRectangle(b.getLocalBounds().toFloat().reduced(2),3,1);}
    }
    void drawButtonText(juce::Graphics&,juce::TextButton&,bool,bool)override {}
};
juce::String tapPrefix(int tap){return "tap"+juce::String(tap+1)+".";}
}
struct DelayEditor::View {
    struct Knob {
        PointerSlider slider;Rect face,value;juce::String id,unit,manualId,divisionId;bool time=false;int tap=-1;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };
    struct Button {
        juce::ToggleButton button;Rect rect;juce::String id;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> attachment;
    };
    DelayEditor& editor;DelayProcessor& processor;FixedLighting look;juce::Image image;
    std::vector<std::unique_ptr<Knob>> knobs;std::vector<std::unique_ptr<Button>> buttons;
    std::array<juce::TextButton,8> selectors;juce::TextButton tapTempo;
    std::array<Rect,8> selectorRects{};int selected=0;bool synced=false;
    Knob* low=nullptr;Knob* high=nullptr;Button* lowOn=nullptr;Button* highOn=nullptr;
    Rect graph,inputMeter,outputMeter,tempoRect;bool tape;
    delayUi::PublicationFreshness freshness;bool meterLive=false;
    delayUi::SyncStatus syncStatus()const{return delayUi::syncStatus(processor.settings(),processor.delayKind(),processor.bpm());}
    bool unresolved()const{return syncStatus().unavailable;}
    View(DelayEditor& e,DelayProcessor& p):editor(e),processor(p),tape(p.delayKind()==delay::Kind::tape),freshness(p.meterPublication()){
        image=juce::ImageCache::getFromMemory(tape?ApprovedArt::approved_22_png:ApprovedArt::approved_21_png,tape?ApprovedArt::approved_22_pngSize:ApprovedArt::approved_21_pngSize);
        synced=p.settings().sync;
        if(tape){
            graph={268,239,1135,189};inputMeter={74,194,100,259};outputMeter={1499,194,98,259};tempoRect={654,520,157,34};
            const std::array<float,9> centres{111,289,471,653,834,1017,1199,1382,1562};
            const std::array<const char*,9> ids{"time_ms","feedback","wow","flutter","drive","low_hz","high_hz","mix","output"};
            for(int i=0;i<9;++i){auto* k=addKnob(ids[std::size_t(i)],{centres[std::size_t(i)]-57,627,114,112},{centres[std::size_t(i)]-60,751,120,32});if(i==0)makeTime(*k,"time_ms","division",0);}
            addButton("sync",{211,506,154,48});addButton("low_on",{882,506,156,48});addButton("high_on",{1092,506,147,48});addButton("bypass",{1306,506,155,48});
            tapTempo.setButtonText("Tap tempo");tapTempo.setName("Tap tempo / manual delay");tapTempo.setComponentID("tap-tempo");tapTempo.setLookAndFeel(&look);editor.addAndMakeVisible(tapTempo);
            tapTempo.onClick=[this]{processor.tapTempo(juce::Time::getMillisecondCounterHiRes());editor.repaint();};
        }else{
            graph={330,171,1054,130};inputMeter={68,94,146,218};outputMeter={1459,94,154,218};tempoRect={1045,858,108,29};
            const std::array<float,8> centres{76,284,494,697,894,1092,1297,1502};
            const std::array<float,8> timeReadX{43,249,457,658,856,1061,1261,1467};
            const std::array<float,8> otherReadX{136,342,548,748,945,1150,1353,1560};
            for(int i=0;i<8;++i){const float x=centres[std::size_t(i)];const auto prefix=tapPrefix(i);
                auto* time=addKnob(prefix+"time_ms",{x-33,408,66,65},{timeReadX[std::size_t(i)],502,64,18});makeTime(*time,prefix+"time_ms",prefix+"division",i);
                addKnob(prefix+"feedback",{x+61,408,66,65},{otherReadX[std::size_t(i)],502,70,18});
                addKnob(prefix+"pan",{x-33,543,66,64},{timeReadX[std::size_t(i)],638,64,18});addKnob(prefix+"level",{x+61,543,66,64},{otherReadX[std::size_t(i)],638,70,18});
                addButton(prefix+"on",{x-40,672,50,41});addButton(prefix+"solo",{x+23,672,50,41});addButton(prefix+"mute",{x+85,672,50,41});
                auto& selector=selectors[std::size_t(i)];selector.setButtonText("Select tap "+juce::String(i+1));selector.setName(selector.getButtonText());selector.setComponentID("select-tap-"+juce::String(i+1));
                selector.setLookAndFeel(&look);selector.onClick=[this,i]{editor.selectTap(i);};editor.addAndMakeVisible(selector);
                selectorRects[std::size_t(i)]={x-40,364,170,31};
            }
            low=addKnob("tap1.low_hz",{435,810,66,65},{425,887,88,18});high=addKnob("tap1.high_hz",{728,810,66,65},{719,887,88,18});
            lowOn=addButton("tap1.low_on",{544,820,110,43});highOn=addButton("tap1.high_on",{840,820,107,43});
            addKnob("mix",{1221,797,80,79},{1221,884,79,21});addKnob("output",{1385,797,80,79},{1385,884,79,21});
            addButton("sync",{1040,776,119,43});addButton("bypass",{1521,820,95,43});
        }
    }
    ~View(){for(auto& k:knobs)k->slider.setLookAndFeel(nullptr);for(auto& b:buttons)b->button.setLookAndFeel(nullptr);for(auto& s:selectors)s.setLookAndFeel(nullptr);tapTempo.setLookAndFeel(nullptr);}
    void bind(Knob& k,const juce::String& id){
        k.attachment.reset();k.id=id;const auto& specs=processor.parameterSpecs();
        auto it=std::find_if(specs.begin(),specs.end(),[&](const auto& s){return id==s.id;});jassert(it!=specs.end());if(it==specs.end())return;
        k.unit=it->unit;k.slider.division=id.endsWith("division");k.slider.processor=&processor;
        k.slider.setName(it->name);k.slider.setComponentID(id);k.slider.setRange(it->minimum,it->maximum,it->step);
        k.slider.setDoubleClickReturnValue(true,it->initial);k.slider.setTooltip(juce::String(it->name)+" / "+processor.helpText());
        k.attachment=std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(processor.state,id,k.slider);
    }
    void bind(Button& b,const juce::String& id){b.attachment.reset();b.id=id;b.button.setComponentID(id);juce::String name=id;for(const auto& spec:processor.parameterSpecs())if(id==spec.id){name=spec.name;break;}b.button.setName(name);b.button.setButtonText(name);b.button.setTooltip(name+" / "+processor.helpText());b.attachment=std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(processor.state,id,b.button);}
    Knob* addKnob(const juce::String& id,Rect face,Rect value){auto k=std::make_unique<Knob>();k->face=face;k->value=value;k->slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);k->slider.setTextBoxStyle(juce::Slider::NoTextBox,false,0,0);k->slider.setLookAndFeel(&look);k->slider.setMouseDragSensitivity(250);editor.addAndMakeVisible(k->slider);bind(*k,id);auto* ptr=k.get();knobs.push_back(std::move(k));return ptr;}
    Button* addButton(const juce::String& id,Rect rect){auto b=std::make_unique<Button>();b->rect=rect;b->button.setLookAndFeel(&look);editor.addAndMakeVisible(b->button);bind(*b,id);auto* ptr=b.get();buttons.push_back(std::move(b));return ptr;}
    void makeTime(Knob& k,const juce::String& manual,const juce::String& division,int tap){k.time=true;k.manualId=manual;k.divisionId=division;k.tap=tap;bind(k,synced?division:manual);}
    juce::Rectangle<int> scaled(Rect rect)const{return Rect(rect.getX()*editor.getWidth()/artW,rect.getY()*editor.getHeight()/artH,rect.getWidth()*editor.getWidth()/artW,rect.getHeight()*editor.getHeight()/artH).toNearestInt();}
    juce::String valueText(const Knob& k)const{
        const double value=k.slider.getValue();
        if(k.slider.division)return divisions[std::size_t(juce::jlimit(0,20,int(value)))];
        return juce::String(value,k.unit=="Hz"||k.unit=="%"?0:1)+(k.unit.isEmpty()?"":" "+k.unit);
    }
    juce::Font valueFont(const Knob& k)const{
        juce::Font font(juce::FontOptions(tape?19.f:15.f));
        const float width=juce::GlyphArrangement::getStringWidth(font,valueText(k));
        if(width>k.value.getWidth()-4)font.setHeight(std::max(12.f,font.getHeight()*(k.value.getWidth()-4)/width));
        return font;
    }
    juce::StringArray layoutIssues()const{
        juce::StringArray issues;const Rect canvas(0,0,artW,artH);
        for(const auto& k:knobs){
            if(!canvas.contains(k->face)||!canvas.contains(k->value)||!editor.getLocalBounds().contains(k->slider.getBounds()))issues.add(k->id+": geometry outside editor");
            const auto font=valueFont(*k);
            if(juce::GlyphArrangement::getStringWidth(font,valueText(*k))>k->value.getWidth()-3||font.getHeight()>k->value.getHeight())issues.add(k->id+": clipped value text");
        }
        for(const auto& b:buttons)if(!canvas.contains(b->rect)||!editor.getLocalBounds().contains(b->button.getBounds()))issues.add(b->id+": button outside editor");
        const juce::Font statusFont(juce::FontOptions(12));
        if(juce::GlyphArrangement::getStringWidth(statusFont,statusText())>graph.getWidth())issues.add("status text clipped");
        const juce::Font tempoFont(juce::FontOptions(tape?20.f:16.f));
        if(juce::GlyphArrangement::getStringWidth(tempoFont,juce::String(processor.bpm(),2)+" BPM")>tempoRect.getWidth())issues.add("tempo text clipped");
        return issues;
    }
    void meter(juce::Graphics& g,Rect area,float left,float right,bool valid){
        g.setColour(juce::Colours::black);g.fillRect(area);
        const float inset=tape?12.f:22.f;
        const auto bar=Rect(area.getX()+inset,area.getY()+25,21,area.getHeight()-50);
        g.setFont(juce::FontOptions(10));g.setColour(juce::Colours::silver);
        for(int tick=-60;tick<=0;tick+=12){const float y=bar.getBottom()-bar.getHeight()*ui::meterDbPosition(float(tick));
            g.drawText(juce::String(tick),int(area.getCentreX()-15),int(y-6),30,12,juce::Justification::centred);}
        for(int ch=0;ch<2;++ch){
            const float peak=ch?right:left;const bool channelValid=valid&&std::isfinite(peak)&&peak>=0;
            const auto lane=bar.withX(ch?area.getRight()-inset-21:bar.getX());
            ui::paintSegmentedMeter(g,lane,ui::meterAmplitudePosition(peak),channelValid);
            g.setColour(juce::Colours::white);g.setFont(juce::FontOptions(12));
            g.drawText(ch?"R":"L",lane.withY(area.getY()).withHeight(22).toNearestInt(),juce::Justification::centred);
            if(valid){const float db=peak>0?20*std::log10(peak):-160.f;
                g.setColour(channelValid&&db>0?juce::Colours::orange:juce::Colours::white);g.setFont(juce::FontOptions(10));
                const auto read=Rect(ch?area.getCentreX():area.getX(),area.getBottom()-22,area.getWidth()*.5f,22);
                g.drawText(!channelValid?"N/A":peak>0?juce::String(db,1):"-inf",read.toNearestInt(),juce::Justification::centred);
            }
        }
        if(!valid){g.setColour(juce::Colours::white);g.setFont(juce::FontOptions(12));g.drawText("NO DATA",area.withHeight(22).withY(area.getBottom()-22).toNearestInt(),juce::Justification::centred);}
    }
    void paint(juce::Graphics& g){
        g.addTransform(juce::AffineTransform::scale(editor.getWidth()/artW,editor.getHeight()/artH));
        g.drawImage(image,Rect(0,0,artW,artH));
        // Authored signal glows cross the lower glass boundary. Clear that
        // narrow spill too, then restore the immutable baseline, not the signal.
        g.setColour(juce::Colours::black);
        g.fillRect(tape?Rect(268,428,1135,7):Rect(330,301,1054,7));
        g.setColour(tape?juce::Colour(0xff39777f):juce::Colours::silver);
        g.drawHorizontalLine(tape?429:302,tape?268.f:330.f,tape?1403.f:1384.f);
        for(const auto& k:knobs){
            g.setColour(juce::Colours::black);g.fillRect(k->value);
            g.setColour(juce::Colours::white);g.setFont(valueFont(*k));g.drawText(valueText(*k),k->value.toNearestInt(),juce::Justification::centred);
        }
        const auto tokenBefore=processor.meterPublication();const auto snapshot=processor.meterSnapshot();
        PluginTelemetry::Levels levels;const auto* telemetry=processor.pluginTelemetry();
        const bool levelsValid=telemetry&&telemetry->read(levels)&&levels.valid;
        const auto tokenAfter=processor.meterPublication();
        const bool valid=freshness.observe(tokenAfter,tokenBefore==tokenAfter&&snapshot.isPrepared&&snapshot.hasProcessedAudio&&snapshot.valueAvailable&&levelsValid,juce::Time::getMillisecondCounterHiRes());meterLive=valid;
        meter(g,inputMeter,levels.peak[0],levels.peak[1],valid);meter(g,outputMeter,levels.peak[2],levels.peak[3],valid);
        g.setColour(juce::Colours::black);g.fillRect(tempoRect);g.setColour(juce::Colours::white);g.setFont(juce::FontOptions(tape?20.f:16.f));g.drawText(juce::String(processor.bpm(),2)+" BPM",tempoRect.toNearestInt(),juce::Justification::centred);
        g.setColour(juce::Colour(0xff030a0c));g.fillRect(graph);g.setColour(juce::Colour(0xff16353b));
        for(int i=0;i<=16;++i){const float x=graph.getX()+i*graph.getWidth()/16;g.drawVerticalLine(int(x),graph.getY(),graph.getBottom());}
        const auto p=processor.settings();
        if(!tape){
            const auto active=delay::activity(p,delay::Kind::multitap);for(int i=0;i<8;++i)if(active[std::size_t(i)]){
                auto time=p.taps[std::size_t(i)].time;time.sync=p.sync;const auto resolved=delay::resolveTime(time,{processor.bpm()});
                const float x=graph.getX()+float(resolved.processingMilliseconds/4000)*graph.getWidth();const float level=float((p.taps[std::size_t(i)].levelDb+60)/72);
                g.setColour(resolved.resolved()?juce::Colours::cyan:juce::Colours::orange);g.drawLine(x,graph.getBottom()-23,x,graph.getBottom()-23-level*(graph.getHeight()-43),2);
                g.drawText(juce::String(i+1),int(x-10),int(graph.getY()),20,18,juce::Justification::centred);
            }
            g.setColour(juce::Colours::cyan.withAlpha(.75f));g.drawRoundedRectangle(selectorRects[std::size_t(selected)],4,2);
        }else {const auto wave=processor.waveform();if(wave.valid){juce::Path line;const float cy=graph.getY()+graph.getHeight()*.42f;
            for(int i=0;i<64;++i){const float x=graph.getX()+graph.getWidth()*i/63, y=cy-juce::jlimit(-1.f,1.f,wave.samples[std::size_t(i)])*graph.getHeight()*.3f;if(i==0)line.startNewSubPath(x,y);else line.lineTo(x,y);}
            g.setColour(juce::Colours::cyan);g.strokePath(line,juce::PathStrokeType(1.5f));g.setFont(juce::FontOptions(12));g.drawText("Historic last output block / +/-1 FS / 0.."+juce::String(wave.durationMs,2)+" ms",graph.withHeight(18).toNearestInt(),juce::Justification::centred);
        }}
        auto status=graph.withHeight(22).withY(graph.getBottom()-22);g.setColour(juce::Colour(0xee000000));g.fillRect(status);g.setColour(juce::Colours::orange);g.setFont(juce::FontOptions(12));
        const juce::String text=statusText();
        g.drawText(text,status.toNearestInt(),juce::Justification::centred);
    }
    juce::String statusText()const{
        const auto sync=syncStatus();
        if(sync.unavailable)return sync.blocksAudibleExport?"SYNC UNAVAILABLE / manual fallback / blocks export when node is audible":"SYNC UNAVAILABLE / manual fallback / affected wet path inactive; export allowed";
        if(!meterLive)return "NO LIVE AUDIO DATA / waiting for a fresh callback";
        return processor.protectionCount()>0?"Feedback protection events: "+juce::String(juce::int64(processor.protectionCount())):tape?"Tape-inspired wet coloration / real output samples":"Calculated active taps / 0..4000 ms / levels -60..+12 dB";
    }
};
DelayEditor::DelayEditor(DelayProcessor& processor):AudioProcessorEditor(processor),view(std::make_unique<View>(*this,processor)){
    setOpaque(true);setResizable(true,true);setResizeLimits(1003,565,2006,1129);getConstrainer()->setFixedAspectRatio(1672.0/941.0);setSize(1280,720);startTimerHz(30);
}
DelayEditor::~DelayEditor(){stopTimer();view.reset();}
void DelayEditor::paint(juce::Graphics& g){view->paint(g);}
void DelayEditor::resized(){if(!view)return;for(auto& k:view->knobs)k->slider.setBounds(view->scaled(k->face));for(auto& b:view->buttons)b->button.setBounds(view->scaled(b->rect));if(view->tape)view->tapTempo.setBounds(view->scaled({436,506,149,48}));else for(std::size_t i=0;i<8;++i)view->selectors[i].setBounds(view->scaled(view->selectorRects[i]));}
void DelayEditor::selectTap(int i){if(view->tape)return;view->selected=juce::jlimit(0,7,i);const auto prefix=tapPrefix(view->selected);view->bind(*view->low,prefix+"low_hz");view->bind(*view->high,prefix+"high_hz");view->bind(*view->lowOn,prefix+"low_on");view->bind(*view->highOn,prefix+"high_on");repaint();}
int DelayEditor::selectedTap()const noexcept{return view->selected;}
juce::String DelayEditor::statusText()const{return view->statusText();}
juce::StringArray DelayEditor::layoutIssues()const{return view->layoutIssues();}
void DelayEditor::timerCallback(){const auto p=view->processor.settings();if(view->synced!=p.sync){view->synced=p.sync;for(auto& k:view->knobs)if(k->time)view->bind(*k,p.sync?k->divisionId:k->manualId);}
 for(auto& b:view->buttons)if(b->id=="sync")b->button.getProperties().set("unavailable",view->unresolved());
 for(auto& k:view->knobs)if(k->time){auto t=p.taps[std::size_t(k->tap)].time;t.sync=p.sync;const auto resolved=delay::resolveTime(t,{view->processor.bpm()});k->slider.setTooltip((resolved.resolved()?"Effective delay ":"Sync unavailable; manual fallback ")+juce::String(resolved.processingMilliseconds,2)+" ms / project "+juce::String(view->processor.bpm(),2)+" BPM");}
 view->tapTempo.setEnabled(!p.sync);view->tapTempo.setTooltip(p.sync?"Tap tempo unavailable while synced to project BPM":"Tap twice; 100..4000 ms interval sets manual delay");repaint();}
}
