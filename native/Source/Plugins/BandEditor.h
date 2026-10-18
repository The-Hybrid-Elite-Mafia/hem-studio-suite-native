#pragma once
#include "BandProcessors.h"
#include "PluginMeterReadout.h"
#include "../UI/PhotorealisticLookAndFeel.h"
#include "../UI/PhysicalValueSlider.h"
#include "../UI/FaceplateLayers.h"
#include "../UI/ModuleEditorGeometry.h"
namespace hem {
class BandEditor final:public juce::AudioProcessorEditor,private juce::Timer {
public:
    explicit BandEditor(BandModuleProcessor& p):AudioProcessorEditor(p),processor(p){
        setLookAndFeel(&look);setInterceptsMouseClicks(true,true);
        faceplate=juce::ImageCache::getFromMemory(ControlArt::module_faceplate_png,ControlArt::module_faceplate_pngSize);
        branding=juce::ImageCache::getFromMemory(ControlArt::approved_module_reference_png,ControlArt::approved_module_reference_pngSize);
        for(const auto& spec:p.parameterSpecs()){
            auto c=std::make_unique<Control>();c->toggle=spec.toggle;c->id=spec.id;
            c->label.setText(spec.name,juce::dontSendNotification);c->label.setJustificationType(juce::Justification::centred);addAndMakeVisible(c->label);
            if(spec.toggle){c->button.setComponentID(spec.id);c->button.setName(spec.name);c->button.setButtonText(spec.name);c->button.setWantsKeyboardFocus(true);
                c->button.setTooltip(juce::String(spec.id).endsWith("Solo")?"Plugin output solo: saved in sessions and rendered in exports.":spec.name);
                addAndMakeVisible(c->button);c->ba=std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(p.state,spec.id,c->button);c->button.onClick=[this]{refresh();};
            }else{c->slider.setComponentID(spec.id);c->slider.setName(spec.name);c->slider.setTooltip(juce::String(spec.name)+" / "+spec.unit);
                c->slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);c->slider.setTextBoxStyle(juce::Slider::TextBoxBelow,false,106,22);
                c->slider.setTextValueSuffix(juce::String(" ")+spec.unit);c->slider.setDoubleClickReturnValue(true,spec.initial);c->slider.setWantsKeyboardFocus(true);
                c->slider.setRotaryParameters(juce::MathConstants<float>::pi*1.2f,juce::MathConstants<float>::pi*2.8f,true);addAndMakeVisible(c->slider);
                c->sa=std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(p.state,spec.id,c->slider);}
            controls.push_back(std::move(c));
        }
        page.setComponentID("uiBandPage");page.setName("Band page (navigation only)");page.setWantsKeyboardFocus(true);
        lane.setComponentID("uiLanePage");lane.setName("MID/SIDE lane (navigation only)");lane.setWantsKeyboardFocus(true);
        if(p.bandKind==bands::Kind::Dynamic)for(int i=1;i<=8;++i)page.addItem("BAND "+juce::String(i),i);
        if(p.bandKind==bands::Kind::MidSide){for(int i=1;i<=4;++i)page.addItem("BAND "+juce::String(i),i);lane.addItem("MID",1);lane.addItem("SIDE",2);}
        if(p.bandKind==bands::Kind::Dynamics){page.addItem("LOW",1);page.addItem("MID",2);page.addItem("HIGH",3);}
        page.setSelectedId(1,juce::dontSendNotification);lane.setSelectedId(1,juce::dontSendNotification);
        addAndMakeVisible(page);addAndMakeVisible(lane);page.onChange=[this]{resized();refresh();};lane.onChange=page.onChange;
        resetPage.setButtonText("RESET PAGE");resetPage.setComponentID("uiResetPage");resetPage.setName("Reset visible band parameters");
        resetAll.setButtonText("RESET ALL");resetAll.setComponentID("uiResetAll");resetAll.setName("Reset all parameters to factory defaults");
        resetPage.onClick=[this]{reset(false);};resetAll.onClick=[this]{reset(true);};addAndMakeVisible(resetPage);addAndMakeVisible(resetAll);
        setResizable(true,true);setResizeLimits(1000,563,1920,1080);if(auto* c=getConstrainer())c->setFixedAspectRatio(1280./720.);
        setSize(1280,720);startTimerHz(25);refresh();
    }
    ~BandEditor() override {stopTimer();controls.clear();setLookAndFeel(nullptr);}
    void resized() override {
        const auto scale=[this](juce::Rectangle<float> r){return r.transformedBy(juce::AffineTransform::scale(getWidth()/1280.f,getHeight()/720.f)).toNearestInt();};
        page.setVisible(processor.bandKind!=bands::Kind::Imager);lane.setVisible(processor.bandKind==bands::Kind::MidSide);
        page.setBounds(scale({230,342,150,28}));lane.setBounds(scale({390,342,150,28}));
        resetPage.setBounds(scale({850,342,130,28}));resetAll.setBounds(scale({990,342,130,28}));
        const int globals=globalCount();
        for(size_t i=0;i<controls.size();++i){auto& c=*controls[i];const bool visible=int(i)<globals||bandVisible(int(i));
            c.label.setVisible(visible&&!c.toggle);c.slider.setVisible(visible&&!c.toggle);c.button.setVisible(visible&&c.toggle);
            juce::Rectangle<float> r;
            if(int(i)<globals){const float width=900.f/globals;r={230+int(i)*width,232,width-8,102};}
            else if(processor.bandKind==bands::Kind::Imager){const int j=int(i)-globals,column=j/3,control=j%3;
                r=control<2?juce::Rectangle<float>{230+column*300.f+control*150.f,390,140,142}
                           :juce::Rectangle<float>{230+column*300.f,548,290,44};}
            else{const int position=(int(i)-globals)%(processor.bandKind==bands::Kind::MidSide?4:8);r={230+(position%4)*225.f,382+(position/4)*114.f,213,108};}
            c.label.setFont(juce::Font(juce::FontOptions(12.f*getHeight()/720.f)));c.label.setBounds(scale(r.withHeight(18)));
            c.button.setBounds(scale(r.withSizeKeepingCentre(r.getWidth(),34)));c.slider.setBounds(scale(r.withTrimmedTop(18)));
            c.slider.setTextBoxStyle(juce::Slider::TextBoxBelow,false,int(106.f*getWidth()/1280),int(22.f*getHeight()/720));
        }
    }
    void paint(juce::Graphics& g) override {
        g.fillAll(juce::Colour(0xff030609));g.drawImage(faceplate,getLocalBounds().toFloat(),juce::RectanglePlacement::stretchToFit);
        ui::preserveModuleBranding(g,branding,getLocalBounds().toFloat());
        g.addTransform(juce::AffineTransform::scale(getWidth()/1280.f,getHeight()/720.f));
        g.setColour(juce::Colour(0xff78edff));g.setFont(19.f);g.drawFittedText(processor.getName(),866,23,374,55,juce::Justification::centred,2);
        auto readout=ui::pluginMeterReadout(processor);ui::paintPluginMeter(g,ui::moduleMeterPanel(),readout);
        const auto snapshot=processor.bandSnapshot();g.setFont(12.f);g.setColour(juce::Colour(0xffffb36b));
        const auto status=!snapshot.available||!snapshot.processed?"NO PROCESSED AUDIO":snapshot.enableMaximum==0?"PROCESS OFF":snapshot.enableMinimum<1?"PROCESS TRANSITION":"PROCESS ON";
        g.drawText(status,230,92,400,25,juce::Justification::centredLeft);
        g.drawText("OUTPUT SAMPLE PEAK: "+(snapshot.processed?(snapshot.outputPeak>0?juce::String(20*std::log10(snapshot.outputPeak),2)+" dBFS":"-inf dBFS / SILENCE"):"UNAVAILABLE"),630,92,500,25,juce::Justification::centredRight);
        if(processor.bandKind==bands::Kind::Dynamic||processor.bandKind==bands::Kind::Dynamics){const int bands=processor.bandKind==bands::Kind::Dynamic?8:3;
            for(int i=0;i<bands;++i){const int width=900/bands;const juce::String label=processor.bandKind==bands::Kind::Dynamic?"B"+juce::String(i+1):i==0?"LOW":i==1?"MID":"HIGH";
                g.drawText(label+": "+(snapshot.processed?juce::String(snapshot.cuts[size_t(i)],2)+" dB":"--"),230+i*width,620,width,22,juce::Justification::centred);}}
        if(processor.bandKind==bands::Kind::Imager){g.setColour(juce::Colour(0xff78edff));for(int i=0;i<3;++i)g.drawText(i==0?"LOW":i==1?"MID":"HIGH",230+i*300,365,290,20,juce::Justification::centred);}
        g.setColour(juce::Colour(0xffa9bac4));g.setFont(12.f);g.drawFittedText(processor.helpText(),230,650,900,46,juce::Justification::centred,3);
    }
    void refresh(){for(auto& c:controls){const bool active=processor.parameterActive(c->id);c->slider.setEnabled(active);c->button.setEnabled(active);c->label.setEnabled(active);}repaint();}
private:
    struct Control {juce::String id;bool toggle=false;juce::Label label;ui::PhysicalValueSlider slider;juce::ToggleButton button;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> sa;std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> ba;};
    int globalCount() const {return processor.bandKind==bands::Kind::Dynamic||processor.bandKind==bands::Kind::MidSide?2:processor.bandKind==bands::Kind::Dynamics?4:5;}
    bool bandVisible(int index) const {
        if(processor.bandKind==bands::Kind::Imager)return true;
        const int size=processor.bandKind==bands::Kind::MidSide?4:8;
        const int selected=std::max(0,page.getSelectedId()-1)+(processor.bandKind==bands::Kind::MidSide?4*std::max(0,lane.getSelectedId()-1):0);
        return index>=globalCount()+selected*size&&index<globalCount()+(selected+1)*size;
    }
    void reset(bool all){for(size_t i=0;i<controls.size();++i)if(all||(int(i)>=globalCount()&&bandVisible(int(i)))){
        const auto& spec=processor.parameterSpecs()[i];auto* p=processor.state.getParameter(spec.id);p->beginChangeGesture();p->setValueNotifyingHost(p->convertTo0to1(spec.initial));p->endChangeGesture();}refresh();}
    void timerCallback() override {refresh();}
    BandModuleProcessor& processor;ui::PhotorealisticLookAndFeel look;juce::Image faceplate,branding;
    std::vector<std::unique_ptr<Control>> controls;juce::ComboBox page,lane;juce::TextButton resetPage,resetAll;
};
}
