#pragma once
#include "OriginalBankEditorBase.h"
namespace hem {
class OriginalMultibandDynamicsEditor final:public OriginalBankEditorBase {
public:
    explicit OriginalMultibandDynamicsEditor(BandModuleProcessor& p):OriginalBankEditorBase(p,ApprovedArt::approved_04_png,ApprovedArt::approved_04_pngSize){dialCrop={238,294,76,70};setSize(1280,720);refresh();}
    void resized() override {beginLayout();
        {const float crosses[]{275,632,1013},valueX[]{403,774,1145},valueW[]{150,157,161};const size_t ci[]{1,28,2};for(int i=0;i<3;++i)knob(ci[i],{crosses[i],162,valueX[i]+valueW[i]-crosses[i],81},{0,0,87,81},{valueX[i]-crosses[i],38,valueW[i],35},{int(crosses[i]),162,87,81});
            for(int b=0;b<4;++b){if(!modern&&b==2)continue;const juce::String prefix=b==0?"low":b==3?"high":!modern?"mid":b==1?"lowMid":"highMid";
                const float y=294.f+118*b;const juce::String suffix[]{"Threshold","Ratio","Attack","Release","Makeup"};const float xs[]{238,414,582,760,934},vx[]{321,495,668,843,1017},vw[]{68,66,66,67,66};
                for(int k=0;k<5;++k)knob(index(prefix+suffix[k]),{xs[k],y,155,70},{0,0,76,70},{vx[k]-xs[k],33,vw[k],31},{int(xs[k]),int(y),76,70});
                button(index(prefix=="mid"?juce::String("legacyMidOn"):prefix+"On"),{1135,y-5,122,31});button(index(prefix+"Solo"),{1135,y+34,122,31});
            }
            knob(50,{1488,581,99,106},{8,0,84,75},{0,80,98,27},{1496,581,84,75});knob(3,{1488,731,99,105},{8,0,84,74},{0,77,98,27},{1496,731,84,74});button(0,{1470,854,135,43});
        }
        advancedLayout(modern?std::vector<juce::String>{"topology","lowKnee","lowRange","lowMidKnee","lowMidRange","highMidKnee","highMidRange","highKnee","highRange"}:std::vector<juce::String>{"topology","lowKnee","lowRange","midKnee","midRange","highKnee","highRange"});
    }
    void paint(juce::Graphics& g) override {background(g);
        crossoverStatus(g,{70,239,1360,17});meter(g,{1491,212,95,119},false);meter(g,{1491,387,95,143},true);
        labelLegacyMiddle(g,{72,421,119,51},{216,510,1213,208-118});
        for(int row=0;row<4;++row){const juce::Rectangle<float> r(1293,275.f+118*row,102,94);g.setColour(juce::Colour(0xff020a10));g.fillRect(r);
            const bool available=modern||row!=2;const size_t b=!modern&&row==3?2:size_t(row);const double cut=frame.bandCuts[b];
            if(bandMeasured()&&available){g.setColour(juce::Colour(0xffff9f31));g.fillRect(r.withTop(r.getBottom()-float(std::clamp(cut/24.,0.,1.))*r.getHeight()));}
            g.setColour(juce::Colours::white);text(g,bandMeasured()&&available?juce::String(cut,2)+" dB":"--",r,juce::Justification::centred);}
        // Clear the entire glass through the footer boundary; keep the grid baseline.
        g.setColour(juce::Colour(0xff020a10));g.fillRect(juce::Rectangle<float>(418,779,977,103));
        const juce::Rectangle<float> plot(418,779,977,100);grid(g,plot);
        if(!bandMeasured()){historyCount=0;historyCursor=0;lastHistorySample=0;}
        if(bandMeasured()&&frame.revision!=lastHistoryRevision){if(frame.samples<=lastHistorySample){historyCount=0;historyCursor=0;}history[historyCursor]={frame.samples,frame.bandCuts};historyCursor=(historyCursor+1)%history.size();historyCount=std::min(history.size(),historyCount+1);lastHistorySample=frame.samples;lastHistoryRevision=frame.revision;}
        if(bandMeasured()&&historyCount>1){const auto newest=history[(historyCursor+history.size()-1)%history.size()].sample;
            const auto oldest=history[(historyCursor+history.size()-historyCount)%history.size()].sample;const auto span=std::max<uint64_t>(1,newest-oldest);
            for(size_t b=0;b<(frame.topology?4u:3u);++b){juce::Path path;for(size_t n=0;n<historyCount;++n){const auto& h=history[(historyCursor+history.size()-historyCount+n)%history.size()];
                    const float x=plot.getX()+plot.getWidth()*float(double(h.sample-oldest)/double(span)),y=plot.getY()+float(std::clamp(h.cuts[b]/24.,0.,1.))*plot.getHeight();if(n==0)path.startNewSubPath(x,y);else path.lineTo(x,y);}
                g.setColour(juce::Colour::fromHSV(float(b)*.19f,.8f,1.f,1.f));g.strokePath(path,juce::PathStrokeType(1.5f));}}
        g.setColour(juce::Colour(0xff030b10));g.fillRect(juce::Rectangle<float>(418,882,977,28));g.setColour(juce::Colours::whitesmoke);
        text(g,bandMeasured()?"GR 0..24 dB / GUI SAMPLES / "+juce::String(historyCount>1?1000.*double(history[(historyCursor+history.size()-1)%history.size()].sample-history[(historyCursor+history.size()-historyCount)%history.size()].sample)/frame.rate:0.,0)+" ms":stale?"TELEMETRY STALE":"NO AUDIO",{422,882,573,28},juce::Justification::centredLeft);
        for(int b=0;b<(modern?4:3);++b){g.setColour(juce::Colour::fromHSV(float(b)*.19f,.8f,1.f,1.f));const juce::String label=b==0?"LOW":modern?(b==1?"LOW MID":b==2?"HIGH MID":"HIGH"):(b==1?"MID":"HIGH");text(g,label,{998.f+100*b,882,98,28});}advancedPaint(g);

    }
private:
    struct History {uint64_t sample=0;std::array<double,4> cuts{};};std::array<History,256> history{};size_t historyCount=0,historyCursor=0;uint64_t lastHistorySample=0;uint32_t lastHistoryRevision=0;
};
}
