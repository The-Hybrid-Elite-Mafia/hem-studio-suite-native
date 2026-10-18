#pragma once
#include "OriginalBandEditorBase.h"
namespace hem {
class OriginalMidSideEqEditor final:public OriginalBandEditorBase {
public:
    explicit OriginalMidSideEqEditor(BandModuleProcessor& p):OriginalBandEditorBase(p,ApprovedArt::approved_13_png,ApprovedArt::approved_13_pngSize){setSize(1280,720);refresh();}
    void resized() override {layoutTopbar();
        for(int lane=0;lane<2;++lane)for(int b=0;b<5;++b){const size_t base=b==4?34+4*size_t(lane):2+16*size_t(lane)+4*size_t(b);
            const float centres[5][3]{{193,283,372},{500,589,677},{806,895,982},{1110,1199,1287},{1416,1505,1592}};
            const float windowX[5][3]{{147,244,334},{456,550,638},{760,856,943},{1066,1160,1250},{1371,1467,1555}};
            const float windowW[5][3]{{85,78,75},{83,77,78},{84,76,78},{83,77,76},{82,78,77}};
            const float onX[]{147,455,760,1066,1371},typeX[]{285,591,896,1202,1507};
            const float y=lane?604.f:383.f;
            for(int k=0;k<3;++k){const float cx=centres[b][k];const juce::Rectangle<float> dial(cx-35,y,70,70),text(windowX[b][k],lane?684.f:461.f,windowW[b][k],lane?30.f:31.f);
                const auto bounds=dial.getUnion(text);knob(base+1+size_t(k),bounds,dial.translated(-bounds.getX(),-bounds.getY()),text.translated(-bounds.getX(),-bounds.getY()),dial.toNearestInt());}
            button(base,{onX[b],lane?725.f:502.f,120,31});choice(42+size_t(lane)*5+size_t(b),{typeX[b],lane?725.f:502.f,123,31});
        }
        knob(52,{526,796,227,89},{0,0,96,89},{111,35,115,36},{526,796,96,89});
        knob(1,{1289,796,224,89},{0,0,98,89},{116,35,107,36},{1289,796,98,89});
        button(53,{777,819,107,48});button(54,{906,819,108,48});button(0,{1038,819,107,48});resetAll.setBounds(scaled({375,800,100,70}));
    }
    void paint(juce::Graphics& g) override {background(g);meter(g,{64,137,86,165},false);meter(g,{1522,137,86,165},true);
        for(size_t lane=0;lane<2;++lane){const juce::Rectangle<float> r(lane?888.f:245.f,165,562,119);g.setColour(juce::Colour(0xff020a10));g.fillRect(r);
            const auto plot=r.reduced(4,0).withTrimmedTop(33).withTrimmedBottom(33);grid(g,plot);
            if(measured()){juce::Path path;for(int point=0;point<=320;++point){const double frequency=20*std::pow(900.,point/320.);
                    const auto z=std::polar(1.,-2*hem::pi*std::min(frequency,frame.rate*.45)/frame.rate);std::complex<double> response=1;
                    for(size_t b=0;b<5;++b){const auto& c=frame.coefficients[lane*5+b];const auto a=(c[0]+c[1]*z+c[2]*z*z)/(1.+c[3]*z+c[4]*z*z);
                        const auto secondary=(c[5]+c[6]*z+c[7]*z*z)/(1.+c[8]*z+c[9]*z*z);response*=a+c[10]*(secondary-a);}
                    response*=lane?frame.sideParticipation*frame.width*.01:frame.midParticipation;
                    const double db=20*std::log10(std::max(1e-12,std::abs(response)));const float x=plot.getX()+plot.getWidth()*point/320.f,y=plot.getCentreY()-float(std::clamp(db,-24.,24.))*plot.getHeight()/48;
                    if(point==0)path.startNewSubPath(x,y);else path.lineTo(x,y);}
                g.setColour(juce::Colour(lane?0xffffad39:0xff3cecff));g.strokePath(path,juce::PathStrokeType(2));}
            g.setColour(juce::Colour(0xff020a10));g.fillRect(r.reduced(2).removeFromTop(28));g.fillRect(r.reduced(2).removeFromBottom(28));g.setColour(juce::Colours::whitesmoke);
            const auto status=measured()?juce::String(lane?"SIDE":"MID")+" P="+juce::String(lane?frame.sideParticipation:frame.midParticipation,2)+(lane?" W="+juce::String(frame.width,1)+"%":juce::String())+(frame.transition?" TRANSITION":" PRE OUT/BYPASS"):stale?juce::String("TELEMETRY STALE"):juce::String("NO AUDIO");
            text(g,status,r.reduced(5,1).removeFromTop(26));
            text(g,"20 Hz-18 kHz | TOP +24 / BOTTOM -24 dB",r.reduced(5,1).removeFromBottom(26));

        }
    }
};
}
