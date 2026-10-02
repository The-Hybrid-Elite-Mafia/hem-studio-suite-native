#pragma once
#include <JuceHeader.h>

namespace hem::ui {
// Approved artwork is preserved byte-for-byte. Only the native viewport crops
// its ghost badge; no additional controls or meter pixels enter the host.
struct StudioBranding {
    static juce::Rectangle<int> ghostSource() { return {10,702,244,196}; }
    static juce::Rectangle<float> mapped(juce::Rectangle<float> root,float x,float y,float w,float h) {
        return {root.getX()+x*root.getWidth()/1536,root.getY()+y*root.getHeight()/864,w*root.getWidth()/1536,h*root.getHeight()/864};
    }
    static void illuminatedText(juce::Graphics& g,const juce::String& text,juce::Rectangle<float> bounds,float height,juce::Colour accent) {
        const juce::Graphics::ScopedSaveState save(g);
        g.setFont(juce::Font(juce::FontOptions(height).withStyle("Bold")));
        const auto r=bounds.toNearestInt();
        g.setColour(accent.withAlpha(0.18f));
        for(const auto offset:std::array<juce::Point<int>,4>{{{-2,0},{2,0},{0,-2},{0,2}}})
            g.drawFittedText(text,r.translated(offset.x,offset.y),juce::Justification::centred,1,0.7f);
        g.setColour(accent.brighter(0.45f));g.drawFittedText(text,r,juce::Justification::centred,1,0.7f);
    }
    static void draw(juce::Graphics& g,juce::Rectangle<float> root,const juce::Image& ghost) {
        const float scale=root.getWidth()/1536;
        // Keep the original illuminated crown and perimeter metalwork visible.
        const auto title=mapped(root,605,13,415,42);
        g.setColour(juce::Colour(0xff02090d));g.fillRoundedRectangle(mapped(root,598,9,431,66),4*scale);
        illuminatedText(g,"HYBRID ELITE MAFIA",title,34*scale,juce::Colour(0xffffa735));
        illuminatedText(g,"S T U D I O   S U I T E",mapped(root,620,54,385,18),15*scale,juce::Colour(0xff00edff));
        const auto badge=mapped(root,1322,110,184,158);
        g.setColour(juce::Colour(0xff02080d));g.fillRoundedRectangle(badge,5*scale);
        if(ghost.isValid()) {
            const auto source=ghostSource();const auto destination=badge.reduced(4*scale).toNearestInt();
            const juce::Graphics::ScopedSaveState save(g);
            g.setImageResamplingQuality(juce::Graphics::highResamplingQuality);
            g.drawImage(ghost,destination.getX(),destination.getY(),destination.getWidth(),destination.getHeight(),source.getX(),source.getY(),source.getWidth(),source.getHeight());
        }
        g.setColour(juce::Colour(0x4500edff));g.drawRoundedRectangle(badge,5*scale,5*scale);
        g.setColour(juce::Colour(0xff00edff));g.drawRoundedRectangle(badge,5*scale,1.2f*scale);
    }
};
}
