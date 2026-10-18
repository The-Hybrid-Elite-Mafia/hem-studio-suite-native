#include "../Source/Plugins/PluginRegistry.h"
#include "../Source/Plugins/FoundationEditors.h"
#include "FoundationPresentationChecks.h"
#include <iostream>
#include <stdexcept>
#include <tuple>
namespace {
using Rect=juce::Rectangle<float>;
void need(bool b,const juce::String& m){if(!b)throw std::runtime_error(m.toStdString());}
struct Face {const char* id;const char* bytes;int size;std::vector<Rect> rectangles;std::vector<std::array<float,3>> discs;};
// Independently audited reference apertures. These do not query editor geometry
// or share its helpers. Labels, crowns, ghosts, ticks and rings are NOT excluded.
std::vector<Face> faces(){return {
 {"dc-subsonic",ApprovedArt::approved_11_png,ApprovedArt::approved_11_pngSize,
 {{104,69,200,32},{84,210,124,303},{1461,210,124,303},{331,251,1036,233},{447,830,216,36},{816,611,64,60},{816,778,64,60},{1024,672,206,30},{1078,782,93,62},{824,684,51,25},{824,852,51,25},{1020,614,216,34},{1096,850,63,30},{1342,621,241,62},{1342,785,241,62}},{{555,726,87}}},
 {"smart-gate",ApprovedArt::approved_08_png,ApprovedArt::approved_08_pngSize,
 {{104,69,200,32},{96,216,88,249},{1489,216,86,249},{326,205,930,233},{1336,216,64,156},{1341,415,55,51},{355,517,126,44},{512,517,130,44},{1145,516,182,43},{1364,517,181,42},{242,780,93,31},{394,780,92,31},{538,780,91,31},{686,780,82,31},{822,780,88,31},{976,780,102,31},{1320,780,100,31},{1488,780,105,31},{1150,780,98,31}},{{287,706,50},{437,706,50},{581,706,50},{723,706,50},{867,706,50},{1025,706,50},{1369,706,50},{1539,706,50},{1197,706,50}}},
 {"vocal-rider",ApprovedArt::approved_05_png,ApprovedArt::approved_05_pngSize,
 {{104,69,200,32},{121,198,124,226},{1428,198,122,226},{360,236,865,174},{318,701,131,34},{540,701,131,34},{1005,701,129,34},{1223,701,127,34},{98,701,128,34},{1438,533,145,45},{1438,604,145,44},{1438,651,145,17},{1427,688,166,53},{806,510,63,272},{760,798,150,34},{1268,266,67,147}},{{160,615,64},{385,615,64},{605,615,64},{1068,615,64},{1289,615,64}}},
 {"plosive-control",ApprovedArt::approved_07_png,ApprovedArt::approved_07_pngSize,
 {{104,69,200,32},{86,211,86,235},{1503,211,84,235},{292,212,974,224},{1348,207,63,156},{1350,415,60,51},{105,718,151,25},{371,718,148,25},{633,718,152,25},{893,718,152,25},{1155,718,152,25},{1421,718,152,25},{592,826,226,51},{858,826,222,51}},{{180,632,70},{444,632,70},{706,632,70},{967,632,70},{1229,632,70},{1494,632,70}}},
 {"hum-buzz",ApprovedArt::approved_10_png,ApprovedArt::approved_10_pngSize,
 {{104,69,200,32},{114,210,103,268},{1448,210,106,250},{368,259,974,260},{145,540,48,45},{322,828,121,29},{531,828,129,29},{1457,828,129,29},{1240,828,121,29},{737,787,153,48},{1015,828,127,29},{1418,495,174,34},{1418,552,174,32},{735,690,91,60},{847,690,92,60},{912,786,26,23},{912,815,26,23},{1235,865,132,12}},{{383,744,61},{598,744,61},{1522,744,61},{1301,744,61},{1078,744,61}}}
 };}
juce::Image render(juce::Component& c,int dpi=1){juce::Image im(juce::Image::ARGB,c.getWidth()*dpi,c.getHeight()*dpi,true);{juce::Graphics g(im);g.addTransform(juce::AffineTransform::scale(float(dpi)));c.paintEntireComponent(g,true);}return im;}
juce::Image reference(const Face& f,int w,int h){auto art=juce::ImageCache::getFromMemory(f.bytes,f.size);juce::Image im(juce::Image::ARGB,w,h,true);{juce::Graphics g(im);g.setImageResamplingQuality(juce::Graphics::highResamplingQuality);g.drawImage(art,im.getBounds().toFloat(),juce::RectanglePlacement::stretchToFit);}return im;}
bool aperture(const Face& f,float x,float y){for(auto r:f.rectangles)if(r.expanded(2).contains(x,y))return true;for(auto d:f.discs)if(std::hypot(x-d[0],y-d[1])<=d[2]+2)return true;return false;}
int outsideDifferences(const Face& f,const juce::Image& actual,const juce::Image& expected){int bad=0;const float sx=1672.f/actual.getWidth(),sy=941.f/actual.getHeight();for(int y=0;y<actual.getHeight();++y)for(int x=0;x<actual.getWidth();++x)if(!aperture(f,(x+.5f)*sx,(y+.5f)*sy)&&actual.getPixelAt(x,y)!=expected.getPixelAt(x,y)){if(bad<4)std::cerr<<f.id<<" outside "<<x<<","<<y<<" expected="<<expected.getPixelAt(x,y).toString()<<" actual="<<actual.getPixelAt(x,y).toString()<<"\n";++bad;}return bad;}
void save(const juce::Image& im,juce::File dir,const juce::String& name){if(dir==juce::File())return;auto stream=dir.getChildFile(name+".png").createOutputStream();need(stream!=nullptr,"Output unavailable");stream->setPosition(0);stream->truncate();need(juce::PNGImageFormat().writeImageToStream(im,*stream),"PNG failed");}
void process(hem::ModuleProcessor& p,int blocks=4,float l=.16f,float r=.04f){juce::AudioBuffer<float> b(2,512);juce::MidiBuffer m;for(int k=0;k<blocks;++k){for(int i=0;i<512;++i){b.setSample(0,i,l);b.setSample(1,i,r);}p.processBlock(b,m);}}
juce::MouseEvent event(juce::Component& c,juce::Point<float> point,int flags=0,bool dragged=false){const auto now=juce::Time::getCurrentTime();return {juce::Desktop::getInstance().getMainMouseSource(),point,juce::ModifierKeys(flags),1,0,0,0,0,&c,&c,now,c.getLocalBounds().getCentre().toFloat(),now,1,dragged};}
void fixedMaterial(juce::Slider& s){const float r=float(s.getProperties()["bodyRadius"]);if(r<1)return;const float cx=float(s.getProperties()["bodyX"]),cy=float(s.getProperties()["bodyY"]);const auto old=s.getValue();s.setValue(s.getMinimum(),juce::sendNotificationSync);const auto lo=render(s);s.setValue(s.getMaximum(),juce::sendNotificationSync);const auto hi=render(s);const auto a=s.getRotaryParameters();auto distance=[&](int x,int y,float angle){const auto start=juce::Point<float>(cx+std::sin(angle)*r*.30f,cy-std::cos(angle)*r*.30f),end=juce::Point<float>(cx+std::sin(angle)*r*.78f,cy-std::cos(angle)*r*.78f);juce::Point<float> nearest;return juce::Line<float>(start,end).getDistanceFromPoint({float(x),float(y)},nearest);};int staticCount=0,changed=0;for(int y=0;y<lo.getHeight();++y)for(int x=0;x<lo.getWidth();++x)if(std::hypot(x-cx,y-cy)<r*.9f){if(std::min(distance(x,y,a.startAngleRadians),distance(x,y,a.endAngleRadians))>std::max(4.f,r*.06f)){++staticCount;need(lo.getPixelAt(x,y)==hi.getPixelAt(x,y),"Material changed away from pointer: "+s.getComponentID());}else changed+=lo.getPixelAt(x,y)!=hi.getPixelAt(x,y);}need(staticCount>100&&changed>8,"Material/needle test has no sensitivity");s.setValue(old,juce::sendNotificationSync);}
void bakedPointers(const Face& f,hem::FoundationEditor& editor){
 editor.setSize(1672,941);const auto artwork=juce::ImageCache::getFromMemory(f.bytes,f.size);
 const auto material=juce::ImageCache::getFromMemory(ControlArt::dial_body_png,ControlArt::dial_body_pngSize).getClippedImage({382,375,480,480});
 for(auto* component:editor.getChildren())if(auto* s=dynamic_cast<juce::Slider*>(component);s&&s->isVisible()&&float(s->getProperties()["bodyRadius"])>0){
  const float cx=s->getX()+float(s->getProperties()["bodyX"]),cy=s->getY()+float(s->getProperties()["bodyY"]),r=float(s->getProperties()["bodyRadius"]);
  const auto found=std::find_if(f.discs.begin(),f.discs.end(),[&](auto d){return std::hypot(d[0]-cx,d[1]-cy)<.5f;});need(found!=f.discs.end()&&std::abs((*found)[2]-r)<.01,"Inner-body radius is not the independently measured disc");
  const auto saved=s->getValue();s->setValue(s->getMaximum(),juce::sendNotificationSync);
  juce::Image expected(juce::Image::ARGB,1672,941,true);{juce::Graphics g(expected);juce::Path clip;clip.addEllipse(cx-r,cy-r,2*r,2*r);g.reduceClipRegion(clip);g.setColour(juce::Colour(0xff080d12));g.fillEllipse(cx-r,cy-r,2*r,2*r);g.setImageResamplingQuality(juce::Graphics::highResamplingQuality);g.drawImage(material,{cx-r,cy-r,2*r,2*r},juce::RectanglePlacement::stretchToFit);}
  const auto angle=s->getRotaryParameters().endAngleRadians;const auto start=juce::Point<float>(cx+std::sin(angle)*r*.30f,cy-std::cos(angle)*r*.30f),end=juce::Point<float>(cx+std::sin(angle)*r*.78f,cy-std::cos(angle)*r*.78f);
  for(bool enabled:{true,false}){editor.setEnabled(enabled);const auto actual=render(editor);int checked=0;for(int y=int(cy-r+1);y<int(cy+r-1);++y)for(int x=int(cx-r+1);x<int(cx+r-1);++x){juce::Point<float> nearest;if(std::hypot(x-cx,y-cy)>r-1||juce::Line<float>(start,end).getDistanceFromPoint({float(x),float(y)},nearest)<5)continue;const auto old=artwork.getPixelAt(x,y);if(std::min({old.getRed(),old.getGreen(),old.getBlue()})<180)continue;++checked;need(actual.getPixelAt(x,y)==expected.getPixelAt(x,y),"Baked pointer/highlight leaked through fixed body: "+s->getComponentID());}need(checked>12,"Baked-pointer oracle has insufficient authored bright pixels");}
  editor.setEnabled(true);s->setValue(saved,juce::sendNotificationSync);
 }
 std::cout<<"PASS "<<f.id<<" authored bright-pointer pixels replaced by exact fixed material, enabled and disabled\n";
}

void controls(hem::ModuleProcessor& p,hem::FoundationEditor& e){
 for(auto* a:e.getChildren())if(dynamic_cast<juce::Slider*>(a)&&a->isVisible())for(auto* b:e.getChildren())if(a!=b&&dynamic_cast<juce::Slider*>(b)&&b->isVisible())need(!a->getBounds().intersects(b->getBounds()),"Visible dial hit targets overlap");
 for(auto* child:e.getChildren())if(auto* s=dynamic_cast<juce::Slider*>(child);s&&s->isVisible()){
  auto* watched=p.state.getParameter(s->getComponentID());if(!watched){const auto id=s->getComponentID();watched=p.state.getParameter(id=="riderSensitivity"?"floor":id=="plosiveSensitivity"?"threshold":"q");}presentationChecks::Gestures keyEvents;watched->addListener(&keyEvents);const auto saved=s->getValue();const bool enabled=s->isEnabled();s->setEnabled(true);fixedMaterial(*s);
  s->setValue(s->getMinimum(),juce::sendNotificationSync);const int keyBegins=keyEvents.begins;need(s->keyPressed(juce::KeyPress(juce::KeyPress::rightKey))&&s->getValue()>s->getMinimum(),"Keyboard edit failed "+s->getComponentID());need(keyEvents.begins==keyBegins+1&&keyEvents.begins==keyEvents.ends,"Arrow edit lacks one balanced gesture "+s->getComponentID());
  const auto at=s->getLocalBounds().getCentre().toFloat();const auto before=s->getValue();s->mouseDown(event(*s,at,juce::ModifierKeys::leftButtonModifier));s->mouseDrag(event(*s,at+juce::Point<float>(0,-20),juce::ModifierKeys::leftButtonModifier,true));s->mouseUp(event(*s,at,0,true));need(s->getValue()>before,"Actual drag failed "+s->getComponentID());
  s->mouseDoubleClick(event(*s,at));if(auto* parameter=p.state.getParameter(s->getComponentID()))need(std::abs(s->getValue()-parameter->convertFrom0to1(parameter->getDefaultValue()))<.002,"Legacy default mismatch "+s->getComponentID());
  const auto typed=(s->getMinimum()+s->getMaximum())*.5;s->showTextBox();juce::TextEditor* typing=nullptr;for(auto* child:s->getChildren())if(auto* label=dynamic_cast<juce::Label*>(child))if(label->getCurrentTextEditor())typing=label->getCurrentTextEditor();need(typing!=nullptr,"Actual text editor unavailable "+s->getComponentID());const auto previous=s->getValue();typing->setText(juce::String(typed,6)+s->getTextValueSuffix());s->hideTextBox(true);need(s->getValue()==previous,"Cancelled text entry changed parameter");s->showTextBox();for(auto* child:s->getChildren())if(auto* label=dynamic_cast<juce::Label*>(child))if(label->getCurrentTextEditor())typing=label->getCurrentTextEditor();const int textBegins=keyEvents.begins;typing->setText(juce::String(typed,6)+s->getTextValueSuffix());s->hideTextBox(false);need(std::abs(s->getValue()-typed)<std::max(.11,s->getInterval()),"Actual text commit failed "+s->getComponentID());need(keyEvents.begins==keyEvents.ends&&keyEvents.begins>=textBegins,"Text commit left open parameter gesture");
  s->setValue(saved,juce::sendNotificationSync);s->setEnabled(enabled);watched->removeListener(&keyEvents);
 }
}
void slope(){auto p=hem::PluginRegistry::create("dc-subsonic");std::unique_ptr<juce::AudioProcessorEditor> editor(p->createEditor());auto& e=dynamic_cast<hem::FoundationEditor&>(*editor);auto* combo=dynamic_cast<juce::ComboBox*>(presentationChecks::find(e,"steep"));need(combo&&combo->getNumItems()==2,"Slope is not a genuine two-choice ComboBox");auto* parameter=p->state.getParameter("steep");need(dynamic_cast<juce::AudioParameterBool*>(parameter),"Stored slope is no longer boolean");need(!combo->isEnabled()&&combo->getSelectedId()==1,"Disabled/default slope");presentationChecks::set(*p,"subOn",1);e.refreshParameterActivity();presentationChecks::Gestures gestures;parameter->addListener(&gestures);for(int id:{2,1,2}){combo->setSelectedId(id,juce::sendNotificationSync);need(p->state.getRawParameterValue("steep")->load()==(id==2?1.f:0.f),"Slope selection mapping");}need(gestures.begins==3&&gestures.ends==3,"Slope selection gestures");presentationChecks::set(*p,"steep",0);e.refreshParameterActivity();need(combo->getSelectedId()==1,"External slope automation");need(combo->keyPressed(juce::KeyPress(juce::KeyPress::downKey)),"Slope keyboard not consumed");combo->handleAsyncUpdate();need(p->state.getRawParameterValue("steep")->load()==1,"Slope keyboard choice not applied");need(combo->keyPressed(juce::KeyPress(juce::KeyPress::upKey)),"Slope up key not consumed");combo->handleAsyncUpdate();need(p->state.getRawParameterValue("steep")->load()==0,"Slope up choice not applied");const int before=gestures.begins;combo->hidePopup();need(gestures.begins==before,"Popup dismissal changes parameter");presentationChecks::set(*p,"steep",1);juce::MemoryBlock state;p->getStateInformation(state);auto restored=hem::PluginRegistry::create("dc-subsonic");restored->setStateInformation(state.getData(),int(state.getSize()));need(restored->state.getRawParameterValue("steep")->load()==1,"Slope bool state round-trip");presentationChecks::set(*p,"subOn",0);e.refreshParameterActivity();need(!combo->isEnabled(),"Slope remains active with subsonic filter off");need(!combo->keyPressed(juce::KeyPress(juce::KeyPress::upKey))&&p->state.getRawParameterValue("steep")->load()==1,"Disabled slope keyboard changed state");parameter->removeListener(&gestures);std::cout<<"PASS two-choice bool slope selection, balanced gestures, keyboard, automation, bool round-trip, disabled and non-open dismissal (native popup unavailable)\n";}
void presentState(const Face& face){
 auto p=hem::PluginRegistry::create(face.id);std::unique_ptr<juce::AudioProcessorEditor> owned(p->createEditor());auto& e=dynamic_cast<hem::FoundationEditor&>(*owned);e.setSize(1672,941);
 const auto idle=render(e);p->prepareToPlay(48000,512);process(*p,1);const auto warm=render(e);
 auto inject=[&]{process(*p,1);auto& telemetry=const_cast<hem::FoundationTelemetry&>(dynamic_cast<hem::FoundationDisplaySource&>(*p).displayTelemetry());for(int i=0;i<512;++i)telemetry.observe({.1,.03},{.09f,.025f},6,true);};
 inject();const auto active=render(e);auto difference=[](const juce::Image& a,const juce::Image& b,juce::Rectangle<int> r){int n=0;for(int y=r.getY();y<r.getBottom();++y)for(int x=r.getX();x<r.getRight();++x)n+=a.getPixelAt(x,y)!=b.getPixelAt(x,y);return n;};
 juce::Rectangle<int> now;
 const juce::String id(face.id);if(id=="smart-gate")now={1341,415,55,51};else if(id=="plosive-control")now={1350,415,60,51};else if(id=="vocal-rider")now={806,510,63,272};else if(id=="dc-subsonic")now={1342,621,241,62};
 if(id=="dc-subsonic"){need(difference(idle,warm,now)>20,"DC warm-up not shown");process(*p,60);need(difference(warm,render(e),now)>20,"DC half-second measurement missing");}
 if(!now.isEmpty()){
  if(id!="dc-subsonic")need(difference(idle,active,now)>20,"Fresh measurement not visible "+id);
  juce::Thread::sleep(780);const auto stopped=render(e);need(difference(idle,stopped,now)==0,"Stopped measurement remains active "+id);
  inject();render(e);p->releaseResources();need(difference(idle,render(e),now)==0,"Released measurement remains active "+id);
  p->prepareToPlay(48000,512);inject();render(e);std::unique_ptr<juce::AudioProcessorEditor> reopened(p->createEditor());reopened->setSize(1672,941);need(difference(idle,render(*reopened),now)==0,"Reopened editor promoted old measurement "+id);
 }
 std::cout<<"PASS "<<face.id<<" actual present-tense raster freshness and unavailable state\n";
}

void humWaitingNumericLabels(juce::File dir){
 auto processor=hem::PluginRegistry::create("hum-buzz");
 std::unique_ptr<juce::AudioProcessorEditor> editor(processor->createEditor());editor->setSize(1672,941);
 hem::HumAnalysisFrame frame;need(!dynamic_cast<hem::HumAnalysisSource&>(*processor).readHumAnalysis(frame),"Waiting-label fixture unexpectedly has a measured window");
 // Independent complete-label oracle. Coordinates and typography were audited
 // from the canonical Hum graph, not read from the editor or its plot helpers.
 // Checking every expected ink pixel catches truncated digits, including the
 // prior opaque placeholder's partial deletion of the dB gutter and endpoints.
 struct LabelOracle {const char* text;juce::Rectangle<int> bounds;};
 const std::array<LabelOracle,7> labels{{{"0",{370,279,40,17}}, {"-30",{370,325,40,17}},
     {"-60",{370,371,40,17}}, {"-90",{370,417,40,17}}, {"-120",{370,463,40,17}},
     {"30",{414,473,40,17}}, {"1500",{1300,473,40,17}}}};
 for(int dpi:{1,2}){
  juce::Image expected(juce::Image::ARGB,1672*dpi,941*dpi,true);
  {juce::Graphics g(expected);g.fillAll(juce::Colour(0xff020609));g.addTransform(juce::AffineTransform::scale(float(dpi)));
   g.setFont(juce::Font(juce::FontOptions("Arial Narrow",14.f,juce::Font::plain)));g.setColour(juce::Colour(0xffb8dce5));
   for(const auto& label:labels)g.drawText(label.text,label.bounds,juce::Justification::centred);}
  const auto actual=render(*editor,dpi);
  for(const auto& label:labels){int ink=0,missing=0;const auto r=label.bounds*dpi;
   for(int y=r.getY();y<r.getBottom();++y)for(int x=r.getX();x<r.getRight();++x)
    if(expected.getPixelAt(x,y)!=juce::Colour(0xff020609)){++ink;missing+=actual.getPixelAt(x,y)!=expected.getPixelAt(x,y);}
   need(ink>15,"Waiting numeric-label oracle has no ink");need(missing==0,"Waiting Hum numeric label clipped/erased: "+juce::String(label.text)+" at "+juce::String(dpi)+"x; missing="+juce::String(missing));
   std::cout<<"HUM WAITING LABEL "<<label.text<<" dpi="<<dpi<<" completeInkPixels="<<ink<<"\n";
  }
  save(actual,dir,"hum-buzz-waiting-labels-"+juce::String(dpi)+"x");
 }
 std::cout<<"PASS Hum waiting state preserves every numeric glyph: 0/-30/-60/-90/-120 dB and 30/1500 Hz at1x/2x\n";
}

void humMeasured(juce::File dir){
 auto p=hem::PluginRegistry::create("hum-buzz");p->prepareToPlay(48000,512);std::unique_ptr<juce::AudioProcessorEditor> owned(p->createEditor());auto& e=dynamic_cast<hem::FoundationEditor&>(*owned);e.setSize(1672,941);
 auto& width=presentationChecks::slider(e,"notchWidthHz");const auto q=p->state.getRawParameterValue("q")->load();auto& history=dynamic_cast<hem::FoundationDisplaySource&>(*p);auto& analysis=dynamic_cast<hem::HumAnalysisSource&>(*p);
 auto sine=[&](float hz,float amplitude,int blocks){juce::AudioBuffer<float> buffer(2,512);juce::MidiBuffer midi;for(int k=0;k<blocks;++k){for(int i=0;i<512;++i){const float sample=amplitude*std::sin(float((k*512+i)*juce::MathConstants<double>::twoPi*hz/48000));buffer.setSample(0,i,sample);buffer.setSample(1,i,sample*.37f);}p->processBlock(buffer,midi);if(k%32==0)render(e);}};
 auto read=[&]{hem::HumAnalysisFrame frame;for(int i=0;i<300&&!analysis.readHumAnalysis(frame);++i)juce::Thread::sleep(2);need(analysis.readHumAnalysis(frame),"Hum analysis failed to publish real window");return frame;};
 auto bright=[](const juce::Image& im){const auto c=im.getPixelAt(167,562);return c.getGreen()>180&&c.getBlue()>180;};
 need(!bright(render(e)),"Hum lamp lit without audio");sine(50,.1f,1);render(e);sine(50,.1f,240);auto frame=read();need(frame.detected&&frame.fundamental==50,"Measured 50 Hz input not detected");need(bright(render(e)),"Fresh measured Hum detection lamp absent");save(render(e),dir,"hum-buzz-detected");juce::Thread::sleep(780);need(!bright(render(e)),"Stale Hum window retained active lamp");
 // One new audio block can refresh levels; a reopened view still requires a
 // newly published Hum analysis window before indicating current detection.
 sine(50,.1f,1);render(e);std::unique_ptr<juce::AudioProcessorEditor> reopened(p->createEditor());reopened->setSize(1672,941);sine(50,.1f,1);need(!bright(render(*reopened)),"Reopen promoted an old detected Hum window");
 presentationChecks::set(*p,"fundamental",60);e.refreshParameterActivity();need(p->state.getRawParameterValue("q")->load()==q,"Frequency UI update mutated Q");sine(60,.1f,4);e.refreshParameterActivity();hem::FoundationDisplayFrame measured;need(history.displayTelemetry().read(history.displayTelemetry().latestSerial(),measured),"Processed-frequency frame missing");need(std::abs(width.getValue()-hem::ui::foundationMapping::notchWidth(measured.values[13],48000,q))<1e-7,"Width is not bound to processed frequency");need(!bright(render(e)),"Old frequency detection survives settings invalidation");
 presentationChecks::set(*p,"sensitivity",0);sine(60,.0001f,240);frame=read();need(!frame.detected&&frame.thresholdDb==-30,"Sensitivity threshold not applied to analysis window");need(!bright(render(e)),"Below-threshold Hum lamp is active");
 presentationChecks::set(*p,"sensitivity",100);sine(60,.0001f,240);frame=read();need(frame.detected&&frame.thresholdDb==-90,"High sensitivity fails actual low-level hum");need(bright(render(e)),"Detected low-level Hum lamp missing");need(p->state.getRawParameterValue("fundamental")->load()==60,"Analysis auto-tuned the manual frequency");p->releaseResources();need(!bright(render(e)),"Released Hum lamp remains active");
 for(double rate:{44100.,48000.,88200.,96000.,192000.,384000.}){p->prepareToPlay(rate,512);for(float hz:{40.f,50.f,60.f,70.f}){presentationChecks::set(*p,"fundamental",hz);process(*p,int(std::ceil(rate/120/512))+1);e.refreshParameterActivity();hem::FoundationDisplayFrame f;need(history.displayTelemetry().read(history.displayTelemetry().latestSerial(),f),"Mapping-rate telemetry absent");for(double requested:{10.,17.3,30.,60.}){width.setValue(hem::ui::foundationMapping::notchWidth(f.values[13],rate,requested),juce::sendNotificationSync);const auto stored=p->state.getRawParameterValue("q")->load();need(std::abs(stored-requested)<.051,"Exact width/Q inverse across sample rates");need(std::abs(width.getValue()-hem::ui::foundationMapping::notchWidth(f.values[13],rate,stored))<1e-7,"Displayed width does not use snapped stored Q");}}}
 presentationChecks::set(*p,"fundamental",50);presentationChecks::button(e,"fundamental-step-up").onClick();need(std::abs(p->state.getRawParameterValue("fundamental")->load()-50.01f)<1e-4,"Fine frequency step is not +0.01 Hz");presentationChecks::button(e,"fundamental-step-down").onClick();need(std::abs(p->state.getRawParameterValue("fundamental")->load()-50.f)<1e-4,"Fine frequency step is not -0.01 Hz");
 std::cout<<"PASS Hum real detection/threshold/manual tuning/processed frequency, stale/release/reopen, exact width-Q at six rates and 0.01 Hz steps\n";
}

void freshness(){hem::PluginTelemetry meter(false,false);hem::FoundationTelemetry history;meter.prepare(48000);history.prepare(48000);float l[512],r[512];std::fill_n(l,512,.25f);std::fill_n(r,512,.0625f);auto publish=[&]{meter.input(l,r,512);meter.output(l,r,512);for(int i=0;i<512;++i)history.observe({.25,.0625},{.25f,.0625f},3,true);};publish();hem::foundationPresentation::Freshness f;f.arm(&meter,&history);f.poll(&meter,&history,true,10);need(!f.live&&!f.measured,"Editor opened on old frame claimed LIVE");publish();f.poll(&meter,&history,true,20);need(f.live&&f.measured,"New frame missing LIVE");f.poll(&meter,&history,true,771);need(!f.live&&!f.measured&&f.status()=="STALE","Stopped audio claimed LIVE");publish();f.poll(&meter,&history,true,800);need(f.live,"Resumed frame missing LIVE");meter.suspend();f.poll(&meter,&history,false,801);need(!f.live&&!f.measured&&f.status()=="UNAVAILABLE","Release claimed LIVE");meter.prepare(48000);history.prepare(48000);f.poll(&meter,&history,true,802);need(!f.live&&!f.measured,"Reprepare inherited old frame");publish();f.poll(&meter,&history,true,803);need(f.live&&f.measured,"New generation unavailable");l[0]=std::numeric_limits<float>::quiet_NaN();publish();f.poll(&meter,&history,true,804);need(!f.live&&!f.measured&&f.status()=="INVALID","Invalid input claimed measured");std::cout<<"PASS GUI epoch/block/generation/serial freshness: open, live, stop, resume, release, prepare, invalid\n";}
void invalidPublications(){
 using hem::foundationPresentation::Freshness;hem::PluginTelemetry::Levels l;l.epoch=4;l.blocks=7;l.samples=512;l.rate=48000;l.valid=true;hem::FoundationDisplayFrame h;h.generation=9;h.serial=12;h.samples=400;
 Freshness f;f.arm(4,0,false,9,12);f.observe(l,true,4,h,true,9,true,100);need(!f.live,"Failed initial read promoted historical baseline");++l.blocks;++h.serial;f.observe(l,true,4,h,true,9,true,101);need(f.live&&f.measured,"Post-contention publication not live");
 l.samples=0;++l.blocks;++h.serial;f.observe(l,true,4,h,true,9,true,900);need(!f.live&&!f.measured,"Zero-sample publication revived stale state");l.samples=512;l.valid=false;++l.blocks;f.observe(l,true,4,h,true,9,true,901);need(!f.live&&!f.measured,"Invalid publication revived stale state");
 l.valid=true;l.epoch=3;++l.blocks;f.observe(l,true,4,h,true,9,true,902);need(!f.live&&!f.measured,"Epoch race accepted old level snapshot");l.epoch=4;++l.blocks;h.generation=8;++h.serial;f.observe(l,true,4,h,true,9,true,903);need(f.live&&!f.measured,"Generation race accepted old Foundation snapshot");
 h.generation=9;++h.serial;f.observe(l,false,4,h,true,9,true,904);need(!f.live&&!f.measured,"Contended read reused active state");
 Freshness paused;paused.arm(4,1,true,9,10,10);l.epoch=4;l.valid=true;l.samples=512;l.blocks=2;h.generation=9;h.serial=11;paused.observe(l,true,4,h,true,9,true,11);need(paused.live&&paused.measured,"Polling-gap setup not live");++l.blocks;++h.serial;paused.observe(l,true,4,h,true,9,true,900);need(!paused.live&&!paused.measured,"Unseen old block revived after long GUI gap");paused.observe(l,true,4,h,true,9,true,901);need(!paused.live&&!paused.measured,"Gap baseline became live without publication");++l.blocks;++h.serial;paused.observe(l,true,4,h,true,9,true,902);need(paused.live&&paused.measured,"Fresh post-gap publication did not recover");
 std::cout<<"PASS first-read contention baseline, zero-frame, invalid, epoch/generation races, failed-poll and GUI-gap negatives\n";
}

}
int main(int argc,char** argv){juce::ScopedJuceInitialiser_GUI gui;try{juce::File dir;if(argc>1){dir=juce::File(argv[1]);need(dir.createDirectory().wasOk(),"Output directory");}slope();freshness();invalidPublications();presentationChecks::mappings();humWaitingNumericLabels(dir);humMeasured(dir);
 for(const auto& f:faces()){presentState(f);auto p=hem::PluginRegistry::create(f.id);std::unique_ptr<juce::AudioProcessorEditor> owned(p->createEditor());auto& e=dynamic_cast<hem::FoundationEditor&>(*owned);e.setSize(1672,941);bakedPointers(f,e);controls(*p,e);e.refreshParameterActivity();
  for(const auto size:std::vector<std::tuple<int,int,int>>{{1672,941,1},{1280,720,1},{1000,563,1},{1649,928,1},{1672,941,2}}){auto [w,h,dpi]=size;e.setSize(w,h);const auto expected=reference(f,w*dpi,h*dpi);for(int state=0;state<3;++state){e.setEnabled(state!=2);if(state==1){p->prepareToPlay(48000,512);process(*p,60);}const auto actual=render(e,dpi);need(outsideDifferences(f,actual,expected)==0,"Pixels outside independently audited apertures changed: "+juce::String(f.id));if(w==1672&&dpi==1)save(actual,dir,juce::String(f.id)+(state==0?"-idle":state==1?"-active":"-disabled"));if(state==0&&w==1672&&dpi==1){auto mutant=actual.createCopy();mutant.setPixelAt(700,90,juce::Colours::magenta);need(outsideDifferences(f,mutant,expected)>0,"Outside-aperture negative control escaped");}}e.setEnabled(true);
   std::vector<std::pair<juce::Slider*,double>> values;for(auto* child:e.getChildren())if(auto* slider=dynamic_cast<juce::Slider*>(child);slider&&slider->isVisible())values.push_back({slider,slider->getValue()});
   for(bool maximum:{false,true}){for(auto [slider,value]:values)slider->setValue(maximum?slider->getMaximum():slider->getMinimum(),juce::sendNotificationSync);for(bool enabled:{true,false}){e.setEnabled(enabled);need(outsideDifferences(f,render(e,dpi),expected)==0,"Boundary/disabled value changed pixels outside apertures");}}
   e.setEnabled(true);for(auto [slider,value]:values)slider->setValue(value,juce::sendNotificationSync);e.refreshParameterActivity();
  }
  std::cout<<"PASS "<<f.id<<" entire outside-aperture raster, 1x/2x/odd/minimum, active/disabled, actual drag/key/text/default and fixed materials\n";
 }
 std::cout<<"PASS approved Foundation five editor-only contract; native popup/device/listening acceptance remains unverified\n";
}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<"\n";return 1;}}
