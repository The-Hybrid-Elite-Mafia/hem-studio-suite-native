#include "../Source/Plugins/AmpCabinetProcessor.h"
#include "../Source/Plugins/AmpCabinetEditor.h"
#include "../Source/Systems/NativeProcessorState.h"
#include <iostream>
#include <cstdlib>
using namespace hem;
void require(bool ok,const char* message){if(!ok){std::cerr<<"FAIL "<<message<<std::endl;std::exit(1);}}
void set(AmpCabinetProcessor& p,const char* id,float v){auto* x=p.state.getParameter(id);x->setValueNotifyingHost(x->convertTo0to1(v));}
void wav(const juce::File& file){auto stream=file.createOutputStream();require(bool(stream),"fixture output");juce::WavAudioFormat format;std::unique_ptr<juce::AudioFormatWriter> writer(format.createWriterFor(stream.release(),48000,2,24,{},0));require(bool(writer),"fixture writer");juce::AudioBuffer<float> b(2,256);b.clear();b.setSample(0,0,.5f);b.setSample(1,0,.25f);b.setSample(0,32,.125f);require(writer->writeFromAudioSampleBuffer(b,0,256),"fixture write");}
int main(){juce::ScopedJuceInitialiser_GUI gui;
 auto dir=juce::File::getCurrentWorkingDirectory().getChildFile("build-amp");dir.createDirectory();auto file=dir.getNonexistentChildFile("cab-test",".wav");wav(file);
 AmpCabinetProcessor p;p.prepareToPlay(48000,256);require(p.getLatencySamples()==224,"declared latency");require(p.loadIRUnpublished(file,48000).wasOk(),"IR load");set(p,"cabEnabled",1);
 NativeProcessorSnapshot snapshot;require(p.tryCaptureNativeState(snapshot).wasOk(),"capture");require(snapshot.native&&snapshot.native->resources[0].sha256.length()==64,"resource identity");
 juce::MemoryBlock binary;require(NativeProcessorState::encodeBinary(p,snapshot,binary).wasOk(),"encode");NativeProcessorSnapshot decoded;require(NativeProcessorState::decodeBinary(p,binary.getData(),binary.getSize(),decoded).wasOk(),"decode");
 NativeRestoreContext context;context.sampleRate=48000;context.blockSize=256;context.nonRealtime=true;
 AmpCabinetProcessor q;require(q.restoreNativeStateOffThread(decoded,context).wasOk(),"private export restore");q.prepareToPlay(48000,256);require(q.getLatencySamples()==224,"reprepare retains latency");
 juce::AudioBuffer<float> a(2,2048),b(2,2048);juce::MidiBuffer midi;a.clear();b.clear();p.processBlock(a,midi);q.processBlock(b,midi);a.clear();b.clear();a.setSample(0,0,1);a.setSample(1,0,1);b.makeCopyOf(a);p.processBlock(a,midi);q.processBlock(b,midi);
 for(int n=0;n<2048;++n)for(int c=0;c<2;++c){require(std::abs(a.getSample(c,n)-b.getSample(c,n))<1e-6f,"private/live equality");float expected=n==224?(c==0?.5f:.25f):(n==256&&c==0?.125f:0.f);require(std::abs(a.getSample(c,n)-expected)<2e-6f,"IR alignment/stereo gain");}
 for(double rate:{32000.,44100.,96000.,192000.}){AmpCabinetProcessor resampled;auto ctx=context;ctx.sampleRate=rate;require(resampled.restoreNativeStateOffThread(decoded,ctx).wasOk(),"resampled restore");const int delay=32+int(std::ceil(rate*.004));require(resampled.getLatencySamples()==delay&&delay<=int(std::ceil(rate*.006)),"resampled latency ceiling");juce::AudioBuffer<float> signal(2,4096);signal.clear();resampled.processBlock(signal,midi);signal.clear();signal.setSample(0,0,1);signal.setSample(1,0,1);resampled.processBlock(signal,midi);int peakIndex=0;double leftSum=0,rightSum=0;for(int n=0;n<4096;++n){leftSum+=signal.getSample(0,n);rightSum+=signal.getSample(1,n);if(std::abs(signal.getSample(0,n))>std::abs(signal.getSample(0,peakIndex)))peakIndex=n;}require(peakIndex==delay,"resampled measured latency");require(std::abs(leftSum-.625)<.001&&std::abs(rightSum-.25)<.001,"resampled IR DC coefficient preservation");}
 require(p.loadIRUnpublished(file,48000).failed(),"published mutation rejected");require(p.meterSnapshot().hasProcessedAudio,"real meter");
 AmpCabinetProcessor bad;auto invalid=NativeProcessorState::deepCopy(snapshot);invalid.native->resources[0].sha256=juce::String::repeatedString("0",64);require(bad.restoreNativeStateOffThread(invalid,context).failed(),"changed hash rejection");
 auto params=snapshot.parameters.createCopy();for(auto c:params)if(c["id"].toString()=="cabModel")c.setProperty("value",2.5,nullptr);require(!p.validateStoredParameters(params),"fractional choice rejected");
 std::unique_ptr<juce::AudioProcessorEditor> editor(p.createEditor());require(bool(editor),"editor");editor->setSize(1280,720);editor->setVisible(true);
 int sliders=0,choices=0,toggles=0;for(auto* child:editor->getChildren()){
  if(auto* slider=dynamic_cast<juce::Slider*>(child)){++sliders;slider->setValue(slider->getMaximum(),juce::sendNotificationSync);auto* raw=p.state.getRawParameterValue(slider->getComponentID());require(raw&&std::abs(raw->load()-slider->getMaximum())<.001,"slider APVTS wiring");}
  if(auto* choice=dynamic_cast<juce::ComboBox*>(child)){++choices;choice->setSelectedId(2,juce::sendNotificationSync);auto* raw=p.state.getRawParameterValue(choice->getComponentID());require(raw&&raw->load()==1,"choice APVTS wiring");}
  if(auto* button=dynamic_cast<juce::ToggleButton*>(child);button&&button->getComponentID()!="loadCabIR"){++toggles;button->setToggleState(true,juce::sendNotificationSync);auto* raw=p.state.getRawParameterValue(button->getComponentID());require(raw&&raw->load()==1,"toggle APVTS wiring");}
 }
 require(sliders==9&&choices==3&&toggles==4,"all pictured parameter controls");
 auto* ampEditor=dynamic_cast<AmpCabinetEditor*>(editor.get());bool loadRequested=false,closed=false;ampEditor->setLoadRequest([&]{loadRequested=true;});ampEditor->setCloseRequest([&]{closed=true;});
 for(auto* child:editor->getChildren())if(child->getComponentID()=="loadCabIR"){auto* button=dynamic_cast<juce::Button*>(child);require(button&&button->isEnabled(),"load enabled");button->onClick();}require(loadRequested,"host load command");require(ampEditor->keyPressed(juce::KeyPress(juce::KeyPress::escapeKey))&&closed,"escape host back");
 editor->setSize(1000,563);editor->setSize(1280,720);for(auto* child:editor->getChildren()){if(child->getComponentID().isNotEmpty()){require(!child->getBounds().isEmpty(),"editor control bounds");}}auto image=editor->createComponentSnapshot(editor->getLocalBounds());require(image.isValid(),"editor render");auto shot=dir.getChildFile("amp-editor.png").createOutputStream();if(shot){shot->setPosition(0);shot->truncate();}juce::PNGImageFormat png;require(shot&&png.writeImageToStream(image,*shot),"editor screenshot");shot.reset();editor.reset();
 require(file.deleteFile(),"fixture cleanup");std::cout<<"PASS custom IR hash/decode, native binary, private export repeat prepare, stereo impulse gain/alignment, published mutation rejection, strict state, telemetry, approved editor render\n";
}



