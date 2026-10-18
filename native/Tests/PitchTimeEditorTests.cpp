#include "../Source/Plugins/PitchTimeEditors.h"
#include <iostream>
#include <stdexcept>
namespace {
void check(bool b,const char* m){if(!b)throw std::runtime_error(m);}
void set(hem::PitchTimeProcessor& p,const char* id,float v){auto* parameter=p.state.getParameter(id);check(parameter!=nullptr,"Parameter absent");parameter->setValueNotifyingHost(parameter->convertTo0to1(v));}
juce::Component* find(juce::Component& p,const juce::String& id){if(p.getComponentID()==id)return &p;for(auto* c:p.getChildren())if(auto* found=find(*c,id))return found;return nullptr;}
void save(juce::Component& c,const juce::File& f){auto s=f.createOutputStream();check(s!=nullptr,"Raster output failed");s->setPosition(0);s->truncate();check(juce::PNGImageFormat().writeImageToStream(c.createComponentSnapshot(c.getLocalBounds()),*s),"Raster encoding failed");}
}
int main(int argc,char** argv){try{juce::ScopedJuceInitialiser_GUI gui;
 for(bool stretch:{false,true}){hem::PitchTimeProcessor p(stretch);bool rejected=false;try{p.prepareToPlay(48000,128);}catch(const std::runtime_error&){rejected=true;}check(rejected&&!p.processingReady(),"Realtime admission not rejected");
  juce::AudioBuffer<float> audio(2,128);audio.applyGain(0);audio.setSample(0,0,1);juce::MidiBuffer midi;p.processBlock(audio,midi);check(audio.getMagnitude(0,128)==0,"Accidental realtime process not silent");
  hem::pitchtime::Settings s;check(p.settings(48000,2,120,s).wasOk(),"Default offline settings invalid");
  set(p,"output",-6);check(p.settings(48000,2,120,s).wasOk()&&s.outputDb==-6,"Output not in render recipe");
  set(p,"pitchSemitones",24);set(p,"pitchCents",100);check(p.settings(48000,2,120,s).failed(),"Out-of-range combined pitch silently clamped");set(p,"pitchSemitones",0);set(p,"pitchCents",0);
  if(stretch){set(p,"tempoSync",1);check(p.settings(48000,2,60,s).wasOk()&&s.durationRatio==2,"Tempo sync ratio incorrect");check(p.settings(48000,2,20,s).failed(),"Extreme tempo ratio admitted");set(p,"tempoSync",0);}
  else {set(p,"mix",.25f);set(p,"correctionEnabled",1);set(p,"key",9);set(p,"scale",1);check(p.settings(48000,2,120,s).wasOk()&&s.wetMix==.25&&s.correction.enabled&&s.correction.key==9&&s.correction.scale==hem::pitchtime::Scale::major,"Correction/mix mapping wrong");}
  hem::PitchTimeEditor e(p);e.setSize(1672,941);int requests=0;e.setCommandRequest([&](hem::PitchTimeEditor::Command){++requests;});e.requestCommand(hem::PitchTimeEditor::Command::apply);check(requests==0,"No-source apply enabled");
  if(stretch){set(p,"stretchRatio",1.5f);set(p,"targetTempo",80);e.refreshParameterActivity();check(std::abs(p.parameter("stretchRatio")-1.5)<.001&&std::abs(p.parameter("targetTempo")-80)<.11,"APVTS ratio/tempo feedback changed a committed recipe");
   auto* target=dynamic_cast<juce::Slider*>(find(e,"targetTempo"));auto* ratio=dynamic_cast<juce::Slider*>(find(e,"stretchRatio"));auto* source=dynamic_cast<juce::Slider*>(find(e,"sourceTempo"));check(target&&ratio&&source,"Tempo controls missing");target->setValue(60,juce::sendNotificationSync);e.refreshParameterActivity();check(std::abs(p.parameter("stretchRatio")-2)<.001&&std::abs(p.parameter("targetTempo")-60)<.11,"Target slider failed to derive ratio");ratio->setValue(1.5,juce::sendNotificationSync);e.refreshParameterActivity();check(std::abs(p.parameter("stretchRatio")-1.5)<.001&&std::abs(p.parameter("targetTempo")-80)<.11,"Ratio slider failed to derive target");check(p.settings(48000,2,120,s).wasOk(),"Derived control recipe invalid");
   for(int pass=0;pass<3;++pass)for(double r:{.5,.75,1.,1.5,2.}){ratio->setValue(r,juce::sendNotificationSync);e.refreshParameterActivity();check(std::abs(p.parameter("stretchRatio")-r)<.001&&std::abs(p.parameter("targetTempo")-120/r)<.11,"Repeated ratio control feedback drifted");}
   source->setValue(100,juce::sendNotificationSync);e.refreshParameterActivity();check(std::abs(p.parameter("targetTempo")-50)<.11&&p.settings(48000,2,120,s).wasOk(),"Source slider read stale source BPM");
   hem::PitchTimeViewSnapshot synced;synced.projectBpm=80;set(p,"tempoSync",1);e.setSnapshot(synced);check(std::abs(target->getValue()-80)<.11&&std::abs(ratio->getValue()-1.25)<.001,"Synced display not derived from captured BPM");set(p,"tempoSync",0);e.refreshParameterActivity();check(std::abs(target->getValue()-50)<.11&&std::abs(ratio->getValue()-2)<.001&&p.settings(48000,2,120,s).wasOk(),"Leaving sync corrupted manual recipe");
   target->setValue(20,juce::sendNotificationSync);e.refreshParameterActivity();check(p.settings(48000,2,120,s).failed(),"Unsupported tempo ratio silently admitted");target->setValue(50,juce::sendNotificationSync);e.refreshParameterActivity();}
  int recipes=0;bool original=false;e.setRecipeChanged([&]{++recipes;});e.setAuditionRequest([&](bool value){original=value;});set(p,"output",-3);e.refreshParameterActivity();check(recipes==1,"Recipe-change cancellation callback absent");set(p,"bypass",1);e.refreshParameterActivity();check(original&&recipes==1,"Audition bypass not distinct from render recipe");
  hem::PitchTimeViewSnapshot view;view.sourceValid=true;view.selection="Fixture whole source";view.sourceSeconds=1;view.renderedSeconds=1;view.canUndo=true;e.setSnapshot(view);e.requestCommand(hem::PitchTimeEditor::Command::preview);e.requestCommand(hem::PitchTimeEditor::Command::apply);e.requestCommand(hem::PitchTimeEditor::Command::undo);check(requests==3,"Host command not routed");
  view.busy=true;e.setSnapshot(view);e.requestCommand(hem::PitchTimeEditor::Command::apply);check(requests==3,"Busy apply admitted");e.requestCommand(hem::PitchTimeEditor::Command::preview);check(requests==4,"Busy Preview/CANCEL blocked");view.busy=false;
  int closed=0;e.setCloseRequest([&]{++closed;});check(e.keyPressed(juce::KeyPress(juce::KeyPress::escapeKey))&&closed==1,"Escape owner callback failed");
  if(argc>1){auto dir=juce::File::getCurrentWorkingDirectory().getChildFile(argv[1]);check(dir.createDirectory().wasOk(),"Cannot create rasters");e.setSnapshot(view);save(e,dir.getChildFile(stretch?"time-stretch-idle.png":"pitch-formant-idle.png"));}
 }
 std::cout<<"PASS: offline rejection, settings/correction/mix/output, tempo limits, host commands, Escape, raster generation\n";return 0;
 }catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<"\n";return 1;}}
