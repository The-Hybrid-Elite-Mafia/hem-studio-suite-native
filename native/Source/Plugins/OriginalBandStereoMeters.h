#pragma once
#include "BandProcessors.h"
#include "OriginalBandTypography.h"
#include "../UI/SegmentedMeter.h"
namespace hem::ui {
// GUI-only stereo display; no DSP path or sample aggregation. A newly opened
// editor must see a publication newer than its construction baseline.
class OriginalBandStereoMeters {
public:
    explicit OriginalBandStereoMeters(BandModuleProcessor& p){PluginTelemetry::Levels initial;if(auto* source=p.pluginTelemetry())if(source->read(initial))token=publication(initial);}
    void poll(BandModuleProcessor& p,bool bandFresh){
        live=false;PluginTelemetry::Levels next;auto* source=p.pluginTelemetry();const auto state=p.meterSnapshot();
        if(!source||!source->read(next)||!next.valid||!next.blocks||!state.isPrepared||!state.hasProcessedAudio||!p.processingReady())return;
        const auto current=publication(next);const double now=juce::Time::getMillisecondCounterHiRes();
        if(current!=token){token=current;lastAdvance=now;observed=true;}
        levels=next;live=bandFresh&&observed&&now-lastAdvance<500.;
    }
    static juce::Rectangle<float> lane(juce::Rectangle<float> aperture,int channel){
        const float inset=aperture.getWidth()*.12f,width=aperture.getWidth()*.24f;
        return {channel?aperture.getRight()-inset-width:aperture.getX()+inset,aperture.getY()+24,width,aperture.getHeight()-28};
    }
    void paint(juce::Graphics& g,juce::Rectangle<float> aperture,bool output,float scale)const{
        // Erase the complete original glass, including every authored LED.
        g.setColour(juce::Colour(0xff010608));g.fillRect(aperture);
        for(int channel=0;channel<2;++channel){const auto bar=lane(aperture,channel);const float value=levels.peak[size_t((output?2:0)+channel)];
            paintSegmentedMeter(g,bar,meterAmplitudePosition(value),live&&std::isfinite(value)&&value>=0);
            g.setColour(juce::Colours::whitesmoke);bandText(g,channel?"R":"L",{bar.getX()-4,aperture.getY(),bar.getWidth()+8,23},scale,juce::Justification::centred,17);
        }
    }
private:
    static uint64_t publication(const PluginTelemetry::Levels& v){return(uint64_t(v.epoch)<<32)|v.blocks;}
    PluginTelemetry::Levels levels;uint64_t token=0;double lastAdvance=0;bool observed=false,live=false;
};
}
