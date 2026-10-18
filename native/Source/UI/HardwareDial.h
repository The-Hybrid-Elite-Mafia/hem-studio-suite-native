#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <algorithm>
#include <cmath>
namespace hem::ui {
// Message-thread drawing only. All highlights remain in chassis coordinates.
inline void drawHardwareDial(juce::Graphics& g, float cx, float cy, float r,
                             float position, float start, float end,
                             float neutralPosition, bool enabled, bool focused,
                             juce::Colour accent) {
    if (r < 2.0f) return;
    const juce::Graphics::ScopedSaveState state(g);
    const float pi = juce::MathConstants<float>::pi;
    const float a = start + juce::jlimit(0.0f, 1.0f, position) * (end-start);
    const float neutral = start + juce::jlimit(0.0f, 1.0f, neutralPosition) * (end-start);
    const auto circle = [=](float scale) { return juce::Rectangle<float>(cx-r*scale,cy-r*scale,2*r*scale,2*r*scale); };
    const auto point = [=](float angle,float scale) { return juce::Point<float>(cx+std::sin(angle)*r*scale,cy-std::cos(angle)*r*scale); };
    if (!enabled) accent = juce::Colour(0xff64737a);
    for (int i=5;i>=1;--i) { g.setColour(juce::Colours::black.withAlpha(0.07f*float(6-i)));g.fillEllipse(circle(0.95f).expanded(float(i)).translated(0,r*.07f)); }
    g.setColour(juce::Colour(0xff010305));g.fillEllipse(circle(1));
    juce::ColourGradient rim(juce::Colour(0xffd9e8ee),cx-r*.7f,cy-r,juce::Colour(0xff060a0d),cx+r*.8f,cy+r,false);
    rim.addColour(.27,juce::Colour(0xff536770));rim.addColour(.49,juce::Colour(0xff0a1117));rim.addColour(.73,juce::Colour(0xff9aaeb6));
    g.setGradientFill(rim);g.fillEllipse(circle(.96f));
    g.setColour(juce::Colour(0xff05090c));g.fillEllipse(circle(.88f));
    for(int i=0;i<72;++i) {
        float theta=2*pi*float(i)/72;
        float light=.25f+.55f*juce::jlimit(0.f,1.f,(std::cos(theta+.65f)+1)*.5f);
        g.setColour(juce::Colour(0xffb6c8cf).withAlpha(light));
        g.drawLine(juce::Line<float>(point(theta,.90f),point(theta,.96f)),std::max(.5f,r*.012f));
    }
    juce::Path rail;rail.addCentredArc(cx,cy,r*.82f,r*.82f,0,start,end,true);
    g.setColour(accent.withAlpha(enabled?.16f:.08f));g.strokePath(rail,juce::PathStrokeType(r*.045f));
    if (std::abs(a-neutral)>.0001f) {
        juce::Path active;active.addCentredArc(cx,cy,r*.82f,r*.82f,0,std::min(a,neutral),std::max(a,neutral),true);
        if(enabled)for(int i=3;i>=1;--i){g.setColour(accent.withAlpha(.05f*float(4-i)));g.strokePath(active,juce::PathStrokeType(r*(.035f+.035f*i)));}
        g.setColour(accent);g.strokePath(active,juce::PathStrokeType(std::max(1.2f,r*.045f)));
        g.setColour(juce::Colours::white.withAlpha(enabled?.65f:.18f));g.strokePath(active,juce::PathStrokeType(std::max(.5f,r*.012f)));
    }
    juce::ColourGradient cap(juce::Colour(0xff71828b),cx-r*.48f,cy-r*.7f,juce::Colour(0xff040609),cx+r*.5f,cy+r*.65f,false);
    cap.addColour(.30,juce::Colour(0xff253139));cap.addColour(.60,juce::Colour(0xff080d12));
    g.setGradientFill(cap);g.fillEllipse(circle(.72f));
    // Polished conical face: fixed silver wedges, independent of parameter angle.
    for(int i=0;i<180;++i){
        const float t=float(i)*2*pi/180,step=2*pi/180+.002f;
        const float reflection=std::pow(std::max(0.f,std::cos(2*t+.8f)),10.f);
        const float secondary=.28f*std::pow(std::max(0.f,std::cos(2*t-1.65f)),18.f);
        const float light=.065f+.82f*reflection+secondary;
        const int c=juce::jlimit(12,220,int(255*light));
        juce::Path wedge;wedge.startNewSubPath(cx,cy);wedge.lineTo(point(t,.705f));wedge.lineTo(point(t+step,.705f));wedge.closeSubPath();
        g.setColour(juce::Colour::fromRGB(juce::uint8(c),juce::uint8(std::min(235,c+6)),juce::uint8(std::min(240,c+10))));g.fillPath(wedge);
    }

    g.setColour(juce::Colour(0xffb5c9d1).withAlpha(.50f));g.drawEllipse(circle(.72f),std::max(.7f,r*.018f));
    for(int i=0;i<12;++i){g.setColour(juce::Colour(0xffa8c3ce).withAlpha(.025f));g.drawEllipse(circle(.37f+float(i)*.026f),.5f);}
    juce::Path highlight;highlight.addCentredArc(cx,cy,r*.67f,r*.67f,0,-1.15f,.45f,true);
    g.setColour(juce::Colour(0xffd4e6ec).withAlpha(.38f));g.strokePath(highlight,juce::PathStrokeType(std::max(.6f,r*.013f)));
    const juce::Line<float> needle(point(a,.35f),point(a,.68f));
    if(enabled){g.setColour(accent.withAlpha(.14f));g.drawLine(needle,std::max(3.f,r*.11f));}
    g.setColour(accent);g.drawLine(needle,std::max(1.3f,r*.035f));
    g.setColour(juce::Colours::white.withAlpha(enabled?.7f:.2f));g.fillEllipse(juce::Rectangle<float>(r*.045f,r*.045f).withCentre(point(a,.70f)));
    if(focused){g.setColour(juce::Colour(0xff0de8ff));g.drawEllipse(circle(.99f),1.2f);}
}
}
