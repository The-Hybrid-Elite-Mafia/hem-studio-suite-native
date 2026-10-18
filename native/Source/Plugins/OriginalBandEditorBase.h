#pragma once
#include "BandProcessors.h"
#include "OriginalBandControls.h"
#include <ApprovedArt.h>
#include "OriginalBandTopbar.h"
#include "OriginalBandStereoMeters.h"
namespace hem {
// Shared attachment/telemetry plumbing only. Each module supplies its own
// original faceplate geometry and visualization; there is no parameter pager.
class OriginalBandEditorBase:public juce::AudioProcessorEditor,public OriginalBandTopbarOwner,private juce::Timer {
public:
    OriginalBandEditorBase(BandModuleProcessor& p,const void* data,int bytes):AudioProcessorEditor(p),processor(p),stereoMeters(p),topbar(p,p.bandKind==bands::Kind::Dynamics?OriginalBandTopbar::Layout::dynamics:p.bandKind==bands::Kind::Imager?OriginalBandTopbar::Layout::imager:OriginalBandTopbar::Layout::midSide){
        setLookAndFeel(&look);art=juce::ImageCache::getFromMemory(data,bytes);
        for(const auto& spec:p.parameterSpecs()){
            auto c=std::make_unique<Control>();c->id=spec.id;c->toggle=spec.toggle;
            if(spec.toggle){auto& b=c->button;b.setComponentID(spec.id);b.setName(spec.name);b.setWantsKeyboardFocus(true);
                b.setButtonText(c->id=="enabled"?"BYPASS":c->id.endsWith("Solo")?juce::String(spec.name):c->id=="lowMono"?"LOW MONO":"ON");
                if(c->id.endsWith("Solo")&&(p.bandKind==bands::Kind::Dynamics||p.bandKind==bands::Kind::Imager))b.setButtonText("SOLO");
                if(c->id=="enabled"){b.getProperties().set("inverseFaceplate",true);b.setTooltip("PROCESS ENABLE: checked means processing. BYPASS is lit when processing is disabled.");}
                addAndMakeVisible(b);c->ba=std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(p.state,spec.id,b);b.onClick=[this]{refresh();};
            }else{auto& s=c->slider;s.setComponentID(spec.id);s.setName(spec.name);s.setWantsKeyboardFocus(true);
                s.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);s.setTextBoxStyle(juce::Slider::TextBoxBelow,false,75,30);
                s.setTextValueSuffix(juce::String(" ")+(juce::String(spec.unit)=="dBFS peak"?"dBFS":spec.unit));s.setDoubleClickReturnValue(true,spec.initial);
                s.setRotaryParameters(juce::MathConstants<float>::pi*1.2f,juce::MathConstants<float>::pi*2.8f,true);s.setTooltip(juce::String(spec.name)+" / "+spec.unit);
                if(c->id.endsWith("Type"))s.choices={"BELL","LOW SHELF","HIGH SHELF"};
                if(c->id=="topology")s.choices={"LEGACY 3 BAND","FOUR BAND"};
                if(!s.choices.isEmpty()){s.setTextBoxStyle(juce::Slider::NoTextBox,false,0,0);const juce::String id=c->id;
                    s.onChoice=[this,id](int value){auto* v=processor.state.getParameter(id);v->beginChangeGesture();v->setValueNotifyingHost(v->convertTo0to1(float(value)));v->endChangeGesture();refresh();};}
                addAndMakeVisible(s);c->sa=std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(p.state,spec.id,s);
            }controls.push_back(std::move(c));
        }
        resetAll.setComponentID("uiResetAll");resetAll.setName("Reset all parameters");resetAll.onClick=[this]{reset();};addChildComponent(resetAll);
        setResizable(true,true);setResizeLimits(1000,563,1920,1081);if(auto* c=getConstrainer())c->setFixedAspectRatio(1672./941.);
        addAndMakeVisible(topbar);topbar.reset=[this]{reset();};topbar.changed=[this]{refresh();};
        processor.subscribeVisuals(true);startTimerHz(25);
    }
    ~OriginalBandEditorBase() override {stopTimer();processor.subscribeVisuals(false);setLookAndFeel(nullptr);}
    OriginalBandTopbar& bandTopbar() override {return topbar;}
    void layoutTopbar(){topbar.setBounds(getLocalBounds());topbar.toFront(false);}
    virtual void refresh(){for(auto& c:controls){const bool active=processor.parameterActive(c->id);c->button.setEnabled(active);c->slider.setEnabled(active);}repaint();}
    void mouseDown(const juce::MouseEvent& event) override {if(!event.mods.isPopupMenu())return;juce::PopupMenu menu;menu.addItem(1,"Reset all parameters");
        juce::Component::SafePointer<OriginalBandEditorBase> safe(this);menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this),[safe](int choice){if(safe&&choice==1)safe->reset();});}
