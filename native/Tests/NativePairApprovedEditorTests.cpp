// Headless software-raster and real JUCE control/processor tests. No native peer,
// popup, device, listening, or platform-window coverage is claimed here.
#include "../Source/Plugins/NativeOriginalEditors.h"
#include "../Source/Plugins/SpecialtyProcessors.h"
#include <iostream>
#include <stdexcept>
#include <functional>
namespace {
void require(bool value,const juce::String& why){if(!value)throw std::runtime_error(why.toStdString());}
void close(double a,double b,double tolerance,const juce::String& why){require(std::isfinite(a)&&std::abs(a-b)<=tolerance,why+" actual="+juce::String(a,6)+" expected="+juce::String(b,6));}
void set(hem::ModuleProcessor& p,const char* id,float value){auto* parameter=p.state.getParameter(id);require(parameter!=nullptr,"Missing APVTS parameter");parameter->setValueNotifyingHost(parameter->convertTo0to1(value));}
std::unique_ptr<hem::ModuleProcessor> processor(bool stereo){if(stereo)return std::make_unique<hem::SipaProcessor>();return std::make_unique<hem::TransientProcessor>();}
std::unique_ptr<hem::NativePairApprovedEditor> editor(hem::ModuleProcessor& p){
#ifdef HEM_NATIVE_PAIR_RENDERER_MUTANT
 return std::make_unique<hem::NativePairApprovedEditor>(p,p.state.state.getType().toString()=="SIPA");
#else
 auto* raw=p.createEditor();auto* approved=dynamic_cast<hem::NativePairApprovedEditor*>(raw);if(!approved)delete raw;require(approved!=nullptr,"Factory does not use approved native pair editor");return std::unique_ptr<hem::NativePairApprovedEditor>(approved);
#endif
}
juce::Image approved(bool stereo){return stereo?juce::ImageCache::getFromMemory(ApprovedArt::approved_12_png,ApprovedArt::approved_12_pngSize):juce::ImageCache::getFromMemory(ApprovedArt::approved_01_png,ApprovedArt::approved_01_pngSize);}
juce::Image render(juce::Component& c){return c.createComponentSnapshot(c.getLocalBounds());}
void save(const juce::Image& image,const juce::File& out,const juce::String& name){if(out==juce::File())return;auto stream=out.getChildFile(name+".png").createOutputStream();require(stream!=nullptr&&juce::PNGImageFormat().writeImageToStream(image,*stream),"Cannot save raster");}

// Independent authored geometry: this never queries liveApertures()/properties.
// One-pixel physical raster fringe permits antialiasing at aperture boundaries.
struct Knob {const char* id;float x,y,r;juce::Rectangle<float> read;bool bakedUp;};
std::vector<Knob> knobs(bool stereo){if(stereo)return {{"width",431,610,68,{355,699,153,31},false},{"bass",712,610,68,{635,699,151,31},false},{"balance",991,610,68,{915,699,150,31},true},{"output",1268,610,68,{1192,699,151,31},true}};
return {{"attack",335,686,67,{281,779,110,29},true},{"sustain",568,686,67,{514,779,109,29},true},{"speed",800,686,67,{744,779,111,29},false},{"recovery",1034,686,67,{980,779,109,29},false},{"mix",1275,686,67,{1220,779,109,29},false},{"output",1516,686,67,{1461,779,110,29},true}};}
std::vector<juce::Rectangle<float>> glass(bool stereo){
 if(stereo)return {{98,249,116,416},{1458,249,116,416},{340,262,436,193},{858,303,489,80},{1007,406,192,34},
 {450,810,171,54},{713,810,148,54},{947,810,148,54},{1180,810,148,54},{1416,810,149,54}};
 return {{79,207,148,233},{1450,207,148,233},{308,234,1046,270},{76,451,150,23},{1447,451,151,23},{83,482,136,31},{1454,482,135,31}};
}
std::uint64_t outsideMismatches(const juce::Image& actual,bool stereo){
 const float sx=actual.getWidth()/1672.f,sy=actual.getHeight()/941.f;
 juce::Image expected(juce::Image::RGB,actual.getWidth(),actual.getHeight(),true);{juce::Graphics g(expected);g.setImageResamplingQuality(juce::Graphics::highResamplingQuality);g.drawImage(approved(stereo),expected.getBounds().toFloat(),juce::RectanglePlacement::stretchToFit);}
 const auto disks=knobs(stereo);auto rectangles=glass(stereo);for(const auto& k:disks)rectangles.push_back(k.read);
 auto rect=[&](juce::Rectangle<float> r){return juce::Rectangle<float>(r.getX()*sx,r.getY()*sy,r.getWidth()*sx,r.getHeight()*sy).expanded(1.01f);};
 std::uint64_t mismatches=0;
 for(int y=0;y<actual.getHeight();++y)for(int x=0;x<actual.getWidth();++x){const juce::Point<float> p(x+.5f,y+.5f);bool live=false;
  for(const auto& a:rectangles)if(rect(a).contains(p)){live=true;break;}
  if(!live)for(const auto& k:disks){const float dx=(p.x-k.x*sx),dy=(p.y-k.y*sy);const float r=k.r*std::min(sx,sy)+1.01f;if(dx*dx+dy*dy<=r*r){live=true;break;}}
  if(!live&&actual.getPixelAt(x,y)!=expected.getPixelAt(x,y))++mismatches;
 }
 return mismatches;
}
void verifyOutside(const juce::Image& image,bool stereo,const juce::String& why){const auto n=outsideMismatches(image,stereo);require(n==0,why+": outside-aperture differences="+juce::String(n));}
struct Gestures:juce::AudioProcessorParameter::Listener {int begin=0,end=0,values=0;void parameterValueChanged(int,float)override{++values;}void parameterGestureChanged(int,bool b)override{b?++begin:++end;}void clear(){begin=end=values=0;}};
juce::MouseEvent event(juce::Component& c,juce::Point<float> p,juce::Point<float> origin,int modifiers,bool dragged=false){const auto now=juce::Time::getCurrentTime();return {juce::Desktop::getInstance().getMainMouseSource(),p,juce::ModifierKeys(modifiers),1,0,0,0,0,&c,&c,now,origin,now,1,dragged};}
void click(juce::Component& c){auto pos=c.getLocalBounds().getCentre().toFloat();c.mouseDown(event(c,pos,pos,juce::ModifierKeys::leftButtonModifier));c.mouseUp(event(c,pos,pos,0));}
void roundtrip(hem::ModuleProcessor& p,bool stereo){juce::MemoryBlock data;p.getStateInformation(data);auto copy=processor(stereo);copy->setStateInformation(data.getData(),int(data.getSize()));for(const auto& spec:p.parameterSpecs())close(copy->state.getRawParameterValue(spec.id)->load(),p.state.getRawParameterValue(spec.id)->load(),.00001,"State roundtrip "+juce::String(spec.id));}
void controls(hem::ModuleProcessor& p,hem::NativePairApprovedEditor& e,bool stereo){
 e.setSize(1672,941);e.setVisible(true);
 for(const auto& spec:p.parameterSpecs()){
  auto* c=e.findChildWithID(spec.id);require(c!=nullptr,"Missing control "+juce::String(spec.id));auto* parameter=p.state.getParameter(spec.id);Gestures probe;parameter->addListener(&probe);
  const auto check=[&](double expected){close(p.state.getRawParameterValue(spec.id)->load(),expected,std::max(.0001f,spec.step*.51f),"APVTS binding "+juce::String(spec.id));};
  if(auto* s=dynamic_cast<juce::Slider*>(c)){
   close(s->getMinimum(),spec.minimum,1.e-6,"Changed minimum");close(s->getMaximum(),spec.maximum,1.e-6,"Changed maximum");close(s->getValue(),spec.initial,1.e-5,"Changed default");
   for(float value:{spec.minimum,spec.initial,spec.maximum}){s->setValue(value,juce::sendNotificationSync);check(value);roundtrip(p,stereo);}
   if(spec.minimum<0&&spec.maximum>0)close(s->valueToProportionOfLength(0),.5,1.e-7,"Bipolar zero not upright");
   set(p,spec.id,spec.initial);close(s->getValue(),spec.initial,.0001,"Host automation does not update dial");
   s->setValue(spec.minimum,juce::sendNotificationSync);probe.clear();const auto pos=juce::Point<float>(float(s->getProperties()["bodyX"]),float(s->getProperties()["bodyY"]));
   auto* hit=e.getComponentAt(e.getLocalPoint(s,pos.toInt()));require(hit==s||s->isParentOf(hit),"Pointer hit target misses dial");
   s->mouseDown(event(*s,pos,pos,juce::ModifierKeys::leftButtonModifier));s->mouseDrag(event(*s,pos+juce::Point<float>(0,-100),pos,juce::ModifierKeys::leftButtonModifier,true));s->mouseUp(event(*s,pos+juce::Point<float>(0,-100),pos,0,true));
   require(s->getValue()>spec.minimum&&probe.begin==1&&probe.end==1&&probe.values>0,"Actual pointer drag gesture failed "+juce::String(spec.id));check(s->getValue());
   for(bool maximum:{false,true}){probe.clear();const auto end=pos+juce::Point<float>(0,maximum?-1000.f:1000.f);
    s->mouseDown(event(*s,pos,pos,juce::ModifierKeys::leftButtonModifier));s->mouseDrag(event(*s,end,pos,juce::ModifierKeys::leftButtonModifier,true));s->mouseUp(event(*s,end,pos,0,true));check(maximum?spec.maximum:spec.minimum);
    require(probe.begin==1&&probe.end==1,"Endpoint pointer gesture failed");roundtrip(p,stereo);
   }
   probe.clear();s->mouseDoubleClick(event(*s,pos,pos,0));check(spec.initial);require(probe.begin==1&&probe.end==1,"Double click default gesture failed");
   s->setValue(spec.minimum,juce::sendNotificationSync);probe.clear();require(s->keyPressed(juce::KeyPress(juce::KeyPress::rightKey)),"Keyboard arrow unsupported");require(s->getValue()>spec.minimum&&probe.begin==1&&probe.end==1,"Keyboard gesture failed "+juce::String(spec.id)+" begin="+juce::String(probe.begin)+" end="+juce::String(probe.end)+" values="+juce::String(probe.values));check(s->getValue());
   // Actual editable Label/TextEditor child, committed through Slider's API.
   s->showTextBox();juce::Label* label=nullptr;for(auto* child:s->getChildren())if(auto* l=dynamic_cast<juce::Label*>(child);l&&l->getCurrentTextEditor())label=l;
   require(label!=nullptr,"Actual slider text editor absent");label->getCurrentTextEditor()->setText(juce::String(spec.maximum)+" "+spec.unit,false);s->hideTextBox(false);check(spec.maximum);
   s->setValue(spec.initial,juce::sendNotificationSync);require(probe.begin==probe.end,"Unbalanced text/key gestures");
  }else if(auto* b=dynamic_cast<juce::ToggleButton*>(c)){
   for(int i=0;i<2;++i){const bool next=!b->getToggleState();probe.clear();click(*b);check(next?1:0);require(probe.begin==1&&probe.end==1&&probe.values==1,"Actual toggle gesture failed");roundtrip(p,stereo);}
   set(p,spec.id,1);require(b->getToggleState(),"Toggle automation disconnected");set(p,spec.id,spec.initial);
  }else require(false,"Unexpected control class");
  parameter->removeListener(&probe);
 }
 require(!e.hostBypassState().has_value(),"Standalone editor invented host bypass binding");
 bool bypass=false;e.setHostBypassBinding({[&]()->std::optional<bool>{return bypass;},[&](bool v){bypass=v;}});e.requestHostBypass(true);require(bypass,"Host bypass binding broken");
 e.setHostBypassBinding({[]()->std::optional<bool>{return std::nullopt;},[&](bool){throw std::runtime_error("Stale binding written");}});e.requestHostBypass(false);e.setHostBypassBinding({});
}
float distanceToSegment(juce::Point<float> p,juce::Point<float> a,juce::Point<float> b){const auto d=b-a;const float t=std::clamp(((p.x-a.x)*d.x+(p.y-a.y)*d.y)/(d.x*d.x+d.y*d.y),0.f,1.f);return p.getDistanceFrom(a+d*t);}
bool pointerPixel(juce::Point<float> p,const Knob& k,double proportion){const float a=float(juce::MathConstants<double>::pi*(1.25+proportion*1.5));const auto unit=juce::Point<float>(std::sin(a),-std::cos(a));return distanceToSegment(p,juce::Point<float>(k.x,k.y)+unit*(k.r*.30f),juce::Point<float>(k.x,k.y)+unit*(k.r*.78f))<4;}
void material(hem::NativePairApprovedEditor& e,bool stereo){
 e.setSize(1672,941);
 for(const auto& k:knobs(stereo)){auto& s=*dynamic_cast<juce::Slider*>(e.findChildWithID(k.id));s.setValue(s.getMinimum(),juce::sendNotificationSync);const auto low=render(e);
  for(double target:{s.getDoubleClickReturnValue(),s.getMaximum()}){s.setValue(target,juce::sendNotificationSync);auto high=render(e);int checked=0,changed=0;
   for(int y=int(k.y-k.r+3);y<k.y+k.r-3;++y)for(int x=int(k.x-k.r+3);x<k.x+k.r-3;++x){const juce::Point<float> p(x+.5f,y+.5f);if(p.getDistanceFrom({k.x,k.y})>k.r-3||pointerPixel(p,k,0)||pointerPixel(p,k,s.valueToProportionOfLength(target)))continue;++checked;if(low.getPixelAt(x,y)!=high.getPixelAt(x,y))++changed;}
   require(checked>10000&&changed==0,"Dial material changes with value "+juce::String(k.id));
  }
  // Each baked pointer is longer than the moving indicator; probe its former
  // outer tip (including the diagonal ones), enabled AND disabled.
  for(bool enabled:{true,false}){s.setEnabled(enabled);s.setValue(s.getMaximum(),juce::sendNotificationSync);auto image=render(e);int mismatches=0;
   juce::Image expected(juce::Image::RGB,1672,941,true);{juce::Graphics g(expected);g.setImageResamplingQuality(juce::Graphics::highResamplingQuality);const auto fixed=juce::ImageCache::getFromMemory(ControlArt::dial_body_png,ControlArt::dial_body_pngSize);g.drawImage(fixed.getClippedImage({382,375,480,480}),{k.x-k.r,k.y-k.r,2*k.r,2*k.r},juce::RectanglePlacement::stretchToFit);}
   const auto direction=k.bakedUp?juce::Point<float>(0,-1):juce::Point<float>(-.7071068f,.7071068f);
   const auto tip=juce::Point<float>(k.x,k.y)+direction*(k.r*.88f);
   for(int y=int(tip.y)-2;y<=int(tip.y)+2;++y)for(int x=int(tip.x)-2;x<=int(tip.x)+2;++x){if(image.getPixelAt(x,y)!=expected.getPixelAt(x,y))++mismatches;}
   require(mismatches==0,"Baked needle survived "+juce::String(k.id)+(enabled?" enabled":" disabled"));
  }
  s.setEnabled(true);s.setValue(s.getDoubleClickReturnValue(),juce::sendNotificationSync);
 }
}
void feed(hem::ModuleProcessor& p,float l,float r,int blocks=1){juce::MidiBuffer midi;juce::AudioBuffer<float> data(2,256);for(int b=0;b<blocks;++b){for(int i=0;i<256;++i){data.setSample(0,i,l);data.setSample(1,i,r);}p.processBlock(data,midi);}}
void waitAnalysis(hem::ModuleProcessor& p,hem::NativePairApprovedEditor& e){const auto end=juce::Time::getMillisecondCounterHiRes()+1500;do{e.refreshParameterActivity();if(e.displayState().analysisLive)return;juce::Thread::sleep(3);}while(juce::Time::getMillisecondCounterHiRes()<end);require(false,"Analysis worker did not publish a new full output window");}
void meters(const juce::Image& image,bool stereo,const hem::PluginTelemetry::Levels& levels,bool live){
 const int ys=stereo?276:230,h=stereo?350:179;const int x[4]={stereo?110:102,stereo?174:176,stereo?1470:1471,stereo?1534:1545};
 for(int lane=0;lane<4;++lane){const float db=levels.peak[size_t(lane)]>0?20.f*std::log10(levels.peak[size_t(lane)]):-1000;
  if(live&&levels.rms[size_t(lane)]>.001f){const float position=std::clamp((20.f*std::log10(levels.rms[size_t(lane)])+60.f)/60.f,0.f,1.f);
   const float top=std::clamp(ys+h*(1-position)-.75f,float(ys),ys+h-1.5f);const auto c=image.getPixelAt(x[lane]+15,int(std::floor(top+.75f)));
   require(c.getRed()>170&&c.getGreen()>170&&c.getBlue()>170,"Independent RMS centre marker missing");
  }
  for(int segment=0;segment<24;++segment){const float threshold=-60.f+60.f*(segment+1)/24;const bool lit=live&&std::isfinite(db)&&db>=threshold;
   const auto expected=juce::Colour(!lit?0xff10232b:threshold>=-3?0xffff655b:threshold>=-12?0xffffbd59:0xff41d7c5);
   const int y=int(ys+(23-segment+.5)*h/24.);require(image.getPixelAt(x[lane]+3,y)==expected,"Independent meter segment oracle lane="+juce::String(lane)+" segment="+juce::String(segment));}
 }
}
void lifecycle(bool stereo,const juce::File& out){
 auto p=processor(stereo);p->prepareToPlay(48000,256);auto e=editor(*p);e->setSize(1672,941);
 const auto idle=render(*e);require(!e->displayState().live,"New editor live before callback");
 if(!stereo){require(idle.getPixelAt(1353,374)==juce::Colour(0xff020709),"Right-edge baked waveform residue");require(idle.getPixelAt(90,350)==juce::Colour(0xff020709),"Baked meter glow residue");}
 feed(*p,.5f,.05f,40);waitAnalysis(*p,*e);const auto live=render(*e);require(e->displayState().live&&e->displayState().analysisLive,"Fresh callback/window not live");meters(live,stereo,e->displayState().levels,true);verifyOutside(live,stereo,"active");save(live,out,stereo?"sipa-live":"transient-live");
 require(e->displayState().frame.phaseL[400]>.45f&&e->displayState().frame.phaseR[400]<.06f,"Goniometer is not real output L/R");
 auto reopened=editor(*p);reopened->setSize(1672,941);render(*reopened);require(!reopened->displayState().live&&!reopened->displayState().analysisLive,"Reopen revives frozen block or analysis");
 feed(*p,.25f,.01f);render(*reopened);require(reopened->displayState().live&&!reopened->displayState().analysisLive,"New block revived old analysis window");
 juce::Thread::sleep(780);const auto stale=render(*e);require(!e->displayState().live&&!e->displayState().analysisLive,"Stopped source not stale");meters(stale,stereo,e->displayState().levels,false);
 // One real sample advances producer meters but is too short to complete the
 // 256-sample worker packet. The old analysis frame remains deliberately stale.
 {juce::MidiBuffer midi;juce::AudioBuffer<float> tiny(2,1);tiny.setSample(0,0,.33f);tiny.setSample(1,0,.11f);p->processBlock(tiny,midi);}
 const auto freshLevels=render(*e);require(e->displayState().live&&!e->displayState().analysisLive,"Fresh producer levels incorrectly gated by stale analysis worker/frame");meters(freshLevels,stereo,e->displayState().levels,true);
 p->releaseResources();render(*e);require(!e->displayState().live&&e->displayState().status=="UNAVAILABLE","Release retains live state");
 p->prepareToPlay(96000,256);render(*e);require(!e->displayState().live&&!e->displayState().analysisLive,"Reprepare revives previous epoch");
 feed(*p,.4f,.1f,40);waitAnalysis(*p,*e);require(e->displayState().live,"Reprepare fresh callback did not recover");
 feed(*p,std::numeric_limits<float>::quiet_NaN(),.2f);const auto invalid=render(*e);require(!e->displayState().live&&e->displayState().status=="INVALID","NaN source still live");meters(invalid,stereo,e->displayState().levels,false);
 p->prepareToPlay(48000,256);render(*e);feed(*p,.2f,.1f,40);waitAnalysis(*p,*e);require(e->displayState().live,"Invalid recovery failed");
 feed(*p,std::numeric_limits<float>::infinity(),.2f);render(*e);require(!e->displayState().live,"Infinite source still live");
}
void correlation(const juce::File& out){
 auto p=processor(true);p->prepareToPlay(48000,256);auto e=editor(*p);e->setSize(1672,941);
 const auto check=[&](float expected,bool valid,const juce::String& why){const auto image=render(*e);require(e->displayState().live,"Correlation lacks fresh input");require(e->displayState().meter.valueAvailable==valid,why+" availability");if(valid)close(e->displayState().meter.value,expected,.00001,why);
  const int amberX=985,cyanX=1220;const auto background=juce::Colour(0xff020709);
  require(image.getPixelAt(amberX,343)==(valid&&expected<0?juce::Colour(0xffffb36b):background),why+" negative bar pixels");
  require(image.getPixelAt(cyanX,343)==(valid&&expected>0?juce::Colour(0xff00cddd):background),why+" positive bar pixels");
  if(valid&&expected==0)require(image.getPixelAt(1103,345)==juce::Colour(0xffd9e5ea),"Measured zero missing neutral marker");
  // Old right-hand cyan glow occupied this pixel outside the narrow new bar.
  require(image.getPixelAt(1266,339)==background,"Baked correlation bloom residue");save(image,out,why);
 };
 e->refreshParameterActivity();feed(*p,.3f,.3f);check(1,true,"correlation-positive");e->refreshParameterActivity();feed(*p,.3f,-.3f);check(-1,true,"correlation-negative");
 e->refreshParameterActivity();{juce::MidiBuffer midi;juce::AudioBuffer<float> data(2,256);for(int i=0;i<256;++i){data.setSample(0,i,(i&1)?.4f:-.4f);data.setSample(1,i,(i&2)?.4f:-.4f);}p->processBlock(data,midi);}check(0,true,"correlation-zero");
 e->refreshParameterActivity();feed(*p,.3f,0);check(0,false,"correlation-undefined-one-silent");e->refreshParameterActivity();feed(*p,0,0);check(0,false,"correlation-undefined-silence");
}
bool gainReadoutsMatch(const juce::Image& actual,const std::array<float,4>& gains){
 const auto number=[](float v){return(v>0?juce::String("+"):juce::String())+juce::String(v,2);};
 juce::Image expected(juce::Image::RGB,1672,941,true);juce::Graphics g(expected);
 for(int branch=0;branch<2;++branch){const auto area=branch?juce::Rectangle<int>(1454,482,135,31):juce::Rectangle<int>(83,482,136,31);
  g.setColour(juce::Colour(0xff020709));g.fillRect(area);g.setColour(juce::Colour(0xffffc374));g.setFont(juce::Font(juce::FontOptions("Arial Narrow",13.f,juce::Font::plain)));
  g.drawFittedText("PRE "+number(gains[size_t(branch)])+"\nPOST "+number(gains[size_t(branch+2)]),area,juce::Justification::centred,2);
  for(int y=area.getY();y<area.getBottom();++y)for(int x=area.getX();x<area.getRight();++x)if(actual.getPixelAt(x,y)!=expected.getPixelAt(x,y))return false;
 }
 return true;
}
void gain(){auto p=processor(false);p->prepareToPlay(48000,256);auto e=editor(*p);e->setSize(1672,941);
 for(float sign:{-1.f,1.f})for(float mix:{0.f,50.f,100.f}){set(*p,"attack",sign*9);set(*p,"sustain",sign*6);set(*p,"mix",mix);p->prepareToPlay(48000,256);render(*e);
  feed(*p,.02f,.002f,180);bool seen[2]={false,false};
  for(int block=0;block<30;++block){feed(*p,block%5==0?.7f:.015f,block%5==0?.07f:.0015f);render(*e);const auto& s=e->displayState();require(s.live,"Gain readout not live");
   const auto image=render(*e);require(gainReadoutsMatch(image,s.levels.gains),"Displayed signed pre/post gain numbers differ from real branch publication");
   for(int index=0;index<4;++index){auto wrong=s.levels.gains;wrong[size_t(index)]+=1;require(!gainReadoutsMatch(image,wrong),"Gain readout oracle accepted corrupted branch number");}
   for(int branch=0;branch<2;++branch){const float pre=s.levels.gains[size_t(branch)],post=s.levels.gains[size_t(branch+2)];
    require(pre*sign>=0,"Branch gain sign lost");seen[branch]=seen[branch]||std::abs(pre)>.01f;
    const float expected=20*std::log10(1+mix*.01f*(std::pow(10.f,pre/20)-1));close(post,expected,.002,"Independently calibrated post-mix branch dB");
   }
  }
  require(seen[0]&&seen[1],"Stimulus did not exercise both attack and sustain branches");
 }
}
bool cyanNear(const juce::Image& image,float x,float y){for(int dy=-2;dy<=2;++dy)for(int dx=-2;dx<=2;++dx){const auto c=image.getPixelAt(juce::roundToInt(x)+dx,juce::roundToInt(y)+dy);if(c.getRed()<90&&c.getGreen()>170&&c.getBlue()>170)return true;}return false;}
bool amberNear(const juce::Image& image,float x,float y){for(int dy=-2;dy<=2;++dy)for(int dx=-2;dx<=2;++dx){const auto c=image.getPixelAt(juce::roundToInt(x)+dx,juce::roundToInt(y)+dy);if(c.getRed()>200&&c.getGreen()>110&&c.getBlue()<100)return true;}return false;}
void analysisPixels(){
 for(bool stereo:{false,true}){auto p=processor(stereo);p->prepareToPlay(48000,256);auto e=editor(*p);e->setSize(1672,941);
  feed(*p,.5f,.1f,40);waitAnalysis(*p,*e);auto image=render(*e);
  // Independently derived signed +/-1FS mapping in each documented waveform.
  const float middle=stereo?358.f:358.5f,halfHeight=stereo?60.f:92.5f,x=stereo?650.f:850.f;
  require(cyanNear(image,x,middle-.5f*halfHeight),"Real output-L waveform sample painted at wrong sign/amplitude");
  require(amberNear(image,x,middle-.1f*halfHeight),"Real output-R waveform sample painted at wrong sign/amplitude");
  if(stereo){
   for(auto pair:{juce::Point<float>(.5f,.5f),{.5f,-.5f},{-.5f,.5f},{-.5f,-.5f}}){
    p->prepareToPlay(48000,256);e->refreshParameterActivity();feed(*p,pair.x,pair.y,40);waitAnalysis(*p,*e);image=render(*e);
    const float side=(pair.x-pair.y)*.5f,mid=(pair.x+pair.y)*.5f;
    const float px=435.5f+side*71.5f,py=359.5f-mid*71.5f;
    require(cyanNear(image,px,py),"Real output goniometer does not follow M-up/S-right axes");
    require(!cyanNear(image,435.5f-side*71.5f,359.5f+mid*71.5f),"Goniometer has a mirrored/fabricated point");
   }
  }
 }
}
void publicationClock(){
 hem::NativePairPublicationClock clock;clock.seed(8,std::nullopt,100);
 require(!clock.observe(8,false,0,0,false,100),"Failed read created live state");
 require(!clock.observe(8,true,99,256,true,101),"Contention fallback revived historical first coherent block");
 require(!clock.observe(8,true,99,256,true,102),"Unchanged coherent baseline became live");
 require(clock.observe(8,true,100,256,true,103),"New publication after contention not live");
 require(!clock.observe(8,true,101,0,true,104),"Zero-frame publication revived view");
 require(clock.observe(8,true,102,256,true,105),"Positive frame did not recover");
 require(!clock.observe(8,true,103,256,true,900),"Delayed first GUI poll revived historical producer block");
 require(clock.observe(8,true,104,256,true,901),"New producer block after delayed GUI baseline not live");
 require(!clock.observe(9,false,0,0,false,901),"New epoch without coherent audio became live");
 require(clock.observe(9,true,1,256,true,902),"New-epoch fresh publication failed");
 hem::NativePairFrameBaseline frames;frames.seed(std::nullopt);
 require(!frames.newer(87)&&!frames.newer(87),"Failed constructor frame read revived old analysis");
 require(frames.newer(88),"New frame after contended baseline was not accepted");
 frames.seed(0);require(frames.newer(1),"Known empty new epoch suppressed first frame");
}
void raster(bool stereo,const juce::File& out){auto p=processor(stereo);auto e=editor(*p);controls(*p,*e,stereo);material(*e,stereo);
 for(auto size:{juce::Point<int>(1672,941),{1280,720},{1000,563},{3344,1882},{1373,773}})for(bool enabled:{true,false}){
  e->setSize(size.x,size.y);e->setEnabled(enabled);auto image=render(*e);const auto name=juce::String(stereo?"sipa":"transient")+"-"+juce::String(size.x)+(enabled?"-idle":"-disabled");verifyOutside(image,stereo,name);
  for(const auto& spec:p->parameterSpecs()){const auto* c=e->findChildWithID(spec.id);require(e->getLocalBounds().contains(c->getBounds()),"Control outside resized editor");require(c->getWidth()>=30&&c->getHeight()>=25,"Hit target too small");}
  if(size.x==1672||size.x==1373)save(image,out,name);
 }
 // Independent negative control: a single crown/ghost/ring pixel MUST fail.
 e->setSize(1672,941);for(const auto point:{juce::Point<int>(500,70),{100,800},stereo?juce::Point<int>(345,610):juce::Point<int>(247,686)}){
  auto broken=render(*e);broken.setPixelAt(point.x,point.y,juce::Colours::magenta);require(outsideMismatches(broken,stereo)>0,"Outside-aperture oracle accepted crown/ghost/ring mutant");}
 p->prepareToPlay(48000,256);e->refreshParameterActivity();feed(*p,.4f,.04f,40);waitAnalysis(*p,*e);
 for(auto size:{juce::Point<int>(1672,941),{1280,720},{1000,563},{3344,1882},{1373,773}})for(bool enabled:{true,false}){
  e->setSize(size.x,size.y);e->setEnabled(enabled);e->refreshParameterActivity();feed(*p,.4f,.04f,8);e->refreshParameterActivity();
  const auto active=render(*e);require(e->displayState().live,"Active resize matrix lost fresh producer blocks");verifyOutside(active,stereo,"active resize matrix");
 }
}
}
int main(int argc,char** argv){juce::ScopedJuceInitialiser_GUI gui;try{juce::File out;if(argc>1){out=juce::File(argv[1]);require(out.createDirectory().wasOk(),"Cannot create output directory");}
 require(juce::SHA256(ApprovedArt::approved_01_png,size_t(ApprovedArt::approved_01_pngSize)).toHexString()=="30224311bfae9b0cd7a9daa8b3ddfe5555faf1ebb73b8f00fb47d02f1a824b4c","Approved01 asset bytes changed");
 require(juce::SHA256(ApprovedArt::approved_12_png,size_t(ApprovedArt::approved_12_pngSize)).toHexString()=="c55489e6b21195a15d2f5f7cee02bdb2b0945d2d81dbe33d5edac3e2edac1baf","Approved12 asset bytes changed");
 for(bool stereo:{false,true}){raster(stereo,out);std::cout<<"PASS "<<(stereo?"sipa":"transient")<<" independent approved pixels, actual controls/state, fixed material, baked-needle removal, disabled/1x/2x/odd sizes\n";lifecycle(stereo,out);std::cout<<"PASS lifecycle/new-publication/stale/release/reprepare/reopen/NaN/Inf\n";}
 publicationClock();correlation(out);gain();analysisPixels();std::cout<<"PASS real signed correlation (-1/0/+1/undefined), real output samples, signed pre/post-mix branch telemetry; headless in-memory only\n";return 0;
}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
