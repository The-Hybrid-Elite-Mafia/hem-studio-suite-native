#pragma once
#include "Editors.h"
#include "ModulationProcessors.h"
#include "../UI/ApprovedFaceplates.h"
#include "../UI/SegmentedMeter.h"
namespace hem {
class ModulationEditor final:public ModuleEditor {
public:
    ModulationEditor(ModulationProcessor&p,int catalog):ModuleEditor(p,false),processor(p),number(catalog),art(ui::approvedFaceplate(catalog)){
        setLookAndFeel(&look);
        if(number==23){
            choice("mode",0,"CHORUS",{274,552,178,56});choice("mode",1,"FLANGER",{469,552,179,56});choice("mode",2,"PHASER",{667,552,179,56});
            combo("shape",{"Sine","Triangle","Soft square"},{890,559,188,45});toggle("sync","TEMPO SYNC",{1154,564,40,40});toggle("bypass","BYPASS",{1508,564,40,40});
            const char*ids[]{"depth","rate","feedback","mix","output"};const float x[]{357,526,693,861,1031};for(int i=0;i<5;++i)dial(ids[i],x[i],757,54,{x[i]-56,832,112,35});
            dial("delay",1232,774,48,{1174,841,113,34});dial("center",1406,774,48,{1351,841,113,34});combo("stages",{"2 stages","4 stages","6 stages","8 stages"},{1499,758,113,55});
        }else{
            choice("mode",0,"ROTARY",{276,516,219,56});choice("mode",1,"TREMOLO",{516,516,219,56});choice("mode",2,"AUTOPAN",{756,516,219,56});
            toggle("sync","TEMPO SYNC",{1051,530,108,40});toggle("bypass","BYPASS",{1425,527,156,44});
            const char*ids[]{"speed","depth","width","mix","output"};const float x[]{312,511,710,911,1110};for(int i=0;i<5;++i)dial(ids[i],x[i],715,57,{x[i]-69,790,137,33});
            dial("ramp",1535,715,57,{1466,790,137,33});choice("rotary",0,"SLOW",{1249,641,136,42});choice("rotary",1,"FAST",{1249,696,136,42});choice("rotary",2,"BRAKE",{1249,751,136,42});combo("shape",{"Sine","Triangle","Soft square"},{1239,862,301,42});
        }
        setResizable(true,true);setResizeLimits(1000,563,1920,1080);if(auto*c=getConstrainer())c->setFixedAspectRatio(1672./941.);setSize(1280,720);resized();refreshParameterActivity();
    }
    ~ModulationEditor()override{for(auto&c:controls)c->combo.setLookAndFeel(nullptr);controls.clear();setLookAndFeel(nullptr);}
    void resized()override{
        const float sx=getWidth()/1672.f,sy=getHeight()/941.f;auto scale=[&](juce::Rectangle<float>b){return juce::Rectangle<float>(b.getX()*sx,b.getY()*sy,b.getWidth()*sx,b.getHeight()*sy).toNearestInt();};
        for(auto&c:controls){if(c->kind==1){const auto b=c->body.getUnion(c->read);c->slider.setBounds(scale(b));auto&props=c->slider.getProperties();props.set("bodyRadius",c->body.getWidth()*.5f*sx);props.set("bodyX",(c->body.getCentreX()-b.getX())*sx);props.set("bodyY",(c->body.getCentreY()-b.getY())*sy);props.set("readX",int((c->read.getX()-b.getX())*sx));props.set("readY",int((c->read.getY()-b.getY())*sy));props.set("readW",int(c->read.getWidth()*sx));props.set("readH",int(c->read.getHeight()*sy));c->slider.setTextBoxStyle(juce::Slider::TextBoxBelow,false,int(c->read.getWidth()*sx),int(c->read.getHeight()*sy));c->slider.resized();}else if(c->kind==2)c->combo.setBounds(scale(c->body));else c->button.setBounds(scale(c->body));}
    }
    void refreshParameterActivity()override{
        display=processor.modulationDisplay();
        for(auto&c:controls){const bool enabled=processor.parameterActive(c->id);if(c->kind==1)c->slider.setEnabled(enabled);else if(c->kind==2)c->combo.setEnabled(enabled);else c->button.setEnabled(enabled);if(c->choice>=0)c->button.setToggleState(int(std::lround(processor.state.getRawParameterValue(c->id)->load()))==c->choice,juce::dontSendNotification);if(c->id=="rate"||c->id=="speed"){
                const auto nativeBpm=processor.nativeTempoBpm();
                const bool synced=nativeBpm>=0?(nativeBpm>0&&processor.state.getRawParameterValue("sync")->load()>.5f):display.syncActive;
                c->slider.textFromValueFunction=[](double value){return juce::String(value,2).trimCharactersAtEnd("0").trimCharactersAtEnd(".");};
                c->slider.setTextValueSuffix(synced?" c/b":" Hz");
                c->slider.updateText();
                c->slider.setTooltip(synced?"Cycles per quarter-note beat (c/b); actual host BPM":"Free-running cycles per second (Hz)");
            }}
        repaint();
    }
    void paint(juce::Graphics&g)override{
        g.fillAll(juce::Colours::black);if(art.isValid())g.drawImage(art,getLocalBounds().toFloat(),juce::RectanglePlacement::stretchToFit);const juce::Graphics::ScopedSaveState save(g);g.addTransform(juce::AffineTransform::scale(getWidth()/1672.f,getHeight()/941.f));paintMeters(g);paintMotion(g);
        const bool requested=processor.state.getRawParameterValue("sync")->load()>.5f;
        // Native project tempo is configuration, independent of live audio.
        // Do not invent a processed waveform or poll a host playhead from GUI.
        const auto nativeBpm=processor.nativeTempoBpm();
        const bool tempoAvailable=nativeBpm>=0?nativeBpm>0:display.tempoAvailable;
        const auto bpm=nativeBpm>=0?nativeBpm:double(display.bpm);
        text(g,number==23?juce::Rectangle<float>(1269,560,153,43):juce::Rectangle<float>(1210,528,161,38),tempoAvailable?juce::String(bpm,1)+" BPM":requested?"NO HOST BPM":"-- BPM",18);
    }
private:
    struct Control{juce::String id;int kind=0,choice=-1;juce::Rectangle<float>body,read;ui::PhysicalValueSlider slider;juce::ToggleButton button;juce::ComboBox combo;std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>sa;std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>ba;std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment>ca;};
    void button(const char*id,const char*title,juce::Rectangle<float>b,std::function<void()>action){auto c=std::make_unique<Control>();c->id=id;c->body=b;c->button.setComponentID(id);c->button.setName(title);c->button.setButtonText(title);c->button.setClickingTogglesState(false);c->button.onClick=std::move(action);look.registerButton(c->button,art,b);addAndMakeVisible(c->button);controls.push_back(std::move(c));}
    void toggle(const char*id,const char*title,juce::Rectangle<float>b){button(id,title,b,{});auto&c=*controls.back();if(number==23){look.registerButton(c.button,art,b,true);c.button.getProperties().set("indicatorOnly",true);c.button.setTooltip(title);}c.button.setClickingTogglesState(true);c.ba=std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(processor.state,id,c.button);}
    void choice(const char*id,int value,const char*title,juce::Rectangle<float>b){button(id,title,b,[this,id,value]{if(auto*p=processor.state.getParameter(id)){p->beginChangeGesture();p->setValueNotifyingHost(p->convertTo0to1(float(value)));p->endChangeGesture();}refreshParameterActivity();});auto&c=*controls.back();c.choice=value;c.button.setComponentID(value==0?juce::String(id):juce::String(id)+"_"+juce::String(value));c.button.getProperties().set("modulationChoiceParameter",id);c.button.getProperties().set("modulationChoiceValue",value);for(const auto&s:processor.parameterSpecs())if(c.id==s.id)c.button.setName(s.name);}
    void combo(const char*id,juce::StringArray choices,juce::Rectangle<float>b){auto c=std::make_unique<Control>();c->id=id;c->kind=2;c->body=b;c->combo.setComponentID(id);c->combo.setLookAndFeel(&comboLook);for(const auto&s:processor.parameterSpecs())if(c->id==s.id)c->combo.setName(s.name);c->combo.addItemList(choices,1);c->combo.setJustificationType(juce::Justification::centred);addAndMakeVisible(c->combo);c->ca=std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(processor.state,id,c->combo);controls.push_back(std::move(c));}
    void dial(const char*id,float x,float y,float radius,juce::Rectangle<float>read){auto c=std::make_unique<Control>();c->id=id;c->kind=1;c->body={x-radius,y-radius,2*radius,2*radius};c->read=read;c->slider.setComponentID(id);c->slider.getProperties().set("preserveAuthoredDialRing",true);c->slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);c->slider.setRotaryParameters(juce::MathConstants<float>::pi*1.25f,juce::MathConstants<float>::pi*2.75f,true);addAndMakeVisible(c->slider);look.registerDial(id,juce::ImageCache::getFromMemory(OriginalArt::native_transient_png,OriginalArt::native_transient_pngSize),{268,619,134,134});for(const auto&s:processor.parameterSpecs())if(c->id==s.id){c->slider.setName(s.name);c->slider.setTextValueSuffix(juce::String(" ")+s.unit);c->slider.setDoubleClickReturnValue(true,s.initial);c->slider.setTooltip(juce::String(s.name)+": "+juce::String(s.minimum)+" to "+juce::String(s.maximum)+" "+s.unit);}c->sa=std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(processor.state,id,c->slider);for(const auto&s:processor.parameterSpecs())if(c->id==s.id)c->slider.setDoubleClickReturnValue(true,s.initial);controls.push_back(std::move(c));}
    static void blank(juce::Graphics&g,juce::Rectangle<float>b){g.setColour(juce::Colour(0xff030a10));g.fillRect(b);}
    static void text(juce::Graphics&g,juce::Rectangle<float>b,const juce::String&t,float size){blank(g,b);g.setColour(juce::Colours::cyan);g.setFont(FoundationLook::faceFont(size));g.drawText(t,b,juce::Justification::centred);}
    void paintMeters(juce::Graphics&g){PluginTelemetry::Levels levels;const bool valid=processor.processingReady()&&processor.meterSnapshot().hasProcessedAudio&&processor.pluginTelemetry()&&processor.pluginTelemetry()->read(levels)&&levels.valid;for(int side=0;side<2;++side){const auto b=number==23?juce::Rectangle<float>(side?1467:90,225,120,251):juce::Rectangle<float>(side?1502:88,207,84,274);blank(g,b);for(int ch=0;ch<2;++ch){const float peak=valid?levels.peak[size_t(side*2+ch)]:0,db=peak>1.e-6f?20*std::log10(peak):-120;const float width=number==23?24.f:21.f;ui::paintSegmentedMeter(g,{b.getX()+7+ch*(b.getWidth()-width-14),b.getY()+3,width,b.getHeight()-6},std::clamp((db+60)/60,0.f,1.f),valid);}}}
    void paintMotion(juce::Graphics&g){const auto plot=number==23?juce::Rectangle<float>(327,216,1050,219):juce::Rectangle<float>(298,226,1073,204);blank(g,plot);g.setColour(juce::Colour(0xff17323c));for(int i=0;i<=20;++i){const float x=plot.getX()+i*plot.getWidth()/20;g.drawVerticalLine(int(x),plot.getY(),plot.getBottom());}for(int i=0;i<=4;++i){const float y=plot.getY()+i*plot.getHeight()/4;g.drawHorizontalLine(int(y),plot.getX(),plot.getRight());}
        if(display.valid&&display.count>1)for(int ch=0;ch<2;++ch){juce::Path path;for(size_t j=0;j<display.count;++j){const auto i=(display.cursor+modulation::Display::capacity-display.count+j)%modulation::Display::capacity;const float value=ch?display.right[i]:display.left[i],x=plot.getRight()-float(display.count-1-j)*plot.getWidth()/float(modulation::Display::capacity-1),y=plot.getCentreY()-std::clamp(value,-1.f,1.f)*plot.getHeight()*.45f;if(j)path.lineTo(x,y);else path.startNewSubPath(x,y);}g.setColour(ch?juce::Colours::orange:juce::Colours::cyan);g.strokePath(path,juce::PathStrokeType(1.7f));}
        const juce::String status=!display.valid?"WAITING FOR AUDIO":juce::String("L cyan / R orange | last 4 s | ")+juce::String(display.rateHz,2)+" Hz"+(number==24&&processor.state.getRawParameterValue("mode")->load()<.5f?" horn":"");text(g,number==23?juce::Rectangle<float>(390,446,950,24):juce::Rectangle<float>(398,436,900,22),status,16);
    }
    ModulationProcessor&processor;int number;juce::Image art;FoundationLook look;HardwareLookAndFeel comboLook;std::vector<std::unique_ptr<Control>>controls;modulation::Display display;
};
}
