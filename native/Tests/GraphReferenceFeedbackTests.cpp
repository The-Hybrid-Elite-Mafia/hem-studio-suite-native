#include "../Source/Plugins/Processors.h"
#include "../Source/Plugins/ReferenceMatchProcessor.h"
#include <iostream>
#include <stdexcept>
static void check(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
int main(){try{juce::ScopedJuceInitialiser_GUI gui;
 for(double rate:{44100.,48000.,96000.})for(int block:{64,256,511,1024})for(int kind=0;kind<2;++kind){
  std::unique_ptr<hem::ModuleProcessor> p=kind?std::unique_ptr<hem::ModuleProcessor>(new hem::TruePeakProcessor):std::unique_ptr<hem::ModuleProcessor>(new hem::VcaProcessor);
  p->prepareToPlay(rate,block);juce::AudioBuffer<float> b(2,block);juce::MidiBuffer midi;
  for(int total=0;total<int(rate*6);total+=block){for(int ch=0;ch<2;++ch)for(int i=0;i<block;++i)b.setSample(ch,i,.7f*std::sin(float(total+i)*.13f));p->processBlock(b,midi);}
  hem::PluginTelemetry::Levels levels;check(p->pluginTelemetry()->read(levels),"processor telemetry");
  const auto count=std::min<std::uint64_t>(hem::ReductionHistory::capacity,levels.historySerial);hem::PluginTelemetry::MeterPoint first,last;
  check(p->pluginTelemetry()->readMeterPoint(levels.historySerial-count+1,first)&&p->pluginTelemetry()->readMeterPoint(levels.historySerial,last),"VCA/limiter history endpoints");
  check(double(last.endSample-first.startSample)>=4*rate,"VCA/limiter four seconds");
 }
 hem::ReferenceMatchProcessor p;p.prepareToPlay(48000,256);int loadCalls=0;
 p.editorActions.loadReference=[&]{++loadCalls;};p.editorActions.status=[] {return juce::String("STOP PLAYBACK BEFORE THIS ACTION");};
 auto editor=std::unique_ptr<juce::AudioProcessorEditor>(p.createEditor());
 auto* load=dynamic_cast<juce::Button*>(editor->findChildWithID("loadReference"));check(load&&load->isEnabled(),"load button enabled with host callback");load->onClick();check(loadCalls==1,"load invokes host callback");
 auto* bypass=dynamic_cast<juce::Button*>(editor->findChildWithID("bypass"));check(bypass,"bypass reachable");bypass->setToggleState(true,juce::sendNotification);check(p.state.getRawParameterValue("bypass")->load()==1,"bypass button APVTS binding");
 bypass->setToggleState(false,juce::sendNotification);p.state.getParameter("output")->setValueNotifyingHost(p.state.getParameter("output")->convertTo0to1(-12));
 juce::AudioBuffer<float> b(2,256);juce::MidiBuffer midi;
 auto render=[&]{for(int n=0;n<100;++n){for(int ch=0;ch<2;++ch)juce::FloatVectorOperations::fill(b.getWritePointer(ch),.25f,256);p.processBlock(b,midi);}return b.getSample(0,255);};
 check(std::abs(render()-.25f*std::pow(10.f,-12.f/20))<1e-5,"output DSP enabled");
 bypass->setToggleState(true,juce::sendNotification);check(std::abs(render()-.25f)<1e-5,"bypass returns actual dry signal");
 check(p.startSourceCapture(),"capture arms");check(p.captureProgressText().contains("WAITING FOR AUDIO"),"armed capture explains missing track audio");
 b.clear();p.processBlock(b,midi);check(p.captureProgressText().contains("CAPTURING"),"capture reports progress");
 p.cancelCaptureWhileCallbackExcluded();check(p.captureProgressText().isEmpty(),"cancel releases pending status");
 std::cout<<"PASS 24 VCA/TruePeak rate/block histories; Reference load dispatch, bypass UI/APVTS and audible-path sample equivalence, armed/progress/cancel status. Native OS picker interaction not automated.\n";return 0;
 }catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
