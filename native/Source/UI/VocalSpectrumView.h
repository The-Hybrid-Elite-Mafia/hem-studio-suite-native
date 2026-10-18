#pragma once
#include "../Systems/VocalSpectrum.h"
#include <juce_gui_basics/juce_gui_basics.h>
namespace hem::ui {
// Message-thread-owned state. No clock or display activity in the audio callback.
class VocalSpectrumView {
public:
    void paint(juce::Graphics& g,VocalSpectrum& source) {
        const double now=juce::Time::getMillisecondCounterHiRes();
        if(snapshot.epoch!=source.epoch()) { snapshot.sequence=0;lastArrival=0; }
        if(source.copyLatest(incoming)&&incoming.sequence!=snapshot.sequence) { snapshot=incoming;lastArrival=incoming.completedAt; }
        const bool waiting=snapshot.sequence==0,stale=!waiting&&now-lastArrival>750;
        const juce::Rectangle<float> panel{775,120,327,145},plot{809,160,282,78};
        g.setColour(juce::Colour(0xff071319));g.fillRoundedRectangle(panel,5);
        g.setColour(juce::Colour(0xffa6c4cc));g.setFont(13.f);
        g.drawText("VOCAL SPECTRUM",785,125,307,17,juce::Justification::centredLeft);
        g.setFont(10.f);g.drawText("POST-FX / MAX L/R / dBFS (peak/bin)",785,143,307,13,juce::Justification::centredLeft);
        const double top=waiting?20000.:std::min(20000.,snapshot.sampleRate*.5);
        const auto x=[&](double hz) { return plot.getX()+float(std::log(hz/20)/std::log(top/20))*plot.getWidth(); };
        const auto y=[&](float db) {return plot.getBottom()-juce::jlimit(0.f,1.f,(db+90)/90)*plot.getHeight();};
        for(int db=0;db>=-90;db-=30) {
            g.setColour(juce::Colour(0xff20353d));g.drawHorizontalLine(int(y(float(db))),plot.getX(),plot.getRight());
            g.setColour(juce::Colour(0xff91a5ab));g.drawText(juce::String(db),779,int(y(float(db)))-6,26,12,juce::Justification::centredRight);
        }
        for(double hz:{20.,100.,1000.,10000.}) if(hz<=top) {
            g.setColour(juce::Colour(0xff20353d));g.drawVerticalLine(int(x(hz)),plot.getY(),plot.getBottom());
            g.setColour(juce::Colour(0xff91a5ab));g.drawText(hz>=1000?juce::String(int(hz/1000))+"k":juce::String(int(hz)),int(x(hz))-12,239,28,12,juce::Justification::centred);
        }
        g.setColour(juce::Colour(0xff91a5ab));g.drawText("Hz",1080,239,18,12,juce::Justification::centredRight);
        if(!waiting&&!stale&&!snapshot.silent&&top>20) {
            juce::Path curve;bool first=true;
            for(int i=1;i<VocalSpectrum::bins;++i) {
                const double hz=i*snapshot.sampleRate/VocalSpectrum::size;
                if(hz<20)continue;if(hz>top)break;
                if(first){curve.startNewSubPath(x(hz),y(snapshot.db[i]));first=false;}
                else curve.lineTo(x(hz),y(snapshot.db[i]));
            }
            g.setColour(juce::Colour(0xff62d9de));g.strokePath(curve,juce::PathStrokeType(1.3f));
        }
        juce::String status;
        if(!source.operational())status="ANALYSER UNAVAILABLE";
        else if(waiting)status="WAITING FOR FULL AUDIO WINDOW";
        else if(stale)status="STALE - NO NEW ANALYSIS";
        else if(snapshot.silent)status="SILENCE";
        else status=juce::String(snapshot.sampleRate/1000.,1)+" kHz / 8192 FFT";
        if(!waiting&&snapshot.droppedFrames)status+=" / DROPPED DATA";
        g.setColour(juce::Colour(stale?0xffe4b06a:0xffa6c4cc));g.setFont(10.f);
        g.drawText(status,785,252,307,12,juce::Justification::centredLeft);
    }
private:
    VocalSpectrum::Snapshot snapshot,incoming;
    double lastArrival=0;
};
}
