#include "../Source/Plugins/PluginRegistry.h"
#include "../Source/UI/PhotorealisticLookAndFeel.h"
#include "../Source/UI/PhysicalValueSlider.h"
#include "../Source/UI/FaceplateLayers.h"
#include <BinaryData.h>
#include <iostream>
#include <stdexcept>
namespace {
void require(bool v,const char* text){if(!v)throw std::runtime_error(text);}
juce::Component* find(juce::Component& c,const juce::String& id){if(c.getComponentID()==id)return &c;for(auto* child:c.getChildren())if(auto* r=find(*child,id))return r;return nullptr;}
void save(const juce::Image& image,const juce::File& dir,const juce::String& name){if(dir==juce::File())return;auto out=dir.getChildFile(name+".png").createOutputStream();require(out!=nullptr&&juce::PNGImageFormat().writeImageToStream(image,*out),"Cannot write native review PNG");}
juce::Image dial(hem::ui::PhotorealisticLookAndFeel& look,float p,int scale){juce::Slider s;s.setRange(-60,6,.1);s.setBounds(0,0,120,120);juce::Image image(juce::Image::ARGB,120*scale,120*scale,true);juce::Graphics g(image);g.addTransform(juce::AffineTransform::scale(float(scale)));look.drawRotarySlider(g,0,0,120,120,p,juce::MathConstants<float>::pi*1.2f,juce::MathConstants<float>::pi*2.8f,s);return image;}
}
int main(int argc,char** argv){juce::ScopedJuceInitialiser_GUI gui;try{
 juce::File dir;if(argc>1){dir=juce::File(argv[1]);require(dir.createDirectory().wasOk(),"Cannot create review directory");}
 hem::ui::PhotorealisticLookAndFeel look;require(look.assetsReady(),"RGBA artwork unavailable");
 for(int scale:{1,2}){auto a=dial(look,0,scale),b=dial(look,1,scale);require(a.getPixelAt(0,0).getAlpha()==0,"Opaque plate around sprite");int changed=0;for(int y=0;y<a.getHeight();++y)for(int x=0;x<a.getWidth();++x)if(a.getPixelAt(x,y)!=b.getPixelAt(x,y))++changed;require(changed>20,"Indicator did not move");for(int y=57*scale;y<63*scale;++y)for(int x=57*scale;x<63*scale;++x)require(a.getPixelAt(x,y)==b.getPixelAt(x,y),"Body lighting rotates");save(a,dir,"dial-min-"+juce::String(scale)+"x");save(dial(look,.5f,scale),dir,"dial-zero-"+juce::String(scale)+"x");save(b,dir,"dial-max-"+juce::String(scale)+"x");}
 int count=0;for(const auto& entry:hem::PluginRegistry::entries()){
  auto p=entry.create();std::unique_ptr<juce::AudioProcessorEditor> editor(p->createEditor());
  for(const auto& spec:p->parameterSpecs()){
   auto* c=find(*editor,spec.id);require(c!=nullptr,"Missing parameter control");if(spec.toggle)continue;
   auto* s=dynamic_cast<hem::ui::PhysicalValueSlider*>(c);require(s!=nullptr,"Not a physical value slider");auto* parameter=p->state.getParameter(spec.id);const auto expected=parameter->convertTo0to1(spec.initial);
   require(std::abs(s->getValue()-spec.initial)<.001,"Default differs from DSP");
   if(hem::ui::isBipolar(spec.minimum,spec.maximum))require(s->valueToProportionOfLength(0)==.5,"Bipolar zero not at noon");
   if(spec.minimum==0)require(s->valueToProportionOfLength(0)==0,"Unipolar zero not left");
   for(double q:{0.,.25,.5,.75,1.}){auto v=s->proportionOfLengthToValue(q);require(std::abs(s->valueToProportionOfLength(v)-q)<1e-5,"Mapping not reversible");s->setValue(v,juce::sendNotificationSync);require(std::abs(p->state.getRawParameterValue(spec.id)->load()-s->getValue())<=std::max(.001f,spec.step),"Pointer/readout/DSP mismatch");}
   s->setValue(spec.initial,juce::sendNotificationSync);require(std::abs(parameter->getValue()-expected)<1e-5,"Host normalization changed");
  }
  for(const auto size:{juce::Point<int>(1000,563),juce::Point<int>(1920,1080),juce::Point<int>(1280,720)}){
   editor->setSize(size.x,size.y);
   for(const auto& spec:p->parameterSpecs()){auto* c=find(*editor,spec.id);require(c!=nullptr&&c->getWidth()>0&&c->getHeight()>0&&editor->getLocalBounds().contains(c->getBounds()),"Control escapes native editor after resize");}
  }
  for(int scale:{1,2}){juce::Image image(juce::Image::ARGB,editor->getWidth()*scale,editor->getHeight()*scale,true);{juce::Graphics g(image);g.addTransform(juce::AffineTransform::scale(float(scale)));editor->paintEntireComponent(g,true);}save(image,dir,juce::String(entry.id)+"-"+juce::String(scale)+"x");}
  ++count;
 }
 // Actual native layer composition, for seam inspection (not an app interaction screenshot).
 const auto original=juce::ImageCache::getFromMemory(BinaryData::studio_suite_master_png,BinaryData::studio_suite_master_pngSize);
 const auto clean=juce::ImageCache::getFromMemory(ControlArt::host_clean_regions_png,ControlArt::host_clean_regions_pngSize);const auto mask=hem::ui::makeHostCleanupMask();
 require(mask.getPixelAt(768,40).getAlpha()==0&&mask.getPixelAt(385,746).getAlpha()==255,"Cleanup changes title or misses dial");
 for(int scale:{1,2}){juce::Image image(juce::Image::ARGB,1536*scale,864*scale,true);{juce::Graphics g(image);g.addTransform(juce::AffineTransform::scale(float(scale)));g.drawImage(original,{0,0,1536,864},juce::RectanglePlacement::stretchToFit);hem::ui::paintCleanHostSockets(g,clean,mask,{0,0,1536,864});}save(image,dir,"host-clean-sockets-"+juce::String(scale)+"x");}
 std::cout<<"PASS "<<count<<" native editors; physical mappings and alpha rendering; native review artifacts exported when requested\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
