#pragma once
#include "../UI/PhysicalValueSlider.h"
#include "../HardwareLookAndFeel.h"
#include "ReferencePointerCleanup.h"
#include "OriginalBandTypography.h"
namespace hem::ui {
// Code-rendered functional layer over immutable owner-provided raster chrome.
// The copied body stays stationary; its extraction-pose pointer is removed once
// on the message thread, and one live pointer is drawn from the physical value.
class ReferenceSlider final:public PhysicalValueSlider {
public:
    juce::Image dial;juce::Rectangle<int> dialBounds,textBounds;juce::StringArray choices;
    std::function<void(int)> onChoice;
    void setDial(const juce::Image& reference,juce::Rectangle<int> source){
        dial=stationaryReferenceDial(reference.getClippedImage(source));
    }
    juce::String getTextFromValue(double value) override {
        if(!choices.isEmpty())return choices[juce::jlimit(0,choices.size()-1,int(std::round(value)))];
        return PhysicalValueSlider::getTextFromValue(value);
    }
    double getValueFromText(const juce::String& text) override {
        for(int i=0;i<choices.size();++i)if(text.trim().equalsIgnoreCase(choices[i]))return i;
        return PhysicalValueSlider::getValueFromText(text);
    }
    void mouseDown(const juce::MouseEvent& e) override {choiceMouseWasLeft=e.mods.isLeftButtonDown();PhysicalValueSlider::mouseDown(e);}
    void mouseUp(const juce::MouseEvent& e) override {
        PhysicalValueSlider::mouseUp(e);
        if(!choices.isEmpty()&&!e.mouseWasDraggedSinceMouseDown()&&choiceMouseWasLeft)showChoices();choiceMouseWasLeft=false;
    }
    void showChoices(){if(choices.isEmpty()||!isEnabled())return;juce::PopupMenu menu;
        for(int i=0;i<choices.size();++i)menu.addItem(i+1,choices[i],true,int(std::round(getValue()))==i);
        juce::Component::SafePointer<ReferenceSlider> safe(this);
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this),[safe](int result){if(safe&&result>0&&safe->onChoice)safe->onChoice(result-1);});
    }
