#pragma once
namespace hem::ui {
inline juce::Rectangle<float> foundationPlotData(juce::Rectangle<float> outer){return outer.withTrimmedTop(26).withTrimmedBottom(26).reduced(2);}
inline float foundationPlotY(juce::Rectangle<float> data,float value,float low,float high){return data.getBottom()-std::clamp((value-low)/(high-low),0.f,1.f)*data.getHeight();}
inline void paintFoundationTrace(juce::Graphics& g,juce::Rectangle<float> data,const juce::Path& path){
 const juce::Graphics::ScopedSaveState state(g);g.reduceClipRegion(data.expanded(1).toNearestInt());
 g.setColour(juce::Colour(0xff11e9f7));g.strokePath(path,juce::PathStrokeType(1.6f));
}
inline juce::Rectangle<float> riderThumbBounds(float gain){
 const juce::Rectangle<float> aperture(777,510,122,272);constexpr float height=73;
 const float travel=aperture.getHeight()-height;
 const float y=aperture.getY()+(12-std::clamp(gain,-12.f,12.f))*travel/24;
 return {811,y,51,height};
}
}
