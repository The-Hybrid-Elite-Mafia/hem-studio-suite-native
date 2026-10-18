#include "AmpCabinetEditor.h"
#include "../UI/SegmentedMeter.h"
#include <ApprovedArt.h>
namespace hem {
AmpCabinetEditor::AmpCabinetEditor(AmpCabinetProcessor& p):ModuleEditor(p,false),processor(p),art(juce::ImageCache::getFromMemory(ApprovedArt::approved_25_png,ApprovedArt::approved_25_pngSize)){
 setLookAndFeel(&look);setWantsKeyboardFocus(true);
 const char* ids[]{"gain","bass","mid","treble","master"};const float xs[]{66,207,349,492,634};
 for(int i=0;i<5;++i)dial(ids[i],{xs[i],432,112,115},{xs[i]+2,559,108,37});
 dial("micPosition",{715,699,106,110},{714,817,103,28});dial("micDistance",{853,699,106,110},{856,817,97,28});
 dial("micAngle",{993,699,106,110},{994,817,99,28});dial("output",{1316,692,116,117},{1321,824,102,27});
 button("ampEnabled","AMP ENABLE",{599,310,142,49},true);button("cabEnabled","CAB ENABLE",{1467,309,133,38},true);
 button("micEnabled","MIC ENABLE",{1138,724,103,63},true);button("bypass","BYPASS",{1485,735,111,49},true);
 button("loadCabIR","LOAD CAB IR",{1074,309,114,38},false);controls.back()->button.onClick=[this]{if(loadRequest)loadRequest();};
 choice("ampModel",{81,327,487,31},3,AmpCabinetProcessor::ampModelName);choice("cabModel",{826,321,209,25},4,AmpCabinetProcessor::cabinetModelName);choice("micModel",{383,727,270,30},3,AmpCabinetProcessor::micModelName);
 identity.setComponentID("cabIRIdentity");identity.setJustificationType(juce::Justification::centred);identity.setColour(juce::Label::textColourId,juce::Colours::cyan);identity.setColour(juce::Label::backgroundColourId,juce::Colour(0xff03090c));addAndMakeVisible(identity);
 setResizable(true,true);setResizeLimits(1000,563,3344,1882);if(auto* c=getConstrainer())c->setFixedAspectRatio(1672./941.);if(auto* telemetry=p.pluginTelemetry();telemetry&&telemetry->read(levels)){blocks=levels.blocks;epoch=levels.epoch;}setSize(1280,720);resized();refreshParameterActivity();
}
AmpCabinetEditor::~AmpCabinetEditor(){controls.clear();setLookAndFeel(nullptr);}
bool AmpCabinetEditor::keyPressed(const juce::KeyPress& key){if(key==juce::KeyPress::escapeKey&&closeRequest){closeRequest();return true;}return false;}
void AmpCabinetEditor::dial(const char* id,juce::Rectangle<float> body,juce::Rectangle<float> read){auto c=std::make_unique<Control>();c->id=id;c->type=1;c->body=body;c->read=read;c->slider.setComponentID(id);
 c->slider.getProperties().set("preserveAuthoredDialRing",true);c->slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);c->slider.setRotaryParameters(juce::MathConstants<float>::pi*1.25f,juce::MathConstants<float>::pi*2.75f,true);c->slider.setMouseDragSensitivity(250);
 for(const auto& s:processor.parameterSpecs())if(c->id==s.id){c->slider.setName(s.name);c->slider.setTextValueSuffix(juce::String(" ")+s.unit);c->slider.setDoubleClickReturnValue(true,s.initial);c->slider.setTooltip(juce::String(s.name)+" "+s.unit);}
 addAndMakeVisible(c->slider);c->sa=std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(processor.state,id,c->slider);controls.push_back(std::move(c));}
