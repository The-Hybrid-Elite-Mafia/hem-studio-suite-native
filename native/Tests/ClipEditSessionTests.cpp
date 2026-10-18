#include "../Source/SessionState.h"
#include "../Source/Systems/PresetManager.h"
#include <iostream>
#include <stdexcept>
#if JUCE_WINDOWS
#include <windows.h>
#endif
static void pumpMessages(){
#if JUCE_WINDOWS
 MSG message; while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){TranslateMessage(&message);DispatchMessageW(&message);} juce::Thread::sleep(10);
#else
 juce::MessageManager::getInstance()->runDispatchLoopUntil(10);
#endif
}
static void check(bool b,const char* message){if(!b)throw std::runtime_error(message);}
static void wave(const juce::File& file,float level){auto stream=file.createOutputStream();check(bool(stream),"fixture stream");juce::WavAudioFormat format;std::unique_ptr<juce::AudioFormatWriter> writer(format.createWriterFor(stream.release(),48000,2,24,{},0));check(bool(writer),"fixture writer");juce::AudioBuffer<float> b(2,480);b.clear();b.setSample(0,0,level);check(writer->writeFromAudioSampleBuffer(b,0,480),"fixture write");}
int main(){juce::ScopedJuceInitialiser_GUI gui;try{
 using namespace hem;const auto dir=juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("hem-clip-session-"+juce::Uuid().toString());check(dir.createDirectory().wasOk(),"fixture directory");
 const auto original=dir.getChildFile("original.wav"),rendered=dir.getChildFile("rendered.wav"),xml=dir.getChildFile("session.xml"),json=dir.getChildFile("session.json");wave(original,.2f);wave(rendered,.3f);
 auto identity=[](const juce::File& file){return clipEdit::MediaIdentity{file.getFullPathName(),juce::SHA256(file).toHexString(),file.getSize(),480,48000,2};};
 clipEdit::State edit;edit.trackIndex=2;edit.original=identity(original);edit.rendered=identity(rendered);edit.recipe.backend=pitchtime::backendIdentity;edit.recipe.settings.pitchSemitones=.12345678901234567;
 SessionSnapshot s;MonitorProcessor monitor;s.monitorParameters=SessionState::captureParameters(monitor);s.tracks[2].source=rendered;s.tracks[2].clipEdit=edit;
 auto tree=SessionState::encode(s,xml);check(int(tree["version"])==4&&tree.getChild(3).getChild(8).hasType("CLIP_EDIT"),"v4 clip appended after eight slots");
 SessionState::save(s,xml);auto loaded=SessionState::load(xml);check(loaded.tracks[2].clipEdit&&loaded.tracks[2].clipEdit->recipe.settings.pitchSemitones==edit.recipe.settings.pitchSemitones,"exact XML recipe recall");
 PresetManager::save(s,json);loaded=PresetManager::load(json);check(loaded.tracks[2].clipEdit&&clipEdit::sameIdentity(loaded.tracks[2].clipEdit->rendered,edit.rendered),"JSON media recall");
 auto rejects=[&](juce::ValueTree t){try{SessionState::decode(t,xml);return false;}catch(const std::exception&){return true;}};
 auto bad=tree.createCopy();bad.setProperty("version",3,nullptr);check(rejects(bad),"legacy version rejects clip");bad=tree.createCopy();bad.getChild(3).getChild(8).setProperty("trackIndex",1,nullptr);check(rejects(bad),"cross-track clip rejected");
 bad=tree.createCopy();bad.getChild(3).getChild(8).setProperty("renderedActive",false,nullptr);check(rejects(bad),"active path mismatch rejected");bad=tree.createCopy();bad.getChild(3).removeChild(0,nullptr);check(rejects(bad),"clip cannot replace mandatory slot");
 auto legacy=s;legacy.tracks[2].clipEdit.reset();auto old=SessionState::encode(legacy,xml);check(int(old["version"])==1&&old.getChild(3).getNumChildren()==8,"legacy layout unchanged absent clip");
 // Worker exercises real private playback preparation + verified metadata recall.
 juce::TimeSliceThread disk("clip-session-read-ahead");check(disk.startThread(),"disk thread");bool completed=false,committed=false,success=false;
 {SessionWorker worker(disk,[&](std::unique_ptr<TrackBank>& bank,const SessionSnapshot&,SessionWorker::Context){committed=bank&&bank->tracks[2].clipEdit.has_value()&&bank->tracks[2].loaded;return juce::Result::ok();},[](const juce::String&){},[&](bool,const juce::File&,const juce::Result& result){success=result.wasOk();completed=true;});
 check(worker.load(xml,{48000,64,1}),"worker start");for(int i=0;i<300&&!completed;++i)pumpMessages();check(completed&&success&&committed,"worker verified private clip publication");}
 // Change original bytes; inactive undo source must block publication too.
 {auto stream=original.createOutputStream();check(bool(stream),"change original");stream->setPosition(original.getSize()-1);stream->writeByte(99);}
 completed=committed=success=false;
 {SessionWorker worker(disk,[&](std::unique_ptr<TrackBank>&,const SessionSnapshot&,SessionWorker::Context){committed=true;return juce::Result::ok();},[](const juce::String&){},[&](bool,const juce::File&,const juce::Result& result){success=result.wasOk();completed=true;});check(worker.load(xml,{48000,64,2}),"changed worker start");for(int i=0;i<300&&!completed;++i)pumpMessages();check(completed&&!success&&!committed,"changed inactive original blocks commit");}
 disk.stopThread(2000);
 std::cout<<"PASS Clip session: XML/JSON recall, exact recipe, v1layout preservation, v4scope/path/slot rejection, real private worker staging and changed-original commit rejection\n";
 // Leave uniquely named fixtures for inspection; no user paths deleted.
 return 0;
 }catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
