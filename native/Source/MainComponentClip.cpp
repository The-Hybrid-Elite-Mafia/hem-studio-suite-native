#include "MainComponent.h"
#include "Plugins/PitchTimeEditors.h"
#include "Systems/PitchTimeClipRenderer.h"
#include "Systems/ClipEditState.h"
#include <juce_cryptography/juce_cryptography.h>
#include <thread>
#include <optional>

namespace {
using View = hem::PitchTimeViewSnapshot;
void clipRequire(bool b,const char* text){if(!b)throw std::runtime_error(text);}
struct ClipCancelled {};
void clipCheck(const std::atomic<bool>& cancel){if(cancel.load())throw ClipCancelled{};}
class ClipHashStream final:public juce::InputStream {
 juce::FileInputStream stream;const std::atomic<bool>& cancel;
public:
 ClipHashStream(const juce::File& f,const std::atomic<bool>& c):stream(f),cancel(c){clipRequire(stream.getStatus().wasOk(),"Cannot open selected source");}
 juce::int64 getTotalLength()override{return stream.getTotalLength();}juce::int64 getPosition()override{return stream.getPosition();}
 bool setPosition(juce::int64 p)override{return stream.setPosition(p);}bool isExhausted()override{return stream.isExhausted();}
 int read(void* d,int n)override{clipCheck(cancel);int r=stream.read(d,n);clipRequire(stream.getStatus().wasOk(),"Source hash read failed");return r;}
};
juce::String clipHash(const juce::File& f,const std::atomic<bool>& c){ClipHashStream s(f,c);auto n=s.getTotalLength();auto hash=juce::SHA256(s).toHexString();clipRequire(n>0&&s.getPosition()==n&&f.getSize()==n,"Source changed while hashing");return hash;}
std::vector<float> clipEnvelope(const juce::File& f,const std::atomic<bool>& cancel){
 juce::AudioFormatManager formats;formats.registerBasicFormats();std::unique_ptr<juce::AudioFormatReader> r(formats.createReaderFor(f));
 clipRequire(r&&r->lengthInSamples>0&&r->numChannels<=2,"Cannot inspect source waveform");std::vector<float> bins(1024,0);juce::AudioBuffer<float> block(int(r->numChannels),4096);
 for(juce::int64 pos=0;pos<r->lengthInSamples;pos+=4096){clipCheck(cancel);int n=int(std::min<juce::int64>(4096,r->lengthInSamples-pos));clipRequire(r->read(&block,0,n,pos,true,true),"Waveform read failed");
  for(int i=0;i<n;++i){float value=0;for(int c=0;c<block.getNumChannels();++c){float sample=block.getSample(c,i);clipRequire(std::isfinite(sample),"Non-finite source waveform");if(std::abs(sample)>std::abs(value))value=sample;}auto index=size_t((pos+i)*1024/r->lengthInSamples);if(std::abs(value)>std::abs(bins[index]))bins[index]=value;}}
 return bins;
}
juce::String clipFingerprint(hem::PitchTimeProcessor& p){auto state=p.state.copyState();for(int i=state.getNumChildren();--i>=0;)if(state.getChild(i)["id"].toString()=="bypass")state.removeChild(i,nullptr);return state.toXmlString();}
juce::String clipNote(double midi){static const char* names[]{"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};if(!std::isfinite(midi))return "UNVOICED";int note=int(std::lround(midi));return juce::String(names[(note%12+12)%12])+juce::String(note/12-1);}
}
struct MainComponent::ClipHistory {}; // State is value-owned by TrackEngine::clipEdit.
struct MainComponent::ClipOperation {
 enum class Work {inspect,preview,apply,toggle};
 int module=0,track=0;unsigned hostGeneration=0;double targetRate=48000;int targetBlock=256;bool loop=false;
 juce::File source;juce::String sourceHash,fingerprint;juce::int64 frames=0;double rate=0;int channels=0;
 std::unique_ptr<hem::PitchTimeProcessor> processor;juce::Component::SafePointer<hem::PitchTimeEditor> editor;
 View view,finishedView;std::thread worker;std::atomic<bool> cancel{false},done{false},previewActive{false},audioInvalid{false},auditionOriginal{false};bool running=false,ready=false;
 Work work=Work::inspect;juce::String error;hem::pitchtime::ClipRenderRequest request;hem::pitchtime::ClipRenderResult rendered;
 std::optional<hem::clipEdit::State> toggleState,pendingState;
 std::vector<juce::File> privateFiles;juce::File committedFile;
 std::unique_ptr<hem::StemPlayback> previewOriginal,previewRendered,preparedOriginal,preparedRendered,preparedApply;
 uint64_t originalEpoch=0;
 juce::AudioBuffer<float> originalBuffer,renderedBuffer;std::array<std::atomic<float>,4> peaks{};
 ~ClipOperation(){cancel=true;if(worker.joinable())worker.join();if(previewOriginal&&originalEpoch)previewOriginal->transport->discardPreparedEpoch(originalEpoch);previewOriginal.reset();previewRendered.reset();preparedOriginal.reset();preparedRendered.reset();preparedApply.reset();for(const auto& file:privateFiles)if(file!=committedFile)file.deleteFile();}
};

bool MainComponent::clipOperationBusy()const{return clipOperation&&(clipOperation->running||clipOperation->previewActive.load());}
void MainComponent::invalidateClipAudio()noexcept{if(auto* op=clipAudio.load(std::memory_order_acquire)){op->cancel=true;op->previewActive=false;op->audioInvalid=true;}}
void MainComponent::closeClipOperation(){
 auto op=clipOperation;if(!op)return;op->cancel=true;
 {const GraphControlGuard lock(*this);op->previewActive=false;clipAudio.store(nullptr,std::memory_order_release);clipOperation.reset();}
 if(op->worker.joinable())op->worker.join(); // never inside callback exclusion
}
void MainComponent::renderClipPreview(juce::AudioBuffer<float>& output,int n){
 auto* op=clipAudio.load(std::memory_order_acquire);if(!op||!op->previewActive.load(std::memory_order_relaxed)||op->audioInvalid.load()||!op->previewOriginal||!op->previewRendered)return;
 if(n>op->originalBuffer.getNumSamples()){op->previewActive=false;return;}
 op->originalBuffer.clear();op->renderedBuffer.clear();juce::AudioSourceChannelInfo a(&op->originalBuffer,0,n),b(&op->renderedBuffer,0,n);
 const bool first=op->previewOriginal->transport->renderRt(a),second=op->previewRendered->transport->renderRt(b);
 if(!first||!second){transportDropouts.fetch_add(1);trackRenderFaults.fetch_add(1);trackRenderSilencedCallbacks.fetch_add(1);trackScheduler.latchFault();op->previewActive=false;output.clear();return;}
 const bool original=op->auditionOriginal.load(std::memory_order_relaxed);auto& chosen=original?op->originalBuffer:op->renderedBuffer;
 for(int c=0;c<2;++c){output.copyFrom(c,0,chosen,c,0,n);op->peaks[size_t(c)].store(op->originalBuffer.getMagnitude(c,0,n),std::memory_order_relaxed);op->peaks[size_t(c+2)].store(chosen.getMagnitude(c,0,n),std::memory_order_relaxed);}
 if(!(original?op->previewOriginal:op->previewRendered)->transport->isPlaying())op->previewActive=false;
}
bool MainComponent::clipPreviewReady(int frames)const noexcept{
 auto* op=clipAudio.load(std::memory_order_acquire);if(!op||!op->previewActive.load()||op->audioInvalid.load())return true;
 if(!op->previewOriginal||!op->previewRendered)return false;
 return op->previewOriginal->transport->availableRt(frames)&&op->previewRendered->transport->availableRt(frames);
}
void MainComponent::openClipOperation(int module,EditorOpenIntent intent){
 if(module<0||module>=int(hem::PluginRegistry::entries().size())||!hem::PluginRegistry::entries()[size_t(module)].clipOperation)return;
 if(nativeResourceLoadBusy()||playing()||vocals.recorder.busy()||sessionWorker->isBusy()||exportWorker->isBusy()||importer->hasPendingWork()){
  message.setText("Pause and finish pending work before a source operation",juce::dontSendNotification);return;}
 rememberEditorOrigin(intent);closePluginEditor();prepareOfflineSession();
 auto op=std::make_shared<ClipOperation>();op->module=module;op->track=selectedTrack;op->hostGeneration=generation;op->targetRate=sampleRate;op->targetBlock=blockSize;op->loop=looping;
 op->processor=std::make_unique<hem::PitchTimeProcessor>(juce::String(hem::PluginRegistry::entries()[size_t(module)].id)=="time-pitch-stretch");
 op->originalBuffer.setSize(2,blockSize);op->renderedBuffer.setSize(2,blockSize);op->view.projectBpm=projectBpm.load();
 auto editor=std::make_unique<hem::PitchTimeEditor>(*op->processor);op->editor=editor.get();juce::Component::SafePointer<MainComponent> safe(this);
 editor->setAuditionRequest([weak=std::weak_ptr<ClipOperation>(op)](bool original){if(auto operation=weak.lock())operation->auditionOriginal=original;});
 editor->setCommandRequest([safe](hem::PitchTimeEditor::Command c){if(safe)safe->clipCommand(int(c));});
 const juce::Component::SafePointer<hem::PitchTimeEditor> expectedEditor(editor.get());
 editor->setCloseRequest([safe,expectedEditor]{
  // Leave the editor's key/callback stack before retiring its captured state.
  juce::MessageManager::callAsync([safe,expectedEditor]{
   if(safe&&expectedEditor&&safe->shellPage==ShellPage::plugin&&safe->editedProcessor==expectedEditor->getAudioProcessor())safe->workspace->backFromEditor();
  });
 });
 {const GraphControlGuard lock(*this);clipOperation=op;clipAudio.store(op.get(),std::memory_order_release);}
 editedProcessor=op->processor.get();editedTrack=selectedTrack;editedSlot=-1;workspace->setEditor(std::move(editor));workspace->setClipCommands([safe](int c){if(safe)safe->clipCommand(c);},false,false);showShellPage(ShellPage::plugin);
 if(!current().loaded){op->view.status="Select a loaded stem in the mixer. This operation edits the whole selected stem.";op->editor->setSnapshot(op->view);return;}
 op->source=current().playback->file;op->view.selection="Track "+juce::String(selectedTrack+1)+" / whole stem / "+op->source.getFileName();op->view.busy=true;op->view.status="Reading source identity and waveform...";op->editor->setSnapshot(op->view);
 op->fingerprint=clipFingerprint(*op->processor);op->finishedView=op->view;op->running=true;auto* job=op.get();auto* disk=&diskThread;
 try{job->worker=std::thread([job,disk]{try{juce::AudioFormatManager formats;formats.registerBasicFormats();std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(job->source));
  clipRequire(reader&&reader->lengthInSamples>0&&reader->numChannels>=1&&reader->numChannels<=2,"Unsupported selected source");job->frames=reader->lengthInSamples;job->rate=reader->sampleRate;job->channels=int(reader->numChannels);reader.reset();
  job->sourceHash=clipHash(job->source,job->cancel);job->finishedView.sourceEnvelope=clipEnvelope(job->source,job->cancel);job->finishedView.sourceSeconds=double(job->frames)/job->rate;job->finishedView.sourceValid=true;
  job->preparedOriginal=std::make_unique<hem::StemPlayback>();job->preparedOriginal->prepareFile(job->source,formats,*disk,job->targetRate,job->targetBlock,false);clipCheck(job->cancel);
 }catch(const ClipCancelled&){job->error="Source operation cancelled";}catch(const std::exception& e){job->error=e.what();}catch(...){job->error="Source inspection failed";}job->done.store(true,std::memory_order_release);});}catch(...){job->running=false;job->view.busy=false;job->view.status="Unable to start bounded source worker; original unchanged";}
}
void MainComponent::clipCommand(int command){
 if(command==0&&trackScheduler.faulted()){message.setText("Audio overload: press PLAY to resume audio before Preview",juce::dontSendNotification);return;}
 auto op=clipOperation;if(!op)return;using Command=hem::PitchTimeEditor::Command;const auto action=static_cast<Command>(command);
 if(action==Command::selectSource){closePluginEditor();showShellPage(ShellPage::mixer);message.setText("Select a loaded track, then reopen Pitch/Formant or Time/Pitch in the vault",juce::dontSendNotification);return;}
 if(op->running){if(action==Command::preview){op->cancel=true;op->view.status="Cancelling private render...";}return;}
 if(action==Command::preview&&op->previewActive.load()){{const GraphControlGuard lock(*this);op->previewActive=false;}op->view.status="Preview stopped / production source unchanged";return;}
 if(!op->ready){if(action==Command::preview)openClipOperation(op->module);return;}
 if(playing()||vocals.recorder.busy()||sessionWorker->isBusy()||exportWorker->isBusy()||importer->hasPendingWork())return;
 if(op->track!=selectedTrack||op->hostGeneration!=generation||bank->tracks[size_t(op->track)].playback->file!=op->source){closePluginEditor();message.setText("Selected source changed; reopen the source operation",juce::dontSendNotification);return;}
 op->work=action==Command::undo?ClipOperation::Work::toggle:action==Command::apply?ClipOperation::Work::apply:ClipOperation::Work::preview;
 if(op->work==ClipOperation::Work::toggle){if(!bank->tracks[size_t(op->track)].clipEdit)return;op->toggleState=bank->tracks[size_t(op->track)].clipEdit;}
 else{hem::pitchtime::Settings settings;auto valid=op->processor->settings(op->rate,op->channels,projectBpm.load(),settings);if(valid.failed()){op->view.status=valid.getErrorMessage();return;}
  op->request={};op->request.source=op->source;op->request.expectedSourceSha256=op->sourceHash;op->request.sourceGeneration=op->hostGeneration;op->request.frameCount=op->frames;op->request.settings=settings;op->request.capturedTempoBpm=projectBpm.load();
  const auto directory=op->source.getParentDirectory().getChildFile("HEM Rendered Clips");op->request.destination=hem::pitchtime::PitchTimeClipRenderer::uniqueDestination(directory);
 }
 {const GraphControlGuard lock(*this);op->previewActive=false;}
 op->previewRendered.reset();for(const auto& file:op->privateFiles)file.deleteFile();op->privateFiles.clear();
 op->cancel=false;op->done=false;op->error.clear();op->rendered={};op->pendingState.reset();op->fingerprint=clipFingerprint(*op->processor);op->finishedView=op->view;op->finishedView.pitch.clear();op->running=true;op->view.busy=true;op->view.renderedValid=false;op->view.renderedEnvelope.clear();op->view.status="Rendering private source operation...";
 auto* job=op.get();auto* disk=&diskThread;
 try{job->worker=std::thread([job,disk]{try{juce::AudioFormatManager formats;formats.registerBasicFormats();auto cancelled=[job]{return job->cancel.load();};
  if(job->work==ClipOperation::Work::toggle){const auto& old=*job->toggleState;hem::clipEdit::State next;hem::clipEdit::MediaIdentity target;const auto status=hem::clipEdit::stageToggle(old,!old.renderedActive,hem::clipEdit::activeMedia(old),next,target,cancelled);clipRequire(status.wasOk(),status.getErrorMessage().toRawUTF8());clipRequire(juce::File(hem::clipEdit::activeMedia(old).path)==job->source,"Undo source identity changed");
   job->preparedApply=std::make_unique<hem::StemPlayback>();job->preparedApply->prepareFile(juce::File(target.path),formats,*disk,job->targetRate,job->targetBlock,job->loop);job->pendingState=std::move(next);
  }else{clipRequire(job->request.destination.getParentDirectory().createDirectory().wasOk(),"Cannot create HEM Rendered Clips next to source; original unchanged");juce::int64 nextPoint=0;const auto step=std::max<juce::int64>(1,job->frames/1023);
   job->rendered=hem::pitchtime::PitchTimeClipRenderer::render(job->request,cancelled,[&](auto frame,const hem::pitchtime::CorrectionFrame& p){if(frame<nextPoint||job->finishedView.pitch.size()>=1024)return;nextPoint=frame+step;job->finishedView.pitch.push_back({double(frame)/job->rate,(p.inputMidi-69)*100,(p.targetMidi-69)*100,p.input.voiced,p.targetValid});job->finishedView.inputNote=p.input.voiced?clipNote(p.inputMidi):"UNVOICED";job->finishedView.targetNote=p.targetValid?clipNote(p.targetMidi):"UNVOICED";});
   clipRequire(job->rendered.status==hem::pitchtime::ClipRenderResult::Status::complete,job->rendered.error.toRawUTF8());job->privateFiles.push_back(job->request.destination);hem::clipEdit::State record;auto capture=hem::clipEdit::captureMetadata(job->rendered.metadata,job->track,record);clipRequire(capture.wasOk(),capture.getErrorMessage().toRawUTF8());auto verified=hem::clipEdit::verifyFiles(record,cancelled);clipRequire(verified.wasOk(),verified.getErrorMessage().toRawUTF8());job->pendingState=std::move(record);
   job->finishedView.renderedEnvelope=clipEnvelope(job->request.destination,job->cancel);job->finishedView.renderedSeconds=double(job->rendered.metadata.renderedFrames)/job->rate;job->finishedView.renderedValid=true;
   job->preparedRendered=std::make_unique<hem::StemPlayback>();job->preparedRendered->prepareFile(job->request.destination,formats,*disk,job->targetRate,job->targetBlock,false);
   if(job->work==ClipOperation::Work::preview){const auto ready=job->previewOriginal->transport->prepareEpoch(0,false,job->originalEpoch,[job]{return job->cancel.load();});if(ready.failed())throw std::runtime_error(ready.getErrorMessage().toStdString());}
   if(job->work==ClipOperation::Work::apply){job->preparedApply=std::make_unique<hem::StemPlayback>();job->preparedApply->prepareFile(job->request.destination,formats,*disk,job->targetRate,job->targetBlock,job->loop);}
  }clipCheck(job->cancel);
 }catch(const ClipCancelled&){job->error="Source operation cancelled";}catch(const std::exception& e){job->error=e.what();}catch(...){job->error="Source operation failed";}job->done.store(true,std::memory_order_release);});}catch(...){job->running=false;job->view.busy=false;job->view.status="Unable to start bounded source worker; original unchanged";}
}
void MainComponent::pollClipOperation(){
 auto op=clipOperation;if(!op)return;
 if(op->audioInvalid.load()||op->hostGeneration!=generation||op->loop!=looping||op->track!=selectedTrack||(op->source!=juce::File()&&bank->tracks[size_t(op->track)].playback->file!=op->source)){closePluginEditor();message.setText("Source/device changed: private operation cancelled",juce::dontSendNotification);return;}
 const auto currentFingerprint=clipFingerprint(*op->processor);
 if(op->running&&currentFingerprint!=op->fingerprint&&op->work!=ClipOperation::Work::inspect)op->cancel=true;
 if(op->running&&op->done.load(std::memory_order_acquire)){
  if(op->worker.joinable())op->worker.join();op->running=false;op->view.busy=false;
  if(op->cancel.load()||op->error.isNotEmpty()){op->view.status=op->error.isNotEmpty()?op->error:"Recipe changed; stale render discarded";op->preparedApply.reset();op->preparedRendered.reset();if(op->originalEpoch&&op->previewOriginal){op->previewOriginal->transport->discardPreparedEpoch(op->originalEpoch);op->originalEpoch=0;}}
  else if(op->work==ClipOperation::Work::inspect){op->ready=true;op->view=std::move(op->finishedView);op->view.busy=false;op->view.status="Whole selected stem / offline / Preview leaves production unchanged";{const GraphControlGuard lock(*this);op->previewOriginal.swap(op->preparedOriginal);}}
  else if(op->work==ClipOperation::Work::preview){op->view=std::move(op->finishedView);op->view.busy=false;op->view.status=deviceManager.getCurrentAudioDevice()?"Preview audition only / Bypass compares original / Apply commits":"Preview rendered / no audio device / Apply commits";
   {const GraphControlGuard lock(*this);if(!trackScheduler.faulted()&&op->originalEpoch&&op->previewOriginal->transport->preparedEpoch()==op->originalEpoch){const bool play=deviceManager.getCurrentAudioDevice()!=nullptr;op->previewOriginal->transport->commitPreparedEpoch(op->originalEpoch,play);op->originalEpoch=0;op->previewRendered.swap(op->preparedRendered);if(play){op->previewRendered->transport->start();op->previewActive=true;}}else op->view.status="Preview cancelled after source/device overload";}if(op->originalEpoch){op->previewOriginal->transport->discardPreparedEpoch(op->originalEpoch);op->originalEpoch=0;}op->preparedRendered.reset();
  }else{
   bool committed=false;auto& track=bank->tracks[size_t(op->track)];
   {const GraphControlGuard lock(*this);if(!playing()&&op->hostGeneration==generation&&track.playback->file==op->source&&op->preparedApply){track.playback.swap(op->preparedApply);++generation;committed=true;}}
   if(committed){op->committedFile=track.playback->file;track.loaded=true;track.name=track.playback->file.getFileName();track.sourceRate=track.playback->sourceRate;track.clipEdit=std::move(op->pendingState);const int module=op->module;op->ready=false;const bool toggled=op->work==ClipOperation::Work::toggle;closePluginEditor();openClipOperation(module,EditorOpenIntent::replacement);message.setText(toggled?"Source restored / one-level Undo-Redo retained":"Rendered source applied / original retained / controls reset to neutral",juce::dontSendNotification);return;}
   op->view.status="Source changed before stopped commit; original retained";
  }
 }
 if(!op->running&&currentFingerprint!=op->fingerprint){op->view.renderedValid=false;op->view.renderedEnvelope.clear();op->view.pitch.clear();op->view.inputNote="UNAVAILABLE";op->view.targetNote="UNAVAILABLE";op->view.status="Recipe changed: Preview or Apply renders new settings";{const GraphControlGuard lock(*this);op->previewActive=false;}op->fingerprint=currentFingerprint;}
 op->view.projectBpm=projectBpm.load();op->view.canUndo=bank->tracks[size_t(op->track)].clipEdit.has_value();op->view.peaksValid=op->previewActive.load();for(int i=0;i<4;++i)op->view.peaks[size_t(i)]=op->peaks[size_t(i)].load();if(op->editor)op->editor->setSnapshot(op->view);juce::Component::SafePointer<MainComponent> safe(this);workspace->setClipCommands([safe](int c){if(safe)safe->clipCommand(c);},op->running,op->view.canUndo);
}
void MainComponent::captureClipEdits(hem::SessionSnapshot& snapshot)const{for(size_t i=0;i<snapshot.tracks.size();++i){const auto& record=bank->tracks[i].clipEdit;if(record&&juce::File(hem::clipEdit::activeMedia(*record).path)==bank->tracks[i].playback->file)snapshot.tracks[i].clipEdit=record;else snapshot.tracks[i].clipEdit.reset();}}
void MainComponent::restoreClipEdits(const hem::SessionSnapshot& snapshot){for(size_t i=0;i<snapshot.tracks.size();++i)bank->tracks[i].clipEdit=snapshot.tracks[i].clipEdit;}






