#pragma once
#include "Editors.h"
#include "../DSP/FoundationTelemetry.h"
#include "../DSP/FoundationCore.h"
#include "../DSP/HumAnalysis.h"
#include "../UI/FoundationMappings.h"
#include "../UI/FoundationPlotGeometry.h"
#include <ApprovedArt.h>
#include "FoundationApprovedPresentation.h"
#include "../UI/FoundationMaterialLook.h"
#include "../UI/PluginTelemetryView.h"

namespace hem {
// Original immutable art supplies the authored chrome, crown, typography and
// ghost. Live widgets repaint complete dial interiors and readout apertures;
// authored placeholder values/pointers are never used as live state.
class FoundationEditor : public ModuleEditor {
public:
    FoundationEditor(ModuleProcessor& p,FoundationKind k):ModuleEditor(p,false),processor(p),kind(k),source(dynamic_cast<FoundationDisplaySource*>(&p)) {
        switch(kind){
        case FoundationKind::cleaner:art=juce::ImageCache::getFromMemory(ApprovedArt::approved_11_png,ApprovedArt::approved_11_pngSize);break;
        case FoundationKind::gate:art=juce::ImageCache::getFromMemory(ApprovedArt::approved_08_png,ApprovedArt::approved_08_pngSize);break;
        case FoundationKind::rider:art=juce::ImageCache::getFromMemory(ApprovedArt::approved_05_png,ApprovedArt::approved_05_pngSize);break;
        case FoundationKind::plosive:art=juce::ImageCache::getFromMemory(ApprovedArt::approved_07_png,ApprovedArt::approved_07_pngSize);break;
        case FoundationKind::hum:art=juce::ImageCache::getFromMemory(ApprovedArt::approved_10_png,ApprovedArt::approved_10_pngSize);break;
        }
        setLookAndFeel(&look);
        for(const auto& spec:p.parameterSpecs()){
            auto c=std::make_unique<Control>();c->id=spec.id;c->isToggle=spec.toggle;c->caption=spec.name;c->isChoice=kind==FoundationKind::cleaner&&c->id=="steep";
            if(c->isChoice){c->choice.setComponentID(spec.id);c->choice.setName("Subsonic slope");c->choice.setTooltip("Subsonic filter slope: exactly 12 or 24 dB/oct. Existing steep boolean parameter.");
                c->choice.addItem("12 dB/oct",1);c->choice.addItem("24 dB/oct",2);c->choice.setLookAndFeel(&slopeLook);c->choice.setColour(juce::ComboBox::textColourId,juce::Colours::transparentBlack);addChildComponent(c->choice);
                auto* raw=c.get();c->mappedAttachment=std::make_unique<juce::ParameterAttachment>(*p.state.getParameter(spec.id),[raw](float value){const juce::ScopedValueSetter<bool> guard(raw->updating,true);raw->choice.setSelectedId(value>=.5f?2:1,juce::dontSendNotification);});
                c->choice.onChange=[raw]{if(raw->updating||!raw->choice.isEnabled())return;const auto id=raw->choice.getSelectedId();if(id==1||id==2)raw->mappedAttachment->setValueAsCompleteGesture(id==2?1.f:0.f);};c->mappedAttachment->sendInitialUpdate();}
            else if(spec.toggle){c->toggle.setComponentID(spec.id);c->toggle.setName(spec.name);c->toggle.setButtonText(spec.name);addChildComponent(c->toggle);
                c->button=std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(p.state,spec.id,c->toggle);}
            else {c->slider.setComponentID(spec.id);c->slider.setName(spec.name);c->slider.setTooltip(juce::String(spec.name)+" / "+spec.unit+(kind==FoundationKind::gate?" / Double-click: legacy parameter default (not a factory profile reset)":""));
                c->slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);c->slider.setTextBoxStyle(juce::Slider::TextBoxBelow,false,100,30);
                c->slider.setTextValueSuffix(juce::String(" ")+(juce::String(spec.unit).startsWith("dBFS")?juce::String("dB"):juce::String(spec.unit)));c->slider.setRotaryParameters(juce::MathConstants<float>::pi*1.2f,juce::MathConstants<float>::pi*2.8f,true);
                c->slider.setDoubleClickReturnValue(true,spec.initial);c->slider.setMouseDragSensitivity(250);addChildComponent(c->slider);
                c->attachment=std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(p.state,spec.id,c->slider);}
            controls.push_back(std::move(c));
        }
        using namespace ui::foundationMapping;
        if(kind==FoundationKind::rider)makeMapped("riderSensitivity","floor","Sensitivity / linked RMS activity floor","%",0,100,riderSensitivity,riderFloor);
        if(kind==FoundationKind::plosive)makeMapped("plosiveSensitivity","threshold","Sensitivity / low-band RMS threshold; Burst Contrast also applies","%",0,100,plosiveSensitivity,plosiveThreshold);
        if(kind==FoundationKind::hum)makeMapped("notchWidthHz","q","Full-depth isolated first-notch -3 dB bandwidth; shared Q at higher harmonics","Hz",
            notchWidth(fundamental(),mappingRate(),60),notchWidth(fundamental(),mappingRate(),10),
            [this](double q){return notchWidth(fundamental(),mappingRate(),q);},[this](double width){return notchQ(fundamental(),mappingRate(),width);});
        configureLayout();
        hostBypassButton.setComponentID("foundation-host-bypass");hostBypassButton.setButtonText("BYPASS");
        hostBypassButton.setTooltip("Existing host-slot bypass. Delay compensation remains active. Unavailable outside a bound host slot.");
        hostBypassButton.onClick=[this]{requestHostBypass(hostBypassButton.getToggleState());};addAndMakeVisible(hostBypassButton);
        if(kind==FoundationKind::rider){for(int i=0;i<2;++i){speed[size_t(i)].setComponentID(i?"rider-speed-fast":"rider-speed-slow");speed[size_t(i)].setButtonText(i?"FAST":"SLOW");speed[size_t(i)].onClick=[this,i]{applySpeed(i!=0);};addAndMakeVisible(speed[size_t(i)]);}}
        advanced.setButtonText("ADVANCED");advanced.setComponentID("foundation-advanced");advanced.setClickingTogglesState(true);
        advanced.setTooltip(processor.helpText());
        advanced.onClick=[this]{advancedVisible=advanced.getToggleState();resized();repaint();};addAndMakeVisible(advanced);
        if(kind==FoundationKind::gate){expander.setButtonText("EXPANDER");expander.onClick=[this]{setParameter("gate",0);};addAndMakeVisible(expander);}
        if(kind==FoundationKind::hum){for(int i=0;i<2;++i){base[size_t(i)].setButtonText(i?"SIXTY Hz":"FIFTY Hz");base[size_t(i)].onClick=[this,i]{setParameter("fundamental",i?60:50);};addAndMakeVisible(base[size_t(i)]);}
            for(int i=0;i<2;++i){fine[size_t(i)].setComponentID(i?"fundamental-step-down":"fundamental-step-up");fine[size_t(i)].setButtonText(i?"-":"+");
                fine[size_t(i)].onClick=[this,i]{const float current=processor.state.getRawParameterValue("fundamental")->load();setParameter("fundamental",std::clamp(current+(i?-.01f:.01f),40.f,70.f));};addAndMakeVisible(fine[size_t(i)]);}}

