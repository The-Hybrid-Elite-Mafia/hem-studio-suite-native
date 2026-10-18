#pragma once
#include "../HardwareLookAndFeel.h"
#include <ControlArt.h>
namespace hem::ui {
// Only constructed on the message thread. Immutable cached resources are shared;
// no audio callback decodes images or touches these objects.
class PhotorealisticLookAndFeel final:public HardwareLookAndFeel {
public:
 PhotorealisticLookAndFeel():body(juce::ImageCache::getFromMemory(ControlArt::dial_body_png,ControlArt::dial_body_pngSize)),buttons(juce::ImageCache::getFromMemory(ControlArt::button_states_png,ControlArt::button_states_pngSize)){}
 bool assetsReady() const noexcept{return body.isValid()&&body.hasAlphaChannel()&&buttons.isValid()&&buttons.hasAlphaChannel();}
 void drawRotarySlider(juce::Graphics& g,int x,int y,int w,int h,float position,float start,float end,juce::Slider& slider) override {
  if(w<=0||h<=0)return;
  const juce::Graphics::ScopedSaveState state(g);g.reduceClipRegion(x,y,w,h);
  const float size=float(std::min(w,h)),cx=float(x)+float(w)*.5f,cy=float(y)+float(h)*.5f;
  const float opacity=slider.isEnabled()?1.f:.38f;
  if(!body.isValid()){g.setColour(juce::Colours::orange);g.drawFittedText("DIAL ASSET MISSING",x,y,w,h,juce::Justification::centred,2);return;}
  g.setOpacity(opacity);g.setImageResamplingQuality(juce::Graphics::highResamplingQuality);
  g.drawImage(body,{cx-size*.49f,cy-size*.49f,size*.98f,size*.98f},juce::RectanglePlacement::stretchToFit);
  const float p=std::isfinite(position)?std::clamp(position,0.f,1.f):0.f,angle=start+p*(end-start);
  const auto at=[&](float r){return juce::Point<float>(cx+std::sin(angle)*r,cy-std::cos(angle)*r);};
  const juce::Line<float> mark(at(size*.095f),at(size*.205f));
  const float scale=size/120.f;
  const auto accent=(slider.isMouseOverOrDragging()?juce::Colour(0xffffb248):juce::Colour(0xff8af4ff)).withAlpha(opacity);
  g.setColour(accent.withMultipliedAlpha(.15f));g.drawLine(mark,5.f*scale);
  g.setColour(accent);g.drawLine(mark,2.f*scale);
  g.setColour(juce::Colours::white.withAlpha(opacity*.85f));g.drawLine(mark,.7f*scale);
  if(slider.hasKeyboardFocus(true)){g.setColour(accent);g.drawEllipse(cx-size*.41f,cy-size*.41f,size*.82f,size*.82f,1);}
 }
 void drawButtonBackground(juce::Graphics& g,juce::Button& button,const juce::Colour&,bool over,bool down) override {
  const juce::Graphics::ScopedSaveState state(g);paintButton(g,button.getLocalBounds().toFloat().reduced(1),button.getToggleState()||down,button.isEnabled());
  if(over||button.hasKeyboardFocus(true)){g.setColour(juce::Colour(0xff71eaff));g.drawRoundedRectangle(button.getLocalBounds().toFloat().reduced(3),3,1);}
 }
 void drawToggleButton(juce::Graphics& g,juce::ToggleButton& button,bool over,bool down) override {
  const juce::Graphics::ScopedSaveState state(g);const auto bounds=button.getLocalBounds().toFloat().reduced(2);
  paintButton(g,bounds,button.getToggleState()||down,button.isEnabled());
  g.setColour((button.getToggleState()?juce::Colour(0xffffdeb1):juce::Colour(0xffb4f4ff)).withAlpha(button.isEnabled()?1.f:.4f));
  g.setFont(juce::Font(juce::FontOptions(std::clamp(bounds.getHeight()*.37f,10.f,15.f)).withStyle("Bold")));
  g.drawFittedText(button.getButtonText(),bounds.reduced(8,3).toNearestInt(),juce::Justification::centred,2);
  if(over||button.hasKeyboardFocus(true)){g.setColour(juce::Colour(0xff71eaff));g.drawRoundedRectangle(bounds.reduced(1),3,1);}
 }
private:
 void paintButton(juce::Graphics& g,juce::Rectangle<float> b,bool on,bool enabled) const {
  if(!buttons.isValid())return;
  // Tight authored body rectangles deliberately exclude the active sprite's
  // irregular outer halo. Same destination/body bounds in both states.
  const auto source=on?juce::Rectangle<int>(115,780,1023,170):juce::Rectangle<int>(115,283,1023,183);
  g.setOpacity(enabled?1.f:.4f);g.setImageResamplingQuality(juce::Graphics::highResamplingQuality);
  g.drawImage(buttons.getClippedImage(source),b,juce::RectanglePlacement::stretchToFit);
 }
 const juce::Image body,buttons;
};
}
