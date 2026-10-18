#pragma once
#include "../UI/ButtonLight.h"
#include "ReferenceMatchProcessor.h"
#include <ApprovedArt.h>
namespace hem {
// Immutable approved15 chassis; redraw only glass apertures, knob disks and
// live indicators. No standalone window or audition engine is created here.
class ReferenceMatchEditor final:public juce::AudioProcessorEditor,private juce::Timer {
    struct Look:juce::LookAndFeel_V4 {
        juce::Slider::SliderLayout getSliderLayout(juce::Slider& s)override {juce::Slider::SliderLayout l;auto r=s.getLocalBounds();l.textBoxBounds=r.removeFromBottom(int(r.getHeight()*.20));l.sliderBounds=r.withHeight(int(s.getHeight()*150.f/210.f));return l;}
        void drawRotarySlider(juce::Graphics& g,int x,int y,int w,int h,float p,float start,float end,juce::Slider&)override{
            auto b=juce::Rectangle<float>(float(x),float(y),float(w),float(h));float diameter=std::min(b.getWidth(),b.getHeight())*.99f;auto disk=juce::Rectangle<float>(diameter,diameter).withCentre(b.getCentre());
            g.setGradientFill(juce::ColourGradient(juce::Colour(0xff4c5055),disk.getTopLeft(),juce::Colour(0xff080a0d),disk.getBottomRight(),false));g.fillEllipse(disk);g.setColour(juce::Colour(0xffa6acaf));g.drawEllipse(disk,1.5f);float angle=start+(end-start)*p;auto center=disk.getCentre();g.setColour(juce::Colour(0xffffaa43));g.drawLine(center.x+std::sin(angle)*diameter*.27f,center.y-std::cos(angle)*diameter*.27f,center.x+std::sin(angle)*diameter*.44f,center.y-std::cos(angle)*diameter*.44f,3);
        }
        void drawButtonBackground(juce::Graphics& g,juce::Button& b,const juce::Colour&,bool hover,bool down)override{hem::ui::drawButtonLight(g,b.getLocalBounds().toFloat().reduced(2),b.getToggleState(),down,hover,b.isEnabled(),2);if(down||b.getToggleState()){g.setColour(juce::Colour(0xffff9b34));g.fillRect(8,3,std::max(1,b.getWidth()-16),3);}}
        void drawButtonText(juce::Graphics&,juce::TextButton&,bool,bool)override{}
    } look;
    ReferenceMatchProcessor& processor;
    ReferenceMatchProcessor::EditorActions actions;
    juce::Image artwork;
    std::array<juce::Slider,3> knobs;
    std::array<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>,3> attachments;
    juce::TextButton load,capture,source,reference,match,revert,bypass;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> bypassAttachment;
    ReferenceMatchProcessor::Snapshot snapshot;
    ReferenceMatchProcessor::Levels levels;
    uint32_t lastSerial=0;double lastAudio=0;bool witnessed=false;
    juce::String status="NO PROFILE",identity="NO REFERENCE";
    juce::Rectangle<int> scaled(juce::Rectangle<float> r)const{return {int(r.getX()*getWidth()/1672),int(r.getY()*getHeight()/941),int(r.getWidth()*getWidth()/1672),int(r.getHeight()*getHeight()/941)};}
public:
    explicit ReferenceMatchEditor(ReferenceMatchProcessor& p):juce::AudioProcessorEditor(p),processor(p),actions(p.editorActions){
        artwork=juce::ImageCache::getFromMemory(ApprovedArt::approved_15_png,ApprovedArt::approved_15_pngSize);setLookAndFeel(&look);setWantsKeyboardFocus(true);
        const char* ids[]={"matchAmount","smoothing","output"};for(size_t i=0;i<3;++i){auto& s=knobs[i];s.setName(ids[i]);s.setComponentID(ids[i]);s.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);s.setTextBoxStyle(juce::Slider::TextBoxBelow,false,150,36);s.setRotaryParameters(juce::MathConstants<float>::pi*1.25f,juce::MathConstants<float>::pi*2.75f,true);s.setColour(juce::Slider::textBoxBackgroundColourId,juce::Colour(0xff02080b));s.setColour(juce::Slider::textBoxTextColourId,juce::Colour(0xffffb565));s.setColour(juce::Slider::textBoxOutlineColourId,juce::Colours::transparentBlack);s.setWantsKeyboardFocus(true);addAndMakeVisible(s);attachments[i]=std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(p.state,ids[i],s);}
        knobs[0].setTooltip("Scale corrective section gains in dB; worker-refitted, not wet/dry.");knobs[1].setTextValueSuffix(" oct");knobs[2].setTextValueSuffix(" dB");
        auto button=[&](juce::TextButton& b,const char* id){b.setName(id);b.setComponentID(id);b.setTooltip(id);b.setWantsKeyboardFocus(true);addAndMakeVisible(b);};button(load,"loadReference");button(capture,"captureSource");button(source,"auditionSource");button(reference,"auditionReference");button(match,"match");button(revert,"revert");button(bypass,"bypass");bypass.setClickingTogglesState(true);bypassAttachment=std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(p.state,"bypass",bypass);
        load.setEnabled(bool(actions.loadReference));match.setEnabled(bool(actions.match));revert.setEnabled(bool(actions.revert));source.setEnabled(bool(actions.audition));reference.setEnabled(bool(actions.audition));
        load.onClick=[this]{if(actions.loadReference)actions.loadReference();};match.onClick=[this]{if(actions.match)actions.match();};revert.onClick=[this]{if(actions.revert)actions.revert();};source.onClick=[this]{if(actions.audition)actions.audition(false);};reference.onClick=[this]{if(actions.audition)actions.audition(true);};capture.setEnabled(bool(actions.captureSource));capture.onClick=[this]{if(actions.captureSource)actions.captureSource();};
        load.setTooltip("Stop playback, then select WAV, AIFF or FLAC; analyzes the first 30 seconds.");capture.setTooltip("Play audio on this track, then capture up to 10 seconds. Click again to finish early.");match.setTooltip("Load a reference and capture source audio, then stop playback and Match.");revert.setTooltip("Stop playback to restore the previous committed match.");bypass.setTooltip("Bypass corrective EQ and output gain; without a match at unity output, sound is already unchanged.");
        setResizable(true,false);setResizeLimits(836,471,3344,1882);getConstrainer()->setFixedAspectRatio(1672./941.);setSize(1280,720);lastSerial=processor.levels().serial;startTimerHz(10);
    }
    ~ReferenceMatchEditor()override{stopTimer();if(actions.audition)actions.audition(false);bypassAttachment.reset();for(auto& a:attachments)a.reset();setLookAndFeel(nullptr);}
    void setCloseRequest(std::function<void()> close){actions.back=std::move(close);}
    bool keyPressed(const juce::KeyPress& k)override{if(k==juce::KeyPress::escapeKey){if(actions.audition)actions.audition(false);if(actions.back)actions.back();return true;}return false;}
    void resized()override{for(int i=0;i<3;++i)knobs[size_t(i)].setBounds(scaled({float(446+i*291),658,166,210}));load.setBounds(scaled({260,239,190,41}));capture.setBounds(scaled({260,421,190,43}));source.setBounds(scaled({1287,673,150,47}));reference.setBounds(scaled({1456,673,152,47}));match.setBounds(scaled({1276,772,154,47}));revert.setBounds(scaled({1460,772,157,47}));bypass.setBounds(scaled({1292,842,307,41}));}
    void paint(juce::Graphics& original)override{
        original.drawImage(artwork,getLocalBounds().toFloat());juce::Graphics::ScopedSaveState guard(original);original.addTransform(juce::AffineTransform::scale(getWidth()/1672.f,getHeight()/941.f));auto& g=original;
        g.setColour(juce::Colour(0xff030c11));g.fillRect(541,234,862,249);g.fillRect(65,183,112,361);g.fillRect(1489,183,116,361);
        g.setColour(juce::Colour(0xff15303b));for(int i=1;i<12;++i)g.drawVerticalLine(541+i*72,234,483);for(int i=1;i<6;++i)g.drawHorizontalLine(234+i*41,541,1403);
        if(snapshot.committed){auto plot=[&](const reference_match::Profile& p,juce::Colour color){juce::Path path;for(size_t i=0;i<reference_match::bins;++i){float x=541+862*float(std::log(p.hz[i]/30.0)/std::log(20000.0/30.0)),db=float(10*std::log10(std::max(1e-12,p.power[i]/p.meanSquare)));float y=483-249*juce::jlimit(0.f,1.f,(db+60)/60);if(i==0)path.startNewSubPath(x,y);else path.lineTo(x,y);}g.setColour(color);g.strokePath(path,juce::PathStrokeType(2));};plot(snapshot.source,juce::Colour(0xff27dcf4));plot(snapshot.reference,juce::Colour(0xffff9936));}
        const bool live=witnessed&&juce::Time::getMillisecondCounterHiRes()-lastAudio<750;for(int c=0;c<4;++c){float amplitude=c<2?levels.input[size_t(c)]:levels.output[size_t(c-2)];int x=c==0?75:c==1?139:c==2?1503:1567;float db=20*std::log10(std::max(1e-6f,amplitude));int count=live?int(juce::jlimit(0.f,1.f,(db+60)/60)*30):0;for(int i=0;i<30;++i){g.setColour(i<count?(i>26?juce::Colours::red:i>20?juce::Colours::orange:juce::Colour(0xff21e25b)):juce::Colour(0xff101c22));g.fillRect(x,532-i*11,31,8);}}
        auto field=[&](juce::Rectangle<int> r,const juce::String& text){g.setColour(juce::Colour(0xff030b10));g.fillRect(r);g.setColour(juce::Colour(0xffb5e0e8));g.setFont(16);g.drawFittedText(text,r.reduced(4),juce::Justification::centred,2);};field({258,308,193,38},identity);field({258,525,193,37},status);g.setColour(juce::Colour(0xffbed4dd));g.setFont(13);g.drawText("NORMALIZED POWER / -60 TO 0 dB",548,239,330,20,juce::Justification::left);
    }
private:
    void timerCallback()override{levels=processor.levels();if(levels.serial!=lastSerial){lastSerial=levels.serial;lastAudio=juce::Time::getMillisecondCounterHiRes();witnessed=true;}ReferenceMatchProcessor::Snapshot next;auto result=processor.captureSnapshot(next);if(result.wasOk()){snapshot=std::move(next);status=snapshot.committed?"MATCH READY":"NO MATCH";identity=snapshot.referenceIdentity.path.isEmpty()?"NO REFERENCE LOADED":juce::File(snapshot.referenceIdentity.path).getFileName();}else status="FIT PENDING";if(actions.status){auto hostStatus=actions.status();if(hostStatus.isNotEmpty())status=hostStatus;}auto captureStatus=processor.captureProgressText();if(captureStatus.isNotEmpty())status=captureStatus;if(bypass.getToggleState())status="BYPASSED / "+status;if(actions.referenceLabel){auto draftLabel=actions.referenceLabel();if(draftLabel.isNotEmpty())identity=draftLabel;}if(actions.sourceCaptured){reference_match::Profile p;if(processor.takeCapturedProfile(p)){status=p.status==reference_match::Status::ready?"SOURCE CAPTURED":"CAPTURE INVALID";actions.sourceCaptured(p);}}repaint();}
};
}
