#pragma once
#include "../UI/PluginTelemetryView.h"
#include "Editors.h"
#include "PluginMeterReadout.h"
#include "../UI/FoundationMaterialLook.h"
#include <OriginalArt.h>

namespace hem {
// Original flat artwork + explicit live apertures. Material lighting never
// rotates. APVTS owns values/automation; UI mapping changes no parameter ranges.
// Retained unused Tape compatibility renderer. The live approved28 factory
// uses OriginalNonlinearEditor; approved01/12 use the new editor-local helper.
class NativeTapeCompatibilityEditor : public ModuleEditor {
public:
    NativeTapeCompatibilityEditor(ModuleProcessor& p,bool stereo,bool tape=false):ModuleEditor(p,false),processor(p),isStereo(stereo),isTape(tape) {
        art=stereo?juce::ImageCache::getFromMemory(OriginalArt::native_sipa_png,OriginalArt::native_sipa_pngSize)
                  :juce::ImageCache::getFromMemory(OriginalArt::native_transient_png,OriginalArt::native_transient_pngSize);
        if(tape)art=juce::ImageCache::getFromMemory(OriginalArt::native_tape_png,OriginalArt::native_tape_pngSize);
        if(stereo)removeStrayGlyphs();
        setLookAndFeel(&look);
        for(const auto& spec:p.parameterSpecs()){
            auto c=std::make_unique<Control>();c->id=spec.id;c->toggle=spec.toggle;
            if(c->toggle){c->button.setComponentID(spec.id);c->button.setName(spec.name);c->button.setButtonText(spec.name);c->button.setWantsKeyboardFocus(true);addAndMakeVisible(c->button);
                c->buttonAttachment=std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(p.state,spec.id,c->button);}
            else {auto& s=c->slider;s.setComponentID(spec.id);s.setName(spec.name);s.setWantsKeyboardFocus(true);
                s.setTooltip(juce::String(spec.name)+": "+juce::String(spec.minimum)+" to "+juce::String(spec.maximum)+" "+spec.unit+". Double-click restores the existing parameter default.");
                s.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);s.setTextBoxStyle(juce::Slider::TextBoxBelow,false,120,32);
                s.setTextValueSuffix(juce::String(" ")+spec.unit);s.setRotaryParameters(juce::MathConstants<float>::pi*1.25f,juce::MathConstants<float>::pi*2.75f,true);
                s.setDoubleClickReturnValue(true,spec.initial);s.setMouseDragSensitivity(250);addAndMakeVisible(s);
                c->attachment=std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(p.state,spec.id,s);}
            controls.push_back(std::move(c));
        }
        if(stereo){
            dial("width",431,610,68,{355,699,153,31});dial("bass",712,610,68,{635,699,151,31});
            dial("balance",991,610,68,{915,699,150,31});dial("output",1268,610,68,{1192,699,151,31});
            button("bassOn",{444,802,183,70});button("mono",{707,802,160,70});button("invertL",{940,802,161,70});
            button("invertR",{1173,802,162,70});button("swap",{1409,802,164,70});
        }else if(tape){
            dial("drive",429,691,72,{360,795,138,25});dial("warmth",691,691,72,{622,795,138,25});
            dial("rolloff",953,691,72,{884,795,138,25});dial("mix",1214,691,72,{1145,795,138,25});dial("output",1481,691,72,{1412,795,138,25});
        }else{
            dial("attack",335,686,67,{281,779,110,29});dial("sustain",568,686,67,{514,779,109,29});
            dial("speed",800,686,67,{744,779,111,29});dial("recovery",1034,686,67,{980,779,109,29});
            dial("mix",1275,686,67,{1220,779,109,29});dial("output",1516,686,67,{1461,779,110,29});
        }
        if(!stereo&&!tape){
            dial("attack",335,745,68,{269,839,132,32});dial("sustain",568,745,68,{502,839,132,32});
            dial("speed",800,745,68,{734,839,132,32});dial("recovery",1034,745,68,{968,839,132,32});
            dial("mix",1275,745,68,{1209,839,132,32});dial("output",1516,745,68,{1450,839,132,32});
        }
        if(stereo){
            dial("width",431,700,68,{355,790,153,31});dial("bass",712,700,68,{635,790,151,31});
            dial("balance",991,700,68,{915,790,150,31});dial("output",1268,700,68,{1192,790,151,31});
            button("bassOn",{270,828,237,38});button("mono",{533,828,237,38});button("invertL",{796,828,237,38});button("invertR",{1059,828,237,38});button("swap",{1322,828,237,38});
        }else if(tape){
            dial("drive",429,745,72,{360,839,138,29});dial("warmth",691,745,72,{622,839,138,29});dial("rolloff",953,745,72,{884,839,138,29});dial("mix",1214,745,72,{1145,839,138,29});dial("output",1481,745,72,{1412,839,138,29});
        }
        for(auto& c:controls)if(c->toggle)look.registerButton(c->button,art,c->body);else look.registerDial(c->id,art,c->body);
        setResizable(true,true);setResizeLimits(1000,563,1920,1080);if(auto* c=getConstrainer())c->setFixedAspectRatio(1672.0/941.0);

