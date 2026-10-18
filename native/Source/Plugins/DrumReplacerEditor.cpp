#include "DrumReplacerEditor.h"
#include "../UI/SegmentedMeter.h"
#include <ApprovedArt.h>
namespace hem {
namespace {
constexpr float sampleX[]{64,471,869,1267};
constexpr float levelX[]{340,737,1135,1535};
void text(juce::Graphics& g,juce::Rectangle<float> r,const juce::String& s,float size=16){
 g.setColour(juce::Colour(0xffa4dce6));g.setFont(size);g.drawFittedText(s,r.toNearestInt(),juce::Justification::centred,2);
}
}
DrumReplacerEditor::DrumReplacerEditor(DrumReplacerProcessor& p):ModuleEditor(p,false),processor(p),
 art(juce::ImageCache::getFromMemory(ApprovedArt::approved_16_png,ApprovedArt::approved_16_pngSize)){
 setLookAndFeel(&look);setWantsKeyboardFocus(true);
 for(int i=0;i<4;++i){const auto suffix=juce::String(i+1);const auto x=sampleX[i];
  addButton("load"+suffix,"LOAD",{x,446,174,46},false,i);
  addButton("enable"+suffix,"ENABLE",{x,570,83,42},true,i);
  addButton("solo"+suffix,"SOLO",{x+98,570,81,42},true,i);
  addDial("level"+suffix,levelX[i],521,47,{levelX[i]-56,585,112,29});
  auto& label=identities[size_t(i)];label.setComponentID("sample"+suffix+"Identity");
  label.setJustificationType(juce::Justification::centred);label.setColour(juce::Label::textColourId,juce::Colour(0xffa4dce6));
  label.setColour(juce::Label::backgroundColourId,juce::Colour(0xff03090c));addAndMakeVisible(label);
 }
 const char* ids[]{"sensitivity","retrigger","velocity","blend","output"};
 const float xs[]{431,652,873,1093,1315};
 for(int i=0;i<5;++i)addDial(ids[i],xs[i],773,61,{xs[i]-66,842,132,30});
 addButton("hostBypass","BYPASS",{1462,809,141,47},false);
 controls.back()->button.onClick=[this]{const auto state=hostBypassState();if(state)requestHostBypass(!*state);};
 setResizable(true,true);setResizeLimits(1000,563,3344,1882);
 if(auto* c=getConstrainer())c->setFixedAspectRatio(1672./941.);
 PluginTelemetry::Levels initial;if(auto* source=p.pluginTelemetry();source&&source->read(initial)){lastBlocks=initial.blocks;lastEpoch=initial.epoch;}
 lastHits=p.hitCount();setSize(1280,720);resized();refreshParameterActivity();
}
DrumReplacerEditor::~DrumReplacerEditor(){controls.clear();setLookAndFeel(nullptr);}
void DrumReplacerEditor::setLoadRequest(std::function<void(int)> f){loadRequest=std::move(f);refreshParameterActivity();}
void DrumReplacerEditor::setCloseRequest(std::function<void()> f){closeRequest=std::move(f);}
bool DrumReplacerEditor::keyPressed(const juce::KeyPress& key){if(key==juce::KeyPress::escapeKey&&closeRequest){closeRequest();return true;}return false;}
void DrumReplacerEditor::addButton(const juce::String& id,const juce::String& title,juce::Rectangle<float> r,bool parameter,int layer){
 auto c=std::make_unique<Control>();c->id=id;c->body=r;c->layer=layer;c->button.setComponentID(id);
 c->button.setName(title+(layer>=0?" layer "+juce::String(layer+1):juce::String()));c->button.setButtonText(title);
 look.registerButton(c->button,art,r,true);c->button.setClickingTogglesState(parameter);addAndMakeVisible(c->button);
 if(parameter)c->ba=std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(processor.state,id,c->button);
 else if(layer>=0){c->button.setTooltip("Load a sample through the host's prepared replacement path.");c->button.onClick=[this,layer]{if(loadRequest)loadRequest(layer);};}
 controls.push_back(std::move(c));
}
void DrumReplacerEditor::addDial(const juce::String& id,float x,float y,float radius,juce::Rectangle<float> read){
 auto c=std::make_unique<Control>();c->id=id;c->dial=true;c->body={x-radius,y-radius,2*radius,2*radius};c->read=read;
 c->slider.getProperties().set("preserveAuthoredDialRing",true);c->slider.setComponentID(id);
 c->slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
 c->slider.setRotaryParameters(juce::MathConstants<float>::pi*1.25f,juce::MathConstants<float>::pi*2.75f,true);
 c->slider.setMouseDragSensitivity(250);addAndMakeVisible(c->slider);
 for(const auto& s:processor.parameterSpecs())if(id==s.id){c->slider.setName(s.name);c->slider.setTextValueSuffix(juce::String(" ")+s.unit);
  c->slider.setTooltip(juce::String(s.name)+": "+juce::String(s.minimum)+" to "+juce::String(s.maximum)+" "+s.unit);
  c->slider.setDoubleClickReturnValue(true,s.initial);}
 c->sa=std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(processor.state,id,c->slider);controls.push_back(std::move(c));
}
void DrumReplacerEditor::resized(){const float sx=getWidth()/1672.f,sy=getHeight()/941.f;
 const auto scale=[&](juce::Rectangle<float> r){return juce::Rectangle<float>(r.getX()*sx,r.getY()*sy,r.getWidth()*sx,r.getHeight()*sy).toNearestInt();};
 for(auto& c:controls)if(c->dial){const auto b=c->body.getUnion(c->read);c->slider.setBounds(scale(b));auto& v=c->slider.getProperties();
  v.set("bodyRadius",c->body.getWidth()*.5f*sx);v.set("bodyX",(c->body.getCentreX()-b.getX())*sx);v.set("bodyY",(c->body.getCentreY()-b.getY())*sy);
  v.set("readX",int((c->read.getX()-b.getX())*sx));v.set("readY",int((c->read.getY()-b.getY())*sy));v.set("readW",int(c->read.getWidth()*sx));v.set("readH",int(c->read.getHeight()*sy));
  c->slider.setTextBoxStyle(juce::Slider::TextBoxBelow,false,int(c->read.getWidth()*sx),int(c->read.getHeight()*sy));c->slider.resized();
 }else c->button.setBounds(scale(c->body));
 for(int i=0;i<4;++i)identities[size_t(i)].setBounds(scale({sampleX[i]+1,514,173,36}));
}
void DrumReplacerEditor::refreshParameterActivity(){
 levels={};auto* source=processor.pluginTelemetry();const bool read=source&&source->read(levels);
 const auto now=juce::Time::getMillisecondCounterHiRes();const auto meter=processor.meterSnapshot();
 historyValid=processor.detectorHistory(history)&&history.count>0&&history.sampleRate>0;
 PluginTelemetry::Levels after;const bool coherent=read&&source->read(after)&&after.epoch==levels.epoch;
 const bool eligible=coherent&&levels.valid&&levels.blocks&&processor.processingReady()&&meter.isPrepared&&meter.hasProcessedAudio;
 const bool epochChanged=read&&levels.epoch!=lastEpoch;const bool advanced=read&&(levels.blocks!=lastBlocks||epochChanged);
 const auto hits=processor.hitCount();if(epochChanged||hits<lastHits)hitAt=-1;
 if(advanced){lastBlocks=levels.blocks;lastEpoch=levels.epoch;lastAt=eligible?now:-1;if(eligible&&hits>lastHits&&!epochChanged)hitAt=now;}
 lastHits=hits;if(!eligible)lastAt=-1;fresh=eligible&&lastAt>=0&&now>=lastAt&&now-lastAt<=750;
 trigger=fresh&&hitAt>=0&&now>=hitAt&&now-hitAt<=250;
 const auto descriptors=processor.sampleDescriptors();const bool pending=processor.hasPendingNativeStateWork();
 for(int i=0;i<4;++i){const auto& d=descriptors[size_t(i)];const bool loaded=d.frames>0;
  identities[size_t(i)].setText(pending?"LOADING":loaded?juce::File(d.path).getFileName():"NO SAMPLE",juce::dontSendNotification);
  identities[size_t(i)].setTooltip(loaded?d.path:processor.lastResourceError());}
 for(auto& c:controls){if(c->id.startsWith("load"))c->button.setEnabled(bool(loadRequest)&&!pending);
  else if(c->id=="hostBypass"){const auto state=hostBypassState();c->button.setEnabled(state.has_value()&&bool(hostBypassBinding().write));c->button.setToggleState(state.value_or(false),juce::dontSendNotification);}
  else if(c->layer>=0)c->button.setEnabled(descriptors[size_t(c->layer)].frames>0&&!pending);}
 repaint();
}
void DrumReplacerEditor::paint(juce::Graphics& g){g.fillAll(juce::Colours::black);g.drawImage(art,getLocalBounds().toFloat(),juce::RectanglePlacement::stretchToFit);
 const juce::Graphics::ScopedSaveState save(g);g.addTransform(juce::AffineTransform::scale(getWidth()/1672.f,getHeight()/941.f));
 for(int side=0;side<2;++side){const float x=side?1468.f:87.f;g.setColour(juce::Colour(0xff020609));g.fillRect(x,129.f,116.f,228.f);
  for(int channel=0;channel<2;++channel){const juce::Rectangle<float> lane{x+12+channel*61.f,146,23,191};const auto index=size_t(side*2+channel);
   ui::paintSegmentedMeter(g,lane,ui::meterAmplitudePosition(levels.peak[index]),fresh);
   ui::paintMeterMark(g,lane,ui::meterAmplitudePosition(levels.rms[index]),juce::Colours::white,1.5f,ui::MeterMarkRole::rms,fresh);}
  text(g,{x,130,116,14},"L                 R",11);text(g,{x,340,116,15},fresh?"-60 .. 0 dBFS":"NO AUDIO / STALE",10);
 }
 // Replace all illustrative pixels with a coherent, bounded audio publication.
 g.setColour(juce::Colour(0xff03090c));g.fillRect(312.f,198.f,1075.f,142.f);
 if(fresh&&historyValid){
  const juce::Rectangle<float> plot{316,220,1065,91};
  const auto first=history.bins[0].firstSample,last=history.bins[size_t(history.count-1)].endSample;
  const double span=double(last-first);
  if(span>0){const auto x=[&](uint64_t frame){return plot.getX()+float(double(frame-first)/span)*plot.getWidth();};
   const auto y=[&](float amplitude){return plot.getCentreY()-std::clamp(amplitude,-1.f,1.f)*plot.getHeight()*.5f;};
   g.setColour(juce::Colour(0xff19323a));g.drawHorizontalLine(int(plot.getCentreY()),plot.getX(),plot.getRight());
   juce::Path threshold;uint64_t visibleHits=0;
   for(int i=0;i<history.count;++i){const auto& bin=history.bins[size_t(i)];const float centre=(x(bin.firstSample)+x(bin.endSample))*.5f;
    g.setColour(juce::Colour(0xff24d6ed));g.drawLine(centre,y(bin.minimum),centre,y(bin.maximum),std::max(1.f,(x(bin.endSample)-x(bin.firstSample))*.65f));
    const float ty=y(std::pow(10.f,bin.thresholdDb/20.f));if(i==0)threshold.startNewSubPath(x(bin.firstSample),ty);else threshold.lineTo(x(bin.firstSample),ty);threshold.lineTo(x(bin.endSample),ty);
    if(bin.hits){visibleHits+=bin.hits;g.setColour(juce::Colour(0xffffa42a));const float onset=x(bin.firstOnsetSample);g.drawLine(onset,plot.getY(),onset,plot.getBottom(),1.f);
     if(bin.hits>1)text(g,{onset-10,plot.getY(),20,12},juce::String(bin.hits),9);}
   }
   g.setColour(juce::Colour(0xffffa42a));g.strokePath(threshold,juce::PathStrokeType(1.f));
   text(g,{317,200,1063,18},"Measured stereo min/max +/-1 FS | Orange threshold / first onset per bin | Counts for multiple onsets",11);
   text(g,{317,316,1063,20},"0 - "+juce::String(span*1000/history.sampleRate,1)+" ms | Bins up to "+juce::String(history.samplesPerBin*1000/history.sampleRate,2)+" ms | "+juce::String(juce::uint64(visibleHits))+" onsets | Before 3 ms replacement delay",12);
  }
 }else text(g,{335,235,1026,62},fresh?"COLLECTING DETECTOR HISTORY":"NO AUDIO / STALE",22);
 g.setColour(trigger?juce::Colour(0xffff991f):juce::Colour(0xff070d10));g.fillEllipse(1511.f,727.f,38.f,38.f);
}
}

