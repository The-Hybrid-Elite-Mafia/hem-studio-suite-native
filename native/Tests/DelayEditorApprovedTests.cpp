#include "../Source/Plugins/DelayEditors.h"
#include "../Source/UI/SegmentedMeterModel.h"
#include <ApprovedArt.h>
#include <iostream>
#include <stdexcept>
using namespace hem;
static void require(bool b,const char* reason){if(!b)throw std::runtime_error(reason);}
static void parameter(ModuleProcessor& p,const char* id,float value){auto* v=p.state.getParameter(id);require(v!=nullptr,"Missing parameter");v->setValueNotifyingHost(v->convertTo0to1(value));}
static juce::Component* find(juce::Component& parent,const juce::String& id){if(parent.getComponentID()==id)return &parent;for(auto* child:parent.getChildren())if(auto* found=find(*child,id))return found;return nullptr;}
static void verifyPointer(juce::Slider& slider){
 const auto bounds=slider.getBounds();slider.setSize(100,100);
 const auto initial=slider.getValue();
 for(double value:{slider.getMinimum(),initial,slider.getMaximum()}){
  slider.setValue(value,juce::sendNotificationSync);const double fraction=slider.valueToProportionOfLength(slider.getValue());
  juce::Image raster(juce::Image::ARGB,100,100,true);
  {juce::Graphics g(raster);slider.getLookAndFeel().drawRotarySlider(g,0,0,100,100,float(fraction),0,0,slider);}
  // Independent expected coordinates: min at (left), middle at noon, max right.
  const double angle=hem::pi+fraction*hem::pi;const int x=int(std::round(50+std::cos(angle)*35)),y=int(std::round(50+std::sin(angle)*35));
  bool white=false;for(int dy=-2;dy<=2;++dy)for(int dx=-2;dx<=2;++dx){const auto c=raster.getPixelAt(x+dx,y+dy);white|=c.getRed()>200&&c.getGreen()>200&&c.getBlue()>200;}
  require(white,"Rendered pointer position disagrees with physical parameter range");
 }
 if(slider.getMinimum()<0&&slider.getMaximum()>0)require(std::abs(slider.valueToProportionOfLength(0)-.5)<1e-12,"Bipolar zero is not noon");
 slider.setValue(initial,juce::sendNotificationSync);slider.setBounds(bounds);
}
static void verifyControls(DelayProcessor& p){
 DelayEditor editor(p);const bool tape=p.delayKind()==delay::Kind::tape;
 for(int tap=0;tap<(tape?1:8);++tap){editor.selectTap(tap);
  for(const auto& spec:p.parameterSpecs()){
   const juce::String id=spec.id;if(id.endsWith("division"))continue;
   if(!tape&&id.startsWith("tap")&&!id.startsWith("tap"+juce::String(tap+1)+"."))continue;
   auto* component=find(editor,id);require(component!=nullptr,"Reference control has no live binding");
   if(spec.toggle){auto* toggle=dynamic_cast<juce::ToggleButton*>(component);require(toggle!=nullptr,"Toggle is not independently interactive");toggle->setToggleState(!toggle->getToggleState(),juce::sendNotificationSync);require(p.state.getRawParameterValue(id)->load()==float(toggle->getToggleState()),"Toggle/APVTS binding mismatch");toggle->setToggleState(spec.initial>.5f,juce::sendNotificationSync);}
   else{auto* slider=dynamic_cast<juce::Slider*>(component);require(slider!=nullptr,"Rotary is not independently interactive");verifyPointer(*slider);slider->setValue(spec.maximum,juce::sendNotificationSync);require(std::abs(p.state.getRawParameterValue(id)->load()-spec.maximum)<.11f,"Rotary/APVTS binding mismatch");require(editor.layoutIssues().isEmpty(),"Maximum value fails native geometry/text fit");slider->setValue(spec.initial,juce::sendNotificationSync);}
   require(editor.getLocalBounds().contains(component->getBounds()),"Control outside reference layout");
  }
 }
 parameter(p,"sync",1);editor.refreshBindings();require(find(editor,tape?"division":"tap1.division")!=nullptr,"Time control did not bind separate division state");parameter(p,"sync",0);editor.refreshBindings();require(find(editor,tape?"time_ms":"tap1.time_ms")!=nullptr,"Manual time binding was lost");
 if(tape)require(find(editor,"tap-tempo")!=nullptr,"Original Tap Tempo button missing");
 // Offscreen raster checks require no GUI window or audio device.
 // Original neutral lighting is fixed in the image.
 for(auto size:{juce::Point<int>(1003,565),juce::Point<int>(1338,753),juce::Point<int>(1672,941),juce::Point<int>(2006,1129)}){editor.setSize(size.x,size.y);require(editor.layoutIssues().isEmpty(),"Resized native geometry/text fit failed");juce::Image image(juce::Image::ARGB,size.x,size.y,true);juce::Graphics g(image);editor.paintEntireComponent(g,true);require(image.isValid(),"Editor failed to render");}
}
static void renderEvidence(){
 const auto dir=juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("hem-delay-r2-renders-"+juce::Uuid().toString());require(dir.createDirectory().wasOk(),"Cannot create native render evidence directory");
 std::cout<<"Native render evidence: "<<dir.getFullPathName()<<std::endl;
 juce::String report;
 for(bool tape:{false,true}){
  std::unique_ptr<DelayProcessor> p=tape?std::unique_ptr<DelayProcessor>(new TapeDelayProcessor):std::unique_ptr<DelayProcessor>(new MultitapDelayProcessor);DelayEditor editor(*p);
  for(int mode=0;mode<5;++mode){
   if(mode==1){parameter(*p,"mix",100);parameter(*p,"sync",1);parameter(*p,tape?"division":"tap1.division",19);if(!tape)parameter(*p,"tap1.on",1);p->setBlockTempo(20);}
   if(mode==2){parameter(*p,"mix",0);p->releaseResources();}
   if(mode==3){parameter(*p,"sync",0);parameter(*p,"mix",65);parameter(*p,"output",-3);p->setBlockTempo(120);parameter(*p,tape?"time_ms":"tap1.time_ms",750);parameter(*p,tape?"feedback":"tap1.feedback",35);p->prepareToPlay(48000,256);}
   if(mode==4)juce::Thread::sleep(300); // Message-side stale-publication evidence; no DSP clock.
   editor.refreshBindings();
   for(auto size:{juce::Point<int>(1003,565),juce::Point<int>(1338,753),juce::Point<int>(1672,941),juce::Point<int>(2006,1129)}){
    editor.setSize(size.x,size.y);const auto name=juce::String(tape?"tape":"multitap")+"-state"+juce::String(mode)+"-"+juce::String(size.x)+"x"+juce::String(size.y);
    const auto issues=editor.layoutIssues();report+=name+": "+(issues.isEmpty()?"PASS geometry/text fit":issues.joinIntoString("; "))+"\n";
    if(mode==3){juce::AudioBuffer<float> block(2,256);for(int c=0;c<2;++c)for(int n=0;n<256;++n)block.setSample(c,n,float(.25*std::sin(2*hem::pi*997*n/48000)));juce::MidiBuffer midi;p->processBlock(block,midi);}
    juce::Image image(juce::Image::ARGB,size.x,size.y,true);{juce::Graphics g(image);editor.paintEntireComponent(g,true);}
    if(mode==4)require(editor.statusText().contains("NO LIVE AUDIO DATA"),"Native stale render still claims live telemetry");
    auto stream=dir.getChildFile(name+".png").createOutputStream();require(stream&&juce::PNGImageFormat().writeImageToStream(image,*stream),"Cannot save native render evidence");
    require(dir.getChildFile("geometry-text-fit.txt").replaceWithText(report),"Cannot save incremental geometry/text report");
    require(issues.isEmpty(),"Native evidence geometry/text fit failed");
   }
  }
 }
 require(dir.getChildFile("geometry-text-fit.txt").replaceWithText(report),"Cannot save geometry/text report");
}