        if(kind==FoundationKind::rider){rideStatus.setComponentID("rider-live-readonly");rideStatus.setName("Live automatic riding gain, read-only measurement");rideStatus.setText("read-only",juce::dontSendNotification);rideStatus.setJustificationType(juce::Justification::centred);rideStatus.setFont(10.f);rideStatus.setColour(juce::Label::backgroundColourId,juce::Colours::transparentBlack);addAndMakeVisible(rideStatus);}
        setResizable(true,false);setResizeLimits(1000,563,1920,1080);if(auto* c=getConstrainer())c->setFixedAspectRatio(1672.0/941.0);
        freshness.arm(processor.pluginTelemetry(),source?&source->displayTelemetry():nullptr);
        if(auto* analysis=dynamic_cast<HumAnalysisSource*>(&processor)){HumAnalysisFrame f;if(analysis->readHumAnalysis(f)){humOpenedEpoch=f.epoch;humOpenedSample=f.endSample;}}
        configureMaterials();setSize(1280,720);resized();refreshParameterActivity();
    }
    ~FoundationEditor() override {controls.clear();setLookAndFeel(nullptr);}
    void refreshParameterActivity() override {
        freshness.poll(processor.pluginTelemetry(),source?&source->displayTelemetry():nullptr,processor.processingReady());
        for(auto& c:controls){const bool enabled=processor.parameterActive(c->id);c->slider.setEnabled(enabled);c->toggle.setEnabled(enabled);c->choice.setEnabled(enabled);if(c->isChoice)c->choice.setSelectedId(processor.state.getRawParameterValue(c->id)->load()>.5f?2:1,juce::dontSendNotification);}
        if(kind==FoundationKind::gate)expander.setToggleState(processor.state.getRawParameterValue("gate")->load()<.5f,juce::dontSendNotification);
        if(kind==FoundationKind::hum)for(int i=0;i<2;++i)base[size_t(i)].setToggleState(std::abs(processor.state.getRawParameterValue("fundamental")->load()-(i?60:50))<.005f,juce::dontSendNotification);
        for(auto& c:controls)if(c->mapped){
            const juce::ScopedValueSetter<bool> guard(c->updating,true);
            if(c->id=="notchWidthHz"){
                c->slider.setRange(ui::foundationMapping::notchWidth(fundamental(),mappingRate(),60),ui::foundationMapping::notchWidth(fundamental(),mappingRate(),10),0);
                c->slider.setTooltip("Isolated full-depth first-notch width for stored Q and last processed fundamental ("+juce::String(fundamental(),2)+" Hz); higher harmonics share Q. " + juce::String(processor.actualRate.load()>0?"Actual sample rate ":"Preview sample rate ")+juce::String(mappingRate(),0)+" Hz. Stored Q is unchanged by rate/frequency display updates.");
            }
            auto* parameter=processor.state.getParameter(c->sourceId);
            c->slider.setValue(c->fromRaw(processor.state.getRawParameterValue(c->sourceId)->load()),juce::dontSendNotification);
            c->slider.setDoubleClickReturnValue(true,c->fromRaw(parameter->convertFrom0to1(parameter->getDefaultValue())));
        }
        if(kind==FoundationKind::rider){const auto state=ui::foundationMapping::riderSpeed(processor.state.getRawParameterValue("rise")->load(),processor.state.getRawParameterValue("fall")->load());
            speed[0].setToggleState(state==ui::foundationMapping::RiderSpeed::slow,juce::dontSendNotification);speed[1].setToggleState(state==ui::foundationMapping::RiderSpeed::fast,juce::dontSendNotification);}
        const auto bypassState=hostBypassState();hostBypassButton.setEnabled(bypassState.has_value());hostBypassButton.setToggleState(bypassState.value_or(false),juce::dontSendNotification);hostBypassButton.setButtonText(bypassState.has_value()?"BYPASS":"HOST ONLY");
        repaint();
    }
    void resized() override {
        const float scale=float(getWidth())/1672.f,sy=float(getHeight())/941.f;
        auto scaled=[&](juce::Rectangle<float> r){return juce::Rectangle<float>(r.getX()*scale,r.getY()*sy,r.getWidth()*scale,r.getHeight()*sy).toNearestInt();};
        int extra=0;
        for(auto& c:controls){
            auto& component=c->component();
            component.setVisible(c->front||advancedVisible);
            auto body=c->body;auto read=c->read;
            if(!c->front){const float x=380+extra*230.f;body={x,280,120,120};read={x-20,407,160,33};++extra;}
            if(c->isToggle){component.setBounds(scaled(body));continue;}
            const auto bounds=body.expanded(body.getWidth()>0?(c->front?4.f:26.f):0.f).getUnion(read);component.setBounds(scaled(bounds));
            auto& props=c->slider.getProperties();props.set("bodyRadius",body.getWidth()*.5f*scale);props.set("preserveAuthoredDialRing",c->front);props.set("bodyX",(body.getCentreX()-bounds.getX())*scale);props.set("bodyY",(body.getCentreY()-bounds.getY())*sy);
            props.set("readX",int((read.getX()-bounds.getX())*scale));props.set("readY",int((read.getY()-bounds.getY())*sy));props.set("readW",int(read.getWidth()*scale));props.set("readH",int(read.getHeight()*sy));
            c->slider.setTextBoxStyle(juce::Slider::TextBoxBelow,false,int(read.getWidth()*scale),int(read.getHeight()*sy));c->slider.resized();
        }
        advanced.setBounds(scaled({104,69,200,32}));hostBypassButton.setBounds(scaled(bypassArea));
        speed[0].setBounds(scaled({1438,533,145,45}));speed[1].setBounds(scaled({1438,604,145,44}));rideStatus.setBounds(scaled({767,799,138,11}));
        expander.setBounds(scaled({512,517,130,44}));
        base[0].setBounds(scaled({735,690,91,60}));base[1].setBounds(scaled({847,690,92,60}));fine[0].setBounds(scaled({912,786,26,23}));fine[1].setBounds(scaled({912,815,26,23}));
    }
    void paint(juce::Graphics& g) override {
        g.fillAll(juce::Colours::black);g.setImageResamplingQuality(juce::Graphics::highResamplingQuality);
        g.drawImage(art,getLocalBounds().toFloat(),juce::RectanglePlacement::stretchToFit);
        const juce::Graphics::ScopedSaveState save(g);g.addTransform(juce::AffineTransform::scale(float(getWidth())/1672.f,float(getHeight())/941.f));
        paintData(g);
        if(kind==FoundationKind::cleaner){
            text(g,{824,684,51,25},processor.state.getRawParameterValue("dcOn")->load()>.5f?"ON":"OFF",20);
            text(g,{824,852,51,25},processor.state.getRawParameterValue("subOn")->load()>.5f?"ON":"OFF",20);
            auto* steep=processor.state.getRawParameterValue("steep");text(g,{1020,614,216,34},steep->load()>.5f?"24 dB/oct":"12 dB/oct",24,processor.parameterActive("steep"));
        }
        if(kind==FoundationKind::rider){const bool custom=!speed[0].getToggleState()&&!speed[1].getToggleState();text(g,{1438,651,145,17},custom?"CUSTOM":"",14);}
        if(kind==FoundationKind::cleaner){const auto state=hostBypassState();text(g,{1096,850,63,30},state.has_value()?(state.value()?"ON":"OFF"):"--",20);}
        if(kind==FoundationKind::hum)text(g,{1235,865,132,12},processor.actualRate.load()>0?"":"48 kHz PREVIEW",10);
        if(advancedVisible){
            g.setColour(juce::Colour(0xff081319));g.fillRoundedRectangle({300,209,1080,283},8);g.setColour(juce::Colour(0xff08dce8));g.drawRoundedRectangle({300,209,1080,283},8,2);
            g.setColour(juce::Colours::white);g.setFont(20);g.drawText("ADVANCED / existing parameter compatibility",320,222,1030,25,juce::Justification::centred);
            int i=0;for(const auto& c:controls)if(!c->front){g.setFont(16);g.drawText(c->caption,int(340+i++*230),251,200,24,juce::Justification::centred);}
            g.setColour(juce::Colour(0xffbdcbd0));g.setFont(14);g.drawFittedText(kind==FoundationKind::gate?juce::String("Double-click: legacy parameter default. Factory profiles apply to new inserts only. Profile reset unavailable: host has no undo."):processor.helpText(),320,450,1030,29,juce::Justification::centred,2);
        }
    }
private:
    struct Control {juce::String id,caption,sourceId;bool isToggle=false,isChoice=false,front=false,mapped=false,updating=false,dragging=false;
        std::function<double(double)> fromRaw,toRaw;std::unique_ptr<juce::ParameterAttachment> mappedAttachment;juce::Rectangle<float> body,read;foundationPresentation::Slider slider;juce::ToggleButton toggle;foundationPresentation::SlopeChoice choice;
        juce::Component& component(){return isChoice?static_cast<juce::Component&>(choice):isToggle?static_cast<juce::Component&>(toggle):static_cast<juce::Component&>(slider);}
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> button;
        ~Control(){choice.onChange=nullptr;choice.setLookAndFeel(nullptr);if(mappedAttachment&&dragging)mappedAttachment->endGesture();slider.onValueChange=nullptr;slider.onDragStart=nullptr;slider.onDragEnd=nullptr;mappedAttachment.reset();}
    };
    double mappingRate() const noexcept {const double rate=processor.actualRate.load();return foundation::supportedRate(rate)?rate:48000.;}
    double fundamental() const noexcept {
        FoundationDisplayFrame frame;
        if(source&&processor.processingReady()&&processor.meterSnapshot().hasProcessedAudio&&source->displayTelemetry().read(source->displayTelemetry().latestSerial(),frame)&&frame.values[13]>0)return frame.values[13];
        return processor.state.getRawParameterValue("fundamental")->load();
    }
    void applySpeed(bool fast){
        auto* rise=processor.state.getParameter("rise");auto* fall=processor.state.getParameter("fall");
        rise->beginChangeGesture();fall->beginChangeGesture();
        rise->setValueNotifyingHost(rise->convertTo0to1(fast?200.f:800.f));fall->setValueNotifyingHost(fall->convertTo0to1(fast?75.f:300.f));
        fall->endChangeGesture();rise->endChangeGesture();refreshParameterActivity();
    }
    void makeMapped(const char* id,const char* rawId,const char* name,const char* unit,double minimum,double maximum,
                    std::function<double(double)> from,std::function<double(double)> to){
        auto owned=std::make_unique<Control>();auto* c=owned.get();c->id=id;c->sourceId=rawId;c->caption=name;c->mapped=true;c->fromRaw=std::move(from);c->toRaw=std::move(to);
        c->slider.setComponentID(id);c->slider.setName(name);c->slider.setTooltip(name);c->slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);c->slider.setTextBoxStyle(juce::Slider::TextBoxBelow,false,100,30);
        c->slider.setRange(minimum,maximum,0);c->slider.setNumDecimalPlacesToDisplay(juce::String(id)=="notchWidthHz"?4:2);c->slider.setTextValueSuffix(juce::String(" ")+unit);
        c->slider.setRotaryParameters(juce::MathConstants<float>::pi*1.2f,juce::MathConstants<float>::pi*2.8f,true);c->slider.setMouseDragSensitivity(250);addChildComponent(c->slider);
        auto* parameter=processor.state.getParameter(rawId);
        c->mappedAttachment=std::make_unique<juce::ParameterAttachment>(*parameter,[c](float raw){const juce::ScopedValueSetter<bool> guard(c->updating,true);c->slider.setValue(c->fromRaw(raw),juce::dontSendNotification);});
        c->slider.onDragStart=[c]{c->dragging=true;c->mappedAttachment->beginGesture();};
        c->slider.onDragEnd=[c]{c->mappedAttachment->endGesture();c->dragging=false;};
        c->slider.onValueChange=[c,parameter]{if(c->updating)return;const auto& range=parameter->getNormalisableRange();
            const double converted=c->toRaw(c->slider.getValue());if(!std::isfinite(converted))return;
            const float value=range.snapToLegalValue(float(converted));
            if(c->dragging)c->mappedAttachment->setValueAsPartOfGesture(value);else c->mappedAttachment->setValueAsCompleteGesture(value);};
        c->mappedAttachment->sendInitialUpdate();controls.push_back(std::move(owned));
    }
    void dial(const char* id,float cx,float cy,float r,juce::Rectangle<float> read){for(auto& c:controls)if(c->id==id){c->front=true;c->body={cx-r,cy-r,r*2,r*2};c->read=read;}}
    void toggle(const char* id,juce::Rectangle<float> area,const char* caption){for(auto& c:controls)if(c->id==id){c->front=true;c->body=area;c->toggle.setButtonText(caption);}}
    void configureLayout(){
        switch(kind){
        case FoundationKind::cleaner:
            dial("cutoff",555,726,87,{447,830,216,36});toggle("dcOn",{816,611,64,60},"DC");toggle("subOn",{816,778,64,60},"HPF");toggle("steep",{1024,672,206,30},"24 dB/oct");
            bypassArea={1078,782,93,62};plot={331,251,1036,233};inputMeter={84,210,124,303};outputMeter={1461,210,124,303};break;
        case FoundationKind::gate:
            dial("threshold",287,706,50,{242,780,93,31});dial("ratio",437,706,50,{394,780,92,31});dial("open",581,706,50,{538,780,91,31});
            dial("hold",723,706,50,{686,780,82,31});dial("close",867,706,50,{822,780,88,31});dial("hysteresis",1025,706,50,{976,780,102,31});
            dial("range",1369,706,50,{1320,780,100,31});dial("output",1539,706,50,{1488,780,105,31});toggle("gate",{355,517,126,44},"GATE");
            dial("lookaheadMs",1197,706,50,{1150,780,98,31});toggle("keyListen",{1145,516,182,43},"KEY LISTEN");bypassArea={1364,517,181,42};
            plot={326,205,930,233};inputMeter={96,216,88,249};outputMeter={1489,216,86,249};reductionMeter={1336,216,64,156};break;
        case FoundationKind::rider:
            dial("target",385,615,64,{318,701,131,34});dial("maxUp",605,615,64,{540,701,131,34});dial("maxDown",1068,615,64,{1005,701,129,34});dial("output",1289,615,64,{1223,701,127,34});
            dial("riderSensitivity",160,615,64,{98,701,128,34});bypassArea={1427,688,166,53};
            plot={360,236,865,174};inputMeter={121,198,124,226};outputMeter={1428,198,122,226};break;
        case FoundationKind::plosive:
            dial("range",444,632,70,{371,718,148,25});dial("attack",706,632,70,{633,718,152,25});dial("release",967,632,70,{893,718,152,25});dial("cutoff",1229,632,70,{1155,718,152,25});dial("output",1494,632,70,{1421,718,152,25});
            dial("plosiveSensitivity",180,632,70,{105,718,151,25});toggle("listenRemoved",{592,826,226,51},"LISTEN REMOVED");bypassArea={858,826,222,51};
            plot={292,212,974,224};inputMeter={86,211,86,235};outputMeter={1503,211,84,235};reductionMeter={1348,207,63,156};break;
        case FoundationKind::hum:
            dial("harmonics",383,744,61,{322,828,121,29});dial("depth",598,744,61,{531,828,129,29});dial("output",1522,744,61,{1457,828,129,29});
            // Existing Q is explicitly retained, not falsely labeled as Hz width.
            dial("notchWidthHz",1301,744,61,{1240,828,121,29});
            dial("fundamental",815,796,0,{737,787,153,48});
            dial("sensitivity",1078,744,61,{1015,828,127,29});toggle("listenRemoved",{1418,495,174,34},"LISTEN REMOVED");bypassArea={1418,552,174,32};
            plot={368,259,974,260};inputMeter={114,210,103,268};outputMeter={1448,210,106,250};break;
        }
    }
    void configureMaterials(){
        for(auto& c:controls){if(c->front&&!c->isToggle)look.registerDial(c->id,art,c->body);if(c->front&&c->isToggle)look.registerButton(c->toggle,art,c->body);}
        look.registerButton(hostBypassButton,art,bypassArea);
        if(kind==FoundationKind::gate)look.registerButton(expander,art,{512,517,130,44});
        if(kind==FoundationKind::rider){look.registerButton(speed[0],art,{1438,533,145,45});look.registerButton(speed[1],art,{1438,604,145,44});riderThumb=art.getClippedImage({811,607,51,73}).createCopy();}
        if(kind==FoundationKind::hum){look.registerButton(base[0],art,{735,690,91,60});look.registerButton(base[1],art,{847,690,92,60});look.registerButton(fine[0],art,{912,786,26,23});look.registerButton(fine[1],art,{912,815,26,23});}
    }
    void setParameter(const char* id,float v){if(auto* p=processor.state.getParameter(id)){p->beginChangeGesture();p->setValueNotifyingHost(p->convertTo0to1(v));p->endChangeGesture();}refreshParameterActivity();}
    static void text(juce::Graphics& g,juce::Rectangle<float> area,const juce::String& value,float font=20,bool enabled=true){
        const juce::Graphics::ScopedSaveState clip(g);g.reduceClipRegion(area.toNearestInt());g.setColour(juce::Colour(0xff020609));g.fillRect(area);
        foundationPresentation::label(g,area,value,font,juce::Colour(0xffffc374).withAlpha(enabled?1.f:.4f));
    }
    static void lamp(juce::Graphics& g,juce::Rectangle<float> mask,juce::Point<float> centre,bool active){
        const juce::Graphics::ScopedSaveState clip(g);g.reduceClipRegion(mask.toNearestInt());g.setColour(juce::Colour(0xff03090c));g.fillRect(mask);
        g.setColour(juce::Colour(0xff515e63));g.fillEllipse(centre.x-18,centre.y-18,36,36);
        g.setColour(juce::Colour(0xff080e12));g.fillEllipse(centre.x-16,centre.y-16,32,32);
        if(active){g.setColour(juce::Colour(0xff0ae9f3).withAlpha(.2f));g.fillEllipse(centre.x-21,centre.y-21,42,42);}
        g.setColour(active?juce::Colour(0xff09eaf4):juce::Colour(0xff15212a));g.fillEllipse(centre.x-13,centre.y-13,26,26);
    }
    void paintMeter(juce::Graphics& g,juce::Rectangle<float> area,float db,bool valid){
        const juce::Graphics::ScopedSaveState clip(g);g.reduceClipRegion(area.toNearestInt());g.setColour(juce::Colour(0xff020609));g.fillRect(area);
        ui::paintSegmentedMeter(g,area.reduced(10,24),std::clamp(db/24.f,0.f,1.f),valid&&std::isfinite(db),true);
        foundationPresentation::label(g,area.withHeight(22),valid?juce::String(db,1):"--",14);
        foundationPresentation::label(g,area.withY(area.getBottom()-22).withHeight(20),"0..24",12);
    }
    void paintData(juce::Graphics& g){
        freshness.poll(processor.pluginTelemetry(),source?&source->displayTelemetry():nullptr,processor.processingReady());
        const auto& latest=freshness.frame;const bool valid=freshness.historyRead;const bool current=freshness.measured;const auto& v=latest.values;
        foundationPresentation::stereoMeter(g,inputMeter,freshness,false);foundationPresentation::stereoMeter(g,outputMeter,freshness,true);
        if(!reductionMeter.isEmpty())paintMeter(g,reductionMeter,v[10],current);
        const auto data=ui::foundationPlotData(plot).withTrimmedLeft(kind==FoundationKind::hum?44:32).withTrimmedBottom(20);
        if(kind==FoundationKind::hum){
            HumAnalysisFrame frame;const auto* analysis=dynamic_cast<const HumAnalysisSource*>(&processor);const bool measured=analysis&&analysis->readHumAnalysis(frame);
            const bool windowNew=measured&&(frame.epoch!=humOpenedEpoch||frame.endSample!=humOpenedSample);
            const bool windowCurrent=windowNew&&current&&frame.endSample<=latest.endSample&&double(latest.endSample-frame.endSample)/mappingRate()<=double(frame.seconds)*1.25+.1;
            { const juce::Graphics::ScopedSaveState graphClip(g);g.reduceClipRegion(plot.toNearestInt());g.setColour(juce::Colour(0xff020609));g.fillRect(plot);
            g.setColour(juce::Colour(0xff183944));for(int i=0;i<=4;++i)g.drawHorizontalLine(int(data.getY()+i*data.getHeight()/4),data.getX(),data.getRight());
            g.setFont(ui::hardwareFont(14));g.setColour(juce::Colour(0xffb8dce5));
            for(int db:{0,-30,-60,-90,-120})g.drawText(juce::String(db),plot.getX()+2,ui::foundationPlotY(data,float(db),-120,0)-8,40,17,juce::Justification::centred);
            for(float hz:{30.f,50.f,100.f,300.f,1000.f,1500.f}){const float x=data.getX()+std::log(hz/30)/std::log(50.f)*data.getWidth();g.drawText(juce::String(int(hz)),std::clamp(x-20,data.getX(),data.getRight()-40),data.getBottom()+2,40,17,juce::Justification::centred);}
            if(measured){juce::Path path;for(int i=0;i<HumAnalysisFrame::bins;++i){const float x=data.getX()+(i+.5f)*data.getWidth()/HumAnalysisFrame::bins,y=ui::foundationPlotY(data,frame.spectrum[size_t(i)],-120,0);if(i==0)path.startNewSubPath(x,y);else path.lineTo(x,y);}auto fill=path;fill.lineTo(data.getRight(),data.getBottom());fill.lineTo(data.getX(),data.getBottom());fill.closeSubPath();g.setGradientFill(juce::ColourGradient(juce::Colour(0xff12e1f3).withAlpha(.45f),data.getX(),data.getY(),juce::Colour(0xff12e1f3).withAlpha(.025f),data.getX(),data.getBottom(),false));g.fillPath(fill);ui::paintFoundationTrace(g,data,path);}
            else foundationPresentation::label(g,data,"COLLECTING MEASURED WINDOW",19);
            text(g,plot.withHeight(24),"INPUT HARMONIC BAND POWER / 30 ... 1500 Hz LOG / dBFS RMS",16);
            text(g,plot.withY(plot.getBottom()-24).withHeight(24),measured?(windowCurrent?juce::String():"STALE / ")+juce::String(frame.seconds,3)+" s window @ "+juce::String(double(frame.endSample)/mappingRate(),3)+" s / threshold "+juce::String(frame.thresholdDb,1)+" dBFS RMS / 12 dB prominence":"Continuous input measurement / no automatic tuning",16);
            }
            lamp(g,{145,540,48,45},{167,562},windowCurrent&&frame.detected);return;
        }
        // Clear the complete illustrated signal including its edge glow.
        { const juce::Graphics::ScopedSaveState graphClip(g);g.reduceClipRegion(plot.toNearestInt());
        g.setColour(juce::Colour(0xff020609));g.fillRect(plot);g.setColour(juce::Colour(0xff183944));
        for(int i=0;i<=8;++i){const float x=data.getX()+data.getWidth()*i/8;g.drawVerticalLine(int(x),data.getY(),data.getBottom());}
        for(int i=0;i<=4;++i){const float y=data.getY()+data.getHeight()*i/4;g.drawHorizontalLine(int(y),data.getX(),data.getRight());}
        g.setColour(juce::Colours::white);g.setFont(16);
        const bool wave=kind==FoundationKind::cleaner;const bool signedGain=kind==FoundationKind::rider;
        g.drawText(wave?"OUTPUT L cyan / R orange / +1 ... -1 FS":signedGain?"+12 / 0 / -12 dB   current riding gain":kind==FoundationKind::gate?"0 ... 24 dB   applied gate-path reduction":"0 ... 24 dB   wet-scaled detector reduction",plot.withHeight(24).toNearestInt(),juce::Justification::centredLeft);
        g.drawText(current?"4 seconds / measured history":"LAST MEASURED HISTORY / "+(freshness.live?juce::String("WAITING FOR CURRENT FRAME"):freshness.status()),plot.withY(plot.getBottom()-24).withHeight(24).toNearestInt(),juce::Justification::centredRight);
        g.setFont(ui::hardwareFont(14));g.setColour(juce::Colour(0xffb8dce5));
        for(int i=0;i<=4;++i){const float value=wave?1-i*.5f:signedGain?12-i*6.f:i*6.f;g.drawText(juce::String(value,wave?1:0),plot.getX()+2,data.getY()+i*data.getHeight()/4-8,28,17,juce::Justification::centred);}
        g.drawText("-4 s",data.getX(),data.getBottom()+2,50,17,juce::Justification::left);g.drawText("NOW",data.getRight()-50,data.getBottom()+2,50,17,juce::Justification::right);
        if(!valid){g.drawText(processor.meterSnapshot().hasProcessedAudio?"COLLECTING MEASURED AUDIO":"NO PROCESSED AUDIO",data.toNearestInt(),juce::Justification::centred);}
        else {
            const juce::Graphics::ScopedSaveState clipped(g);g.reduceClipRegion(data.expanded(1).toNearestInt());
            const auto& telemetry=source->displayTelemetry();const uint32_t end=telemetry.latestSerial();
            const float sampleRate=float(processor.actualRate.load());const float points=sampleRate>0?4*sampleRate/std::max(1u,latest.samples):480;
            juce::Path path;bool started=false;
            const int count=std::min(480,int(std::min(end,uint32_t(480))));
            for(int i=count-1;i>=0;--i){FoundationDisplayFrame f;if(!telemetry.read(end-uint32_t(i),f))continue;const float x=data.getRight()-data.getWidth()*i/points;
                if(wave){for(int c=0;c<2;++c){g.setColour(c?juce::Colour(0xffff941c):juce::Colour(0xff10eaff));const float y1=data.getCentreY()-std::clamp(f.values[size_t(6+c*2)],-1.f,1.f)*data.getHeight()*.5f;const float y2=data.getCentreY()-std::clamp(f.values[size_t(7+c*2)],-1.f,1.f)*data.getHeight()*.5f;g.drawLine(x,std::min(y1,y2),x,std::max(std::min(y1,y2)+.5f,std::max(y1,y2)),1);}}
                else {const float y=signedGain?ui::foundationPlotY(data,f.values[10],-12,12):ui::foundationPlotY(data,-f.values[10],-24,0);if(started)path.lineTo(x,y);else{path.startNewSubPath(x,y);started=true;}}
            }
            if(!wave&&!path.isEmpty()){
                auto fill=path;const float zero=signedGain?data.getCentreY():data.getY();const auto bounds=path.getBounds();fill.lineTo(bounds.getRight(),zero);fill.lineTo(bounds.getX(),zero);fill.closeSubPath();
                g.setGradientFill(juce::ColourGradient(juce::Colour(0xffffb032).withAlpha(.08f),data.getX(),zero,juce::Colour(0xffffb032).withAlpha(.55f),data.getX(),data.getBottom(),false));g.fillPath(fill);
                g.setColour(juce::Colour(0xffffb94b));g.strokePath(path,juce::PathStrokeType(1.8f));
            }
        }
        }
        if(kind==FoundationKind::cleaner){text(g,{1342,621,241,62},current&&v[12]>.5f?"L "+juce::String(v[2]*100,2)+"%  R "+juce::String(v[3]*100,2)+"%":(current?"WARMING UP / 0.5 s":"-- %"),20);text(g,{1342,785,241,62},current&&v[12]>.5f?"L "+juce::String(v[4]*100,2)+"%  R "+juce::String(v[5]*100,2)+"%":(current?"WARMING UP / 0.5 s":"-- %"),20);}
        if(kind==FoundationKind::gate)lamp(g,{1341,415,55,51},{1367,440},current&&v[11]>.5f);
        if(kind==FoundationKind::plosive)lamp(g,{1350,415,60,51},{1379,440},current&&v[11]>.5f);
        if(kind==FoundationKind::rider){
            // Erase the one baked thumb and shadow, preserving the authored
            // ruler everywhere else. Only the measured thumb moves.
            const juce::Rectangle<float> bakedThumb(806,604,63,78);g.setColour(juce::Colour(0xff080d10));g.fillRect(bakedThumb);
            g.setColour(juce::Colours::black);g.fillRect(829,604,14,78);
            if(current)g.drawImage(riderThumb,ui::riderThumbBounds(v[10]),juce::RectanglePlacement::stretchToFit);
            text(g,{760,798,150,34},"",22);text(g,{763,810,144,22},current?juce::String(v[10],1)+" dB":"-- dB",20);
            text(g,{1268,266,67,147},current?(v[11]>.5f?"ACTIVE":"IDLE"):"--",14);

        }
    }
    ModuleProcessor& processor;const FoundationKind kind;FoundationDisplaySource* source;
    foundationPresentation::Freshness freshness;foundationPresentation::SlopeLook slopeLook;FoundationLook look;juce::Image art,riderThumb;std::vector<std::unique_ptr<Control>> controls;
    juce::Label rideStatus;juce::TextButton advanced,expander;juce::ToggleButton hostBypassButton;std::array<juce::TextButton,2> base,fine,speed;
    uint64_t humOpenedEpoch=0,humOpenedSample=0;bool advancedVisible=false;juce::Rectangle<float> plot,inputMeter,outputMeter,reductionMeter,bypassArea;
};
class DcSubsonicEditor final:public FoundationEditor {public:explicit DcSubsonicEditor(ModuleProcessor& p):FoundationEditor(p,FoundationKind::cleaner){}};
class SmartGateEditor final:public FoundationEditor {public:explicit SmartGateEditor(ModuleProcessor& p):FoundationEditor(p,FoundationKind::gate){}};
class VocalRiderEditor final:public FoundationEditor {public:explicit VocalRiderEditor(ModuleProcessor& p):FoundationEditor(p,FoundationKind::rider){}};
class PlosiveEditor final:public FoundationEditor {public:explicit PlosiveEditor(ModuleProcessor& p):FoundationEditor(p,FoundationKind::plosive){}};
class HumBuzzEditor final:public FoundationEditor {public:explicit HumBuzzEditor(ModuleProcessor& p):FoundationEditor(p,FoundationKind::hum){}};
}
