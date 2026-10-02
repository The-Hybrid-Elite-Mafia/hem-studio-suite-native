#include "Editors.h"
#include "PluginMeterReadout.h"
namespace hem {
ModuleEditor::ModuleEditor(ModuleProcessor& p):AudioProcessorEditor(p),processor(p){
 setLookAndFeel(&look);for(const auto& spec:p.parameterSpecs()){
    auto c=std::make_unique<Control>();c->isToggle=spec.toggle;
    if(spec.toggle){c->toggle.setComponentID(spec.id);c->toggle.setName(spec.name);addAndMakeVisible(c->toggle);c->toggle.setButtonText(spec.name);c->buttonAttachment=std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(p.state,spec.id,c->toggle);}
    else {addAndMakeVisible(c->label);c->label.setText(spec.name,juce::dontSendNotification);c->label.setJustificationType(juce::Justification::centred);
        c->knob.setComponentID(spec.id);c->knob.setName(spec.name);c->knob.setTooltip(juce::String(spec.name)+" / "+spec.unit);addAndMakeVisible(c->knob);c->knob.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);c->knob.setTextBoxStyle(juce::Slider::TextBoxBelow,false,110,22);c->knob.setTextValueSuffix(juce::String(" ")+spec.unit);
        c->knob.setMouseDragSensitivity(250);c->knob.setDoubleClickReturnValue(true,spec.initial);
        c->sliderAttachment=std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(p.state,spec.id,c->knob);}
    controls.push_back(std::move(c));}
 setSize(960,520);startTimerHz(25);
}
ModuleEditor::~ModuleEditor(){stopTimer();controls.clear();setLookAndFeel(nullptr);}
void ModuleEditor::resized(){const int cols=4,rows=(int(controls.size())+cols-1)/cols;const int top=125,cellW=getWidth()/cols,cellH=juce::jmax(1,(getHeight()-top)/rows);
 for(size_t i=0;i<controls.size();++i){auto& c=*controls[i];juce::Rectangle<int> b(int(i)%cols*cellW,top+int(i)/cols*cellH,cellW,cellH);
    b.reduce(4,2);if(c.isToggle)c.toggle.setBounds(b);else{c.label.setBounds(b.removeFromTop(20));c.knob.setBounds(b);}}}
void ModuleEditor::paint(juce::Graphics& g){
 g.fillAll(juce::Colour(0xf0080b10));
 juce::ColourGradient glass(juce::Colour(0xff253039),0,0,juce::Colour(0xff07090d),0,float(getHeight()),false);
 g.setGradientFill(glass);g.fillRoundedRectangle(getLocalBounds().toFloat().reduced(3),12);
 g.setColour(juce::Colour(0x25ffffff));g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(5),10,1);g.setColour(juce::Colour(0xff00ddff));g.drawRect(getLocalBounds(),1);
 g.setFont(18.f);g.drawText(processor.getName(),12,6,getWidth()-24,26,juce::Justification::centred);
 const auto area=juce::Rectangle<float>(16,42,float(getWidth()-32),77);
 const auto readout=ui::pluginMeterReadout(processor);
 if(auto* eq=dynamic_cast<SurgicalEqProcessor*>(&processor)){
    // This is the analytic TARGET response of the controls, not an analyser.
    // Keep the separately measured output peak and readiness visible beside it.
    auto responseArea=area.withWidth(area.getWidth()*.52f);
    const auto meterArea=area.withTrimmedLeft(responseArea.getWidth()+8);
    g.setColour(juce::Colour(0xff021018));g.fillRect(responseArea);
    g.setColour(juce::Colour(0xffa9bac4));g.setFont(11.f);
    g.drawText("ANALYTIC CONTROL RESPONSE / TARGET",responseArea.withHeight(15).toNearestInt(),juce::Justification::centred);
    const auto plot=responseArea.reduced(31,0).withY(area.getY()+19).withHeight(39);
    const auto p=eq->settings();const double sr=eq->actualRate.load();
    const double maxHz=std::max(20.0,std::min(20000.0,sr*.45));
    g.setFont(9.f);
    for(const float db:{-24.f,0.f,24.f}){
        const float y=plot.getCentreY()-db*plot.getHeight()/48;
        g.setColour(juce::Colour(0xff344751));g.drawHorizontalLine(int(std::round(y)),plot.getX(),plot.getRight());
        g.setColour(juce::Colour(0xffa9bac4));g.drawText(juce::String(db,0),int(responseArea.getX()),int(y)-6,28,12,juce::Justification::centredRight);
    }
    juce::Path curve;
    for(int i=0;i<256;++i){const double hz=20*std::pow(maxHz/20,double(i)/255);const float db=EqCore::response(hz,sr,p);
        const float x=plot.getX()+plot.getWidth()*float(i)/255,y=plot.getCentreY()-juce::jlimit(-24.f,24.f,db)*plot.getHeight()/48;
        if(i==0)curve.startNewSubPath(x,y);else curve.lineTo(x,y);}
    g.setColour(juce::Colours::orange);g.strokePath(curve,juce::PathStrokeType(2));
    g.setColour(juce::Colour(0xffa9bac4));g.setFont(10.f);
    g.drawText("dB / 20 Hz",int(responseArea.getX()+4),int(area.getY()+63),100,13,juce::Justification::centredLeft);
    g.drawText(juce::String(maxHz/1000,1)+" kHz / LOG",int(responseArea.getRight()-124),int(area.getY()+63),120,13,juce::Justification::centredRight);
    ui::paintPluginMeter(g,meterArea,readout);
 }else ui::paintPluginMeter(g,area,readout);
}
}
