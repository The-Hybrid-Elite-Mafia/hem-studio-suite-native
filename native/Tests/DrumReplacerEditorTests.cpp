#include "../Source/Plugins/DrumReplacerEditor.h"
#include <ApprovedArt.h>
#include <iostream>
#include <stdexcept>
namespace {
void require(bool b,const char* message){if(!b)throw std::runtime_error(message);}
juce::Component* find(juce::Component& p,const juce::String& id){if(p.getComponentID()==id)return &p;for(auto* c:p.getChildren())if(auto* found=find(*c,id))return found;return nullptr;}
juce::Button& button(juce::Component& p,const juce::String& id){auto* b=dynamic_cast<juce::Button*>(find(p,id));require(b!=nullptr,"Missing button");return *b;}
void save(juce::Component& editor,const juce::File& file){auto image=editor.createComponentSnapshot(editor.getLocalBounds());auto stream=file.createOutputStream();require(stream!=nullptr,"Cannot open snapshot");stream->setPosition(0);stream->truncate();require(juce::PNGImageFormat().writeImageToStream(image,*stream),"Cannot encode snapshot");}
}
int main(int argc,char** argv){try{
 juce::ScopedJuceInitialiser_GUI gui;
 hem::DrumReplacerProcessor processor;processor.prepareToPlay(48000,128);
 hem::DrumReplacerEditor editor(processor);editor.setSize(1672,941);
 int parameters=0;
 for(const auto& spec:processor.parameterSpecs()){
  auto* c=find(editor,spec.id);require(c!=nullptr,"Missing actual APVTS binding");++parameters;
  if(auto* slider=dynamic_cast<juce::Slider*>(c)){
   const auto initial=processor.state.getRawParameterValue(spec.id)->load();
   slider->setValue(spec.maximum,juce::sendNotificationSync);
   require(std::abs(processor.state.getRawParameterValue(spec.id)->load()-spec.maximum)<.01f,"Slider does not update processor");
   auto* parameter=processor.state.getParameter(spec.id);parameter->setValueNotifyingHost(parameter->convertTo0to1(initial));
  }
 }
 require(parameters==17,"Unexpected parameter coverage");
 int loaded=-1;for(int i=1;i<=4;++i){require(!button(editor,"load"+juce::String(i)).isEnabled(),"Unbound load enabled");require(!button(editor,"enable"+juce::String(i)).isEnabled(),"Empty layer enable active");}
 editor.setLoadRequest([&](int index){loaded=index;});
 for(int i=1;i<=4;++i){auto& b=button(editor,"load"+juce::String(i));require(b.isEnabled(),"Bound load disabled");b.onClick();require(loaded==i-1,"Wrong sample layer callback");}
 editor.setLoadRequest({});require(!button(editor,"load1").isEnabled(),"Removed load binding remains active");
 require(!button(editor,"hostBypass").isEnabled(),"Unbound bypass enabled");
 bool bypass=false;editor.setHostBypassBinding({[&]()->std::optional<bool>{return bypass;},[&](bool v){bypass=v;}});
 button(editor,"hostBypass").onClick();require(bypass&&button(editor,"hostBypass").getToggleState(),"Bypass not host controlled");
 editor.setHostBypassBinding({});require(!button(editor,"hostBypass").isEnabled(),"Removed bypass binding active");
 int closed=0;require(!editor.keyPressed(juce::KeyPress(juce::KeyPress::escapeKey)),"Unbound Escape consumed");
 editor.setCloseRequest([&]{++closed;});require(editor.keyPressed(juce::KeyPress(juce::KeyPress::escapeKey))&&closed==1,"Escape did not reach owner");
 const auto art=juce::ImageCache::getFromMemory(ApprovedArt::approved_16_png,ApprovedArt::approved_16_pngSize);
 const auto raster=editor.createComponentSnapshot(editor.getLocalBounds());
 for(const auto point:std::array<juce::Point<int>,6>{{{20,20},{836,125},{50,400},{420,640},{1200,890},{1640,900}}})
  require(raster.getPixelAt(point.x,point.y)==art.getPixelAt(point.x,point.y),"Immutable chassis pixel changed");
 // The artwork's bright illustrative waveform must be fully obscured when no
 // measured history exists. This sample is inside the authored orange onset.
 require(raster.getPixelAt(330,205)!=art.getPixelAt(330,205),"Baked waveform still visible");
 if(argc>1){const auto directory=juce::File::getCurrentWorkingDirectory().getChildFile(argv[1]);require(directory.createDirectory().wasOk(),"Cannot create raster directory");save(editor,directory.getChildFile("drum-editor-idle.png"));}
 juce::AudioBuffer<float> audio(2,128);juce::MidiBuffer midi;
 for(int block=0;block<24;++block){audio.clear();if(block%8==0){audio.setSample(0,0,.8f);audio.setSample(1,0,.4f);}processor.processBlock(audio,midi);editor.refreshParameterActivity();}
 require(processor.hitCount()>0,"No real detector event published");
 const auto measured=editor.createComponentSnapshot(editor.getLocalBounds());
 require(measured.getPixelAt(360,240)!=raster.getPixelAt(360,240),"Actual detector history not rendered");
 if(argc>1){const auto directory=juce::File::getCurrentWorkingDirectory().getChildFile(argv[1]);save(editor,directory.getChildFile("drum-editor-measured.png"));}
 juce::Thread::sleep(800);editor.refreshParameterActivity();
 const auto stale=editor.createComponentSnapshot(editor.getLocalBounds());
 require(stale.getPixelAt(360,240)==raster.getPixelAt(360,240),"Stale detector history remains lit");
 for(const auto size:std::array<juce::Point<int>,2>{{{1000,563},{1280,720}}}){editor.setSize(size.x,size.y);const auto resized=editor.createComponentSnapshot(editor.getLocalBounds());require(resized.getWidth()==size.x&&resized.getHeight()==size.y,"Raster resize failed");}
 std::cout<<"PASS: 17 bindings, host load/bypass/Escape, immutable chassis pixels, blanked illustrative trace, real audio rendering\n";return 0;
 }catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<"\n";return 1;}}