void AmpCabinetEditor::button(const char* id,const char* text,juce::Rectangle<float> body,bool param){auto c=std::make_unique<Control>();c->id=id;c->body=body;c->button.setName(text);c->button.setComponentID(id);c->button.setButtonText(text);c->button.setClickingTogglesState(param);look.registerButton(c->button,art,body,true);addAndMakeVisible(c->button);if(param)c->ba=std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(processor.state,id,c->button);controls.push_back(std::move(c));}
void AmpCabinetEditor::choice(const char* id,juce::Rectangle<float> body,int count,const char*(*name)(int)noexcept){auto c=std::make_unique<Control>();c->id=id;c->type=2;c->body=body;c->combo.setName(id);c->combo.setComponentID(id);for(int i=0;i<count;++i)c->combo.addItem(name(i),i+1);addAndMakeVisible(c->combo);c->ca=std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(processor.state,id,c->combo);controls.push_back(std::move(c));}
void AmpCabinetEditor::resized(){const float sx=getWidth()/1672.f,sy=getHeight()/941.f;const auto scale=[&](juce::Rectangle<float> r){return juce::Rectangle<float>(r.getX()*sx,r.getY()*sy,r.getWidth()*sx,r.getHeight()*sy).toNearestInt();};
 for(auto& c:controls){if(c->type==1){auto b=c->body.getUnion(c->read);c->slider.setBounds(scale(b));auto& v=c->slider.getProperties();v.set("bodyRadius",c->body.getWidth()*.43f*sx);v.set("bodyX",(c->body.getCentreX()-b.getX())*sx);v.set("bodyY",(c->body.getCentreY()-b.getY())*sy);v.set("readX",int((c->read.getX()-b.getX())*sx));v.set("readY",int((c->read.getY()-b.getY())*sy));v.set("readW",int(c->read.getWidth()*sx));v.set("readH",int(c->read.getHeight()*sy));c->slider.setTextBoxStyle(juce::Slider::TextBoxBelow,false,int(c->read.getWidth()*sx),int(c->read.getHeight()*sy));c->slider.resized();}else if(c->type==2)c->combo.setBounds(scale(c->body));else c->button.setBounds(scale(c->body));}
 identity.setBounds(scale({1208,307,234,40}));
}
void AmpCabinetEditor::refreshParameterActivity(){auto* t=processor.pluginTelemetry();const bool valid=t&&t->read(levels)&&levels.valid&&processor.processingReady()&&processor.meterSnapshot().hasProcessedAudio;const auto now=juce::Time::getMillisecondCounterHiRes();if(valid&&(levels.blocks!=blocks||levels.epoch!=epoch)){blocks=levels.blocks;epoch=levels.epoch;lastAt=now;}fresh=valid&&lastAt>=0&&now-lastAt<=750;
 const auto d=processor.irDescriptor();identity.setText(d.frames?juce::File(d.path).getFileName():"NO CUSTOM IR",juce::dontSendNotification);identity.setTooltip(d.path.isNotEmpty()?d.path:processor.lastResourceError());
 for(auto& c:controls){if(c->id=="loadCabIR")c->button.setEnabled(bool(loadRequest)&&!processor.hasPendingNativeStateWork());if(c->id=="cabModel")c->combo.setItemEnabled(4,d.frames>0);}
 repaint();
}
void AmpCabinetEditor::paint(juce::Graphics& g){g.fillAll(juce::Colours::black);g.drawImage(art,getLocalBounds().toFloat(),juce::RectanglePlacement::stretchToFit);const juce::Graphics::ScopedSaveState save(g);g.addTransform(juce::AffineTransform::scale(getWidth()/1672.f,getHeight()/941.f));
 for(int side=0;side<2;++side){float x=side?1458.f:110.f;g.setColour(juce::Colour(0xff020609));g.fillRect(x,102.f,103.f,110.f);for(int ch=0;ch<2;++ch){juce::Rectangle<float> lane{x+14+ch*53.f,108,20,97};ui::paintSegmentedMeter(g,lane,ui::meterAmplitudePosition(levels.peak[size_t(side*2+ch)]),fresh);ui::paintMeterMark(g,lane,ui::meterAmplitudePosition(levels.rms[size_t(side*2+ch)]),juce::Colours::white,1.5f,ui::MeterMarkRole::rms,fresh);}}
 const juce::Rectangle<float> plot{846,396,750,154};g.setColour(juce::Colour(0xff03090c));g.fillRect(plot);const auto settings=processor.settings();const auto ir=processor.irResponse();const double rate=processor.actualRate.load();
 if(rate>0){for(int line=0;line<2;++line){juce::Path path;for(int i=0;i<128;++i){const double hz=20*std::pow(1000.,i/127.);if(hz>=rate*.5)break;auto p=settings;float magnitude=1;if(p.cabEnabled&&p.cabModel==3)magnitude=ir[size_t(i)];magnitude*=AmpCabinetCore::response(hz,rate,p,line==1);const float db=20*std::log10(std::max(magnitude,1e-8f));const float x=plot.getX()+plot.getWidth()*i/127.f,y=plot.getBottom()-std::clamp((db+48)/60,0.f,1.f)*plot.getHeight();if(i)path.lineTo(x,y);else path.startNewSubPath(x,y);}g.setColour(line?juce::Colours::orange:juce::Colours::cyan);g.strokePath(path,juce::PathStrokeType(1.5f));}}
 g.setColour(juce::Colour(0xff03090c));g.fillRect(847.f,551.f,748.f,18.f);g.setColour(juce::Colours::cyan);g.setFont(12.f);g.drawText("20 Hz - 20 kHz | -48 to +12 dB | cyan: cab RMS L/R; orange: cab + modeled mic",847,551,748,18,juce::Justification::centred);
}
}