static void verifyApprovedRaster(DelayProcessor& p){
 const bool tape=p.delayKind()==delay::Kind::tape;
 DelayEditor e(p);require(e.getWidth()==1280&&e.getHeight()==720,"Host initial geometry changed");
 require(std::abs(e.getConstrainer()->getFixedAspectRatio()-1672.0/941.0)<1e-10,"Authored aspect ratio not locked");
 e.setSize(1672,941);parameter(p,"mix",0);parameter(p,"output",0);p.prepareToPlay(48000,256);
 const auto art=juce::ImageCache::getFromMemory(tape?ApprovedArt::approved_22_png:ApprovedArt::approved_21_png,tape?ApprovedArt::approved_22_pngSize:ApprovedArt::approved_21_pngSize);
 const auto area=tape?juce::Rectangle<float>(1499,194,98,259):juce::Rectangle<float>(1459,94,154,218);
 const float inset=tape?12.f:22.f;const auto bar=juce::Rectangle<float>(area.getX()+inset,area.getY()+25,21,area.getHeight()-50);
 juce::AudioBuffer<float> block(2,256);juce::MidiBuffer midi;
 for(float amplitude:{0.f,.001f,.25f,1.f,2.f}){
  const float right=amplitude*.125f;
  for(int c=0;c<2;++c)for(int n=0;n<256;++n)block.setSample(c,n,c?right:amplitude);
  p.processBlock(block,midi);juce::Image image(juce::Image::ARGB,1672,941,true);{juce::Graphics g(image);e.paintEntireComponent(g,true);}
  for(int side=0;side<2;++side)for(int ch=0;ch<2;++ch){
   const auto panel=side?area:(tape?juce::Rectangle<float>(74,194,100,259):juce::Rectangle<float>(68,94,146,218));
   const float cx=ch?panel.getRight()-inset-10.5f:panel.getX()+inset+10.5f;
   const int expected=int(std::floor(ui::meterAmplitudePosition(ch?right:amplitude)*24+1e-5));int lit=0;
   for(int i=0;i<24;++i){const int y=int(bar.getBottom()-(i+.5f)*bar.getHeight()/24);auto c=image.getPixelAt(int(cx),y);lit+=c.getRed()>40&&c.getGreen()>80;}
   require(lit==expected,"Independent stereo input/output segmented peak calibration mismatch");
  }
  // Sample immutable rings and chassis, outside live glass and opaque inner bodies.
  for(auto pt:tape?std::array<juce::Point<int>,4>{{{111,621},{173,683},{500,100},{900,900}}}:std::array<juce::Point<int>,4>{{{76,402},{114,441},{500,100},{900,920}}})
   require(image.getPixelAt(pt.x,pt.y)==art.getPixelAt(pt.x,pt.y),"Authored ring/chassis altered");
 }
 juce::Thread::sleep(300);juce::Image frozen(juce::Image::ARGB,1672,941,true);{juce::Graphics g(frozen);e.paintEntireComponent(g,true);}
 require(e.statusText().contains("NO LIVE AUDIO DATA"),"Frozen telemetry claimed live data");
 for(int ch=0;ch<2;++ch)for(int i=0;i<24;++i){auto c=frozen.getPixelAt(int(ch?area.getRight()-inset-10.5f:bar.getCentreX()),int(bar.getBottom()-(i+.5f)*bar.getHeight()/24));require(c.getRed()<40&&c.getGreen()<80,"Frozen stereo peak remains lit");}
 p.releaseResources();juce::Image idle(juce::Image::ARGB,1672,941,true);{juce::Graphics g(idle);e.paintEntireComponent(g,true);}
 require(e.statusText().contains("NO LIVE AUDIO DATA"),"Released processor claimed live data");
 for(int i=0;i<24;++i){auto c=idle.getPixelAt(int(bar.getCentreX()),int(bar.getBottom()-(i+.5f)*bar.getHeight()/24));require(c.getRed()<40&&c.getGreen()<80,"Baked/live meter remains after release");}
 for(int y=tape?430:303;y<(tape?435:308);++y)for(int x=335;x<1380;++x){auto c=idle.getPixelAt(x,y);require(c.getGreen()<100&&c.getRed()<100,"Authored signal glow survived below graph boundary");}
 if(tape)for(int y=395;y<405;++y)for(int x=280;x<1390;++x){auto c=idle.getPixelAt(x,y);require(c.getGreen()<100&&c.getRed()<100,"Baked lower waveform survived graph clearing");}
 // The original lower-left pointer must not coexist with a noon live pointer.
 auto* dial=dynamic_cast<juce::Slider*>(find(e,tape?"time_ms":"tap1.time_ms"));require(dial,"Missing pointer test dial");
 dial->setValue(dial->proportionOfLengthToValue(.5),juce::sendNotificationSync);
 dial->setSize(100,100);juce::Image face(juce::Image::ARGB,100,100,true);{juce::Graphics g(face);dial->getLookAndFeel().drawRotarySlider(g,0,0,100,100,.5,0,0,*dial);}
 require(face.getPixelAt(28,72).getAlpha()==255,"Inner body does not cover baked pointer");
}
int main(){try{juce::ScopedJuceInitialiser_GUI gui;MultitapDelayProcessor multi;TapeDelayProcessor tape;
 for(auto* p:std::array<DelayProcessor*,2>{&multi,&tape}){verifyControls(*p);verifyApprovedRaster(*p);}
 renderEvidence();std::cout<<"PASS: approved Delay rings/chassis, pointer mapping, controls, independent stereo segmented peak scale, freshness, waveform clearing, four raster scales\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
