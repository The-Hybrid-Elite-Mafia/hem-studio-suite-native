#pragma once
#include "../UI/SegmentedMeter.h"
#include "../UI/HardwarePanel.h"
#include "../UI/PhysicalValueSlider.h"
#include "../Systems/PluginTelemetry.h"
#include "../DSP/FoundationTelemetry.h"

namespace hem::foundationPresentation {
// GUI-only publication freshness. Opening a view arms it at the current counters;
// old audio is never promoted to live merely because the editor has opened.
class Freshness {
public:
    // The overload accepting captured counters makes the bounded-reader edge
    // cases testable without changing or instrumenting an audio producer.
    void arm(unsigned currentEpoch,unsigned currentBlock,bool coherent,unsigned currentGeneration,unsigned currentSerial,double openedAt=-1) {
        epoch=currentEpoch;block=currentBlock;baselineKnown=coherent;generation=currentGeneration;serial=currentSerial;
        levelAt=historyAt=-1;lastPollAt=openedAt;staleBaseline=false;historyBaselineKnown=true;
    }
    void arm(PluginTelemetry* meter, const FoundationTelemetry* history) {
        PluginTelemetry::Levels initial;const unsigned initialEpoch=meter?meter->analysis.epoch():0;
        const bool coherent=meter&&meter->read(initial)&&initial.epoch==initialEpoch;
        arm(initialEpoch,coherent?initial.blocks:0,coherent,history?history->currentGeneration():0,history?history->latestSerial():0,juce::Time::getMillisecondCounterHiRes());
    }
    void poll(PluginTelemetry* meter, const FoundationTelemetry* history, bool prepared,
              double now=juce::Time::getMillisecondCounterHiRes()) {
        PluginTelemetry::Levels captured;FoundationDisplayFrame capturedHistory;
        const bool levelOk=meter&&meter->read(captured);
        const bool historyOk=history&&history->read(history->latestSerial(),capturedHistory);
        observe(captured,levelOk,meter?meter->analysis.epoch():0,capturedHistory,historyOk,
                history?history->currentGeneration():0,prepared&&meter,now);
    }
    void observe(const PluginTelemetry::Levels& captured,bool levelOk,unsigned currentEpoch,
                 const FoundationDisplayFrame& capturedHistory,bool historyOk,unsigned currentGeneration,
                 bool prepared,double now) {
        available=prepared;levels=captured;frame=capturedHistory;
        levelRead=levelOk&&levels.epoch==currentEpoch;
        historyRead=historyOk&&frame.generation==currentGeneration&&frame.samples>0;
        if(currentEpoch!=epoch){epoch=currentEpoch;block=0;levelAt=-1;baselineKnown=true;staleBaseline=false;}
        if(currentGeneration!=generation){generation=currentGeneration;historyAt=-1;historyBaselineKnown=true;}
        // There is no producer wall-clock timestamp. After a long GUI gap, an
        // unseen block may already be old. Establish new baselines, then require
        // another positive publication before presenting current measurements.
        const bool gap=lastPollAt>=0&&now-lastPollAt>=750;lastPollAt=now;
        if(gap){levelAt=historyAt=-1;baselineKnown=historyBaselineKnown=false;staleBaseline=true;}
        if(levelRead&&!baselineKnown){block=levels.blocks;baselineKnown=true;}
        else if(available&&levelRead&&levels.valid&&levels.samples>0&&levels.blocks>0&&levels.blocks!=block){block=levels.blocks;levelAt=now;staleBaseline=false;}
        if(historyRead&&!historyBaselineKnown){serial=frame.serial;historyBaselineKnown=true;}
        else if(available&&historyRead&&frame.serial!=serial){serial=frame.serial;historyAt=now;}
        live=available&&levelRead&&levels.valid&&levels.samples>0&&levelAt>=0&&now-levelAt<750;
        measured=live&&historyRead&&historyAt>=0&&now-historyAt<750;
    }
    juce::String status() const {
        if(!available)return "UNAVAILABLE";
        if(levelRead&&!levels.valid&&levels.blocks)return "INVALID";
        if(levelAt<0)return staleBaseline?"STALE":"WAITING";
        return live?"LIVE":"STALE";
    }
    PluginTelemetry::Levels levels; FoundationDisplayFrame frame;
    bool available=false,levelRead=false,historyRead=false,live=false,measured=false;
private:
    unsigned epoch=0,block=0,generation=0,serial=0;
    bool baselineKnown=false,historyBaselineKnown=true,staleBaseline=false;double levelAt=-1,historyAt=-1,lastPollAt=-1;
};
class Slider final:public ui::PhysicalValueSlider {
public:
    bool keyPressed(const juce::KeyPress& key) override {
        if(!isEnabled())return false;
        const int code=key.getKeyCode();
        if(code==juce::KeyPress::leftKey||code==juce::KeyPress::rightKey||code==juce::KeyPress::upKey||code==juce::KeyPress::downKey){
            const juce::Slider::ScopedDragNotification gesture(*this);return ui::PhysicalValueSlider::keyPressed(key);
        }
        return ui::PhysicalValueSlider::keyPressed(key);
    }
};
class SlopeChoice final:public juce::ComboBox {
public:
    bool keyPressed(const juce::KeyPress& key) override {return isEnabled()&&juce::ComboBox::keyPressed(key);}
};
// Only the lower, authored arrow bay is a ComboBox hit target. Its selected
// value is painted in the separate authored readout above it.
class SlopeLook final:public juce::LookAndFeel_V4 {
public:
    void drawComboBox(juce::Graphics& g,int w,int h,bool,int,int,int,int,juce::ComboBox& box) override {
        g.fillAll(juce::Colour(0xff15191c));
        g.setColour(juce::Colours::white.withAlpha(box.isEnabled()?1.f:.35f));
        juce::Path arrow;arrow.addTriangle(w*.46f,h*.38f,w*.54f,h*.38f,w*.5f,h*.67f);g.fillPath(arrow);
        if(box.hasKeyboardFocus(true)){g.setColour(juce::Colour(0xff08dce8));g.drawRect(1,1,w-2,h-2);}
    }
    juce::Font getComboBoxFont(juce::ComboBox&) override {return juce::Font(juce::FontOptions(12.f));}
};
inline void label(juce::Graphics& g,juce::Rectangle<float> area,const juce::String& value,float size,juce::Colour colour=juce::Colour(0xffc7d9dc)) {
    const juce::Graphics::ScopedSaveState clip(g);g.reduceClipRegion(area.toNearestInt());
    g.setColour(colour);g.setFont(ui::hardwareFont(size));g.drawFittedText(value,area.toNearestInt(),juce::Justification::centred,2);
}
// Stereo peak segments and 300 ms RMS marks, both directly measured at the
// processor input/output taps. No scalar maximum is copied into the other lane.
inline void stereoMeter(juce::Graphics& g,juce::Rectangle<float> area,const Freshness& state,bool output) {
    const juce::Graphics::ScopedSaveState clip(g);g.reduceClipRegion(area.toNearestInt());
    g.setColour(juce::Colour(0xff020609));g.fillRect(area);
    const int offset=output?2:0;const float gap=6, laneWidth=(area.getWidth()-18)*.5f;
    const auto lanes=area.withTrimmedTop(20).withTrimmedBottom(42).reduced(6,0);
    for(int channel=0;channel<2;++channel) {
        const auto lane=juce::Rectangle<float>(lanes.getX()+channel*(laneWidth+gap),lanes.getY(),laneWidth,lanes.getHeight());
        const float peak=state.levels.peak[size_t(offset+channel)],rms=state.levels.rms[size_t(offset+channel)];
        const bool valid=state.live&&std::isfinite(peak)&&std::isfinite(rms);
        ui::paintSegmentedMeter(g,lane,ui::meterAmplitudePosition(peak),valid);
        ui::paintMeterMark(g,lane,ui::meterAmplitudePosition(rms),juce::Colour(0xffe0f9ff),1.5f,ui::MeterMarkRole::rms,valid);
        label(g,{lane.getX(),area.getY(),lane.getWidth(),18},channel?"R":"L",14);
        const auto db=peak>0?juce::String(20*std::log10(peak),1):juce::String("-inf");
        label(g,{lane.getX()-2,lanes.getBottom()+1,lane.getWidth()+4,19},valid?db:"--",12);
    }
    label(g,area.withY(area.getBottom()-21).withHeight(19),state.live?"PK / RMS":state.status(),11);
}
}
