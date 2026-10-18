#pragma once
#include <JuceHeader.h>
namespace hem::ui {
// UI-only drawing in either component pixels or the 1672-wide art coordinate
// system. The minimum is in logical screen pixels, independent of render DPI.
inline constexpr float minimumBandTextPixels=14.f;
inline juce::Font bandFont(float pixels,bool bold=false){return juce::Font(juce::FontOptions("Arial Narrow",std::max(minimumBandTextPixels,pixels),bold?juce::Font::bold:juce::Font::plain));}
struct BandTextRun {juce::String text;juce::Rectangle<float> bounds;juce::Font font;float screenScale=1;};
// Optional message-thread observer used by the native raster fixture. No audio
// code includes this header, and no observer is installed by the application.
inline void (*bandTextObserver)(const BandTextRun&)=nullptr;
inline void bandText(juce::Graphics& g,const juce::String& text,juce::Rectangle<float> bounds,float scale,
                     juce::Justification alignment=juce::Justification::centred,float designHeight=17,bool bold=false){
    auto font=bandFont(designHeight*scale,bold);font.setHeight(font.getHeight()/scale);
    const float width=juce::GlyphArrangement::getStringWidth(font,text);
    if(width>bounds.getWidth())font.setHorizontalScale(std::max(.70f,bounds.getWidth()/width));
    if(bandTextObserver)bandTextObserver({text,bounds,font,scale});g.setFont(font);g.drawText(text,bounds,alignment,false);
}
inline juce::String bandValueText(juce::Slider& slider,const juce::String& full,float width,const juce::Font& font){
    auto fits=[&](const juce::String& value){return juce::GlyphArrangement::getStringWidth(font,value)<=width;};
    if(fits(full))return full;
    const auto unit=slider.getTextValueSuffix().trim();const double value=slider.getValue();juce::String compact;
    if(unit=="Hz"){
        compact=value>=1000?juce::String(value/1000.,3):juce::String(value,0);
        if(value>=1000){while(compact.endsWithChar('0'))compact=compact.dropLastCharacters(1);if(compact.endsWithChar('.'))compact=compact.dropLastCharacters(1);compact+="k";}
    }else{auto number=[](double v){auto out=juce::String(v,3);while(out.endsWithChar('0'))out=out.dropLastCharacters(1);if(out.endsWithChar('.'))out=out.dropLastCharacters(1);return out;};
        compact=unit=="ms"&&value>=1000?number(value/1000)+"s":number(value)+unit;
        if(!fits(compact))compact=number(value);}
    return compact.isEmpty()?full:compact;
}
inline void bandPeakMeter(juce::Graphics& g,juce::Rectangle<float> r,double peak,bool valid,float scale){
    g.setColour(juce::Colour(0xff010608));g.fillRect(r);
    if(valid&&peak>0){const float amount=float(std::clamp((20*std::log10(peak)+60)/66,0.,1.));g.setColour(juce::Colour(peak>1?0xffff6c31:0xff35dfea));g.fillRect(r.withTop(r.getBottom()-r.getHeight()*amount));}
    g.setColour(juce::Colours::white);const float row=std::max(25.f,16.f/scale);const auto inner=r.reduced(5,0);
    bandText(g,"+6",inner.withHeight(row),scale);
    bandText(g,!valid?"--":peak>0?juce::String(20*std::log10(peak),1):"-inf",{inner.getX(),r.getBottom()-2*row,inner.getWidth(),row},scale);
    bandText(g,"-60",{inner.getX(),r.getBottom()-row,inner.getWidth(),row},scale);
}
}