        setSize(1280,720);resized();
    }
    ~NativeTapeCompatibilityEditor() override {controls.clear();setLookAndFeel(nullptr);}
    void refreshParameterActivity() override {repaint();}
    void resized() override {
        const float sx=getWidth()/1672.f,sy=getHeight()/941.f;
        auto scaled=[&](juce::Rectangle<float> r){return juce::Rectangle<float>(r.getX()*sx,r.getY()*sy,r.getWidth()*sx,r.getHeight()*sy).toNearestInt();};
        for(auto& c:controls){if(c->toggle){c->button.setBounds(scaled(c->body));continue;}
            auto& s=c->slider;const auto b=c->body.expanded(26).getUnion(c->read);s.setBounds(scaled(b));auto& props=s.getProperties();
            props.set("bodyRadius",c->body.getWidth()*.62f*sx);props.set("bodyX",(c->body.getCentreX()-b.getX())*sx);props.set("bodyY",(c->body.getCentreY()-b.getY())*sy);
            props.set("readX",int((c->read.getX()-b.getX())*sx));props.set("readY",int((c->read.getY()-b.getY())*sy));props.set("readW",int(c->read.getWidth()*sx));props.set("readH",int(c->read.getHeight()*sy));
            s.setTextBoxStyle(juce::Slider::TextBoxBelow,false,int(c->read.getWidth()*sx),int(c->read.getHeight()*sy));s.resized();
        }
    }
    // Declared editable apertures used by pixel-mask verification. No artwork
    // outside these regions may be altered (except the JUCE resize corner).
    std::vector<juce::Rectangle<int>> liveApertures() const {
        std::vector<juce::Rectangle<int>> r;
        for(const auto& c:controls){r.push_back(c->body.expanded(c->toggle?0.f:4.f).getSmallestIntegerContainer());if(!c->toggle)r.push_back(c->read.getSmallestIntegerContainer());}
        if(isStereo){for(auto b:{juce::Rectangle<int>(340,262,436,193),{862,307,478,45},{1007,406,192,34},{98,249,116,416},{1458,249,116,416},
                {326,604,8,12},{529,604,8,12},{607,604,7,12},{808,604,8,12},{885,604,8,12},{1087,604,8,12},{1163,604,8,12},{1366,604,7,12}})r.push_back(b);}
        else {for(auto b:{juce::Rectangle<int>(288,224,1075,285),{359,458,1004,51},{96,209,119,231},{1465,209,119,231},{76,451,150,23},{1447,451,151,23},{83,482,136,31},{1454,482,135,31}})r.push_back(b);}
        return r;
    }

