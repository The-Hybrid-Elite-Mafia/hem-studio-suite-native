#pragma once
#include "BandProcessors.h"
#include "OriginalBandControls.h"
#include <ApprovedArt.h>
#include "OriginalBandTopbar.h"
#include "OriginalBandStereoMeters.h"
namespace hem {
// Owner raster is immutable chrome. Every changing value/pointer/meter/curve
// below is a real attached control or a bounded audio-thread measurement.
class OriginalDynamicEqEditor final:public juce::AudioProcessorEditor,public OriginalBandTopbarOwner,private juce::Timer {
public:
    explicit OriginalDynamicEqEditor(BandModuleProcessor& p):AudioProcessorEditor(p),processor(p),stereoMeters(p),topbar(p,OriginalBandTopbar::Layout::dynamicEq){
        setLookAndFeel(&look);art=juce::ImageCache::getFromMemory(ApprovedArt::approved_03_png,ApprovedArt::approved_03_pngSize);
        for(const auto& spec:p.parameterSpecs()){
            auto control=std::make_unique<Control>();control->id=spec.id;control->toggle=spec.toggle;
            if(spec.toggle){auto& button=control->button;button.setComponentID(spec.id);button.setName(spec.name);button.setWantsKeyboardFocus(true);
                button.setButtonText(juce::String(spec.id)=="enabled"?"BYPASS":juce::String(spec.id).endsWith("DynamicOn")?"DYNAMIC ON":juce::String(spec.id).endsWith("Up")?"DOWN":"BAND ON");
                if(juce::String(spec.id)=="enabled"){button.getProperties().set("inverseFaceplate",true);button.setTooltip("PROCESS ENABLE: checked means processing. The BYPASS lamp is lit when processing is disabled.");}
                if(juce::String(spec.id).endsWith("Up")){button.getProperties().set("directionFaceplate",true);button.setTooltip("UP: dynamic boost above threshold. DOWN: dynamic cut above threshold.");}
                addAndMakeVisible(button);control->ba=std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(p.state,spec.id,button);button.onClick=[this]{refresh();};
            }else{auto& slider=control->slider;slider.setComponentID(spec.id);slider.setName(spec.name);slider.setWantsKeyboardFocus(true);
                slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);slider.setTextBoxStyle(juce::Slider::TextBoxBelow,false,70,25);
                slider.setTextValueSuffix(juce::String(" ")+(juce::String(spec.unit)=="dBFS peak"?"dBFS":spec.unit));slider.setDoubleClickReturnValue(true,spec.initial);
                slider.setRotaryParameters(juce::MathConstants<float>::pi*1.2f,juce::MathConstants<float>::pi*2.8f,true);
                slider.setTooltip(juce::String(spec.name)+" / "+spec.unit);
                if(juce::String(spec.id).endsWith("Type")){slider.choices={"BELL","LOW SHELF","HIGH SHELF"};slider.setTextBoxStyle(juce::Slider::NoTextBox,false,0,0);
                    slider.setTooltip("Bell / resonant-Q low shelf / resonant-Q high shelf. Q is damping, not shelf slope.");
                    const juce::String id(spec.id);slider.onChoice=[this,id](int value){auto* parameter=processor.state.getParameter(id);parameter->beginChangeGesture();parameter->setValueNotifyingHost(parameter->convertTo0to1(float(value)));parameter->endChangeGesture();};}
                addAndMakeVisible(slider);control->sa=std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(p.state,spec.id,slider);
            }controls.push_back(std::move(control));
        }
        page.setComponentID("uiBandPage");page.setName("Selected dynamics band");
        for(int i=0;i<8;++i){page.addItem("BAND "+juce::String(i+1),i+1);auto& tab=tabs[size_t(i)];tab.setButtonText("BAND "+juce::String(i+1));tab.setComponentID("uiBandTab"+juce::String(i+1));tab.setWantsKeyboardFocus(true);addAndMakeVisible(tab);tab.onClick=[this,i]{page.setSelectedId(i+1,juce::sendNotificationSync);};}
        addChildComponent(page);page.setSelectedId(1,juce::dontSendNotification);page.onChange=[this]{resized();refresh();};
        resetAll.setComponentID("uiResetAll");resetAll.setName("Reset all parameters");resetAll.onClick=[this]{reset(true);};addChildComponent(resetAll);
        resetPage.setComponentID("uiResetPage");resetPage.setName("Reset selected band");resetPage.onClick=[this]{reset(false);};addChildComponent(resetPage);
        setResizable(true,true);setResizeLimits(1000,563,1920,1081);if(auto* constrainer=getConstrainer())constrainer->setFixedAspectRatio(1672./941.);
        addAndMakeVisible(topbar);topbar.reset=[this]{reset(true);};topbar.changed=[this]{refresh();};
        setSize(1280,720);processor.subscribeVisuals(true);startTimerHz(25);refresh();
    }
    ~OriginalDynamicEqEditor() override {stopTimer();processor.subscribeVisuals(false);setLookAndFeel(nullptr);}
    OriginalBandTopbar& bandTopbar() override {return topbar;}
    void resized() override {
        topbar.setBounds(getLocalBounds());topbar.toFront(false);
        const int selected=std::clamp(page.getSelectedId()-1,0,7);
        for(int b=0;b<8;++b){const size_t i=2+size_t(b)*8,e=66+size_t(b)*3;const float x=24.f+205*b;
            const float centre[8][3]{{56,119,182},{259,323,388},{464,527,591},{668,732,795},{875,938,1001},{1080,1143,1206},{1285,1349,1412},{1490,1554,1618}};
            const float textOffset[]{0,64,126};for(int k=0;k<3;++k){const float left=centre[b][k]-32;
                knob(i+1+size_t(k),{left,521,64,101},{0,0,64,64},{x+textOffset[k]+7-left,71,50,29},{int(left),521,64,64});}
            button(i,{x+10,630,90,40});choice(e,{x+108,630,76,40});
            const float detailX[]{368,581,796,1011},detailDialX[]{375,590,809,1027};
            for(int d=0;d<4;++d)knob(i+4+size_t(d),{detailX[d],761,110,120},{detailDialX[d]-detailX[d],4,90,84},{0,89,110,30},{int(detailDialX[d]),765,90,84},b==selected);
            button(e+1,{1213,759,139,40},b==selected);button(e+2,{1213,829,139,40},b==selected);
            tabs[size_t(b)].setBounds(scaled({87.f+190*b,390,172,38}));tabs[size_t(b)].setToggleState(b==selected,juce::dontSendNotification);
        }
        knob(1,{1467,738,108,112},{8,0,88,82},{7,85,93,27},{1475,738,88,82});button(0,{1436,854,166,40});
        page.setBounds(scaled({87,390,172,38}));resetAll.setBounds(scaled({70,50,205,50}));resetPage.setBounds(scaled({1331,50,130,50}));
    }
    void paint(juce::Graphics& g) override {
        g.fillAll(juce::Colour(0xff020608));g.drawImage(art,getLocalBounds().toFloat(),juce::RectanglePlacement::stretchToFit);
        g.addTransform(juce::AffineTransform::scale(getWidth()/1672.f,getHeight()/941.f));
        const bool measured=processor.processingReady()&&processor.meterSnapshot().hasProcessedAudio&&frame.processed&&!stale;stereoMeters.poll(processor,measured);
        meter(g,{65,175,69,184},false);meter(g,{1538,175,69,184},true);
        const juce::Rectangle<float> graph(242,151,1233,195);g.setColour(juce::Colour(0xff020a10));g.fillRect(graph);
        const auto plot=graph.reduced(4,0).withTrimmedTop(33).withTrimmedBottom(33);
        g.setColour(juce::Colour(0xff18313b));for(int x=0;x<=20;++x)g.drawVerticalLine(int(plot.getX()+plot.getWidth()*x/20),plot.getY(),plot.getBottom());
        for(int y=0;y<=8;++y)g.drawHorizontalLine(int(plot.getY()+plot.getHeight()*y/8),plot.getX(),plot.getRight());
        g.setColour(juce::Colour(0xff73868d));g.drawHorizontalLine(int(plot.getCentreY()),plot.getX(),plot.getRight());
        if(measured){juce::Path curve;for(int point=0;point<=512;++point){const double frequency=20*std::pow(900.,point/512.);
                const auto z=std::polar(1.,-2*hem::pi*std::min(frequency,frame.rate*.45)/frame.rate);std::complex<double> response=1;
                for(size_t band=0;band<8;++band){const auto& c=frame.coefficients[band];const auto primary=(c[0]+c[1]*z+c[2]*z*z)/(1.+c[3]*z+c[4]*z*z);
                    const auto secondary=(c[5]+c[6]*z+c[7]*z*z)/(1.+c[8]*z+c[9]*z*z);response*=primary+c[10]*(secondary-primary);}
                const double db=20*std::log10(std::max(1e-12,std::abs(response)));const float x=plot.getX()+plot.getWidth()*point/512.f;
                const float y=plot.getCentreY()-float(std::clamp(db,-24.,24.))*plot.getHeight()/48;
                if(point==0)curve.startNewSubPath(x,y);else curve.lineTo(x,y);}
            g.setColour(juce::Colour(0xff3cecff));g.strokePath(curve,juce::PathStrokeType(2));}
        g.setColour(juce::Colour(0xff020a10));g.fillRect(graph.reduced(2).removeFromTop(29));g.fillRect(graph.reduced(2).removeFromBottom(29));
        g.setColour(juce::Colours::whitesmoke);const float scale=getWidth()/1672.f;
        ui::bandText(g,measured?juce::String("EQ PRE OUTPUT / BYPASS | 20 Hz-18 kHz | TOP +24 / BOTTOM -24 dB")+(frame.transition?" | TYPE TRANSITION":""):stale?"AUDIO TELEMETRY STALE":"NO PROCESSED AUDIO",graph.reduced(8,2).removeFromTop(27),scale,juce::Justification::centredLeft);
        if(measured){const int b=std::clamp(page.getSelectedId()-1,0,7);ui::bandText(g,"BAND "+juce::String(b+1)+" DYNAMIC: "+juce::String(frame.signedDynamics[size_t(b)],2)+" dB",graph.reduced(8,2).removeFromBottom(27),scale,juce::Justification::centredRight);}

    }
    void mouseDown(const juce::MouseEvent& event) override {if(!event.mods.isPopupMenu())return;juce::PopupMenu menu;menu.addItem(1,"Reset all parameters");menu.addItem(2,"Reset selected band");
        juce::Component::SafePointer<OriginalDynamicEqEditor> safe(this);menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this),[safe](int choice){if(safe&&choice>0)safe->reset(choice==1);});}
    void refresh(){for(auto& c:controls){const bool active=processor.parameterActive(c->id);c->button.setEnabled(active);c->slider.setEnabled(active);}repaint();}
