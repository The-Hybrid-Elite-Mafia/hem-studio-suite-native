#pragma once
#include <ControlArt.h>
namespace hem::ui {
// New integration repair, not recovered inner-body pixel identity. Keep the
// approved chassis/rings/ticks immutable; replace only the inner dial disk with
// the same stationary material used by the suite's tested dial renderer. This
// removes both white and glowing extraction-pose pointers without inpaint seams.
inline juce::Image stationaryReferenceDial(const juce::Image& raw){
    auto result=raw.createCopy();const int w=raw.getWidth(),h=raw.getHeight();if(w<8||h<8)return result;
    const float radius=std::min(w,h)*.43f;const juce::Rectangle<float> body(w*.5f-radius,h*.5f-radius,2*radius,2*radius);
    juce::Graphics g(result);juce::Path clip;clip.addEllipse(body);g.reduceClipRegion(clip);
    g.setColour(juce::Colour(0xff080d12));g.fillEllipse(body);
    const auto material=juce::ImageCache::getFromMemory(ControlArt::dial_body_png,ControlArt::dial_body_pngSize);
    if(material.isValid()){g.setOpacity(1.f);g.setImageResamplingQuality(juce::Graphics::highResamplingQuality);g.drawImage(material.getClippedImage({382,375,480,480}),body,juce::RectanglePlacement::stretchToFit);}
    return result;
}
}
