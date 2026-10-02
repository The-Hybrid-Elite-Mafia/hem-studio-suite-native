#pragma once
#include "Processors.h"
#include <limits>

namespace hem::ui {
// Display calibration only. DSP values and parameter contracts are untouched.
struct PluginMeterReadout {
    PluginMeterSnapshot snapshot;
    float minimum=-60,maximum=0,value=0;
    std::array<float,6> ticks{{-60,-48,-24,-12,-6,0}};
    int tickCount=6;
    bool signedScale=false,available=false;
    juce::String heading,valueText,detail;
    float position(float v) const noexcept {
        if(std::isnan(v))return 0;
        return juce::jlimit(0.f,1.f,(v-minimum)/(maximum-minimum));
    }
    float zeroPosition() const noexcept {return signedScale?position(0):0;}
    float fillStart() const noexcept {return available?std::min(zeroPosition(),position(value)):zeroPosition();}
    float fillEnd() const noexcept {return available?std::max(zeroPosition(),position(value)):zeroPosition();}
};
inline PluginMeterReadout pluginMeterReadout(const ModuleProcessor& processor) {
    PluginMeterReadout result;
    result.snapshot=processor.meterSnapshot();
    const auto& snapshot=result.snapshot;
    const auto kind=processor.meterKind();
    result.heading=processor.meterLabel();
    juce::String statistic="LAST BLOCK MAX";
    if(kind==PluginMeterKind::correlation){
        result.minimum=-1;result.maximum=1;result.signedScale=true;
        result.ticks={{-1,-.5f,0,.5f,1,0}};result.tickCount=5;
        result.heading="OUTPUT L/R CORRELATION / -1 TO +1";
        statistic="LAST BLOCK CORRELATION";
    }else if(kind==PluginMeterKind::envelopeGain){
        result.minimum=-12;result.maximum=12;result.signedScale=true;
        result.ticks={{-12,-6,0,6,12,0}};result.tickCount=5;
        result.heading="ENVELOPE GAIN / PRE-MIX + PRE-OUTPUT / dB";
        statistic="LAST BLOCK SIGNED ABS-MAX";
    }else if(kind==PluginMeterKind::gainReduction){
        result.minimum=0;result.maximum=24;
        result.ticks={{0,6,12,18,24,0}};result.tickCount=5;
        if(dynamic_cast<const VcaProcessor*>(&processor))
            result.heading="DETECTOR GAIN REDUCTION / PRE-MIX + PRE-MAKEUP / dB";
        else result.heading += " / WET-SCALED DETECTOR";
    }else if(kind==PluginMeterKind::firTruePeak){
        result.heading="OUTPUT PEAK ESTIMATE / 8x, 32-TAP WINDOWED-SINC FIR / dBTP";
    }else result.heading="OUTPUT SAMPLE PEAK / dBFS";

    const double rate=snapshot.sampleRate;
    const auto window=juce::String(snapshot.blockSamples)+" samples / "
        +juce::String(rate>0?1000.0*double(snapshot.blockSamples)/rate:0,2)+" ms";
    result.detail=statistic+" / "+window+" / NO DISPLAY HOLD";
    if(kind==PluginMeterKind::firTruePeak){
        const int latency=snapshot.latencySamples;
        result.detail=snapshot.isPrepared?"REPORTED LATENCY: "+juce::String(latency)+" samples / "
            +juce::String(rate>0?1000.0*double(latency)/rate:0,3)+" ms / "+statistic+" / "+window
            :"REPORTED LATENCY UNAVAILABLE / NOT PREPARED";
    }
    if(!snapshot.hasProcessedAudio){
        result.valueText="NO PROCESSED AUDIO";
        if(kind!=PluginMeterKind::firTruePeak)result.detail="WAITING FOR A NONEMPTY AUDIO BLOCK";
        return result;
    }
    if(!snapshot.valueAvailable){
        result.valueText=kind==PluginMeterKind::correlation
            ?"UNDEFINED / SILENCE OR INSUFFICIENT STEREO ENERGY":"UNAVAILABLE / INVALID MEASUREMENT";
        return result;
    }
    result.available=true;result.value=snapshot.value;
    if(kind==PluginMeterKind::samplePeak||kind==PluginMeterKind::firTruePeak){
        const auto units=kind==PluginMeterKind::firTruePeak?" dBTP EST.":" dBFS";
        result.value=snapshot.value>0?20.f*std::log10(snapshot.value):-std::numeric_limits<float>::infinity();
        result.valueText=snapshot.value>0?juce::String(result.value,2)+units:juce::String("-inf")+units+" / SILENCE";
        if(kind==PluginMeterKind::firTruePeak)result.valueText+=" / GR "+juce::String(snapshot.auxiliary,2)+" dB";
    }else{
        result.valueText=(result.signedScale&&result.value>0?"+":"")+juce::String(result.value,kind==PluginMeterKind::correlation?3:2);
        if(kind!=PluginMeterKind::correlation)result.valueText+=" dB";
        if(!snapshot.signalPresent)result.valueText+=" / SILENT OUTPUT";
    }
    if(std::isfinite(result.value)&&(result.value<result.minimum||result.value>result.maximum))
        result.valueText+=result.value<result.minimum?" / BELOW SCALE":" / ABOVE SCALE";
    return result;
}
inline juce::Rectangle<float> pluginMeterTrack(juce::Rectangle<float> area) noexcept {
    return {area.getX()+18,area.getY()+38,area.getWidth()-36,12};
}
inline constexpr juce::uint32 pluginMeterPositiveColour=0xff00cddd;
inline constexpr juce::uint32 pluginMeterNegativeColour=0xffffb36b;
inline constexpr juce::uint32 pluginMeterZeroColour=0xffd9e5ea;
inline constexpr juce::uint32 pluginMeterTrackColour=0xff061f2b;
inline void paintPluginMeter(juce::Graphics& g,juce::Rectangle<float> area,const PluginMeterReadout& readout) {
    g.setColour(juce::Colour(0xff021018));g.fillRect(area);
    g.setColour(juce::Colour(0xffa9bac4));g.setFont(11.5f);
    g.drawText(readout.heading,area.withHeight(15).toNearestInt(),juce::Justification::centred);
    g.setColour(juce::Colours::white);g.setFont(14.f);
    g.drawText(readout.valueText,area.withY(area.getY()+16).withHeight(18).toNearestInt(),juce::Justification::centred);
    const auto track=pluginMeterTrack(area);
    g.setColour(juce::Colour(pluginMeterTrackColour));g.fillRect(track);
    if(readout.available&&readout.fillEnd()>readout.fillStart()){
        g.setColour(juce::Colour(readout.signedScale&&readout.value<0?pluginMeterNegativeColour:pluginMeterPositiveColour));
        g.fillRect(juce::Rectangle<float>(track.getX()+track.getWidth()*readout.fillStart(),track.getY(),
            track.getWidth()*(readout.fillEnd()-readout.fillStart()),track.getHeight()));
    }
    // The centre line is a calibration mark, not a measurement or level bar.
    if(readout.signedScale){g.setColour(juce::Colour(pluginMeterZeroColour));
        const float x=track.getX()+track.getWidth()*readout.zeroPosition();g.drawVerticalLine(int(std::round(x)),track.getY()-2,track.getBottom()+2);}
    g.setFont(10.5f);g.setColour(juce::Colour(0xffa9bac4));
    for(int i=0;i<readout.tickCount;++i){
        const float tick=readout.ticks[size_t(i)],x=track.getX()+track.getWidth()*readout.position(tick);
        g.drawVerticalLine(int(std::round(x)),track.getBottom(),track.getBottom()+3);
        const auto label=(tick>0&&readout.signedScale?"+":"")+juce::String(tick,std::floor(tick)==tick?0:1);
        g.drawText(label,juce::Rectangle<float>(x-18,area.getY()+52,36,12).toNearestInt(),juce::Justification::centred);
    }
    g.setFont(10.f);g.drawText(readout.detail,area.withY(area.getY()+65).withHeight(12).toNearestInt(),juce::Justification::centred);
}
} // namespace hem::ui
