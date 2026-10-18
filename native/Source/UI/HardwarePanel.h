#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <algorithm>
namespace hem::ui {
inline juce::Font hardwareFont(float size,bool bold=false){return juce::Font(juce::FontOptions("Arial Narrow",size,bold?juce::Font::bold:juce::Font::plain));}
inline juce::Path chamfered(juce::Rectangle<float> b,float cut=7){
    const float c=std::min(cut,std::min(b.getWidth(),b.getHeight())*.22f);juce::Path p;
    p.startNewSubPath(b.getX()+c,b.getY());p.lineTo(b.getRight()-c,b.getY());p.lineTo(b.getRight(),b.getY()+c);p.lineTo(b.getRight(),b.getBottom()-c);
    p.lineTo(b.getRight()-c,b.getBottom());p.lineTo(b.getX()+c,b.getBottom());p.lineTo(b.getX(),b.getBottom()-c);p.lineTo(b.getX(),b.getY()+c);p.closeSubPath();return p;
}
inline void hardwareGlass(juce::Graphics& g,juce::Rectangle<float> b,bool screws=true){
    const juce::Graphics::ScopedSaveState save(g);auto outer=chamfered(b);
    g.setColour(juce::Colour(0xff010305));g.fillRect(b);g.fillPath(outer);
    juce::ColourGradient glass(juce::Colour(0xff17222a),b.getX(),b.getY(),juce::Colour(0xff010407),b.getRight(),b.getBottom(),false);
    glass.addColour(.24,juce::Colour(0xff08131b));glass.addColour(.52,juce::Colour(0xff03080d));
    g.setGradientFill(glass);g.fillPath(chamfered(b.reduced(2)));
    g.setGradientFill(juce::ColourGradient(juce::Colour(0xffbac9d0),b.getX(),b.getY(),juce::Colour(0xff29333b),b.getRight(),b.getBottom(),false));g.strokePath(chamfered(b.reduced(.5f)),juce::PathStrokeType(1));
    g.setColour(juce::Colour(0xff00d7ec).withAlpha(.62f));g.strokePath(chamfered(b.reduced(4),5),juce::PathStrokeType(1));
    if(screws&&b.getWidth()>65&&b.getHeight()>45)for(float x:{b.getX()+9,b.getRight()-9})for(float y:{b.getY()+9,b.getBottom()-9}){
        g.setColour(juce::Colour(0xff010305));g.fillEllipse(x-3.5f,y-3.5f,7,7);
        g.setGradientFill(juce::ColourGradient(juce::Colour(0xffc1ced4),x-2,y-2,juce::Colour(0xff223038),x+2,y+2,false));g.fillEllipse(x-2.5f,y-2.5f,5,5);
        g.setColour(juce::Colours::black);g.drawLine(x-1.4f,y+1.4f,x+1.4f,y-1.4f,.9f);
    }
}
inline void hardwareReadout(juce::Graphics& g,juce::Rectangle<float> b,const juce::String& text,float size=18,bool enabled=true){
    hardwareGlass(g,b,false);g.setColour(juce::Colour(0xffffc374).withAlpha(enabled?1.f:.4f));g.setFont(hardwareFont(size));
    g.drawFittedText(text,b.reduced(6,2).toNearestInt(),juce::Justification::centred,2);
}
}
