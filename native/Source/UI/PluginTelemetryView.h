#pragma once
#include "../Systems/PluginTelemetry.h"
#include "HardwarePanel.h"
namespace hem::ui {
class PluginTelemetryView {
public:
    void poll(PluginTelemetry* source){
        available=source&&source->analysis.operational();if(!source){live=false;return;}
        if(frame.epoch!=source->analysis.epoch())frame.sequence=0;
        levelRead=source->read(levels);source->analysis.copyLatest(frame);
        const double t=source->analysis.lastConsumedAt();fresh=t>0&&juce::Time::getMillisecondCounterHiRes()-t<750;
        live=available&&levelRead&&levels.valid&&fresh;
    }
    bool isLive() const noexcept{return live;}
    juce::String statusText() const{return state();}
    void level(juce::Graphics& g,juce::Rectangle<float> a,bool output){
        hardwareGlass(g,a);const int offset=output?2:0;const float fs=a.getWidth()>125?19.f:17.f;
        label(g,a.withTrimmedTop(8).withHeight(23),output?"OUTPUT":"INPUT",fs,true);
        label(g,a.withY(a.getY()+30).withHeight(20),"dBFS / SAMPLE PK",fs*.75f);
        auto plot=juce::Rectangle<float>(a.getX()+13,a.getY()+60,a.getWidth()-49,std::max(35.f,a.getHeight()-164));
        const float gap=5,laneWidth=(plot.getWidth()-gap)*.5f;
        for(int ch=0;ch<2;++ch){auto lane=plot.withX(plot.getX()+ch*(laneWidth+gap)).withWidth(laneWidth);
            const float pk=levels.peak[offset+ch]>0?20*std::log10(levels.peak[offset+ch]):-160;
            for(int i=0;i<60;++i){const float threshold=-60+float(i),mid=threshold+.5f;
                const auto colour=mid>-3?juce::Colour(0xffff4938):mid>=-18?juce::Colour(0xffffb032):juce::Colour(0xff0cdef2);
                const float step=lane.getHeight()/60;const auto seg=juce::Rectangle<float>(lane.getX(),lane.getBottom()-(i+1)*step,lane.getWidth(),std::max(.6f,step*.72f));
                g.setColour(colour.withAlpha(live&&pk>threshold?1.f:.075f));g.fillRect(seg);
            }
            if(live){const float y=lane.getBottom()-position(levels.rms[offset+ch])*lane.getHeight();g.setColour(juce::Colour(0xffe0f9ff));g.drawLine(lane.getX(),y,lane.getRight(),y,1.8f);}
        }
        for(int tick:{0,-6,-12,-24,-36,-48,-60}){const float y=plot.getY()+(-tick/60.f)*plot.getHeight();label(g,{plot.getRight()+2,y-8,30,17},juce::String(tick),fs*.72f);}
        label(g,{plot.getX(),plot.getBottom()+2,plot.getWidth(),20},"L        R",fs*.8f);
        auto read=a.reduced(9,0).withY(a.getBottom()-78).withHeight(49);
        hardwareReadout(g,read,live?"PK "+db(std::max(levels.peak[offset],levels.peak[offset+1]))+"\nRMS "+db(std::max(levels.rms[offset],levels.rms[offset+1])):state(),fs*.85f);
        label(g,a.withY(a.getBottom()-26).withHeight(18),"BLOCK / RMS 300ms",fs*.68f);
    }
    void waveform(juce::Graphics& g,juce::Rectangle<float> a,bool phase=false,bool gainCaption=false){
        hardwareGlass(g,a);const float fs=a.getWidth()>650?21.f:16.f;auto plot=a.reduced(36,45).withTrimmedBottom(18);
        if(phase){const float side=std::min(plot.getWidth(),plot.getHeight());plot=plot.withSizeKeepingCentre(side,side);}grid(g,plot);
        label(g,a.withY(a.getY()+8).withHeight(30),phase?"POST-FX GONIOMETER / M vertical, S horizontal":"POST-FX WAVEFORM / L CYAN / R AMBER",fs,true);
        const bool ready=live&&frame.sequence;
        if(ready&&!frame.silent){const juce::Graphics::ScopedSaveState clip(g);g.reduceClipRegion(plot.toNearestInt());
            if(phase){g.setColour(juce::Colour(0xff23eaff).withAlpha(.85f));for(int i=0;i<1024;++i){float l=frame.phaseL[i],r=frame.phaseR[i];float x=plot.getCentreX()+juce::jlimit(-1.f,1.f,(l-r)*.5f)*plot.getWidth()*.5f;float y=plot.getCentreY()-juce::jlimit(-1.f,1.f,(l+r)*.5f)*plot.getHeight()*.5f;g.fillEllipse(x-1,y-1,2.2f,2.2f);}}
            else for(int ch=0;ch<2;++ch){juce::Path envelope;const auto colour=juce::Colour(ch?0xffffac32:0xff05dfff);
                auto x=[&](int i){return plot.getX()+float(i)/255*plot.getWidth();};
                auto y=[&](float v){return plot.getCentreY()-juce::jlimit(-1.f,1.f,v)*plot.getHeight()*.5f;};
                for(int i=0;i<256;++i){const auto& p=frame.wave[i];if(i==0)envelope.startNewSubPath(x(i),y(ch?p.hiR:p.hiL));else envelope.lineTo(x(i),y(ch?p.hiR:p.hiL));}
                for(int i=255;i>=0;--i){const auto& p=frame.wave[i];envelope.lineTo(x(i),y(ch?p.loR:p.loL));}envelope.closeSubPath();
                g.setGradientFill(juce::ColourGradient(colour.withAlpha(.55f),plot.getCentreX(),plot.getCentreY(),colour.withAlpha(.06f),plot.getCentreX(),plot.getBottom(),false));g.fillPath(envelope);
                g.setColour(colour.withAlpha(.9f));g.strokePath(envelope,juce::PathStrokeType(1.1f));
            }
        }
        for(int i=0;i<3;++i)label(g,{plot.getX()-32,plot.getY()+i*plot.getHeight()*.5f-8,29,18},i==0?"+1":i==1?"0":"-1",fs*.75f);
        if(phase)for(int i=0;i<3;++i)label(g,{plot.getX()+i*plot.getWidth()*.5f-20,plot.getBottom()+2,40,18},i==0?"-1":i==1?"S 0":"+1",fs*.7f);
        if(!phase&&frame.sequence){label(g,{plot.getX(),plot.getBottom()+3,110,20},"-"+juce::String(double(PluginAnalysis::size)/frame.sampleRate,3)+" s",fs*.7f);label(g,{plot.getRight()-110,plot.getBottom()+3,110,20},"0 s / latest",fs*.7f);}
        juce::String status=!live?state():!frame.sequence?"WAITING FOR FULL AUDIO WINDOW":frame.silent?"SILENCE":phase?"1024 REAL PAIRED SAMPLES / +/-1 FS":juce::String(1000.*PluginAnalysis::size/frame.sampleRate,1)+" ms / 32-SAMPLE MIN-MAX / +/-1 FS";
        if(gainCaption)status+=" / GAIN PRE-OUTPUT";if(ready&&frame.overRange)status+=" / OVER 1 FS";if(frame.sequence&&frame.droppedFrames)status+=" / DROPPED DATA";
        label(g,a.withY(a.getBottom()-27).withHeight(22),status,fs*.7f);
    }
    void gain(juce::Graphics& g,juce::Rectangle<float> a,int branch){
        hardwareGlass(g,a,false);label(g,a.withY(a.getY()+5).withHeight(23),branch?"SUSTAIN / SIGNED BLOCK ABS-MAX dB":"ATTACK / SIGNED BLOCK ABS-MAX dB",17,true);
        const auto value=[](float v){return(v>0?juce::String("+"):juce::String())+juce::String(v,2);};
        label(g,a.withTrimmedTop(28).withTrimmedBottom(4),live?"preMix "+value(levels.gains[branch])+"   /   postMix "+value(levels.gains[branch+2]):state(),19);
    }
    void spectrum(juce::Graphics& g,juce::Rectangle<float> a,const juce::String& title="POST-FX STEREO SPECTRUM"){
        hardwareGlass(g,a);const float fs=a.getWidth()>500?20.f:13.f;auto plot=a.reduced(40,47).withTrimmedBottom(8);grid(g,plot);
        label(g,a.withY(a.getY()+7).withHeight(23),title,fs,true);label(g,a.withY(a.getY()+29).withHeight(17),"L CYAN / R AMBER / dBFS SINE-PEAK PER BIN",fs*.73f);
        const double top=frame.sequence?std::min(20000.,frame.sampleRate*.5):20000.;auto x=[&](double hz){return plot.getX()+float(std::log(hz/20)/std::log(top/20))*plot.getWidth();};
        auto y=[&](float db){return plot.getBottom()-juce::jlimit(0.f,1.f,(db+90)/90)*plot.getHeight();};
        for(int d=0;d>=-90;d-=15){g.setColour(juce::Colour(0xff17353f));g.drawHorizontalLine(int(y(float(d))),plot.getX(),plot.getRight());label(g,{a.getX()+3,y(float(d))-7,33,16},juce::String(d),fs*.65f);}
        for(double hz:{20.,50.,100.,500.,1000.,5000.,10000.,20000.})if(hz<=top){g.setColour(juce::Colour(0xff17353f));g.drawVerticalLine(int(x(hz)),plot.getY(),plot.getBottom());label(g,{x(hz)-20,plot.getBottom()+3,40,17},hz>=1000?juce::String(int(hz/1000))+"k":juce::String(int(hz)),fs*.65f);}
        if(live&&frame.sequence&&!frame.silent){const juce::Graphics::ScopedSaveState clip(g);g.reduceClipRegion(plot.toNearestInt());for(int ch=0;ch<2;++ch){juce::Path trace;bool first=true;float firstX=0,lastX=0;
            for(int i=1;i<PluginAnalysis::bins;++i){double hz=i*frame.sampleRate/PluginAnalysis::size;if(hz<20)continue;if(hz>top)break;lastX=x(hz);const float yy=y(ch?frame.dbR[i]:frame.dbL[i]);if(first){firstX=lastX;trace.startNewSubPath(lastX,yy);first=false;}else trace.lineTo(lastX,yy);}
            if(!first){auto fill=trace;fill.lineTo(lastX,plot.getBottom());fill.lineTo(firstX,plot.getBottom());fill.closeSubPath();const auto colour=juce::Colour(ch?0xffffa229:0xff08dfff);g.setGradientFill(juce::ColourGradient(colour.withAlpha(.48f),plot.getX(),plot.getY(),colour.withAlpha(.025f),plot.getX(),plot.getBottom(),false));g.fillPath(fill);g.setColour(colour.withAlpha(.95f));g.strokePath(trace,juce::PathStrokeType(1.5f));}
        }}
        label(g,a.withY(a.getBottom()-24).withHeight(19),!live?state():!frame.sequence?"WAITING FOR FULL WINDOW":frame.silent?"SILENCE":frame.droppedFrames?"8192 HANN FFT / Hz / DROPPED DATA":"8192 HANN FFT / LOG Hz",fs*.7f);
    }
    void reduction(juce::Graphics& g,juce::Rectangle<float> a,PluginTelemetry* source,const juce::String& title,bool auxiliary=false){
        hardwareGlass(g,a);label(g,a.withTrimmedTop(8).withHeight(28),title,19,true);auto plot=a.reduced(44,48);grid(g,plot);
        for(int db:{0,6,12,18,24}){const float y=plot.getY()+db*plot.getHeight()/24;g.setColour(juce::Colour(0xff17353f));g.drawHorizontalLine(int(y),plot.getX(),plot.getRight());label(g,{a.getX()+4,y-9,35,18},juce::String(db),14);}
        bool drawn=false;PluginTelemetry::MeterPoint last;
        if(live&&source&&source->readMeterPoint(levels.historySerial,last)&&levels.rate>0){juce::Path trace;float firstX=0,lastX=0;const unsigned count=unsigned(std::min<std::uint64_t>(ReductionHistory::capacity,levels.historySerial));
            for(unsigned age=count;age-->0;){PluginTelemetry::MeterPoint p;if(age==0)p=last;else if(!source->readMeterPoint(levels.historySerial-age,p))continue;float left=0,right=0;if(!ReductionHistory::interval(p,last.endSample,levels.rate,left,right))continue;
                const float value=auxiliary?p.auxiliary:p.value;if(!std::isfinite(value))continue;const float x=plot.getX()+left*plot.getWidth();lastX=plot.getX()+right*plot.getWidth();const float y=plot.getY()+juce::jlimit(0.f,24.f,value)*plot.getHeight()/24;
                if(!drawn){firstX=x;trace.startNewSubPath(x,y);drawn=true;}else trace.lineTo(x,y);trace.lineTo(lastX,y);}
            if(drawn){auto fill=trace;fill.lineTo(lastX,plot.getY());fill.lineTo(firstX,plot.getY());fill.closeSubPath();g.setGradientFill(juce::ColourGradient(juce::Colour(0xffffae36).withAlpha(.08f),plot.getX(),plot.getY(),juce::Colour(0xffffae36).withAlpha(.6f),plot.getX(),plot.getBottom(),false));g.fillPath(fill);g.setColour(juce::Colour(0xffffba50));g.strokePath(trace,juce::PathStrokeType(1.8f));}}
        label(g,a.withY(a.getBottom()-29).withHeight(22),!live?state():!drawn?"COLLECTING MEASURED BLOCKS":"4 s AXIS / TIME-BUCKET PEAKS / REDUCTION dB",14);
        label(g,{plot.getX(),plot.getBottom()+2,60,18},"-4 s",13);label(g,{plot.getRight()-60,plot.getBottom()+2,60,18},"NOW",13);
    }
private:
    static void label(juce::Graphics& g,juce::Rectangle<float> a,const juce::String& s,float size,bool header=false){g.setColour(juce::Colour(header?0xffffbf64:0xffb8dce5));g.setFont(hardwareFont(size,header));g.drawFittedText(s,a.toNearestInt(),juce::Justification::centred,2);}
    static void grid(juce::Graphics& g,juce::Rectangle<float> p){g.setColour(juce::Colour(0xff18333e));g.drawRect(p);g.drawHorizontalLine(int(p.getCentreY()),p.getX(),p.getRight());g.drawVerticalLine(int(p.getCentreX()),p.getY(),p.getBottom());}
    static juce::String db(float v){return v>0?juce::String(20*std::log10(v),1):"-inf";}
    static float position(float v){return v>0?juce::jlimit(0.f,1.f,(20*std::log10(v)+60)/60):0;}
    juce::String state() const{return !available?"UNAVAILABLE":!levelRead||!levels.blocks?"WAITING":!levels.valid?"INVALID":!fresh?"STALE":"WAITING";}
    PluginTelemetry::Levels levels;PluginAnalysis::Snapshot frame;bool available=false,levelRead=false,fresh=false,live=false;
};
}
