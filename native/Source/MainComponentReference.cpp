#include "MainComponent.h"
#include "Plugins/ReferenceMatchProcessor.h"
#include <cmath>

struct MainComponent::ReferenceOperation final:juce::Thread {
 ReferenceOperation():juce::Thread("Reference operation"){}
 ~ReferenceOperation()override{signalThreadShouldExit();notify();stopThread(-1);}
 int mode=0,track=0,slot=0,block=0,budget=0;unsigned version=0;double rate=0,bpm=120;
 hem::ModuleProcessor* expected=nullptr;hem::NativeProcessorSnapshot snapshot;
 juce::File file;std::unique_ptr<hem::ReferenceMatchProcessor> candidate;
 hem::reference_match::Profile source;bool hasSource=false;
 std::unique_ptr<hem::InsertChain::PreparedNode> node;
 juce::AudioBuffer<float> audition;int auditionCursor=0;
 std::atomic<bool> busy{false},done{false};juce::Result result=juce::Result::ok();
 void run()override{
  try{
   result=juce::Result::ok();
   if(!candidate||mode==3){std::unique_ptr<hem::ModuleProcessor> restored;hem::NativeRestoreContext context;context.sampleRate=rate;context.blockSize=block;context.tempoBpm=bpm;context.cancelled=[this]{return threadShouldExit();};result=hem::NativeProcessorState::restoreUnpublished(std::make_unique<hem::ReferenceMatchProcessor>(),snapshot,context,restored);if(result.wasOk())candidate.reset(static_cast<hem::ReferenceMatchProcessor*>(restored.release()));}
   if(result.wasOk()&&mode==0)result=candidate->loadReferenceFileUnpublished(file);
   if(result.wasOk()&&mode==2){for(auto param:snapshot.parameters){auto* target=candidate->state.getParameter(param["id"].toString());if(target)target->setValueNotifyingHost(target->convertTo0to1(float(param["value"])));}if(hasSource)result=candidate->setSourceUnpublished(source);if(result.wasOk())result=candidate->matchUnpublished();}
   if(result.wasOk()&&mode==3)result=candidate->revertUnpublished();
   if(result.wasOk()&&(mode==2||mode==3)&&!threadShouldExit()){hem::InsertChain staging;node=staging.stageNode(std::move(candidate),rate,block,budget);if(!node)result=juce::Result::fail("Reference candidate preparation failed");else result=hem::NativeProcessorState::validatePreparedForPublication(*node->processor);}
   if(result.wasOk()&&mode==5)prepareAudition();
   if(threadShouldExit())result=juce::Result::fail("Reference work cancelled");
  }catch(const std::exception& e){result=juce::Result::fail(e.what());}catch(...){result=juce::Result::fail("Reference work failed");}
  done.store(true,std::memory_order_release);
 }
 void prepareAudition(){
  const auto identity=candidate->referenceIdentityOffThread();juce::File media(identity.path);const auto bytes=media.getSize();
  if(identity.path.isEmpty()||!media.existsAsFile()||bytes<=0||bytes>512*1024*1024||bytes!=identity.byteSize){result=juce::Result::fail("Reference unavailable for audition");return;}
  juce::MemoryBlock data;juce::FileInputStream input(media);data.setSize(size_t(bytes));if(!input.openedOk()||input.read(data.getData(),int(bytes))!=bytes||!input.isExhausted()||juce::SHA256(data).toHexString()!=identity.sha256){result=juce::Result::fail("Reference changed; reload before audition");return;}
  juce::AudioFormatManager formats;formats.registerBasicFormats();std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(std::make_unique<juce::MemoryInputStream>(data,false)));
  if(!reader||reader->sampleRate!=identity.sourceRate||reader->lengthInSamples!=identity.frames||int(reader->numChannels)!=identity.channels){result=juce::Result::fail("Reference metadata changed");return;}
  const int frames=int(std::min(identity.frames,juce::int64(identity.sourceRate*30)));juce::AudioBuffer<float> sourcePcm(identity.channels,frames);if(!reader->read(&sourcePcm,0,frames,0,true,true)){result=juce::Result::fail("Reference decode failed");return;}
  const int outputFrames=int(std::ceil(frames*rate/identity.sourceRate));audition.setSize(2,outputFrames);double maximum=0;
  const double cutoff=std::min(1.0,rate/identity.sourceRate)*.94;const int radius=int(std::ceil(24/cutoff));
  for(int n=0;n<outputFrames;++n){if((n&1023)==0&&threadShouldExit()){result=juce::Result::fail("Audition cancelled");return;}double position=n*identity.sourceRate/rate;int base=int(std::floor(position));double norm=0;std::array<double,2> value{};
   for(int k=base-radius+1;k<=base+radius;++k){double x=position-k;if(std::abs(x)>=radius)continue;double w=(std::abs(x)<1e-12?cutoff:std::sin(juce::MathConstants<double>::pi*cutoff*x)/(juce::MathConstants<double>::pi*x))*(.5+.5*std::cos(juce::MathConstants<double>::pi*x/radius));norm+=w;if(k>=0&&k<frames)for(int ch=0;ch<2;++ch)value[size_t(ch)]+=w*sourcePcm.getSample(std::min(ch,identity.channels-1),k);}
   for(int ch=0;ch<2;++ch){double v=value[size_t(ch)]/norm;if(!std::isfinite(v)){result=juce::Result::fail("Reference contains nonfinite PCM");return;}audition.setSample(ch,n,float(v));maximum=std::max(maximum,std::abs(v));}
  }
  // Monitoring-only preview: at most -6dBFS sample peak, no upward normalization.
  audition.applyGain(float(maximum>.5?.5/maximum:1));const int fade=std::min(256,outputFrames/2);for(int ch=0;ch<2;++ch)for(int i=0;i<fade;++i){audition.setSample(ch,i,audition.getSample(ch,i)*float(i)/float(fade));int tail=outputFrames-1-i;audition.setSample(ch,tail,audition.getSample(ch,tail)*float(i)/float(fade));}auditionCursor=0;
 }
};
void MainComponent::setReferenceStatus(const juce::String& text){referenceStatus=text;message.setText(text,juce::dontSendNotification);}
bool MainComponent::referenceOperationBusy()const{return referenceChoosing||(referenceOperation&&referenceOperation->busy.load());}
void MainComponent::bindReferenceEditor(hem::ReferenceMatchProcessor& p,int track,int slot){
 juce::Component::SafePointer<MainComponent> safe(this);auto* expected=&p;
 referenceStatus.clear();p.editorActions.status=[safe]()->juce::String{return safe?safe->referenceStatus:juce::String();};
 auto action=[safe,track,slot,expected](int command){if(safe)safe->referenceAction(command,track,slot,expected);};
 p.editorActions.loadReference=[action]{action(0);};p.editorActions.captureSource=[action]{action(1);};p.editorActions.match=[action]{action(2);};p.editorActions.revert=[action]{action(3);};p.editorActions.audition=[action](bool ref){action(ref?5:4);};p.editorActions.back={}; // Workspace binds Back to the actual editor lifetime.

 // Polling owns captured-profile consumption, so closing/reopening never loses it.
 p.editorActions.sourceCaptured={};p.editorActions.referenceLabel=[safe,expected]()->juce::String{if(!safe)return {};auto op=safe->referenceOperation;if(!op||op->expected!=expected)return {};if(op->busy.load())return "REFERENCE WORKING";if(op->candidate){auto identity=op->candidate->referenceIdentityOffThread();if(identity.path.isNotEmpty())return juce::File(identity.path).getFileName();}return {};};
}
void MainComponent::referenceAction(int action,int track,int slot,hem::ModuleProcessor* expected){
 if((action==1||action==5)&&trackScheduler.faulted()){setReferenceStatus("Audio overload: press PLAY to resume audio before capture/audition");return;}
 if(action==4){const GraphControlGuard lock(*this);referenceAuditionAudio=nullptr;setReferenceStatus("SOURCE selected / press PLAY to hear program");return;}
 if(track<0||track>=trackCount||slot<0||slot>=8||closePhase!=ClosePhase::idle)return;
 auto& chain=bank->tracks[size_t(track)].chain;if(chain.activeProcessor(slot)!=expected||chain.isBusy(slot)||referenceOperationBusy()){setReferenceStatus("Reference busy or insert changed; finish current operation");return;}
 auto* processor=dynamic_cast<hem::ReferenceMatchProcessor*>(expected);if(!processor)return;
 if(drumSampleChoosing||drumSampleJob||clipOperationBusy()||externalLoadPending||sessionWorker->isBusy()||exportWorker->isBusy()||importer->hasPendingWork()||vocals.recorder.busy()){setReferenceStatus("Finish recording or pending file work first");return;}
 if(action!=1&&playing()){setReferenceStatus("STOP PLAYBACK BEFORE THIS ACTION");return;}
 if(action==1&&!playing()){setReferenceStatus("Play source audio before capture");return;}
 if(referenceOperation&&(referenceOperation->expected!=expected||referenceOperation->version!=generation))cancelReferenceOperation();
 if(!referenceOperation){referenceOperation=std::make_shared<ReferenceOperation>();referenceOperation->track=track;referenceOperation->slot=slot;referenceOperation->expected=expected;referenceOperation->version=generation;}
 auto operation=referenceOperation;
 if(action==1){if(processor->captureInProgress()){processor->stopSourceCapture();setReferenceStatus("Finishing source capture on next audio block");}else if(processor->startSourceCapture())setReferenceStatus("Waiting for source audio on this track / maximum 10 s");else setReferenceStatus("Capture unavailable; check audio preparation");return;}
 auto launch=[this,expected,track,slot](int mode,const juce::File& file){auto result=startReferenceWork(mode,file,track,slot,expected);if(result.failed())setReferenceStatus(result.getErrorMessage());};
 if(action==0){setReferenceStatus("Choose WAV, AIFF or FLAC reference");referenceChoosing=true;const auto request=++referenceRequest;referencePicker=std::make_unique<juce::FileChooser>("Load Reference / analyzes first30seconds",juce::File(),"*.wav;*.aif;*.aiff;*.flac");juce::Component::SafePointer<MainComponent> safe(this);referencePicker->launchAsync(juce::FileBrowserComponent::openMode|juce::FileBrowserComponent::canSelectFiles,[safe,request,launch](const juce::FileChooser& chooser){if(!safe||safe->referenceRequest!=request)return;safe->referenceChoosing=false;auto file=chooser.getResult();if(file!=juce::File())launch(0,file);else safe->setReferenceStatus("Reference selection cancelled");});return;}
 if(action==2||action==3||action==5)launch(action,{});
}
juce::Result MainComponent::startReferenceWork(int mode,const juce::File& file,int track,int slot,hem::ModuleProcessor* expected){
 if(mode!=0&&mode!=2&&mode!=3&&mode!=5)return juce::Result::fail("Unknown reference operation");
 if(track<0||track>=trackCount||slot<0||slot>=8||playing()||referenceOperationBusy()||drumSampleChoosing||drumSampleJob||clipOperationBusy()||externalLoadPending||vocals.recorder.busy()||sessionWorker->isBusy()||exportWorker->isBusy()||importer->hasPendingWork()||closePhase!=ClosePhase::idle)return juce::Result::fail("Stop transport and finish pending work first");
 auto& chain=bank->tracks[size_t(track)].chain;if(chain.isBusy(slot)||chain.activeProcessor(slot)!=expected||!dynamic_cast<hem::ReferenceMatchProcessor*>(expected))return juce::Result::fail("Reference insert changed");
 if(referenceOperation&&(referenceOperation->expected!=expected||referenceOperation->version!=generation))cancelReferenceOperation();
 if(!referenceOperation){referenceOperation=std::make_shared<ReferenceOperation>();referenceOperation->track=track;referenceOperation->slot=slot;referenceOperation->expected=expected;}
 auto operation=referenceOperation;auto captured=hem::NativeProcessorState::capture(*expected,operation->snapshot);if(captured.failed())return captured;
 prepareOfflineSession();operation->version=generation;operation->rate=sampleRate;operation->block=blockSize;operation->budget=chain.slotLatency();operation->bpm=projectBpm.load();operation->mode=mode;operation->file=file;
 {const GraphControlGuard lock(*this);referenceAuditionAudio=nullptr;}
 if(auto* p=dynamic_cast<hem::ReferenceMatchProcessor*>(expected)){hem::reference_match::Profile capturedSource;if(p->takeCapturedProfile(capturedSource)&&capturedSource.status==hem::reference_match::Status::ready){operation->source=capturedSource;operation->hasSource=true;}}
 operation->done=false;operation->busy=true;if(!operation->startThread()){operation->busy=false;return juce::Result::fail("Cannot start reference worker");}
 setReferenceStatus("Preparing reference work / existing insert retained");return juce::Result::ok();
}
void MainComponent::pollReferenceOperation(){
 auto op=referenceOperation;if(!op)return;
 if(op->track<0||op->track>=trackCount||bank->tracks[size_t(op->track)].chain.activeProcessor(op->slot)!=op->expected||generation!=op->version){cancelReferenceOperation();return;}
 if(!op->busy.load())if(auto* p=dynamic_cast<hem::ReferenceMatchProcessor*>(op->expected)){hem::reference_match::Profile source;if(p->takeCapturedProfile(source)){if(source.status==hem::reference_match::Status::ready){op->source=source;op->hasSource=true;setReferenceStatus("Source captured / stop transport then Match");}else setReferenceStatus("Source capture invalid; recapture after transport discontinuity or insufficient signal");}}
 if(!op->busy.load()||!op->done.load(std::memory_order_acquire))return;op->waitForThreadToExit(-1);op->busy=false;
 if(op->result.failed()){setReferenceStatus(op->result.getErrorMessage());return;}
 if(playing()||vocals.recorder.busy()||sessionWorker->isBusy()||exportWorker->isBusy()||importer->hasPendingWork()){setReferenceStatus("Reference completion cancelled: host state changed");cancelReferenceOperation();return;}
 if(op->mode==0){setReferenceStatus("Reference loaded and verified / capture source then Match");return;}
 if(op->mode==5){const GraphControlGuard lock(*this);if(trackScheduler.faulted()){setReferenceStatus("Reference audition cancelled after overload; press PLAY to resume audio");return;}op->auditionCursor=0;referenceAuditionAudio=op.get();setReferenceStatus("REFERENCE audition / first30seconds / SOURCE returns to program");return;}
 if(op->mode==2||op->mode==3){for(const auto& spec:op->expected->parameterSpecs()){auto stored=op->snapshot.parameters.getChildWithProperty("id",spec.id);if(!stored.isValid()||op->expected->state.getRawParameterValue(spec.id)->load()!=float(stored["value"])){setReferenceStatus("Parameters changed during Match; run Match again");cancelReferenceOperation();return;}}auto& chain=bank->tracks[size_t(op->track)].chain;bool installed=false;std::unique_ptr<hem::InsertChain::PreparedNode> retired;
  // closePluginEditor cancels host jobs; detach this completed owner first.
  referenceOperation.reset();const bool reopen=editedProcessor==op->expected;if(reopen)closePluginEditor();
  {const GraphControlGuard lock(*this);if(prepared&&generation==op->version&&sampleRate==op->rate&&blockSize==op->block&&!chain.isBusy(op->slot)&&chain.activeProcessor(op->slot)==op->expected){installed=chain.publishStopped(op->slot,std::move(op->node));if(installed)retired=chain.collectRetired(op->slot);}}
  if(retired)retired->processor->releaseResources();setReferenceStatus(installed?"Reference Match committed":"Reference commit rejected: insert/device changed");if(installed&&reopen)openPlugin(op->slot,EditorOpenIntent::replacement);if(workspace)workspace->refresh();
 }
}
void MainComponent::cancelReferenceOperation(){
 ++referenceRequest;referenceChoosing=false;referencePicker.reset();if(referenceAuditionAudio.load(std::memory_order_acquire)){const GraphControlGuard lock(*this);referenceAuditionAudio=nullptr;}
 auto retired=std::move(referenceOperation);if(retired){retired->signalThreadShouldExit();const GraphControlGuard lock(*this);if(bank&&retired->track>=0&&retired->track<trackCount&&bank->tracks[size_t(retired->track)].chain.activeProcessor(retired->slot)==retired->expected)if(auto* p=dynamic_cast<hem::ReferenceMatchProcessor*>(retired->expected))if(p->captureInProgress())p->cancelCaptureWhileCallbackExcluded();} // joins on message thread only
}
void MainComponent::cancelReferenceCaptures(){
 const GraphControlGuard lock(*this);referenceAuditionAudio=nullptr;
 bool cancelled=false;if(bank)for(auto& track:bank->tracks)for(int slot=0;slot<8;++slot)if(auto* p=dynamic_cast<hem::ReferenceMatchProcessor*>(track.chain.activeProcessor(slot)))if(p->captureInProgress()){p->cancelCaptureWhileCallbackExcluded();cancelled=true;}
 if(cancelled){if(referenceOperation)referenceOperation->hasSource=false;setReferenceStatus("Capture cancelled by transport change; play then recapture");}
}
void MainComponent::renderReferenceAudition(juce::AudioBuffer<float>& buffer,int frames){
 auto* op=referenceAuditionAudio.load(std::memory_order_acquire);if(!op||frames<=0)return;
 const int available=std::min(frames,op->audition.getNumSamples()-op->auditionCursor);if(available<=0){referenceAuditionAudio.store(nullptr,std::memory_order_release);return;}
 // Replace only the host listening buffer; never read or write track sources,
 // recorder inputs, private export processors or persisted production routing.
 for(int ch=0;ch<buffer.getNumChannels();++ch){buffer.clear(ch,0,frames);buffer.copyFrom(ch,0,op->audition,std::min(ch,1),op->auditionCursor,available);}op->auditionCursor+=available;
}
