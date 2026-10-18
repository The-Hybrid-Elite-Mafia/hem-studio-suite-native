#pragma once
#include "Editors.h"
#include "ReverbProcessors.h"
#include "../UI/ApprovedFaceplates.h"
#include "../UI/SegmentedMeter.h"
namespace hem {
class ReverbEditor final:public ModuleEditor {
public:
 explicit ReverbEditor(ConvolutionReverbProcessor&p):ModuleEditor(p,false),processor(p),convolution(&p),art(ui::approvedFaceplate(19)){
    initialise();
    const char*ids[]{"predelay","irSize","lowCut","highCut","mix","output"};const float x[]{358,556,757,963,1166,1348};
    for(int i=0;i<6;++i)dial(ids[i],x[i],751,55,{x[i]-65,float(i==2||i==3?806:817),130,float(i==2||i==3?27:32)});
    toggle("lowCutOn","LOW CUT ON",{673,837,165,43});toggle("highCutOn","HIGH CUT ON",{878,837,165,43});
    toggle("soloWet","SOLO WET",{1468,700,161,54});toggle("bypass","BYPASS",{1467,780,163,54});
    button("loadIR","LOAD IR",{310,555,182,57},[this]{chooseIR();});finish();
 }
 explicit ReverbEditor(AlgorithmicReverbProcessor&p):ModuleEditor(p,false),processor(p),algorithmic(&p),art(ui::approvedFaceplate(20)){
    initialise();const char*ids[]{"size","decay","predelay","diffusion","modDepth","modRate","damping","mix","output"};const float x[]{125,303,483,661,839,1018,1194,1372,1549};
    for(int i=0;i<9;++i)dial(ids[i],x[i],679,52,{x[i]-60,744,120,35});
    choice(0,"HALL",{440,511,153,42});choice(1,"ROOM",{615,511,151,42});choice(2,"PLATE",{788,511,151,42});
    toggle("soloWet","SOLO WET",{1038,511,152,42});toggle("bypass","BYPASS",{1206,511,153,42});finish();
 }
 ~ReverbEditor()override{chooser.reset();controls.clear();setLookAndFeel(nullptr);}
 void resized()override{
    const float sx=getWidth()/1672.f,sy=getHeight()/941.f;
    auto scaled=[&](juce::Rectangle<float>b){return juce::Rectangle<float>(b.getX()*sx,b.getY()*sy,b.getWidth()*sx,b.getHeight()*sy).toNearestInt();};
    for(auto&c:controls){if(c->dial){auto b=c->body.getUnion(c->read);c->slider.setBounds(scaled(b));auto&props=c->slider.getProperties();props.set("bodyRadius",c->body.getWidth()*.5f*sx);props.set("bodyX",(c->body.getCentreX()-b.getX())*sx);props.set("bodyY",(c->body.getCentreY()-b.getY())*sy);props.set("readX",int((c->read.getX()-b.getX())*sx));props.set("readY",int((c->read.getY()-b.getY())*sy));props.set("readW",int(c->read.getWidth()*sx));props.set("readH",int(c->read.getHeight()*sy));c->slider.setTextBoxStyle(juce::Slider::TextBoxBelow,false,int(c->read.getWidth()*sx),int(c->read.getHeight()*sy));c->slider.resized();}else c->button.setBounds(scaled(c->body));}
 }
 void refreshParameterActivity()override{
    for(auto&c:controls){if(c->dial)c->slider.setEnabled(processor.parameterActive(c->id));if(c->choice>=0)c->button.setToggleState(int(std::lround(processor.state.getRawParameterValue("algorithm")->load()))==c->choice,juce::dontSendNotification);}
    if(algorithmic){const auto snap=processor.meterSnapshot();const auto sampleTime=algorithmic->tailSampleTime();if(sampleTime<lastSampleTime||!processor.processingReady())tailCount=tailCursor=0;lastSampleTime=sampleTime;if(snap.hasProcessedAudio&&snap.processedBlocks!=lastBlocks){lastBlocks=snap.processedBlocks;tailTime[tailCursor]=double(sampleTime)/processor.actualRate.load();tail[tailCursor]=algorithmic->wetTailDb();tailCursor=(tailCursor+1)%tail.size();tailCount=std::min(tailCount+1,tail.size());}}
    repaint();
 }
 void paint(juce::Graphics&g)override{
    g.fillAll(juce::Colours::black);if(art.isValid())g.drawImage(art,getLocalBounds().toFloat(),juce::RectanglePlacement::stretchToFit);
    const juce::Graphics::ScopedSaveState save(g);g.addTransform(juce::AffineTransform::scale(getWidth()/1672.f,getHeight()/941.f));
    paintMeters(g);if(convolution)paintIR(g);else paintTail(g);
 }
private:
 struct Control{juce::String id;bool dial=false;int choice=-1;juce::Rectangle<float>body,read;ui::PhysicalValueSlider slider;juce::ToggleButton button;std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>sa;std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>ba;};
 void initialise(){setLookAndFeel(&look);tail.fill(-120);}
 void finish(){setResizable(true,true);setResizeLimits(1000,563,1920,1080);if(auto*c=getConstrainer())c->setFixedAspectRatio(1672./941.);setSize(1280,720);resized();refreshParameterActivity();}
 void dial(const char*id,float x,float y,float radius,juce::Rectangle<float>read){auto c=std::make_unique<Control>();c->id=id;c->dial=true;c->body={x-radius,y-radius,2*radius,2*radius};c->read=read;c->slider.setComponentID(id);c->slider.getProperties().set("preserveAuthoredDialRing",true);c->slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);c->slider.setRotaryParameters(juce::MathConstants<float>::pi*1.25f,juce::MathConstants<float>::pi*2.75f,true);addAndMakeVisible(c->slider);
    // Reference dial has a removable downward marker. Only the central moving
    // disc covers the new faceplate; its authored orange/cyan ring stays intact.
    look.registerDial(id,juce::ImageCache::getFromMemory(OriginalArt::native_transient_png,OriginalArt::native_transient_pngSize),{268,619,134,134});
    for(const auto&s:processor.parameterSpecs())if(c->id==s.id){c->slider.setName(s.name);c->slider.setTextValueSuffix(juce::String(" ")+s.unit);c->slider.setDoubleClickReturnValue(true,s.initial);c->slider.setTooltip(juce::String(s.name)+": "+juce::String(s.minimum)+" to "+juce::String(s.maximum)+" "+s.unit);}
    c->sa=std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(processor.state,id,c->slider);for(const auto&s:processor.parameterSpecs())if(c->id==s.id)c->slider.setDoubleClickReturnValue(true,s.initial);controls.push_back(std::move(c));}
 void button(const char*id,const char*label,juce::Rectangle<float>b,std::function<void()>action){auto c=std::make_unique<Control>();c->id=id;c->body=b;c->button.setComponentID(id);c->button.setName(label);c->button.setButtonText(label);c->button.setClickingTogglesState(false);c->button.onClick=std::move(action);look.registerButton(c->button,art,b);addAndMakeVisible(c->button);controls.push_back(std::move(c));}
 void toggle(const char*id,const char*label,juce::Rectangle<float>b){button(id,label,b,{});auto&c=*controls.back();c.button.setClickingTogglesState(true);c.ba=std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(processor.state,id,c.button);}
 void choice(int value,const char*label,juce::Rectangle<float>b){const auto id=value==0?juce::String("algorithm"):"algorithm_"+juce::String(value);button(id.toRawUTF8(),label,b,[this,value]{if(auto*p=processor.state.getParameter("algorithm")){p->beginChangeGesture();p->setValueNotifyingHost(p->convertTo0to1(float(value)));p->endChangeGesture();}refreshParameterActivity();});controls.back()->choice=value;controls.back()->button.setName("ALGORITHM");controls.back()->button.getProperties().set("reverbChoiceParameter","algorithm");controls.back()->button.getProperties().set("reverbChoiceValue",value);}
 void chooseIR(){chooser=std::make_unique<juce::FileChooser>("Load an impulse response",juce::File(),"*.wav;*.aif;*.aiff;*.flac");juce::Component::SafePointer<ReverbEditor>safe(this);chooser->launchAsync(juce::FileBrowserComponent::openMode|juce::FileBrowserComponent::canSelectFiles,[safe](const juce::FileChooser&c){if(safe&&safe->convolution&&c.getResult().existsAsFile())safe->convolution->requestIRFile(c.getResult());});}
 static void blank(juce::Graphics&g,juce::Rectangle<float>b){g.setColour(juce::Colour(0xff03090e));g.fillRect(b);}
 static void text(juce::Graphics&g,juce::Rectangle<float>b,const juce::String&s,float size=18){blank(g,b);g.setColour(juce::Colours::cyan);g.setFont(FoundationLook::faceFont(size));g.drawText(s,b,juce::Justification::centred);}
 static float db(float p){return p>1.e-6f?20*std::log10(p):-120;}
 void paintMeters(juce::Graphics&g){PluginTelemetry::Levels levels;const bool valid=processor.processingReady()&&processor.meterSnapshot().hasProcessedAudio&&processor.pluginTelemetry()&&processor.pluginTelemetry()->read(levels)&&levels.valid;
    for(int side=0;side<2;++side){const juce::Rectangle<float>area=convolution?juce::Rectangle<float>(side?1462:79,215,134,320):juce::Rectangle<float>(side?1439:110,138,122,312);blank(g,area);for(int ch=0;ch<2;++ch){const float peak=valid?db(levels.peak[size_t(side*2+ch)]):-120;const juce::Rectangle<float>lane=convolution?juce::Rectangle<float>(float(side?1476+ch*81:93+ch*80),227,23,299):juce::Rectangle<float>(float(side?1455+ch*65:126+ch*65),149,26,292);ui::paintSegmentedMeter(g,lane,std::clamp((peak+60)/60,0.f,1.f),valid);}
    if(convolution){const float p=valid?std::max(levels.peak[size_t(side*2)],levels.peak[size_t(side*2+1)]):0;text(g,{float(side?1458:77),546,134,32},valid?(p>1.e-6f?juce::String(db(p),1)+" dBFS":"-inf dBFS"):"-- dBFS",17);}}
 }
 void paintIR(juce::Graphics&g){const auto info=convolution->resourceInfo();const auto display=convolution->irDisplay();const juce::Rectangle<float>plot(326,240,1040,243);blank(g,plot);
    if(display.loaded&&info.ready){float scale=0;for(float v:display.waveform)scale=std::max(scale,std::abs(v));scale=scale>0?scale:1.f;juce::Path wave,env;for(size_t i=0;i<display.waveform.size();++i){const float x=plot.getX()+plot.getWidth()*i/(display.waveform.size()-1.f),y=plot.getCentreY()-display.waveform[i]/scale*plot.getHeight()*.46f,e=plot.getBottom()-display.envelope[i]/scale*plot.getHeight()*.92f;if(i){wave.lineTo(x,y);env.lineTo(x,e);}else{wave.startNewSubPath(x,y);env.startNewSubPath(x,e);}}g.setColour(juce::Colours::cyan);g.strokePath(wave,juce::PathStrokeType(1.5f));g.setColour(juce::Colours::orange);g.strokePath(env,juce::PathStrokeType(1.5f));}else text(g,{400,322,885,64},info.status,24);
    text(g,{549,575,469,37},info.loading?"Loading IR...":info.ready?(std::abs(display.sizePercent-processor.state.getRawParameterValue("irSize")->load())>.05f?juce::String("Preparing IR SIZE..."):info.name):info.status,17);
    text(g,{1058,581,142,28},display.loaded?juce::String(display.seconds,3)+" s":"--",17);
    text(g,{1237,581,143,28},info.sourceRate>0?juce::String(info.sourceRate/1000,1)+" kHz":"--",17);
    text(g,{580,487,525,24},display.loaded?"0-"+juce::String(display.seconds,2)+" s | IR / measured RMS (peak-normalized)":"NO IR: DRY-SAFE",16);
    if(info.ready&&!info.loading&&info.status!="IR ready")text(g,{452,251,803,27},info.status,17);else if(display.renderedFrames>480000)text(g,{400,251,900,27},"LONG IR: CHECK CPU HEADROOM, ESPECIALLY WHEN RELOADING",17);
 }
 void paintTail(juce::Graphics&g){const juce::Rectangle<float>plot(376,244,960,201);blank(g,plot);if(tailCount>1){juce::Path measured;bool started=false;for(size_t i=0;i<tailCount;++i){const auto index=(tailCursor+tail.size()-tailCount+i)%tail.size();const double age=double(algorithmic->tailSampleTime())/processor.actualRate.load()-tailTime[index];if(age>10)continue;const float x=plot.getRight()-plot.getWidth()*float(age/10),y=plot.getBottom()-std::clamp((tail[index]+90)/90,0.f,1.f)*plot.getHeight();if(started)measured.lineTo(x,y);else{measured.startNewSubPath(x,y);started=true;}}g.setColour(juce::Colours::cyan);g.strokePath(measured,juce::PathStrokeType(1.5f));}
    // A labelled analytical reference, never presented as measured audio.
    const float decay=processor.state.getRawParameterValue("decay")->load();juce::Path reference;for(int i=0;i<200;++i){const float sec=i/199.f*10,level=-60*sec/decay,x=plot.getX()+plot.getWidth()*i/199.f,y=plot.getBottom()-std::clamp((level+90)/90,0.f,1.f)*plot.getHeight();if(i)reference.lineTo(x,y);else reference.startNewSubPath(x,y);}g.setColour(juce::Colours::orange.withAlpha(.75f));g.strokePath(reference,juce::PathStrokeType(1.2f));blank(g,{375,447,962,29});text(g,{400,449,910,27},tailCount>0?"cyan: wet RMS, last 10 s | orange: nominal RT60":"WAITING FOR AUDIO | orange: nominal RT60",16);
 }
 ModuleProcessor&processor;ConvolutionReverbProcessor*convolution=nullptr;AlgorithmicReverbProcessor*algorithmic=nullptr;juce::Image art;FoundationLook look;std::vector<std::unique_ptr<Control>>controls;std::unique_ptr<juce::FileChooser>chooser;std::array<float,300>tail{};std::array<double,300>tailTime{};std::uint64_t lastSampleTime=0;size_t tailCursor=0,tailCount=0;uint32_t lastBlocks=0;
};
}
