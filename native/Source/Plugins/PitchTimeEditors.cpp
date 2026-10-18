#include "PitchTimeEditors.h"
#include "../UI/SegmentedMeter.h"
#include <ApprovedArt.h>
namespace hem {namespace {
void label(juce::Graphics& g,juce::Rectangle<float> r,const juce::String& text,float size=15){g.setColour(juce::Colour(0xff9fdfe9));g.setFont(size);g.drawFittedText(text,r.toNearestInt(),juce::Justification::centred,2);}
void blank(juce::Graphics& g,juce::Rectangle<float> r){g.setColour(juce::Colour(0xff03090c));g.fillRect(r);}
}
void PitchTimeEditor::Look::drawLinearSlider(juce::Graphics& g,int x,int y,int width,int height,float position,float min,float max,juce::Slider::SliderStyle style,juce::Slider& slider){
 if(style!=juce::Slider::LinearVertical){juce::LookAndFeel_V4::drawLinearSlider(g,x,y,width,height,position,min,max,style,slider);return;}
 const float cx=x+width*.5f;g.setColour(juce::Colour(0xff030608));g.fillRoundedRectangle(cx-5,float(y),10,float(height),4);
 const float w=std::min(42.f,width*.6f),h=std::min(48.f,height*.19f);juce::Rectangle<float> cap(cx-w/2,position-h/2,w,h);
 g.setGradientFill(juce::ColourGradient(juce::Colour(0xffccd7da),cx,cap.getY(),juce::Colour(0xff263136),cx,cap.getBottom(),false));g.fillRoundedRectangle(cap,3);
 g.setColour(juce::Colour(0xff070c0e));g.drawRoundedRectangle(cap,3,2);g.drawHorizontalLine(int(position),cap.getX()+2,cap.getRight()-2);g.setColour(juce::Colours::white);g.drawHorizontalLine(int(position-1),cap.getX()+3,cap.getRight()-3);
}
PitchTimeEditor::PitchTimeEditor(PitchTimeProcessor& p):ModuleEditor(p,false),processor(p),art(p.timeStretch?juce::ImageCache::getFromMemory(ApprovedArt::approved_18_png,ApprovedArt::approved_18_pngSize):juce::ImageCache::getFromMemory(ApprovedArt::approved_17_png,ApprovedArt::approved_17_pngSize)){
 setLookAndFeel(&look);setWantsKeyboardFocus(true);
 if(p.timeStretch){
  const char* ids[]{"stretchRatio","pitchSemitones","pitchCents","output"};const float x[]{157,403,645,886};
  for(int i=0;i<4;++i)dial(ids[i],x[i],634,59,{x[i]-72,722,144,28});
  toggle("formantPreserve","",{1067,718,31,31});toggle("tempoSync","TEMPO SYNC",{1193,725,192,36});
  numeric("sourceTempo",{1197,588,186,32});numeric("targetTempo",{1197,671,186,32});
  choice("timebase","SECONDS",0,{1440,550,168,35});choice("timebase","BEATS",1,{1440,591,168,35});
  choice("algorithm","TONAL",0,{1440,679,168,28});choice("algorithm","RHYTHMIC",1,{1440,718,168,28});choice("algorithm","MIXED",2,{1440,755,168,28});
  combo("grid",{"Bars (4/4)","Off"},{1290,215,119,23});
  command("preview","PREVIEW",Command::preview,{655,827,267,54});command("apply","APPLY",Command::apply,{950,827,227,54});toggle("bypass","BYPASS",{1198,827,241,54});
 }else{
  fader("pitchSemitones",{235,271,101,284},{222,576,128,27});fader("formantSemitones",{1175,271,100,284},{1162,576,128,27});
  dial("pitchCents",448,468,47,{386,560,121,25});dial("formantCents",1389,468,47,{1326,560,122,25});
  combo("key",{"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"},{374,770,122,35});
  combo("scale",{"Chromatic","Major","Minor"},{527,770,117,35});toggle("correctionEnabled","ENABLE",{677,747,113,74});
  dial("correctionAmount",899,784,45,{841,850,112,25});dial("correctionSpeedMs",1058,784,45,{1008,850,104,25});
  dial("mix",1268,776,51,{1208,850,117,25});dial("output",1431,776,51,{1372,850,116,25});toggle("bypass","BYPASS",{1532,727,80,83});
 }
 setResizable(true,true);setResizeLimits(1000,563,3344,1882);if(auto* c=getConstrainer())c->setFixedAspectRatio(1672./941.);
 if(p.timeStretch)for(auto& c:controls)if(c->id=="targetTempo"||c->id=="sourceTempo"||c->id=="stretchRatio"){
  const auto id=c->id;auto* slider=&c->slider;c->slider.onValueChange=[this,id,slider]{if(updatingTempo)return;updatingTempo=true;
   // JUCE invokes onValueChange before SliderAttachment necessarily commits
   // this slider to APVTS. Read the originating widget, and never refresh/reset
   // it from APVTS inside this notification. The attachment commits afterward.
   const double edited=slider->getValue();const double source=id=="sourceTempo"?edited:processor.parameter("sourceTempo");
   if(id=="targetTempo") {const double ratio=source/edited;if(ratio>=.5&&ratio<=2)setParameter("stretchRatio",ratio);else snapshot.status="Tempo ratio outside 0.5-2; adjust target before rendering";}
   else setParameter("targetTempo",source/(id=="stretchRatio"?edited:processor.parameter("stretchRatio")));
   updatingTempo=false;};}
 for(const auto& spec:p.parameterSpecs())lastValues.push_back(float(p.parameter(spec.id)));
 setSize(1280,720);resized();refreshParameterActivity();
}
PitchTimeEditor::~PitchTimeEditor(){controls.clear();setLookAndFeel(nullptr);}
void PitchTimeEditor::setCommandRequest(std::function<void(Command)> f){commandRequest=std::move(f);refreshParameterActivity();}
void PitchTimeEditor::requestCommand(Command c){if(!commandRequest||(snapshot.busy&&c!=Command::preview))return;if(c!=Command::selectSource&&!snapshot.sourceValid)return;if(c==Command::undo&&!snapshot.canUndo)return;commandRequest(c);}
bool PitchTimeEditor::keyPressed(const juce::KeyPress& k){if(k==juce::KeyPress::escapeKey&&closeRequest){closeRequest();return true;}if(k.getModifiers().isCommandDown()){if(k.getKeyCode()=='Z'){requestCommand(Command::undo);return true;}if(k.getKeyCode()=='O'){requestCommand(Command::selectSource);return true;}if(k.getKeyCode()==juce::KeyPress::returnKey){requestCommand(Command::apply);return true;}}if(k.getKeyCode()==juce::KeyPress::spaceKey){requestCommand(Command::preview);return true;}return false;}
void PitchTimeEditor::setSnapshot(PitchTimeViewSnapshot s){if(!std::isfinite(s.sourceSeconds)||s.sourceSeconds<0)s.sourceValid=false;if(!std::isfinite(s.renderedSeconds)||s.renderedSeconds<0)s.renderedValid=false;s.pitch.erase(std::remove_if(s.pitch.begin(),s.pitch.end(),[](const auto& p){return !std::isfinite(p.seconds)||p.seconds<0;}),s.pitch.end());std::stable_sort(s.pitch.begin(),s.pitch.end(),[](const auto& a,const auto& b){return a.seconds<b.seconds;});s.sourceEnvelope.resize(std::min(size_t(1024),s.sourceEnvelope.size()));s.renderedEnvelope.resize(std::min(size_t(1024),s.renderedEnvelope.size()));s.pitch.resize(std::min(size_t(1024),s.pitch.size()));snapshot=std::move(s);refreshParameterActivity();}
void PitchTimeEditor::setParameter(const char* id,double v){if(auto* p=processor.state.getParameter(id)){p->beginChangeGesture();p->setValueNotifyingHost(p->convertTo0to1(float(v)));p->endChangeGesture();}}
void PitchTimeEditor::dial(const char* id,float x,float y,float radius,juce::Rectangle<float> read){auto c=std::make_unique<Control>();c->id=id;c->kind=1;c->body={x-radius,y-radius,radius*2,radius*2};c->read=read;c->slider.getProperties().set("preserveAuthoredDialRing",true);c->slider.setComponentID(id);c->slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);c->slider.setRotaryParameters(juce::MathConstants<float>::pi*1.25f,juce::MathConstants<float>::pi*2.75f,true);for(const auto& spec:processor.parameterSpecs())if(c->id==spec.id){c->slider.setName(spec.name);c->slider.setTextValueSuffix(juce::String(" ")+spec.unit);c->slider.setDoubleClickReturnValue(true,spec.initial);}addAndMakeVisible(c->slider);c->sa=std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(processor.state,id,c->slider);controls.push_back(std::move(c));}
void PitchTimeEditor::fader(const char* id,juce::Rectangle<float> b,juce::Rectangle<float> read){dial(id,b.getCentreX(),b.getCentreY(),b.getWidth()/2,read);auto& c=*controls.back();c.kind=2;c.body=b;c.slider.setLookAndFeel(&faderLook);c.slider.getProperties().remove("preserveAuthoredDialRing");c.slider.setSliderStyle(juce::Slider::LinearVertical);}
void PitchTimeEditor::numeric(const char* id,juce::Rectangle<float> b){dial(id,b.getCentreX(),b.getCentreY(),1,b);auto& c=*controls.back();c.kind=3;c.body=b;c.slider.setSliderStyle(juce::Slider::IncDecButtons);c.slider.setIncDecButtonsMode(juce::Slider::incDecButtonsDraggable_AutoDirection);}
void PitchTimeEditor::toggle(const char* id,const char* name,juce::Rectangle<float> b){auto c=std::make_unique<Control>();c->id=id;c->body=b;c->button.setComponentID(id);c->button.setName(name);c->button.setButtonText(name);c->button.setClickingTogglesState(true);addAndMakeVisible(c->button);c->ba=std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(processor.state,id,c->button);controls.push_back(std::move(c));}
void PitchTimeEditor::choice(const char* id,const char* name,int v,juce::Rectangle<float> b){auto c=std::make_unique<Control>();c->id=id;c->choice=v;c->body=b;c->button.setComponentID(juce::String(id)+juce::String(v));c->button.setName(name);c->button.setButtonText(name);c->button.onClick=[this,id,v]{setParameter(id,v);refreshParameterActivity();};addAndMakeVisible(c->button);controls.push_back(std::move(c));}
void PitchTimeEditor::combo(const char* id,juce::StringArray items,juce::Rectangle<float> b){auto c=std::make_unique<Control>();c->id=id;c->kind=4;c->body=b;c->combo.setComponentID(id);c->combo.setName(id);c->combo.addItemList(items,1);addAndMakeVisible(c->combo);c->ca=std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(processor.state,id,c->combo);controls.push_back(std::move(c));}
void PitchTimeEditor::command(const char* id,const char* name,Command cmd,juce::Rectangle<float> b){choice(id,name,-1,b);controls.back()->kind=5;controls.back()->button.setComponentID(id);controls.back()->button.onClick=[this,cmd]{requestCommand(cmd);};}
void PitchTimeEditor::resized(){const float sx=getWidth()/1672.f,sy=getHeight()/941.f;auto scale=[&](juce::Rectangle<float> b){return juce::Rectangle<float>(b.getX()*sx,b.getY()*sy,b.getWidth()*sx,b.getHeight()*sy).toNearestInt();};for(auto& c:controls){if(c->kind>=1&&c->kind<=3){auto b=c->body.getUnion(c->read);c->slider.setBounds(scale(b));auto& v=c->slider.getProperties();if(c->kind==1){v.set("bodyRadius",c->body.getWidth()*.5f*sx);v.set("bodyX",(c->body.getCentreX()-b.getX())*sx);v.set("bodyY",(c->body.getCentreY()-b.getY())*sy);}else v.remove("bodyRadius");v.set("readX",int((c->read.getX()-b.getX())*sx));v.set("readY",int((c->read.getY()-b.getY())*sy));v.set("readW",int(c->read.getWidth()*sx));v.set("readH",int(c->read.getHeight()*sy));c->slider.setTextBoxStyle(juce::Slider::TextBoxBelow,false,int(c->read.getWidth()*sx),int(c->read.getHeight()*sy));c->slider.resized();}else if(c->kind==4)c->combo.setBounds(scale(c->body));else c->button.setBounds(scale(c->body));}}
void PitchTimeEditor::refreshParameterActivity(){bool changed=false;size_t i=0;for(const auto& spec:processor.parameterSpecs()){const float v=float(processor.parameter(spec.id));if(i<lastValues.size()&&v!=lastValues[i]){lastValues[i]=v;if(juce::String(spec.id)=="bypass"){if(auditionRequest)auditionRequest(v>.5f);}else changed=true;}++i;}if(changed&&recipeChanged)recipeChanged();for(auto& c:controls){if(c->kind==5){c->button.setEnabled(bool(commandRequest)&&snapshot.sourceValid&&(!snapshot.busy||c->id=="preview"));if(c->id=="preview")c->button.setButtonText(snapshot.busy?"CANCEL":"PREVIEW");}if(c->choice>=0)c->button.setToggleState(int(processor.parameter(c->id.toRawUTF8()))==c->choice,juce::dontSendNotification);if(c->id=="stretchRatio"||c->id=="targetTempo"){const bool sync=processor.parameter("tempoSync")>.5;c->slider.setEnabled(!sync&&!snapshot.busy);const double value=sync?(c->id=="targetTempo"?snapshot.projectBpm:processor.parameter("sourceTempo")/snapshot.projectBpm):processor.parameter(c->id.toRawUTF8());if((sync||lastTempoSync)&&std::isfinite(value))c->slider.setValue(value,juce::dontSendNotification);}}lastTempoSync=processor.timeStretch&&processor.parameter("tempoSync")>.5;repaint();}
void PitchTimeEditor::paint(juce::Graphics& g){g.fillAll(juce::Colours::black);g.drawImage(art,getLocalBounds().toFloat(),juce::RectanglePlacement::stretchToFit);const juce::Graphics::ScopedSaveState save(g);g.addTransform(juce::AffineTransform::scale(getWidth()/1672.f,getHeight()/941.f));const bool stretch=processor.timeStretch;
 const juce::Rectangle<float> plot=stretch?juce::Rectangle<float>(308,245,1105,150):juce::Rectangle<float>(587,257,499,250);blank(g,plot);
 for(int side=0;side<2;++side){const juce::Rectangle<float> box=stretch?juce::Rectangle<float>(side?1505.f:74.f,155,91,284):juce::Rectangle<float>(side?1521.f:72.f,255,79,295);blank(g,box);for(int channel=0;channel<2;++channel)ui::paintSegmentedMeter(g,{box.getX()+8+channel*(box.getWidth()/2),box.getY()+9,21,box.getHeight()-35},ui::meterAmplitudePosition(snapshot.peaks[size_t(side*2+channel)]),snapshot.peaksValid);label(g,{box.getX(),box.getBottom()-22,box.getWidth(),20},snapshot.peaksValid?"FILE PEAK":"NO DATA",10);}
 label(g,plot.withHeight(25),"OFFLINE WHOLE SOURCE | "+snapshot.selection,12);
 auto inner=plot.reduced(10,30);
 if(stretch){auto trace=[&](const std::vector<float>& values,juce::Colour colour){if(values.empty())return;g.setColour(colour);for(size_t i=0;i<values.size();++i){const float x=inner.getX()+float(i)*inner.getWidth()/values.size();const float a=std::isfinite(values[i])?std::clamp(values[i],-1.f,1.f):0;g.drawLine(x,inner.getCentreY(),x,inner.getCentreY()-a*inner.getHeight()*.5f,1);}};
  if(snapshot.sourceValid&&processor.parameter("grid")<.5&&snapshot.sourceSeconds>0){const double barSeconds=240/processor.parameter("sourceTempo");const int count=std::min(64,int(snapshot.sourceSeconds/barSeconds));g.setColour(juce::Colour(0xff18323b));for(int i=0;i<=count;++i){const float x=inner.getX()+float(i*barSeconds/snapshot.sourceSeconds)*inner.getWidth();g.drawVerticalLine(int(x),inner.getY(),inner.getBottom());}}
  if(snapshot.sourceValid)trace(snapshot.sourceEnvelope,juce::Colour(0xff24d6ed));if(snapshot.renderedValid)trace(snapshot.renderedEnvelope,juce::Colour(0xffffa42a));
  label(g,plot.withY(plot.getBottom()-23).withHeight(22),"Cyan source / orange rendered | Each spans its own duration | "+snapshot.status,11);
  for(auto r:{juce::Rectangle<float>(337,450,184,26),juce::Rectangle<float>(1150,450,184,26),juce::Rectangle<float>(1026,572,111,31)})blank(g,r);
  pitchtime::Settings settings;auto valid=processor.settings(48000,2,snapshot.projectBpm,settings);const bool beats=processor.parameter("timebase")>.5;
  const double unit=beats?processor.parameter("sourceTempo")/60:1;const auto suffix=beats?" beats":" s";
  const double targetUnit=beats?(processor.parameter("tempoSync")>.5?snapshot.projectBpm:processor.parameter("targetTempo"))/60:1;
  label(g,{337,450,184,26},snapshot.sourceValid?juce::String(snapshot.sourceSeconds*unit,3)+suffix:"--");label(g,{1150,450,184,26},snapshot.sourceValid&&valid.wasOk()?juce::String(snapshot.sourceSeconds*settings.durationRatio*targetUnit,3)+suffix:"--");label(g,{1026,572,111,31},valid.wasOk()?juce::String(settings.durationRatio,3)+"x":"INVALID");
 }else{
  // Pitch observations are measured source cents and predicted target cents,
  // referenced to MIDI note 69 (A4), never measured output pitch.
  if(!snapshot.pitch.empty()){double end=snapshot.pitch.back().seconds;for(int target=0;target<2;++target){juce::Path line;bool started=false;for(const auto& pt:snapshot.pitch){const bool valid=target?pt.targetValid:pt.inputValid;const double cents=target?pt.targetCents:pt.inputCents;if(!valid||!std::isfinite(cents)||end<=0){started=false;continue;}float x=inner.getX()+float(pt.seconds/end)*inner.getWidth(),y=inner.getCentreY()-float(std::clamp(cents,-2400.,2400.)/2400)*inner.getHeight()*.5f;if(started)line.lineTo(x,y);else line.startNewSubPath(x,y);started=true;}g.setColour(target?juce::Colour(0xffffa42a):juce::Colour(0xff24d6ed));g.strokePath(line,juce::PathStrokeType(1.3f));}}
  label(g,plot.withY(plot.getBottom()-31).withHeight(28),"Cents from A4 +/-2400 | Cyan INPUT / orange TARGET\nMonophonic correction only",11);
  blank(g,{875,548,188,19});label(g,{875,548,188,19},"TARGET (PREDICTED)",13);
  blank(g,{605,575,192,31});blank(g,{874,575,190,31});label(g,{605,575,192,31},snapshot.inputNote.isEmpty()?"NO DATA":snapshot.inputNote);label(g,{874,575,190,31},snapshot.targetNote.isEmpty()?"NO TARGET":snapshot.targetNote);
  for(auto r:{juce::Rectangle<float>(235,271,101,284),juce::Rectangle<float>(1175,271,100,284)})blank(g,r);
 }
 if(!snapshot.sourceValid)label(g,inner,"Select a source in the host\nSpace: Preview | Ctrl+Enter: Apply | Ctrl+Z: Undo",13);
}
}









