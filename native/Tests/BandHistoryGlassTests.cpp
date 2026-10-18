#include "../Source/Plugins/PluginRegistry.h"
#include <ApprovedArt.h>
#include <iostream>
#include <stdexcept>
static void check(bool pass,const char* reason){if(!pass)throw std::runtime_error(reason);}
static bool clearGap(const juce::Image& image,int scale){
 for(int y=880*scale;y<882*scale;++y)for(int x=419*scale;x<1394*scale;++x)if(image.getPixelAt(x,y)!=juce::Colour(0xff020a10))return false;
 return true;
}
int main(int argc,char**argv){juce::ScopedJuceInitialiser_GUI gui;try{
 hem::MultibandDynamicsProcessor p;p.prepareToPlay(48000,257);std::unique_ptr<juce::AudioProcessorEditor> editor(p.createEditor());editor->setSize(1672,941);
 juce::MemoryBlock before,after;p.getStateInformation(before);const auto art=juce::ImageCache::getFromMemory(ApprovedArt::approved_04_png,ApprovedArt::approved_04_pngSize);
 for(bool released:{false,true}){if(released)p.releaseResources();
  for(int scale:{1,2}){juce::Image image(juce::Image::ARGB,1672*scale,941*scale,true);{juce::Graphics g(image);g.addTransform(juce::AffineTransform::scale(float(scale)));editor->paintEntireComponent(g,true);}
   check(clearGap(image,scale),"Unprocessed/released history glass retains authored waveform pixels");
   if(scale==1)check(image.getPixelAt(414,881)==art.getPixelAt(414,881),"Authored cyan axis/frame marker changed");
   auto mutation=image.createCopy();{juce::Graphics g(mutation);g.setColour(juce::Colours::yellow);g.fillRect(500*scale,880*scale,8*scale,2*scale);}check(!clearGap(mutation,scale),"Glass oracle accepted synthetic waveform residue");
   if(argc==2){juce::File dir(argv[1]);check(dir.createDirectory().wasOk(),"Evidence directory");auto stream=dir.getChildFile(juce::String(released?"released":"prepared")+"-"+juce::String(scale)+"x.png").createOutputStream();check(stream&&juce::PNGImageFormat().writeImageToStream(image,*stream),"PNG evidence");}
  }
 }
 p.getStateInformation(after);check(before==after,"History painting changed parameter state");
 std::cout<<"PASS cleared complete Dynamics history glass at1x/2x, prepared/released; cyan frame marker preserved; residue mutation rejected; state unchanged\n";return 0;
}catch(const std::exception&e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