    void paint(juce::Graphics& g) override {
        g.fillAll(juce::Colours::black);g.setImageResamplingQuality(juce::Graphics::highResamplingQuality);g.drawImage(art,getLocalBounds().toFloat(),juce::RectanglePlacement::stretchToFit);
        const juce::Graphics::ScopedSaveState save(g);g.addTransform(juce::AffineTransform::scale(getWidth()/1672.f,getHeight()/941.f));
        // Clear the complete old interior panels, including headings and decorative meter remnants.
        g.setColour(juce::Colour(0xff02070b));g.fillRect(62,isTape?115:145,180,isTape?510:480);g.fillRect(1440,isTape?115:145,181,isTape?510:480);g.fillRect(242,isStereo||isTape?205:185,1208,isStereo||isTape?423:443);
        if(isStereo||isTape){g.setColour(juce::Colour(0xff02070b));g.fillRect(62,635,350,265);}
        ui::hardwareGlass(g,{237,550,1384,325},false);
        if(isStereo||isTape){
            g.setColour(juce::Colour(0xff02070b));g.fillRect(62,635,175,265);
            const auto reference=juce::ImageCache::getFromMemory(ControlArt::reviewed_hardware_reference_png,ControlArt::reviewed_hardware_reference_pngSize);
            g.drawImage(reference.getClippedImage({55,640,170,244}),{62,635,175,240},juce::RectanglePlacement::stretchToFit);
        }
        for(const auto& c:controls)if(!c->toggle){
            // Remove original illuminated rings and pointers rather than leaving ornamental value arcs.
            g.setColour(juce::Colour(0xff020609));g.fillEllipse(c->body.expanded(30));
            auto header=juce::Rectangle<float>(c->body.getCentreX()-110,c->body.getY()-48,220,28);
            ui::hardwareReadout(g,header,c->slider.getName(),22);
        }
        auto reading=ui::pluginMeterReadout(processor);telemetryView.poll(processor.pluginTelemetry());
        if(isTape){
            g.setColour(juce::Colour(0xff02070b));g.fillRect(495,874,700,36);ui::hardwareReadout(g,{505,879,680,27},"4x OVERSAMPLING",21);
            telemetryView.level(g,{66,165,154,442},false);telemetryView.level(g,{1450,165,154,442},true);
            telemetryView.waveform(g,{288,205,1075,341});
            ui::hardwareReadout(g,{288,554,1075,65},telemetryView.isLive()?reading.heading+" / "+reading.valueText:telemetryView.statusText(),22);
        }else if(isStereo){
            telemetryView.level(g,{66,165,154,442},false);telemetryView.level(g,{1450,165,154,442},true);
            telemetryView.waveform(g,{288,205,690,341},true);
            ui::hardwareGlass(g,{996,205,367,341});
            ui::hardwareReadout(g,{1011,230,337,50},"OUTPUT L/R CORRELATION",22);
            const float centre=1179;g.setColour(juce::Colour(0xff71838b));g.drawLine(1018,366,1340,366,2);
            if(reading.available&&telemetryView.isLive()&&reading.value!=0){const float x=centre+std::clamp(reading.value,-1.f,1.f)*161;g.setColour(juce::Colour(0xffffb248));g.fillRect(std::min(x,centre),354.f,std::abs(x-centre),24.f);}
            g.setColour(juce::Colour(0xff71838b));g.drawVerticalLine(int(centre),346,389);
            ui::hardwareReadout(g,{1011,405,337,45},reading.available&&telemetryView.isLive()?juce::String(reading.value,3):telemetryView.isLive()?"UNDEFINED":telemetryView.statusText(),25);
            ui::hardwareReadout(g,{1011,466,337,35},"-1     /     0     /     +1",20);
        }else{
            telemetryView.level(g,{66,165,154,442},false);telemetryView.level(g,{1450,165,154,442},true);
            telemetryView.gain(g,{288,554,526,65},0);telemetryView.gain(g,{836,554,526,65},1);
            telemetryView.waveform(g,{288,205,1075,341},false,true);
        }
    }
private:
    struct Control {juce::String id;bool toggle=false;juce::Rectangle<float> body,read;ui::PhysicalValueSlider slider;juce::ToggleButton button;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> buttonAttachment;};
    ui::PluginTelemetryView telemetryView;
    ModuleProcessor& processor;bool isStereo,isTape;juce::Image art;FoundationLook look;
    std::vector<std::unique_ptr<Control>> controls;
    static juce::Colour cyan(){return juce::Colour(0xff0be7f2);}
    void removeStrayGlyphs(){
        // Message-thread construction only. Repair the eight isolated glyphs in
        // a private image; never mutate the cached original. The neighbouring
        // radial ticks/ring lighting lie outside these narrow pixel bounds.
        art=art.createCopy();
        for(const auto r:{juce::Rectangle<int>(326,604,8,12),{529,604,8,12},{607,604,7,12},{808,604,8,12},
                         {885,604,8,12},{1087,604,8,12},{1163,604,8,12},{1366,604,7,12}}){
            for(int x=r.getX();x<r.getRight();++x){
                const auto above=art.getPixelAt(x,r.getY()-1),below=art.getPixelAt(x,r.getBottom());
                for(int y=r.getY();y<r.getBottom();++y)
                    art.setPixelAt(x,y,above.interpolatedWith(below,float(y-r.getY()+1)/float(r.getHeight()+1)));
            }
        }
    }
    static void blank(juce::Graphics& g,juce::Rectangle<float> r){g.setColour(juce::Colour(0xff020709));g.fillRect(r);}
    static void text(juce::Graphics& g,juce::Rectangle<float> r,const juce::String& s,float size){blank(g,r);g.setColour(juce::Colours::whitesmoke);g.setFont(FoundationLook::faceFont(size));g.drawText(s,r,juce::Justification::centred);}
    static juce::String signedDb(float value){return (value>0?juce::String("+"):juce::String())+juce::String(value,2);}
    void dial(const char* id,float x,float y,float r,juce::Rectangle<float> read){for(auto& c:controls)if(c->id==id){c->body={x-r,y-r,r*2,r*2};c->read=read;}}
    void button(const char* id,juce::Rectangle<float> body){for(auto& c:controls)if(c->id==id)c->body=body;}
    static void unavailable(juce::Graphics& g,juce::Rectangle<float> r,const juce::String& label="UNAVAILABLE"){
        blank(g,r);g.setColour(juce::Colour(0xffa9bac4));g.setFont(FoundationLook::faceFont(17));
        g.drawFittedText(label,r.reduced(5).toNearestInt(),juce::Justification::centred,3);
    }

};
class TapeOriginalEditor : public NativeTapeCompatibilityEditor {public:explicit TapeOriginalEditor(ModuleProcessor& p):NativeTapeCompatibilityEditor(p,false,true){}};
}
#include "NativePairApprovedEditor.h"
namespace hem {
using NativeOriginalEditor = NativePairApprovedEditor;
class TransientEditor final : public NativeOriginalEditor {public:explicit TransientEditor(ModuleProcessor& p):NativeOriginalEditor(p,false){}};
class SipaEditor final : public NativeOriginalEditor {public:explicit SipaEditor(SipaProcessor& p):NativeOriginalEditor(p,true){}};
}