private:
    friend struct OriginalBandPlotFixture;
    struct Control {juce::String id;bool toggle=false;ui::ReferenceSlider slider;juce::ToggleButton button;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> sa;std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> ba;};
    juce::Rectangle<int> scaled(juce::Rectangle<float> rectangle) const {return rectangle.transformedBy(juce::AffineTransform::scale(getWidth()/1672.f,getHeight()/941.f)).toNearestInt();}
    void knob(size_t index,juce::Rectangle<float> bounds,juce::Rectangle<float> dial,juce::Rectangle<float> text,juce::Rectangle<int> crop,bool visible=true){auto& slider=controls[index]->slider;
        if(!slider.dial.isValid())slider.setDial(art,crop);slider.dialBounds=scaled(dial);slider.textBounds=scaled(text);slider.setBounds(scaled(bounds));slider.setVisible(visible);slider.resized();}
    void button(size_t index,juce::Rectangle<float> bounds,bool visible=true){controls[index]->button.setBounds(scaled(bounds));controls[index]->button.setVisible(visible);}
    void choice(size_t index,juce::Rectangle<float> bounds){auto& slider=controls[index]->slider;slider.dialBounds=scaled({0,0,bounds.getWidth(),bounds.getHeight()});slider.textBounds={};slider.setBounds(scaled(bounds));slider.setVisible(true);slider.resized();}
    void meter(juce::Graphics& g,juce::Rectangle<float> bounds,bool output) const {stereoMeters.paint(g,bounds,output,getWidth()/1672.f);}
    void reset(bool all){const int selected=std::clamp(page.getSelectedId()-1,0,7);for(size_t i=0;i<controls.size();++i){if(!all&&!(i>=2+size_t(selected)*8&&i<10+size_t(selected)*8)&&!(i>=66+size_t(selected)*3&&i<69+size_t(selected)*3))continue;
            const auto& spec=processor.parameterSpecs()[i];auto* parameter=processor.state.getParameter(spec.id);parameter->beginChangeGesture();parameter->setValueNotifyingHost(parameter->convertTo0to1(spec.initial));parameter->endChangeGesture();}refresh();}
    void timerCallback() override {bands::VisualFrame next;if(processor.visualSnapshot(next)&&(!next.processed||next.revision!=frame.revision)){lastFrameTime=juce::Time::getMillisecondCounter();frame=next;}
        stale=frame.processed&&juce::Time::getMillisecondCounter()-lastFrameTime>500;refresh();}
    BandModuleProcessor& processor;ui::OriginalBandLookAndFeel look;juce::Image art;std::vector<std::unique_ptr<Control>> controls;
    ui::OriginalBandStereoMeters stereoMeters;OriginalBandTopbar topbar;std::array<juce::TextButton,8> tabs;juce::ComboBox page;juce::TextButton resetAll,resetPage;bands::VisualFrame frame;juce::uint32 lastFrameTime=0;bool stale=false;
};
}