protected:
    friend struct OriginalBandPlotFixture;
    struct Control {juce::String id;bool toggle=false;ui::ReferenceSlider slider;juce::ToggleButton button;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> sa;std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> ba;};
    juce::Rectangle<int> scaled(juce::Rectangle<float> r) const {return r.transformedBy(juce::AffineTransform::scale(getWidth()/1672.f,getHeight()/941.f)).toNearestInt();}
    void knob(size_t i,juce::Rectangle<float> bounds,juce::Rectangle<float> dial,juce::Rectangle<float> text,juce::Rectangle<int> crop,bool visible=true){auto& s=controls[i]->slider;
        if(!s.dial.isValid())s.setDial(art,crop);s.dialBounds=scaled(dial);s.textBounds=scaled(text);s.setBounds(scaled(bounds));s.setVisible(visible);s.resized();}
    void button(size_t i,juce::Rectangle<float> bounds,bool visible=true){controls[i]->button.setBounds(scaled(bounds));controls[i]->button.setVisible(visible);}
    void choice(size_t i,juce::Rectangle<float> bounds,bool visible=true){auto& s=controls[i]->slider;s.dialBounds=scaled({0,0,bounds.getWidth(),bounds.getHeight()});s.textBounds={};s.setBounds(scaled(bounds));s.setVisible(visible);s.resized();}
    void background(juce::Graphics& g){stereoMeters.poll(processor,measured());g.fillAll(juce::Colour(0xff020608));g.drawImage(art,getLocalBounds().toFloat(),juce::RectanglePlacement::stretchToFit);g.addTransform(juce::AffineTransform::scale(getWidth()/1672.f,getHeight()/941.f));}
    bool measured() const {return processor.processingReady()&&processor.meterSnapshot().hasProcessedAudio&&frame.processed&&!stale;}
    float screenScale() const {return getWidth()/1672.f;}
    void text(juce::Graphics& g,const juce::String& value,juce::Rectangle<float> bounds,juce::Justification alignment=juce::Justification::centred,float height=17,bool bold=false) const {ui::bandText(g,value,bounds,screenScale(),alignment,height,bold);}
    void meter(juce::Graphics& g,juce::Rectangle<float> r,bool output) const {stereoMeters.paint(g,r,output,screenScale());}
    static void grid(juce::Graphics& g,juce::Rectangle<float> r){g.setColour(juce::Colour(0xff020a10));g.fillRect(r);g.setColour(juce::Colour(0xff18313b));
        for(int x=0;x<=20;++x)g.drawVerticalLine(int(r.getX()+r.getWidth()*x/20),r.getY(),r.getBottom());
        for(int y=0;y<=4;++y)g.drawHorizontalLine(int(r.getY()+r.getHeight()*y/4),r.getX(),r.getRight());}
    void reset(){for(const auto& spec:processor.parameterSpecs()){auto* p=processor.state.getParameter(spec.id);p->beginChangeGesture();p->setValueNotifyingHost(p->convertTo0to1(spec.initial));p->endChangeGesture();}refresh();}
    BandModuleProcessor& processor;ui::OriginalBandLookAndFeel look;juce::Image art;std::vector<std::unique_ptr<Control>> controls;
    ui::OriginalBandStereoMeters stereoMeters;OriginalBandTopbar topbar;juce::TextButton resetAll;bands::VisualFrame frame;bool stale=false;
private:
    void timerCallback() override {bands::VisualFrame next;if(processor.visualSnapshot(next)&&(!next.processed||next.revision!=frame.revision)){frame=next;lastFrameTime=juce::Time::getMillisecondCounter();}
        stale=frame.processed&&juce::Time::getMillisecondCounter()-lastFrameTime>500;refresh();}
    juce::uint32 lastFrameTime=0;
};
}
