#pragma once
#include <JuceHeader.h>
namespace hem::ui {
// Draw approved fixed branding from original source pixels, not regenerated text.
inline void preserveModuleBranding(juce::Graphics& g,const juce::Image& original,juce::Rectangle<float> destination){
 if(!original.isValid())return;
 const juce::Graphics::ScopedSaveState save(g);
 juce::Path mask;mask.addRectangle(0.f,0.f,1100.f,126.f);
 mask.addRectangle(0.f,700.f,244.f,205.f);
 mask.addRectangle(1517.f,700.f,155.f,205.f);
 mask.addRectangle(0.f,908.f,1672.f,33.f);
 const auto transform=juce::AffineTransform::scale(destination.getWidth()/1672.f,destination.getHeight()/941.f).translated(destination.getX(),destination.getY());
 g.reduceClipRegion(mask,transform);g.setOpacity(1);g.drawImage(original,destination,juce::RectanglePlacement::stretchToFit);
}
// Feather mask built once, off the audio thread. Core removal remains fully
// opaque; only the surrounding 5 pixels blend to prevent hard rectangular seams.
inline juce::Image makeHostCleanupMask(){
 juce::Image mask(juce::Image::ARGB,1536,864,true);juce::Graphics g(mask);
 juce::Path path;path.addEllipse(328.f,686.f,118.f,123.f);path.addEllipse(1091.f,686.f,120.f,123.f);
 path.addRoundedRectangle(475.f,715.f,570.f,71.f,3.f);path.addRoundedRectangle(51.f,25.f,240.f,39.f,3.f);path.addRoundedRectangle(1323.f,24.f,189.f,49.f,3.f);
 for(int i=10;i>=1;--i){g.setColour(juce::Colours::white.withAlpha(float(11-i)/110.f));g.strokePath(path,juce::PathStrokeType(float(i)));}
 g.setColour(juce::Colours::white);g.fillPath(path);return mask;
}
inline void paintCleanHostSockets(juce::Graphics& g,const juce::Image& cleaned,const juce::Image& mask,juce::Rectangle<float> face){
 if(!cleaned.isValid()||!mask.isValid())return;
 const juce::Graphics::ScopedSaveState state(g);
 const auto transform=juce::AffineTransform::scale(face.getWidth()/1536.f,face.getHeight()/864.f).translated(face.getX(),face.getY());
 g.reduceClipRegion(mask,transform);g.setOpacity(1);g.setImageResamplingQuality(juce::Graphics::highResamplingQuality);g.drawImage(cleaned,face,juce::RectanglePlacement::stretchToFit);
}
}
