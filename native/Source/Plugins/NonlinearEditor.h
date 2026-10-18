#pragma once
#include "NonlinearProcessors.h"
#include <ApprovedArt.h>
#include <complex>
#include "../UI/PhysicalValueSlider.h"
#include "../UI/FoundationMaterialLook.h"
#include "../UI/SegmentedMeter.h"
namespace hem {
// Original pixels are retained. Only live control disks and aperture interiors
// are overlaid. Every hit target is attached to a real processor parameter.
class OriginalNonlinearEditor final:public juce::AudioProcessorEditor,private juce::Timer {
    struct Look:juce::LookAndFeel_V4 {
        juce::Image selectorArt;
        SuiteMaterialLook material;
        void drawRotarySlider(juce::Graphics& g,int x,int y,int w,int h,float position,float,float,juce::Slider& slider) override {
            slider.getProperties().set("preserveAuthoredDialRing",true);
            slider.getProperties().set("bodyRadius",std::min(w,h)*.5f);
            slider.getProperties().set("bodyX",x+w*.5f);slider.getProperties().set("bodyY",y+h*.5f);
            material.drawRotarySlider(g,x,y,w,h,position,juce::MathConstants<float>::pi*1.25f,juce::MathConstants<float>::pi*2.75f,slider);
        }
        void drawToggleButton(juce::Graphics& g,juce::ToggleButton& b,bool hover,bool down) override {if(hover||down||b.hasKeyboardFocus(false)){g.setColour(juce::Colours::white.withAlpha(.6f));g.drawRoundedRectangle(b.getLocalBounds().toFloat().reduced(1),3,1);}}
        void drawComboBox(juce::Graphics& g,int w,int h,bool,int,int,int,int,juce::ComboBox& c) override {
            // One opaque full-width live control replaces the baked field.
            // Reuse the original arrow cell once; never draw a second glyph.
            g.fillAll(juce::Colour(0xff03090c));const int arrowWidth=juce::roundToInt(w*47.f/297.f);
            g.drawImage(selectorArt,w-arrowWidth,0,arrowWidth,h,1545,c.getComponentID()=="type"?618:744,47,44);
        }
        void positionComboBoxText(juce::ComboBox& c,juce::Label& label) override {const int arrowWidth=juce::roundToInt(c.getWidth()*47.f/297.f);label.setBounds(6,0,c.getWidth()-arrowWidth-12,c.getHeight());label.setFont(getComboBoxFont(c));}
    } look;
    struct Control {ui::PhysicalValueSlider slider;juce::ToggleButton button;juce::ComboBox choice;juce::Rectangle<float> hit,readout;float cx=0,cy=0,radius=0;int mode=0;const ParameterSpec* spec=nullptr;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> sa;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> ba;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> ca;};
public:
    OriginalNonlinearEditor(ModuleProcessor& p,int which,NonlinearTelemetry& t):AudioProcessorEditor(p),processor(p),kind(which),telemetry(t){
        const void* data=kind==0?ApprovedArt::approved_26_png:kind==1?ApprovedArt::approved_27_png:ApprovedArt::approved_28_png;
        const int bytes=kind==0?ApprovedArt::approved_26_pngSize:kind==1?ApprovedArt::approved_27_pngSize:ApprovedArt::approved_28_pngSize;
        art=juce::ImageCache::getFromMemory(data,bytes);look.selectorArt=art;setLookAndFeel(&look);
        if(kind==0){knob(0,472,711,80,{389,824,171,32});knob(1,784,711,80,{699,824,172,32});knob(2,1096,711,80,{1014,824,168,32});choice(3,{1295,618,297,44},{"Soft symmetric","Soft asymmetric"});choice(4,{1295,744,297,44},{"1x","4x"});button(5,{1280,823,138,38});button(6,{1456,823,144,38});plot={300,259,1090,242};meters={juce::Rectangle<float>(94,211,83,299),{1496,211,80,299}};}
        else if(kind==1){const float xs[]{141,332,678,860,1200,1387};const float bs[]{438,960,1486};
            for(int j=0;j<3;++j){knob(4*j,xs[j*2],562,51,{xs[j*2]-56,631,112,23});knob(4*j+1,xs[j*2+1],562,51,{xs[j*2+1]-56,631,112,23});button(4*j+2,{bs[j],480,98,32});button(4*j+3,{bs[j],537,98,30});}
            knob(12,645,817,48,{582,873,123,23});knob(13,1024,817,48,{965,873,123,23});knob(14,1378,802,51,{1321,873,113,23});button(15,{1495,789,108,36});plot={260,199,1179,160};meters={juce::Rectangle<float>(75,174,92,194),{1504,174,92,194}};
        }else{const float xs[]{429,691,953,1214,1481};for(int j=0;j<5;++j)knob(j,xs[j],691,76,{xs[j]-69,795,138,25});plot={341,265,1038,223};meters={juce::Rectangle<float>(96,198,88,313),{1487,198,88,313}};}
        setResizable(true,true);setResizeLimits(1000,563,3344,1882);getConstrainer()->setFixedAspectRatio(1672.0/941.0);setSize(1280,720);PluginTelemetry::Levels initial; if(auto* source=processor.pluginTelemetry();source&&source->read(initial)){lastEpoch=initial.epoch;lastBlocks=initial.blocks;}startTimerHz(25);
    }
    ~OriginalNonlinearEditor() override {stopTimer();controls.clear();setLookAndFeel(nullptr);}
    void resized() override {const float sx=getWidth()/1672.f,sy=getHeight()/941.f;for(auto& c:controls){const auto b=juce::Rectangle<float>(c->hit.getX()*sx,c->hit.getY()*sy,c->hit.getWidth()*sx,c->hit.getHeight()*sy).toNearestInt();if(c->mode==0)c->slider.setBounds(b);else if(c->mode==1)c->button.setBounds(b);else c->choice.setBounds(b);}}
    void paint(juce::Graphics& g) override {
        g.fillAll(juce::Colours::black);g.addTransform(juce::AffineTransform::scale(getWidth()/1672.f,getHeight()/941.f));g.drawImageAt(art,0,0);
        for(auto& c:controls){const float v=processor.state.getRawParameterValue(c->spec->id)->load();
            if(c->mode==0){g.setColour(juce::Colour(0xff02090e));g.fillRect(c->readout);g.setColour(juce::Colours::lightcyan);g.setFont(c->readout.getHeight()*.8f);
                const float shown=kind==1&&juce::String(c->spec->unit)=="Hz"?ExciterCore::effectiveHz(v,processor.actualRate.load()):v;
                g.drawText(juce::String(shown,juce::String(c->spec->unit)=="dB"?1:0)+" "+c->spec->unit,c->readout,juce::Justification::centred);
            }else if(c->mode==1&&v>.5f){g.setColour(juce::Colours::cyan);g.drawRoundedRectangle(c->hit.expanded(2),4,2);g.fillEllipse(c->hit.getX()+4,c->hit.getCentreY()-2,4,4);}
        }
        // Erase illustrative traces before checking readiness: idle/release must never show fake audio.
        g.setColour(juce::Colour(0xff02090e));g.fillRect(plot);
        const auto tracePlot=plot.withTrimmedTop(20).withTrimmedBottom(20);
        g.setColour(juce::Colour(0xff15313b));
        for(int i=0;i<=8;++i)g.drawVerticalLine(juce::roundToInt(plot.getX()+plot.getWidth()*i/8),tracePlot.getY(),tracePlot.getBottom());
        for(int i=0;i<=4;++i)g.drawHorizontalLine(juce::roundToInt(tracePlot.getY()+tracePlot.getHeight()*i/4),plot.getX(),plot.getRight());
        const auto s=telemetry.snapshot();PluginTelemetry::Levels levels;
        auto* source=processor.pluginTelemetry();const bool read=source&&source->read(levels);
        const auto meter=processor.meterSnapshot();const auto now=juce::Time::getMillisecondCounterHiRes();
        const bool eligible=read&&levels.valid&&levels.blocks&&processor.processingReady()&&meter.isPrepared&&meter.hasProcessedAudio;
        if(read&&(levels.epoch!=lastEpoch||levels.blocks!=lastBlocks)){lastEpoch=levels.epoch;lastBlocks=levels.blocks;lastAudioAt=eligible?now:-1;}
        if(!eligible)lastAudioAt=-1;
        const bool live=eligible&&lastAudioAt>=0&&now>=lastAudioAt&&now-lastAudioAt<=750;
        for(size_t j=0;j<2;++j){const auto rect=meters[j];g.setColour(juce::Colour(0xff02090e));g.fillRect(rect);
            const auto area=rect.reduced(9,27);const float width=(area.getWidth()-8)*.5f;
            for(int ch=0;ch<2;++ch){const int index=int(j)*2+ch;auto lane=area.withX(area.getX()+ch*(width+8)).withWidth(width);
                ui::paintSegmentedMeter(g,lane,ui::meterAmplitudePosition(levels.peak[index]),live);
                ui::paintMeterMark(g,lane,ui::meterAmplitudePosition(levels.rms[index]),juce::Colours::white,1.5f,ui::MeterMarkRole::rms,live);}
            g.setColour(juce::Colours::lightcyan);g.setFont(12);g.drawText("L       R",rect.withHeight(20),juce::Justification::centred);
            g.drawText("-60..0 dBFS",rect.withY(rect.getBottom()-23).withHeight(20),juce::Justification::centred);}
        if(kind==2){g.setColour(juce::Colour(0xff02090e));g.fillRect(juce::Rectangle<float>(657,859,366,34));g.setColour(juce::Colours::lightcyan);g.setFont(21);g.drawText("OVERSAMPLING / FIXED 4x",657,859,366,34,juce::Justification::centred);}
        g.setColour(juce::Colours::lightcyan);g.setFont(13);g.drawText("METERS: L/R SAMPLE PEAK / WHITE = 300 ms RMS",plot.withY(plot.getBottom()-17).withHeight(17),juce::Justification::bottomRight);
        if(!live||!s.ready){g.setColour(juce::Colours::lightcyan);g.setFont(18);g.drawText(!eligible?"WAITING FOR AUDIO":!live?"STALE / AUDIO STOPPED":"WAITING FOR 512 OUTPUT SAMPLES",plot,juce::Justification::centred);return;}
        juce::Path curve;const double sr=processor.actualRate.load();
        if(kind==1){ // GUI-only Hann-windowed 512-point FFT of actual output.
            std::array<std::complex<double>,512> bins{};
            for(size_t i=0;i<512;++i)bins[i]=s.samples[i]*(.5-.5*std::cos(2*pi*double(i)/511));
            for(size_t i=1,j=0;i<512;++i){size_t bit=256;for(;j&bit;bit>>=1)j^=bit;j^=bit;if(i<j)std::swap(bins[i],bins[j]);}
            for(size_t length=2;length<=512;length<<=1){const std::complex<double> step(std::cos(-2*pi/length),std::sin(-2*pi/length));
                for(size_t start=0;start<512;start+=length){std::complex<double> w(1,0);for(size_t j=0;j<length/2;++j){const auto u=bins[start+j],v=bins[start+j+length/2]*w;bins[start+j]=u+v;bins[start+j+length/2]=u-v;w*=step;}}}
            for(int bin=1;bin<=256;++bin){const float db=decibels(float(std::abs(bins[size_t(bin)])/(bin==256?255.5:127.75)));
                const float x=plot.getX()+plot.getWidth()*float(bin-1)/255,y=tracePlot.getBottom()-tracePlot.getHeight()*juce::jlimit(0.f,1.f,(db+96)/96);if(bin==1)curve.startNewSubPath(x,y);else curve.lineTo(x,y);}
            if(auto* e=dynamic_cast<ExciterProcessor*>(&processor)){const float xs[]{489,1009,1543};for(size_t j=0;j<3;++j)if(e->activity(j)>1.e-6f){g.setColour(juce::Colours::cyan.withAlpha(juce::jlimit(.1f,1.f,(decibels(e->activity(j))+120)/90)));g.fillEllipse(xs[j]-8,635,16,16);}}
            g.setFont(12);g.setColour(juce::Colours::lightcyan);g.drawText("OUTPUT L / 512 HANN FFT / SINE-PEAK dBFS -96..0 / LINEAR Hz / "+juce::String(sr/512,1)+".."+juce::String(sr/2,0)+" Hz",plot.withHeight(17),juce::Justification::topRight);
        }else{for(size_t i=0;i<512;++i){const float x=plot.getX()+plot.getWidth()*float(i)/511,y=tracePlot.getCentreY()-juce::jlimit(-1.f,1.f,s.samples[i])*tracePlot.getHeight()*.5f;if(i==0)curve.startNewSubPath(x,y);else curve.lineTo(x,y);}
            g.setFont(12);g.setColour(juce::Colours::lightcyan);g.drawText("LAST OUTPUT / L / +/-1 / "+juce::String(512/sr*(kind==0?1000:1),kind==0?2:4)+(kind==0?" ms":" s"),plot.withHeight(17),juce::Justification::topRight);}
        {const juce::Graphics::ScopedSaveState clip(g);g.reduceClipRegion(tracePlot.toNearestInt());g.setColour(juce::Colours::cyan);g.strokePath(curve,juce::PathStrokeType(1.5f));}
    }
private:
    Control& add(int index,int mode,juce::Rectangle<float> hit){auto c=std::make_unique<Control>();c->spec=&processor.parameterSpecs()[size_t(index)];c->mode=mode;c->hit=hit;controls.push_back(std::move(c));return *controls.back();}
    void knob(int index,float x,float y,float r,juce::Rectangle<float> read){auto& c=add(index,0,{x-r,y-r,r*2,r*2});c.cx=x;c.cy=y;c.radius=r;c.readout=read;
        c.slider.setTextValueSuffix(c.spec->unit);c.slider.setComponentID(c.spec->id);c.slider.setName(c.spec->name);c.slider.setTooltip(juce::String(c.spec->name)+" / "+c.spec->unit);c.slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);c.slider.setTextBoxStyle(juce::Slider::NoTextBox,false,0,0);c.slider.setDoubleClickReturnValue(true,c.spec->initial);addAndMakeVisible(c.slider);c.sa=std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(processor.state,c.spec->id,c.slider);}
    void button(int index,juce::Rectangle<float> hit){auto& c=add(index,1,hit);c.button.setComponentID(c.spec->id);c.button.setName(c.spec->name);c.button.setTooltip(c.spec->name);addAndMakeVisible(c.button);c.ba=std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(processor.state,c.spec->id,c.button);}
    void choice(int index,juce::Rectangle<float> hit,const juce::StringArray& labels){auto& c=add(index,2,hit);c.choice.setComponentID(c.spec->id);c.choice.setName(c.spec->name);c.choice.addItemList(labels,1);addAndMakeVisible(c.choice);c.ca=std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(processor.state,c.spec->id,c.choice);}
    void timerCallback() override {repaint();}
    unsigned lastEpoch=0,lastBlocks=0;double lastAudioAt=-1;
    ModuleProcessor& processor;int kind;NonlinearTelemetry& telemetry;juce::Image art;std::vector<std::unique_ptr<Control>> controls;juce::Rectangle<float> plot;std::array<juce::Rectangle<float>,2> meters;
};
}
