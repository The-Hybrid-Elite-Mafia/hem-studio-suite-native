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
        else if(processor.usesWetScaledReductionLabel()) result.heading += " / WET-SCALED DETECTOR";
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
inline void paintInstrumentGlass(juce::Graphics& g,juce::Rectangle<float> area){
 g.setColour(juce::Colours::black);g.fillRoundedRectangle(area,5);
 g.setGradientFill(juce::ColourGradient(juce::Colour(0xff697276),area.getX(),area.getY(),juce::Colour(0xff080a0b),area.getRight(),area.getBottom(),false));g.fillRoundedRectangle(area.reduced(1),4);
 auto glass=area.reduced(5);juce::ColourGradient shade(juce::Colour(0xff172023),glass.getX(),glass.getY(),juce::Colour(0xff010303),glass.getX(),glass.getBottom(),false);shade.addColour(.3,juce::Colour(0xff080e10));shade.addColour(.32,juce::Colour(0xff030709));g.setGradientFill(shade);g.fillRoundedRectangle(glass,2);
 g.setColour(juce::Colour(0xffa0b9bc).withAlpha(.22f));g.drawLine(glass.getX()+2,glass.getY()+1,glass.getRight()-2,glass.getY()+1,1);
}
inline void paintReductionDial(juce::Graphics& g,juce::Rectangle<float> area,const PluginMeterReadout& r){
 paintInstrumentGlass(g,area);
 const float cx=area.getX()+200,cy=area.getBottom()-17,radius=102;
 g.setColour(juce::Colour(0xffd9e5ea));g.setFont(13);
 for(int i=0;i<5;++i){float a=-1.15f+i*.575f;auto point=[&](float rad){return juce::Point<float>(cx+std::sin(a)*rad,cy-std::cos(a)*rad);};auto p=point(radius),q=point(radius-7);g.drawLine({p,q},1);auto t=point(radius-20);g.drawText(juce::String(i*6),int(t.x-15),int(t.y-8),30,16,juce::Justification::centred);}
 if(r.available){const float a=-1.15f+r.position(r.value)*2.3f;g.setColour(juce::Colour(0xffffbe72));g.drawLine(cx,cy,cx+std::sin(a)*(radius-9),cy-std::cos(a)*(radius-9),2);}
 g.setColour(juce::Colour(0xff829093));g.fillEllipse(cx-5,cy-5,10,10);
 auto info=area.withTrimmedLeft(380).reduced(12);
 g.setColour(juce::Colour(0xffc4e5e8));g.setFont(14);g.drawFittedText(r.heading,info.withHeight(36).toNearestInt(),juce::Justification::centred,2);
 g.setColour(juce::Colour(0xffffcd83));g.setFont(24);g.drawFittedText(r.valueText,info.withY(info.getY()+39).withHeight(32).toNearestInt(),juce::Justification::centred,1);
 g.setColour(juce::Colour(0xffaab9bd));g.setFont(12);g.drawFittedText(r.detail,info.withY(info.getY()+78).withHeight(36).toNearestInt(),juce::Justification::centred,2);
}
inline void paintPluginMeter(juce::Graphics& g,juce::Rectangle<float> area,const PluginMeterReadout& readout,float fontScale=1.f) {
    paintInstrumentGlass(g,area);
    const bool vertical=area.getHeight()>area.getWidth();
    auto heading=area.reduced(7).withHeight(vertical?52.f:25.f);
    g.setColour(juce::Colour(0xffa6eaf0));g.setFont((vertical?12.f:14.f)*fontScale);g.drawFittedText(vertical?juce::String("OUTPUT\nPEAK\ndBFS"):readout.heading,heading.toNearestInt(),juce::Justification::centred,3,1.f);
    auto value=area.reduced(6).withY(heading.getBottom()+3).withHeight(vertical?42.f:27.f);
    g.setColour(juce::Colour(0xffffcd83));g.setFont((vertical?13.f:22.f)*fontScale);g.drawFittedText(readout.valueText,value.toNearestInt(),juce::Justification::centred,3);
    auto track=vertical?juce::Rectangle<float>(area.getX()+14,value.getBottom()+12,area.getWidth()-45,area.getBottom()-value.getBottom()-90):juce::Rectangle<float>(area.getX()+22,value.getBottom()+8,area.getWidth()-44,std::max(10.f,area.getHeight()-135));
    const int segments=vertical?48:64;
    for(int i=0;i<segments;++i){const float low=float(i)/segments,high=float(i+1)/segments;
        const bool lit=readout.available&&high>readout.fillStart()&&low<readout.fillEnd();
        const auto colour=readout.signedScale?(low<readout.zeroPosition()?juce::Colour(0xffffb36b):juce::Colour(0xff00cddd)):high>.95f?juce::Colour(0xffff5547):high>.8f?juce::Colour(0xffffb248):high>.55f?juce::Colour(0xff44df95):juce::Colour(0xff00cddd);
        g.setColour(colour.withAlpha(lit?1.f:.10f));
        if(vertical)g.fillRect(track.getX(),track.getBottom()-high*track.getHeight(),track.getWidth(),std::max(.5f,track.getHeight()/segments-1));
        else g.fillRect(track.getX()+low*track.getWidth(),track.getY(),std::max(.5f,track.getWidth()/segments-2),track.getHeight());
    }
    g.setFont(12*fontScale);g.setColour(juce::Colour(0xffb8c9ce));
    for(int i=0;i<readout.tickCount;++i){const float tick=readout.ticks[size_t(i)],pos=readout.position(tick);const auto label=juce::String(tick,std::floor(tick)==tick?0:1);
        if(vertical)g.drawText(label,int(track.getRight()+3),int(track.getBottom()-pos*track.getHeight()-6),30,int(16*fontScale),juce::Justification::left);
        else g.drawText(label,int(track.getX()+pos*track.getWidth()-18),int(track.getBottom()+3),36,int(16*fontScale),juce::Justification::centred);
    }
    const auto detail=vertical?juce::String(readout.snapshot.blockSamples)+" samples\n"+juce::String(readout.snapshot.sampleRate>0?1000.*readout.snapshot.blockSamples/readout.snapshot.sampleRate:0.,2)+" ms\nNO HOLD":readout.detail;
    g.setFont(12.f*fontScale);g.drawFittedText(detail,area.reduced(6).withY(area.getBottom()-(vertical?66.f:40.f)).withHeight(vertical?60.f:36.f).toNearestInt(),juce::Justification::centred,vertical?3:2,1.f);
}
} // namespace hem::ui
