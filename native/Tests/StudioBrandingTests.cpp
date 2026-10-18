#include "../Source/UI/StudioBranding.h"
#include <BinaryData.h>
#include <iostream>
#include <stdexcept>
namespace {
void require(bool b,const char* m){if(!b)throw std::runtime_error(m);}
int cyanPixels(const juce::Image& image,juce::Rectangle<int> bounds){int n=0;for(int y=bounds.getY();y<bounds.getBottom();++y)for(int x=bounds.getX();x<bounds.getRight();++x){const auto c=image.getPixelAt(x,y);if(c.getGreen()>150&&c.getBlue()>150&&c.getRed()<130)++n;}return n;}
int amberPixels(const juce::Image& image,juce::Rectangle<int> bounds){int n=0;for(int y=bounds.getY();y<bounds.getBottom();++y)for(int x=bounds.getX();x<bounds.getRight();++x){const auto c=image.getPixelAt(x,y);if(c.getRed()>190&&c.getGreen()>100&&c.getBlue()<150)++n;}return n;}
}
int main(){juce::ScopedJuceInitialiser_GUI gui;try{
 const auto ghost=juce::ImageCache::getFromMemory(BinaryData::ghost_brand_reference_png,BinaryData::ghost_brand_reference_pngSize);
 require(ghost.isValid()&&ghost.getWidth()==1672&&ghost.getHeight()==941,"Approved embedded ghost is unavailable");
 const auto crop=hem::ui::StudioBranding::ghostSource();require(crop==juce::Rectangle<int>(10,702,244,196),"Approved ghost viewport changed");
 require(cyanPixels(ghost,juce::Rectangle<int>(85,755,50,30))>20,"Approved ghost eyes are absent from source");
 for(int scale:{1,2}){
  juce::Image image(juce::Image::ARGB,1536*scale,864*scale,true);const auto root=image.getBounds().toFloat();
  // Native Direct2D image readback requires the drawing context to be closed.
  {juce::Graphics g(image);hem::ui::StudioBranding::draw(g,root,ghost);}
  const auto mapped=[&](float x,float y,float w,float h){return hem::ui::StudioBranding::mapped(root,x,y,w,h).toNearestInt();};
  require(cyanPixels(image,mapped(1326,114,176,150))>150*scale*scale,"Ghost identity/signature is not visible at startup");
  require(amberPixels(image,mapped(605,13,415,42))>500*scale*scale,"Top brand is not illuminated amber");
  require(cyanPixels(image,mapped(620,54,385,18))>150*scale*scale,"Studio Suite subtitle is not illuminated cyan");
  require(image.getPixelAt(1350*scale,150*scale).getAlpha()==255,"Ghost badge is not opaque");
  require(image.getPixelAt(500*scale,300*scale).getAlpha()==0,"Branding paints unrelated controls or meters");
 }
 std::cout<<"PASS embedded approved ghost identity, startup illuminated brand/subtitle, cropped badge and isolated painting at 1x/2x\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
