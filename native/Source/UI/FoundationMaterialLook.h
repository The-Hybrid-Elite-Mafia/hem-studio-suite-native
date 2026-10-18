#pragma once
#include <map>
#include <ControlArt.h>
#include "HardwareDial.h"
#include "HardwarePanel.h"

namespace hem {
// Message-thread hardware painting. Material lighting stays fixed; only live values move.
class SuiteMaterialLook final : public juce::LookAndFeel_V4 {
public:
    static juce::Font faceFont(float height) { return juce::Font(juce::FontOptions("Arial Narrow",height,juce::Font::plain)); }
    SuiteMaterialLook(){
        setColour(juce::Slider::textBoxTextColourId,juce::Colour(0xffffcd83));
        setColour(juce::Slider::textBoxBackgroundColourId,juce::Colours::black);
        setColour(juce::Slider::textBoxOutlineColourId,juce::Colours::transparentBlack);
        setColour(juce::Slider::textBoxHighlightColourId,juce::Colour(0xff075c69));
    }
    // Compatibility registration API: the shared renderer uses live geometry,
    // never cropped baked dial faces or pointers.
    void registerDial(const juce::String&,const juce::Image&,juce::Rectangle<float>) {}
    void registerButton(juce::Button&,const juce::Image&,juce::Rectangle<float>,bool=true) {}
    juce::Slider::SliderLayout getSliderLayout(juce::Slider& s) override {
        if(!s.getProperties().contains("bodyRadius"))return LookAndFeel_V4::getSliderLayout(s);
        juce::Slider::SliderLayout result;result.sliderBounds=s.getLocalBounds();
        result.textBoxBounds={int(s.getProperties()["readX"]),int(s.getProperties()["readY"]),int(s.getProperties()["readW"]),int(s.getProperties()["readH"])};return result;
    }
    juce::Label* createSliderTextBox(juce::Slider& s) override {
        auto* label=LookAndFeel_V4::createSliderTextBox(s);label->setFont(faceFont(26));label->setJustificationType(juce::Justification::centred);return label;
    }
    juce::Font getLabelFont(juce::Label& label) override {
        if(auto* slider=dynamic_cast<juce::Slider*>(label.getParentComponent()))return faceFont(slider->getProperties().contains("readH")?std::clamp(float(int(slider->getProperties()["readH"]))*0.86f,10.f,32.f):16.f);
        return label.getFont();
    }
    void drawRotarySlider(juce::Graphics& g,int x,int y,int w,int h,float pos,float start,float end,juce::Slider& s) override {
        const juce::Graphics::ScopedSaveState save(g);
        g.reduceClipRegion(s.getLocalBounds());
        const float r=s.getProperties().contains("bodyRadius")?float(s.getProperties()["bodyRadius"]):std::min(w,h)*.42f;
        const float cx=s.getProperties().contains("bodyX")?float(s.getProperties()["bodyX"]):x+w*.5f;
        const float cy=s.getProperties().contains("bodyY")?float(s.getProperties()["bodyY"]):y+h*.5f;
        const float neutral=s.getMinimum()<0&&s.getMaximum()>0?float(s.valueToProportionOfLength(0)):0.f;
        const auto unit=s.getTextValueSuffix().trim().toLowerCase();
        const auto id=s.getComponentID().toLowerCase();
        const auto name=s.getName().toLowerCase();
        const bool frequencyTimeQ=unit.contains("hz")||unit=="ms"||unit=="s"||id=="q"||id.endsWith("q")||name.contains("frequency")||name.contains("cutoff");
        juce::Colour accent(frequencyTimeQ?0xff0de8ff:0xffffac35);
        if(s.getProperties().contains("dialAccent")){
            const auto overrideColour=s.getProperties()["dialAccent"].toString();
            accent=overrideColour=="cyan"?juce::Colour(0xff0de8ff):overrideColour=="amber"?juce::Colour(0xffffac35):juce::Colour::fromString(overrideColour);
        }
        if(bool(s.getProperties()["preserveAuthoredDialRing"])){
            // NEW recovered-integration renderer: only the inner body changes.
            // Lighting uses immutable legacy material; no authored ring/ticks rotate.
            juce::Path clip;clip.addEllipse(cx-r,cy-r,2*r,2*r);g.reduceClipRegion(clip);
            g.setColour(juce::Colour(0xff080d12));g.fillEllipse(cx-r,cy-r,2*r,2*r);
            const auto material=juce::ImageCache::getFromMemory(ControlArt::dial_body_png,ControlArt::dial_body_pngSize);
            if(material.isValid()){g.setOpacity(1.f);g.setImageResamplingQuality(juce::Graphics::highResamplingQuality);g.drawImage(material.getClippedImage({382,375,480,480}),{cx-r,cy-r,2*r,2*r},juce::RectanglePlacement::stretchToFit);}
            const float angle=start+std::clamp(pos,0.f,1.f)*(end-start);
            const auto point=[&](float radius){return juce::Point<float>(cx+std::sin(angle)*radius,cy-std::cos(angle)*radius);};
            g.setColour(juce::Colours::white.withAlpha(s.isEnabled()?1.f:.5f));g.drawLine({point(r*.30f),point(r*.78f)},std::max(1.4f,r*.045f));
            if(s.hasKeyboardFocus(true)){g.setColour(accent);g.drawEllipse(cx-r*.95f,cy-r*.95f,r*1.9f,r*1.9f,1);}
        }else ui::drawHardwareDial(g,cx,cy,r,pos,start,end,neutral,s.isEnabled(),s.hasKeyboardFocus(true),accent);
    }
    void drawLabel(juce::Graphics& g,juce::Label& label) override {
        if(dynamic_cast<juce::Slider*>(label.getParentComponent())){
            ui::hardwareGlass(g,label.getLocalBounds().toFloat(),false);
            if(!label.isBeingEdited()){g.setColour(juce::Colour(0xffffc374).withAlpha(label.isEnabled()?1.f:.4f));g.setFont(getLabelFont(label));g.drawFittedText(label.getText(),label.getLocalBounds().reduced(3),juce::Justification::centred,1);}
        }else LookAndFeel_V4::drawLabel(g,label);
    }
    void drawButtonBackground(juce::Graphics& g,juce::Button& b,const juce::Colour&,bool,bool down) override {paintButton(g,b,down);}
    void drawButtonText(juce::Graphics& g,juce::TextButton& b,bool,bool) override {paintLegend(g,b);}
    void drawToggleButton(juce::Graphics& g,juce::ToggleButton& b,bool,bool down) override {paintButton(g,b,down);paintLegend(g,b);}
private:
    void paintButton(juce::Graphics& g,juce::Button& b,bool down){
        auto box=b.getLocalBounds().toFloat().reduced(1);ui::hardwareGlass(g,box,false);
        const bool active=b.isEnabled()&&(b.getToggleState()||down);
        if(active){for(int i=4;i>=1;--i){g.setColour(juce::Colour(0xffff961a).withAlpha(.045f*(5-i)));g.strokePath(ui::chamfered(box.reduced(5)),juce::PathStrokeType(float(i*2)));}g.setColour(juce::Colour(0xffffad36));g.strokePath(ui::chamfered(box.reduced(5)),juce::PathStrokeType(1.5f));}
        if(!b.isEnabled()&&b.getToggleState()){g.setColour(juce::Colour(0xffffad36).withAlpha(.38f));g.strokePath(ui::chamfered(box.reduced(5)),juce::PathStrokeType(1.5f));}
        if(b.hasKeyboardFocus(true)){g.setColour(juce::Colour(0xff40f1ff));g.strokePath(ui::chamfered(box.reduced(2)),juce::PathStrokeType(1));}
    }
    void paintLegend(juce::Graphics& g,juce::Button& b){
        const bool on=b.getToggleState()||(b.isEnabled()&&b.isDown());
        if(bool(b.getProperties()["indicatorOnly"])){
            const float diameter=std::max(3.f,std::min(b.getWidth(),b.getHeight())*.27f);
            g.setColour((on?juce::Colour(0xffffdc9d):juce::Colour(0xffa9d2dc)).withAlpha(b.isEnabled()?1.f:.38f));
            g.fillEllipse(juce::Rectangle<float>(diameter,diameter).withCentre(b.getLocalBounds().toFloat().getCentre()));return;
        }
        auto box=b.getLocalBounds().toFloat().reduced(8,4);
        g.setColour((on?juce::Colour(0xffffdc9d):juce::Colour(0xffa9d2dc)).withAlpha(b.isEnabled()?1.f:.38f));g.setFont(faceFont(std::clamp(box.getHeight()*.53f,11.f,24.f)));
        g.drawFittedText(b.getButtonText(),box.toNearestInt(),juce::Justification::centred,2);
    }
};
using FoundationLook = SuiteMaterialLook;
}
