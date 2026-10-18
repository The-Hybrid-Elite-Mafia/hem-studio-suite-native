#pragma once
#include "OriginalBankEditorBase.h"
namespace hem {
class OriginalMultibandImagerEditor final:public OriginalBankEditorBase {
public:
    explicit OriginalMultibandImagerEditor(BandModuleProcessor& p):OriginalBankEditorBase(p,ApprovedArt::approved_14_png,ApprovedArt::approved_14_pngSize){dialCrop={271,685,104,100};advancedBounds={49,542,149,38};setSize(1280,720);refresh();}
    void resized() override {beginLayout();{const float xs[]{417,763,1110};const size_t ci[]{1,14,2};for(int i=0;i<3;++i)knob(ci[i],{xs[i],241,143,156},{0,0,143,124},{7,129,129,27},{int(xs[i]),241,143,124});
            for(int b=0;b<4;++b){if(!modern&&b==2)continue;const juce::String prefix=b==0?"low":b==3?"high":!modern?"mid":b==1?"lowMid":"highMid";const float x=271.f+299*b;
                knob(index(prefix+"Width"),{x,685,105,133},{0,0,104,100},{0,107,105,26},{int(x),685,104,100});
                knob(index(prefix+"Gain"),{x+127,685,105,133},{0,0,104,100},{0,107,105,26},{int(x+127),685,104,100});
                button(index(prefix=="mid"?juce::String("legacyMidOn"):prefix+"On"),{x-6,831,115,39});button(index(prefix+"Solo"),{x+125,831,114,39});}
            knob(3,{1473,612,148,181},{0,0,148,143},{7,147,133,34},{1473,612,148,143});button(0,{1478,823,137,40});}
        advancedLayout({"topology","lowMono"});
    }
    void paint(juce::Graphics& g) override {background(g);meter(g,{78,144,100,324},false);meter(g,{1493,144,99,324},true);
        crossoverStatus(g,{309,400,1063,17});labelLegacyMiddle(g,{568,438,230,25},{843,438,580-299,435});
        for(int row=0;row<4;++row){const juce::Rectangle<float> r(271.f+299*row,493,231,152);const bool available=modern||row!=2;const size_t b=!modern&&row==3?2:size_t(row);
            // Clear the baked grid, then calibrate both axes to the same caption-free radius.
            g.setColour(juce::Colour(0xff030b10));g.fillRect(r);
            const auto plot=r.reduced(4,0).withTrimmedTop(30).withTrimmedBottom(31);
            bool clipped=false;const float radius=(std::min(plot.getWidth(),plot.getHeight())-6)*.5f;
            g.setColour(juce::Colour(0xff314047));
            for(float fraction:{.5f,1.f}){const float d=2*radius*fraction;g.drawEllipse(plot.getCentreX()-d*.5f,plot.getCentreY()-d*.5f,d,d,1);}
            g.drawLine(plot.getCentreX()-radius,plot.getCentreY(),plot.getCentreX()+radius,plot.getCentreY(),1);
            g.drawLine(plot.getCentreX(),plot.getCentreY()-radius,plot.getCentreX(),plot.getCentreY()+radius,1);
            if(bandMeasured()&&available){g.setColour(juce::Colour(0xff45eced));for(unsigned n=0;n<std::min(32u,frame.scopeCount);++n){const auto& point=frame.scopes[b][n];const double mid=(double(point[0])+point[1])*.5,side=(double(point[0])-point[1])*.5;
                    clipped|=std::abs(side)>1||std::abs(mid)>1;const float x=plot.getCentreX()+float(std::clamp(side,-1.,1.))*radius,y=plot.getCentreY()-float(std::clamp(mid,-1.,1.))*radius;g.fillEllipse(x-1.5f,y-1.5f,3.f,3.f);}}
            g.setColour(juce::Colour(0xff030b10));g.fillRect(r.withHeight(26));g.fillRect(juce::Rectangle<float>(r.getX(),r.getBottom()-27,r.getWidth(),27));g.setColour(juce::Colours::whitesmoke);
            text(g,!available?"LEGACY UNUSED":!bandMeasured()?stale?"STALE":"NO AUDIO / PENDING":clipped?"VIEW CLIPPED":"X:S Y:M +/-1 PRE SOLO",r.reduced(3,0).removeFromTop(26));
            text(g,bandMeasured()&&available?(frame.correlationValid[b]?"CORR "+juce::String(frame.correlations[b],3):"CORR UNDEFINED"):"--",r.reduced(3,0).removeFromBottom(27));}

        advancedPaint(g);
    }
};
}
