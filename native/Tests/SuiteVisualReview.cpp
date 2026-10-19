#include "../Source/Plugins/PluginRegistry.h"
#include "../Source/Plugins/Editors.h"
#include "../Source/UI/PluginTelemetryView.h"
#include <iostream>
#include <stdexcept>
static void check(bool b,const char* text){if(!b)throw std::runtime_error(text);}
static void set(hem::ModuleProcessor& p,const char* id,float v){auto* a=p.state.getParameter(id);check(a!=nullptr,"fixture parameter missing");a->setValueNotifyingHost(a->convertTo0to1(v));}
static void snapshot(juce::Component& c,const juce::File& folder,const juce::String& name,float scale=1){
    if(auto* module=dynamic_cast<hem::ModuleEditor*>(&c))module->refreshParameterActivity();
    juce::MemoryOutputStream out;check(juce::PNGImageFormat().writeImageToStream(c.createComponentSnapshot(c.getLocalBounds(),true,scale),out),"encode failed");
    check(folder.getChildFile(name+".png").replaceWithData(out.getData(),out.getDataSize()),"write failed");
}
static void wait(hem::PluginTelemetry& t,std::uint64_t end){hem::PluginAnalysis::Snapshot f;for(int i=0;i<600;++i){if(t.analysis.copyLatest(f)&&f.frameEnd>=end)return;juce::Thread::sleep(5);}throw std::runtime_error("analysis window timeout");}
int main(int argc,char** argv){try{juce::ScopedJuceInitialiser_GUI gui;check(argc>1,"output directory required");juce::File folder(argv[1]);check(folder.createDirectory().wasOk(),"directory failed");
 for(const auto& entry:hem::PluginRegistry::entries()){
    auto p=entry.create();const juce::String id(entry.id);p->prepareToPlay(48000,256);check(p->pluginTelemetry()!=nullptr,"missing registered telemetry");
    auto editor=std::unique_ptr<juce::AudioProcessorEditor>(p->createEditor());snapshot(*editor,folder,id+"-waiting");
    if(id=="vca"){set(*p,"threshold",-28);set(*p,"ratio",4);}
    if(id=="dhta"){set(*p,"threshold",-50);set(*p,"range",12);set(*p,"frequency",2707);}
    if(id=="spectral-restorer"){set(*p,"amount",12);set(*p,"floor",-25);set(*p,"protect",0);}
    if(id=="true-peak"){set(*p,"input",12);set(*p,"ceiling",-6);}
    if(id=="transient"){set(*p,"attackAmount",33);set(*p,"sustainAmount",-33);}
    if(id=="smart-gate"){set(*p,"threshold",-18);set(*p,"range",24);set(*p,"gate",1);}
    if(id=="vocal-presence"){set(*p,"range",12);set(*p,"threshold",-45);set(*p,"cutEnabled",1);}
    if(id=="dc-subsonic"){set(*p,"dcOn",1);set(*p,"subOn",1);}
    if(id=="sub-bass")set(*p,"cutEnabled",1);
    if(id=="vocal-rider"){set(*p,"maxUp",6);set(*p,"maxDown",6);}
    if(id=="plosive-control"){set(*p,"range",12);set(*p,"threshold",-35);}
    if(id=="hum-buzz")set(*p,"depth",24);
    juce::AudioBuffer<float> audio(2,256);juce::MidiBuffer midi;std::uint32_t random=0x7a103u;std::uint64_t sample=0;
    auto feed=[&](int blocks,bool silent){for(int block=0;block<blocks;++block){
        for(int i=0;i<256;++i,++sample){random=1664525u*random+1013904223u;const float noise=float((random>>8)&0xffffff)/8388608.f-1;
            const double seconds=double(sample)/48000;const float env=.03f+.97f*std::exp(-float(sample%12000)/2300.f);
            const float low=.35f*float(std::sin(juce::MathConstants<double>::twoPi*61*seconds))+.2f*float(std::sin(juce::MathConstants<double>::twoPi*143*seconds));
            audio.setSample(0,i,silent?0:env*(low+.2f*noise+.2f*float(std::sin(juce::MathConstants<double>::twoPi*1271*seconds)))+.035f*float(std::sin(juce::MathConstants<double>::twoPi*50*seconds)));
            audio.setSample(1,i,silent?0:env*(low*.75f-.16f*noise+.16f*float(std::sin(juce::MathConstants<double>::twoPi*2707*seconds))));}
        const float inputL=audio.getMagnitude(0,0,256),inputR=audio.getMagnitude(1,0,256);p->processBlock(audio,midi);
        hem::PluginTelemetry::Levels levels;check(p->pluginTelemetry()->read(levels)&&levels.valid,"invalid levels");
        check(levels.peak[0]==inputL&&levels.peak[1]==inputR,"input tap mismatch");check(levels.peak[2]==audio.getMagnitude(0,0,256)&&levels.peak[3]==audio.getMagnitude(1,0,256),"output tap mismatch");
        if(block%4==0)juce::Thread::sleep(1);
    }wait(*p->pluginTelemetry(),sample);};
    feed(768,false);editor->setSize(1280,720);editor->repaint();juce::MemoryBlock before,after;p->getStateInformation(before);
    snapshot(*editor,folder,id+"-active");
    editor->setSize(1000,563);feed(32,false);snapshot(*editor,folder,id+"-minimum");
    feed(32,false);snapshot(*editor,folder,id+"-minimum-2x-raster",2);
    editor->setSize(1920,1080);feed(32,false);snapshot(*editor,folder,id+"-large");
    p->getStateInformation(after);check(before==after,"render/resize changed state");
    editor->setSize(1280,720);for(const auto& spec:p->parameterSpecs())if(spec.toggle)set(*p,spec.id,0);
    feed(768,true);snapshot(*editor,folder,id+"-silent-toggles-off");
    juce::Thread::sleep(800);snapshot(*editor,folder,id+"-stale");
    editor.reset();p->releaseResources();
    std::cout<<"PASS "<<id<<" input/output taps; render preserves state; waiting/active/minimum/2x-raster/large/silent-toggles-off/stale\n"<<std::flush;
 }
}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<"\n";return 1;}return 0;}