private:
    bool choiceMouseWasLeft=false;
};
class OriginalBandLookAndFeel final:public HardwareLookAndFeel {
public:
    OriginalBandLookAndFeel(){setColour(juce::Slider::textBoxBackgroundColourId,juce::Colour(0xff02080c));
        setColour(juce::Slider::textBoxOutlineColourId,juce::Colour(0xff51adbb));setColour(juce::Slider::textBoxTextColourId,juce::Colour(0xffedf6f8));}
    juce::Slider::SliderLayout getSliderLayout(juce::Slider& slider) override {
        if(auto* original=dynamic_cast<ReferenceSlider*>(&slider)){juce::Slider::SliderLayout layout;
            layout.sliderBounds=original->dialBounds;layout.textBoxBounds=original->textBounds;return layout;}
        return HardwareLookAndFeel::getSliderLayout(slider);
    }
    juce::Label* createSliderTextBox(juce::Slider& slider) override {
        auto* label=HardwareLookAndFeel::createSliderTextBox(slider);label->setBorderSize(juce::BorderSize<int>(0));return label;
    }
    juce::Font getLabelFont(juce::Label& label) override {
        return bandFont(std::min(17.f,label.getHeight()*.72f));
    }
    void drawLabel(juce::Graphics& g,juce::Label& label) override {
        if(dynamic_cast<ReferenceSlider*>(label.getParentComponent())){
            // Opaque value apertures must cover the reference's extraction text.
            g.fillAll(juce::Colour(0xff02080c));g.setColour(juce::Colour(0xff51adbb));g.drawRect(label.getLocalBounds(),1);
            if(!label.isBeingEdited()){g.setColour(juce::Colours::whitesmoke.withAlpha(label.isEnabled()?1.f:.45f));
                auto* slider=static_cast<ReferenceSlider*>(label.getParentComponent());const auto area=label.getLocalBounds().reduced(1).toFloat();
                const auto text=bandValueText(*slider,label.getText(),area.getWidth(),bandFont(17));
                bandText(g,text,area,1,juce::Justification::centred,std::min(17.f,label.getHeight()*.72f));}
            return;
        }
        HardwareLookAndFeel::drawLabel(g,label);
    }
    void drawRotarySlider(juce::Graphics& g,int x,int y,int w,int h,float position,float start,float end,juce::Slider& slider) override {
        auto* original=dynamic_cast<ReferenceSlider*>(&slider);if(!original){HardwareLookAndFeel::drawRotarySlider(g,x,y,w,h,position,start,end,slider);return;}
        const auto bounds=juce::Rectangle<float>(float(x),float(y),float(w),float(h));
        if(!original->choices.isEmpty()){buttonSurface(g,bounds,slider.isMouseOverOrDragging(),slider.isEnabled());
            g.setColour(juce::Colours::whitesmoke.withAlpha(slider.isEnabled()?1.f:.45f));
            auto caption=original->getTextFromValue(slider.getValue());if(bounds.getWidth()<70){if(caption=="LOW SHELF")caption="LSH";if(caption=="HIGH SHELF")caption="HSH";}
            bandText(g,caption,bounds.reduced(3,1).withTrimmedRight(h*.25f),1,juce::Justification::centred,16,true);
            juce::Path arrow;const float cx=bounds.getRight()-h*.3f,cy=bounds.getCentreY();arrow.addTriangle(cx-3,cy-2,cx+3,cy-2,cx,cy+2);g.fillPath(arrow);return;}
        if(original->dial.isValid()){g.setImageResamplingQuality(juce::Graphics::highResamplingQuality);g.drawImage(original->dial,bounds,juce::RectanglePlacement::stretchToFit);}
        else{g.setColour(juce::Colour(0xff11191e));g.fillEllipse(bounds);g.setColour(juce::Colour(0xff75bdc9));g.drawEllipse(bounds.reduced(1),1);}
        const float size=std::min(bounds.getWidth(),bounds.getHeight()),cx=bounds.getCentreX(),cy=bounds.getCentreY();
        const float angle=start+std::clamp(position,0.f,1.f)*(end-start);
        const auto at=[&](float radius){return juce::Point<float>(cx+std::sin(angle)*radius,cy-std::cos(angle)*radius);};
        g.setColour(juce::Colour(0xffeaf1f5));g.drawLine({at(size*.12f),at(size*.39f)},std::max(1.25f,size*.041f));
        if(!slider.isEnabled()){g.setColour(juce::Colour(0xaa030609));g.fillRect(bounds);}
        if(slider.hasKeyboardFocus(true)){g.setColour(juce::Colour(0xff4beaff));g.drawEllipse(bounds.reduced(1),1.2f);}
    }
    void drawToggleButton(juce::Graphics& g,juce::ToggleButton& button,bool over,bool down) override {
        const bool inverse=bool(button.getProperties()["inverseFaceplate"]),mode=bool(button.getProperties()["directionFaceplate"]);
        const bool active=inverse?!button.getToggleState():button.getToggleState();const auto bounds=button.getLocalBounds().toFloat();
        buttonSurface(g,bounds,active||down,button.isEnabled());
        const auto text=mode?(button.getToggleState()?"UP":"DOWN"):button.getButtonText();
        g.setColour(juce::Colours::whitesmoke.withAlpha(button.isEnabled()?1.f:.45f));
        bandText(g,text,bounds.reduced(5,1),1,juce::Justification::centred,16,true);
        if(over&&!active){g.setColour(juce::Colours::white.withAlpha(.07f));g.fillRoundedRectangle(bounds.reduced(1),2);}
    }
    void drawButtonBackground(juce::Graphics& g,juce::Button& button,const juce::Colour&,bool over,bool down) override {
        buttonSurface(g,button.getLocalBounds().toFloat(),button.getToggleState()||down||over,button.isEnabled());
    }
    juce::Font getTextButtonFont(juce::TextButton&,int height) override {return bandFont(std::min(18.f,height*.55f),true);}
    void drawButtonText(juce::Graphics& g,juce::TextButton& button,bool,bool) override {
        g.setColour(button.findColour(button.getToggleState()?juce::TextButton::textColourOnId:juce::TextButton::textColourOffId).withMultipliedAlpha(button.isEnabled()?1.f:.45f));
        bandText(g,button.getButtonText(),button.getLocalBounds().toFloat().reduced(5,1),1,juce::Justification::centred,18,true);
    }
private:
    static void buttonSurface(juce::Graphics& g,juce::Rectangle<float> bounds,bool active,bool enabled){
        g.setColour(juce::Colour(0xff010609));g.fillRect(bounds);
        g.setGradientFill(juce::ColourGradient(juce::Colour(0xff263039),bounds.getTopLeft(),juce::Colour(0xff020609),bounds.getBottomLeft(),false));
        g.fillRoundedRectangle(bounds.reduced(1),2);g.setColour(juce::Colour(active&&enabled?0xffff9e34:0xff70848c));g.drawRoundedRectangle(bounds.reduced(1.2f),2,1.1f);
        if(active&&enabled){g.setColour(juce::Colour(0xffffab46));g.fillRoundedRectangle(bounds.getX()+4,bounds.getY()+4,3,std::max(2.f,bounds.getHeight()-8),1);}
    }
};
}
