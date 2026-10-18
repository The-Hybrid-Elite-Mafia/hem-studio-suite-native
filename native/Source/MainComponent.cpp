#include "MainComponent.h"
#include "Plugins/InsertFactory.h"
#include "Plugins/DrumReplacerProcessor.h"
#include "Plugins/AmpCabinetProcessor.h"
#include <thread>
namespace {
juce::String timeText(double t){int n=int(std::max(0.0,t));return juce::String(n/60)+":"+juce::String(n%60).paddedLeft('0',2);}
}
MainComponent::GraphControlGuard::GraphControlGuard(MainComponent& owner):host(owner){
 host.graphControlMutex.lock();if(host.graphControlDepth++==0){host.callbackAdmission.fetch_or(2,std::memory_order_acq_rel);
  while((host.callbackAdmission.load(std::memory_order_acquire)&1)!=0)juce::Thread::sleep(1);
  host.trackScheduler.quiesce();
 }
}
MainComponent::GraphControlGuard::~GraphControlGuard(){if(--host.graphControlDepth==0){host.latchControlGap();host.callbackAdmission.fetch_and(~2u,std::memory_order_release);}host.graphControlMutex.unlock();}
void MainComponent::latchControlGap()noexcept{
 if(controlGapPending.exchange(false,std::memory_order_acq_rel)&&!trackScheduler.faulted()){
  faultResumeAtCurrent.store(true,std::memory_order_release);trackRenderFaults.fetch_add(1,std::memory_order_relaxed);trackScheduler.latchFault();
 }
}
struct MainComponent::TransportJob {
 TransportRequest request;std::array<std::shared_ptr<hem::ReadAheadTransport>,13> sources;
 std::array<uint64_t,13> tokens{};std::atomic<bool> cancel{false},done{false},retired{false};std::thread worker;
 juce::Result result=juce::Result::ok();bool retirementStarted=false;
 ~TransportJob(){cancel=true;if(worker.joinable())worker.join();for(size_t i=0;i<sources.size();++i)if(sources[i]&&tokens[i])sources[i]->discardPreparedEpoch(tokens[i]);}
 void start(){worker=std::thread([this]{try{for(size_t i=0;i<sources.size();++i)if(sources[i]){
   if(cancel.load()){result=juce::Result::fail("Transport preparation cancelled");break;}
   result=sources[i]->prepareEpoch(request.positions[i],request.loop,tokens[i],[this]{return cancel.load();});if(result.failed())break;
  }}catch(const std::exception& e){result=juce::Result::fail(e.what());}catch(...){result=juce::Result::fail("Transport preparation failed");}done.store(true,std::memory_order_release);});}
 bool retire(){if(retirementStarted)return true;if(worker.joinable())worker.join();try{worker=std::thread([this]{for(size_t i=0;i<sources.size();++i)if(sources[i]&&tokens[i]){sources[i]->discardPreparedEpoch(tokens[i]);tokens[i]=0;}sources={};retired.store(true,std::memory_order_release);});retirementStarted=true;return true;}catch(...){return false;}}
};
bool MainComponent::transportBusy()const{return bool(transportJob)||nextTransportRequest.has_value()||!retiredTransportJobs.empty();}
void MainComponent::cancelTransport(){nextTransportRequest.reset();if(transportJob)transportJob->cancel=true;}
void MainComponent::requestTransport(TransportRequest request){
 // A new transport epoch invalidates any captured spectral Apply position.
 if(spectralOperation){closeSpectralOperation();showShellPage(ShellPage::mixer);}
 if(transportJob||!retiredTransportJobs.empty()){if(transportJob)transportJob->cancel=true;nextTransportRequest=request;message.setText("Preparing latest transport request...",juce::dontSendNotification);return;}
 if(request.generation!=generation)return;
 auto job=std::make_unique<TransportJob>();job->request=request;
 {const GraphControlGuard guard(*this);for(size_t i=0;i<job->sources.size();++i)if(bank->tracks[i].loaded)job->sources[i]=bank->tracks[i].playback->transport;}
 try{job->start();transportJob=std::move(job);message.setText("Preparing playback buffers...",juce::dontSendNotification);}catch(...){message.setText("Unable to start transport preparation; current sources retained",juce::dontSendNotification);}
}
void MainComponent::pollTransport(){
 for(auto i=retiredTransportJobs.begin();i!=retiredTransportJobs.end();)if((*i)->retired.load(std::memory_order_acquire))i=retiredTransportJobs.erase(i);else {if(!(*i)->retire())message.setText("Transport cleanup worker unavailable; sources retained, retrying",juce::dontSendNotification);++i;}
 if(transportJob&&transportJob->done.load(std::memory_order_acquire)){
  auto job=std::move(transportJob);if(job->worker.joinable())job->worker.join();bool committed=false;
  if(!job->cancel.load()&&job->result.wasOk()){
   const GraphControlGuard guard(*this);bool valid=job->request.generation==generation;
   for(size_t i=0;i<job->sources.size();++i)if(job->sources[i])valid=valid&&bank->tracks[i].loaded&&bank->tracks[i].playback->transport==job->sources[i]&&job->sources[i]->preparedEpoch()==job->tokens[i];
   if(valid&&(!trackScheduler.faulted()||job->request.recovery||job->request.explicitPlay)){
    committed=true;for(size_t i=0;i<job->sources.size();++i)if(job->sources[i]){const bool ok=job->sources[i]->commitPreparedEpoch(job->tokens[i],job->request.play);committed=committed&&ok;job->tokens[i]=0;}
    if(committed){looping=job->request.loop;if(job->request.explicitPlay){trackScheduler.clearFault();trackRenderRecovered=false;}}
   }
  }
  if(!job->cancel.load())message.setText(committed?(job->request.play?"Playback ready":"Transport ready / stopped"):(job->result.failed()?job->result.getErrorMessage():"Transport preparation superseded by source/device change"),juce::dontSendNotification);
  if(!job->retire())message.setText("Transport cleanup worker unavailable; sources retained, retrying",juce::dontSendNotification);retiredTransportJobs.push_back(std::move(job));
 }
 if(!transportJob&&retiredTransportJobs.empty()&&nextTransportRequest){auto request=*nextTransportRequest;nextTransportRequest.reset();requestTransport(request);}
}
void MainComponent::recoverTrackRenderFault(){
 if(!trackScheduler.faulted()||trackRenderRecovered)return;
 {const GraphControlGuard guard(*this);
  // Late jobs are quiescent. Stop intents now; prepare every checkpoint on a
  // worker while the sticky fault keeps later callbacks from advancing audio.
  vocals.stopTake();invalidateClipAudio();invalidateSpectralAudio();referenceAuditionAudio=nullptr;
  if(faultResumeAtCurrent.exchange(false,std::memory_order_acq_rel))for(size_t i=0;i<callbackSourcePositions.size();++i)callbackSourcePositions[i]=bank->tracks[i].transport().getCurrentPosition();
  for(auto& track:bank->tracks)track.transport().stop();
 }
 cancelReferenceCaptures();trackRenderRecovered=true;
 TransportRequest request;request.positions=callbackSourcePositions;request.loop=looping;request.recovery=true;request.generation=generation;requestTransport(request);
 message.setText("Audio overload: output silenced and transport/take stopped. Reduce load, then press PLAY to resume. Overloads: "+juce::String(trackRenderFaults.load()),juce::dontSendNotification);
}
struct MainComponent::DrumSampleJob final:juce::Thread {
 enum class Kind {drum,amp};Kind kind;
 hem::NativeProcessorSnapshot snapshot;juce::File file;int layer,track,slot,block,budget;double rate,bpm;unsigned version;
 hem::ModuleProcessor* expected;std::unique_ptr<hem::InsertChain::PreparedNode> node;
 juce::Result result=juce::Result::fail("Sample loading did not finish");std::atomic<bool> done{false};
 DrumSampleJob(hem::NativeProcessorSnapshot s,juce::File f,int l,int t,int i,int b,int budgetIn,double r,double tempo,unsigned v,hem::ModuleProcessor* p)
 :Thread("HEM native resource loader"),kind(l<0?Kind::amp:Kind::drum),snapshot(std::move(s)),file(std::move(f)),layer(l),track(t),slot(i),block(b),budget(budgetIn),rate(r),bpm(tempo),version(v),expected(p){}
 ~DrumSampleJob()override{signalThreadShouldExit();waitForThreadToExit(-1);}
 void run()override{
  try{
   std::unique_ptr<hem::ModuleProcessor> candidate;
   if(kind==Kind::drum)candidate=std::make_unique<hem::DrumReplacerProcessor>();else candidate=std::make_unique<hem::AmpCabinetProcessor>();
   auto* provider=dynamic_cast<hem::NativeResourceStateProvider*>(candidate.get());hem::NativeRestoreContext context;
   context.sampleRate=rate;context.blockSize=block;context.tempoBpm=bpm;context.cancelled=[this]{return threadShouldExit();};
   result=provider->restoreNativeStateOffThread(snapshot,context);
   if(result.wasOk()&&!threadShouldExit()){
    if(kind==Kind::drum)result=static_cast<hem::DrumReplacerProcessor*>(candidate.get())->loadSampleUnpublished(layer,file,rate);
    else result=static_cast<hem::AmpCabinetProcessor*>(candidate.get())->loadIRUnpublished(file,rate);
   }
   if(result.wasOk()&&!threadShouldExit()){
    candidate->setBlockTempo(bpm);hem::InsertChain staging;
    node=staging.stageNode(std::move(candidate),rate,block,budget);
    if(!node||!node->processor->processingReady())result=juce::Result::fail("Sample preparation failed; old insert retained");
   }
   if(threadShouldExit()){node.reset();result=juce::Result::fail("Sample loading cancelled; old insert retained");}
  }catch(const std::exception& e){result=juce::Result::fail(e.what());node.reset();}
   catch(...){result=juce::Result::fail("Sample loading failed; old insert retained");node.reset();}
  done.store(true,std::memory_order_release);
 }
};
bool MainComponent::nativeResourceLoadBusy()const{return transportBusy()||(trackScheduler.faulted()&&!trackRenderRecovered)||drumSampleChoosing||bool(drumSampleJob)||clipOperationBusy()||referenceOperationBusy()||spectralOperationBusy()||instrumentalOperationBusy();}
void MainComponent::chooseDrumSample(int layer,int trackIndex,int slotIndex,hem::ModuleProcessor* expected){
 if(nativeResourceLoadBusy()||closePhase!=ClosePhase::idle||externalLoadPending)return;
 if(layer< -1||layer>=4||trackIndex<0||trackIndex>=trackCount||slotIndex<0||slotIndex>=8)return;
 if(playing()||vocals.recorder.busy()||sessionWorker->isBusy()||exportWorker->isBusy()||importer->hasPendingWork()){
  message.setText("Stop transport and finish recording, import, session or export work before loading a resource",juce::dontSendNotification);return;}
 auto& chain=bank->tracks[size_t(trackIndex)].chain;
 if(chain.isBusy(slotIndex)||chain.activeProcessor(slotIndex)!=expected||(layer<0?!dynamic_cast<hem::AmpCabinetProcessor*>(expected):!dynamic_cast<hem::DrumReplacerProcessor*>(expected)))return;
 drumSampleChoosing=true;const unsigned request=++drumSampleRequest;
 drumSamplePicker=std::make_unique<juce::FileChooser>((layer<0?juce::String("Load cabinet impulse response"):"Load drum sample layer "+juce::String(layer+1)),juce::File(),"*.wav;*.aif;*.aiff;*.flac");
 juce::Component::SafePointer<MainComponent> safe(this);
 drumSamplePicker->launchAsync(juce::FileBrowserComponent::openMode|juce::FileBrowserComponent::canSelectFiles,[safe,request,layer,trackIndex,slotIndex,expected](const juce::FileChooser& chooser){
  if(!safe||request!=safe->drumSampleRequest)return;
  const auto file=chooser.getResult();safe->drumSampleChoosing=false;
  if(file==juce::File())return;
  const auto result=safe->startDrumSampleLoad(file,layer,trackIndex,slotIndex,expected);
  if(result.failed())safe->message.setText(result.getErrorMessage(),juce::dontSendNotification);
 });
}
juce::Result MainComponent::startDrumSampleLoad(const juce::File& file,int layer,int trackIndex,int slotIndex,hem::ModuleProcessor* expected){
 if(nativeResourceLoadBusy()||closePhase!=ClosePhase::idle||externalLoadPending||playing()||vocals.recorder.busy()||sessionWorker->isBusy()||exportWorker->isBusy()||importer->hasPendingWork())return juce::Result::fail("Finish pending work and stop transport before loading a resource");
 if(layer< -1||layer>=4||trackIndex<0||trackIndex>=trackCount||slotIndex<0||slotIndex>=8)return juce::Result::fail("Invalid drum sample destination");
 prepareOfflineSession();auto& chain=bank->tracks[size_t(trackIndex)].chain;double rate;int block,budget;unsigned version;
 {const GraphControlGuard lock(*this);
  if(!prepared||chain.isBusy(slotIndex)||chain.activeProcessor(slotIndex)!=expected)return juce::Result::fail("Drum insert changed; old insert retained");
  rate=sampleRate;block=blockSize;budget=chain.slotLatency();version=generation;
 }
 if(layer<0?!dynamic_cast<hem::AmpCabinetProcessor*>(expected):!dynamic_cast<hem::DrumReplacerProcessor*>(expected))return juce::Result::fail("Selected resource insert changed");
 auto* provider=dynamic_cast<hem::NativeResourceStateProvider*>(expected);
 try{
  hem::NativeProcessorSnapshot snapshot;auto result=provider->tryCaptureNativeState(snapshot);if(result.failed())return result;
  drumSampleJob=std::make_unique<DrumSampleJob>(std::move(snapshot),file,layer,trackIndex,slotIndex,block,budget,rate,projectBpm.load(),version,expected);
  if(!drumSampleJob->startThread()){drumSampleJob.reset();return juce::Result::fail("Could not start sample loader; old insert retained");}
  message.setText("Loading native resource in background / current insert retained until ready",juce::dontSendNotification);return juce::Result::ok();
 }catch(const std::exception& e){return juce::Result::fail(e.what());}
}
void MainComponent::cancelDrumSampleLoad(){
 ++drumSampleRequest;drumSampleChoosing=false;drumSamplePicker.reset();
 if(drumSampleJob)drumSampleJob->signalThreadShouldExit();
}
void MainComponent::pollDrumSampleLoad(){
 if(!drumSampleJob||!drumSampleJob->done.load(std::memory_order_acquire))return;
 auto job=std::move(drumSampleJob);job->waitForThreadToExit(-1); // completed worker, message thread only
 if(job->result.failed()){message.setText(job->result.getErrorMessage(),juce::dontSendNotification);return;}
 auto& chain=bank->tracks[size_t(job->track)].chain;
 auto unchanged=[&]{return prepared&&generation==job->version&&sampleRate==job->rate&&blockSize==job->block&&!chain.isBusy(job->slot)&&chain.activeProcessor(job->slot)==job->expected;};
 if(closePhase!=ClosePhase::idle||job->threadShouldExit()||playing()||vocals.recorder.busy()||sessionWorker->isBusy()||exportWorker->isBusy()||importer->hasPendingWork()){
  message.setText("Sample load cancelled because host work changed; old insert retained",juce::dontSendNotification);return;}
 {const GraphControlGuard lock(*this);if(!unchanged()){message.setText("Device/session/insert changed during sample load; old insert retained",juce::dontSendNotification);return;}}
 // Parameter editing is message-thread serialized. Copy the latest scalar values
 // into the prepared private candidate, not the snapshot taken before decoding.
 // Resource restore itself must never be run against the published processor.
 for(const auto& spec:job->expected->parameterSpecs()){
  const auto* current=job->expected->state.getRawParameterValue(spec.id);auto* target=job->node->processor->state.getParameter(spec.id);
  if(current&&target){
   // IR loading intentionally selects Custom. Retain that selection unless the
   // user changed cabinet model while decoding; all actual newer edits win.
   if(job->kind==DrumSampleJob::Kind::amp&&juce::String(spec.id)=="cabModel"){
    const auto original=job->snapshot.parameters.getChildWithProperty("id",spec.id);
    if(original.isValid()&&current->load()==float(double(original["value"])))continue;
   }
   target->setValueNotifyingHost(target->convertTo0to1(current->load()));
  }
 }
 const bool reopen=editedProcessor==job->expected&&editedTrack==job->track&&editedSlot==job->slot;
 if(reopen)closePluginEditor();
 bool installed=false;std::unique_ptr<hem::InsertChain::PreparedNode> retired;
 {const GraphControlGuard lock(*this);if(unchanged()){installed=chain.publishStopped(job->slot,std::move(job->node));if(installed)retired=chain.collectRetired(job->slot);}}
 if(retired)retired->processor->releaseResources(); // destruction also outside callback lock
 message.setText(installed?(job->kind==DrumSampleJob::Kind::amp?juce::String("Cabinet IR loaded / scalar edits retained"):"Drum sample layer "+juce::String(job->layer+1)+" loaded / scalar edits retained"):"Device/session changed during sample commit; old insert retained",juce::dontSendNotification);
 if(installed&&reopen&&selectedTrack==job->track&&selected==job->slot)openPlugin(job->slot,EditorOpenIntent::replacement);
 if(workspace)workspace->refresh();
}

MainComponent::MainComponent(bool startAudioDevice){
 retiredTransportJobs.reserve(1); // At most one retirement; next preparation waits for it.
 addAndMakeVisible(tempoLabel);tempoLabel.setText("PROJECT BPM",juce::dontSendNotification);tempoLabel.setJustificationType(juce::Justification::centred);
 addAndMakeVisible(tempoControl);tempoControl.setName("Project tempo BPM");tempoControl.setComponentID("project-tempo-bpm");
 tempoControl.setSliderStyle(juce::Slider::IncDecButtons);tempoControl.setTextBoxStyle(juce::Slider::TextBoxLeft,false,100,28);tempoControl.setRange(20,400,.01);tempoControl.setValue(120);
 tempoControl.setTooltip("Real project tempo / 20..400 BPM. Sessions without saved tempo explicitly load at 120 BPM. Captured exports keep their original BPM.");
 tempoControl.onValueChange=[this]{const double b=tempoControl.getValue();if(!std::isfinite(b)||b<20||b>400)return;projectBpm.store(b,std::memory_order_relaxed);
    if(!deviceManager.getCurrentAudioDevice()){const GraphControlGuard guard(*this);for(auto& t:bank->tracks)for(int i=0;i<8;++i)if(auto* p=t.chain.activeProcessor(i))p->setBlockTempo(b);}};
 setLookAndFeel(&overlayLook);setOpaque(true);addAndMakeVisible(inputMeterView);addAndMakeVisible(outputMeterView);
 inputMeterView.setComponentID("host-input-meter");outputMeterView.setComponentID("host-output-meter");
 faceplate=hem::ui::approvedFaceplate(0);
 formats.registerBasicFormats();diskThread.startThread();configureKnob(inputKnob,inputDb,"INPUT");configureKnob(outputKnob,outputDb,"MASTER HEADROOM / OUTPUT");
 for(auto* b:std::array<juce::TextButton*,7>{&play,&stop,&rewind,&forward,&loop,&file,&settings}){addAndMakeVisible(*b);b->setLookAndFeel(&overlayLook);}
 play.setButtonText("PLAY");stop.setButtonText("STOP");rewind.setButtonText("REWIND");forward.setButtonText("+10 SEC");loop.setButtonText("LOOP");file.setButtonText("FILE");settings.setButtonText("AUDIO / I/O");
 for(auto* b:std::array<juce::TextButton*,4>{&headerMenu,&headerRack,&headerHome,&pauseButton})addAndMakeVisible(*b);
 headerMenu.setButtonText("MENU");headerRack.setButtonText("EDIT");headerHome.setButtonText("STUDIO");
 headerMenu.setTooltip("Navigate Studio views");headerRack.setTooltip("Edit the selected track insert rack");headerHome.setTooltip("Return to Studio Home");
 headerRack.onClick=[this]{showRack();};headerHome.onClick=[this]{showShellPage(ShellPage::home);};
 headerMenu.onClick=[this]{juce::PopupMenu menu;const std::array<const char*,6> names{"Home","Mixer","Plugin rack","Plugin Vault","Routing","Settings"};for(size_t i=0;i<names.size();++i)menu.addItem(int(i)+1,names[i]);juce::Component::SafePointer<MainComponent> safe(this);menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&headerMenu),[safe](int choice){if(!safe||choice<1||choice>6)return;if(choice==3)safe->showRack();else safe->showShellPage(static_cast<ShellPage>(choice-1));});};
 play.setTooltip("Play all loaded tracks");stop.setTooltip("Stop all and rewind");rewind.setTooltip("Rewind all");forward.setTooltip("Forward 10 seconds");loop.setTooltip("Loop tracks individually");file.setTooltip("Load audio, save or recall a session, and export the mix");settings.setTooltip("Audio devices");
 file.setComponentID("host-file-menu");exportButton.setComponentID("home-export-mix");
 file.onClick=[this]{juce::PopupMenu menu;menu.addItem(1,"Load audio into selected track...");menu.addItem(2,"Save session...");menu.addItem(3,"Recall session...");menu.addSeparator();menu.addItem(4,"Export mix...");menu.addItem(5,"Selected-track 2D / 3D spectral scanner...");menu.addItem(6,"Instrumental Builder / 13-lane sequencer...");
  juce::Component::SafePointer<MainComponent> safe(this);menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&file),[safe](int choice){if(!safe)return;switch(choice){case 1:safe->openAudio();break;case 2:safe->chooseSession(true);break;case 3:safe->chooseSession(false);break;case 4:safe->showExport();break;case 5:safe->openSpectralOperation();break;case 6:safe->openInstrumentalOperation();break;default:break;}});};
 settings.onClick=[this]{showSettings();};play.onClick=[this]{playAll();};stop.onClick=[this]{stopAll(true);};
 rewind.onClick=[this]{seekAll(0);};forward.onClick=[this]{seekAll(sessionPosition()+10);};
 loop.onClick=[this]{
    if(vocals.recorder.busy()){message.setText("Finish the take before changing loop mode",juce::dontSendNotification);return;}
    if(sessionWorker&&sessionWorker->isBusy())return;
    TransportRequest request;request.play=!trackScheduler.faulted()&&playing();request.loop=!looping;request.generation=generation;
    {const GraphControlGuard guard(*this);for(size_t i=0;i<request.positions.size();++i)request.positions[i]=bank->tracks[i].transport().getCurrentPosition();}
    requestTransport(request);repaint();
 };
 for(int i=0;i<8;++i){addAndMakeVisible(slotButtons[size_t(i)]);slotButtons[size_t(i)].onClick=[this,i]{selectSlot(i);openPlugin(i);};}
 addAndMakeVisible(bay);
 importer=std::make_unique<hem::StemFileImporter>(diskThread,[this]{
    const GraphControlGuard lock(*this);
    return hem::StemFileImporter::Context{prepared?sampleRate:0,blockSize,generation,looping};
 },[this](hem::StemFileImporter::Batch& batch){return commitImport(batch);});
 bay.addAndMakeVisible(*importer);
 sessionWorker=std::make_unique<hem::SessionWorker>(diskThread,
    [this](std::unique_ptr<hem::TrackBank>& b,const hem::SessionSnapshot& s,hem::SessionWorker::Context c){return commitSession(b,s,c);},
    [this](const juce::String& text){message.setText(text,juce::dontSendNotification);},
    [this](bool saving,const juce::File& file,const juce::Result& result){sessionFinished(saving,file,result);},
    [this](hem::TrackBank& b,const hem::SessionSnapshot& s,hem::SessionWorker::Context c){return stageExternalSession(b,s,c);});
 exportWorker=std::make_unique<hem::OfflineMixWorker>([this](const juce::String& text){message.setText(text,juce::dontSendNotification);},&externalHost);
 bay.addAndMakeVisible(exportButton);exportButton.onClick=[this]{showExport();};
 bay.addAndMakeVisible(saveSessionButton);bay.addAndMakeVisible(loadSessionButton);
 bay.addAndMakeVisible(vocalButton);vocalButton.onClick=[this]{showVocals();};
 bay.addAndMakeVisible(analysisButton);bay.addAndMakeVisible(calibrationButton);bay.addAndMakeVisible(routeButton);
 analysisButton.onClick=[this]{juce::PopupMenu menu;menu.addItem(1,"Live master waveform / spectrum");menu.addItem(2,"Selected-track 2D / 3D spectral scanner");
  juce::Component::SafePointer<MainComponent> safe(this);menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&analysisButton),[safe](int choice){if(!safe)return;if(choice==1)safe->showAnalysis();else if(choice==2)safe->openSpectralOperation();});};
 routeButton.onClick=[this]{monitoringRouter.setHeadphoneMode(!monitoringRouter.headphoneMode());};
 calibrationButton.onClick=[this]{openCalibration();};
 profileLoader=std::make_unique<hem::MonitorProfileLoader>(
    [this](const hem::MonitorProfile& p,const hem::MonitoringRouter::Curve& curve,unsigned gen){
        bool installed=false;
        {const GraphControlGuard lock(*this);if(prepared&&generation==gen)installed=monitoringRouter.installPrepared(curve);}
        if(installed){calibrationName=p.name+" / "+p.device+" / "+p.connection+" / "+p.deviceMode;message.setText("Calibration loaded; select HEADPHONE CORRECTION to audition",juce::dontSendNotification);}
        else message.setText("Device changed or profile transition busy; load calibration again",juce::dontSendNotification);
    },[this](const juce::String& error){message.setText(error,juce::dontSendNotification);});
 saveSessionButton.onClick=[this]{chooseSession(true);};loadSessionButton.onClick=[this]{chooseSession(false);};
 for(auto* c:std::array<juce::Component*,14>{&outputButton,&monitorButton,&moduleMenu,&trackMenu,&insert,&remove,&bypass,&rackButton,&openButton,&mute,&solo,&trackTrim,&title,&message})bay.addAndMakeVisible(*c);
 const auto& registry=hem::PluginRegistry::entries();for(size_t i=0;i<registry.size();++i)moduleMenu.addItem(hem::InsertFactory::name(int(i)),int(i)+1);moduleMenu.addItem(hem::InsertFactory::name(hem::InsertFactory::legacyGateChoice()),hem::InsertFactory::legacyGateChoice()+1);moduleMenu.setSelectedId(1);
 for(int i=0;i<trackCount;++i)trackMenu.addItem("TRACK "+juce::String(i+1),i+1);trackMenu.setSelectedId(1);
 trackMenu.onChange=[this]{selectTrack(trackMenu.getSelectedId()-1);};
 insert.onClick=[this]{installSelected(moduleMenu.getSelectedId()-1);};remove.onClick=[this]{installSelected(-1);};bypass.onClick=[this]{auto& chain=current().chain;if(chain.activeProcessor(selected)&&!chain.isBusy(selected))chain.bypass(selected,!chain.isBypassed(selected));};
 pauseButton.onClick=[this]{pauseAll();};outputButton.onClick=[this]{showSettings();};
 monitorButton.onClick=[this]{showMonitor();};
 rackButton.onClick=[this]{showRack();};openButton.onClick=[this]{openPlugin(selected);};
 mute.onClick=[this]{current().muted.store(!current().muted.load());};solo.onClick=[this]{current().solo.store(!current().solo.load());};
 trackTrim.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);trackTrim.setTextBoxStyle(juce::Slider::TextBoxBelow,false,100,24);trackTrim.setRange(-60,6,.1);trackTrim.setValue(0);trackTrim.setTextValueSuffix(" dB / TRACK");trackTrim.setDoubleClickReturnValue(true,0);
 trackTrim.onValueChange=[this]{current().trimDb.store(float(trackTrim.getValue()));};
 trackTrim.onDragStart=[this]{trackTrimGesture=true;};trackTrim.onDragEnd=[this]{trackTrimGesture=false;refreshTrackTrim();};
 trackTrim.setComponentID("home-track-trim");bypass.setComponentID("home-insert-bypass");
 for(auto* l:std::array<juce::Label*,4>{&track,&telemetry,&inValue,&outValue}){addAndMakeVisible(*l);l->setJustificationType(juce::Justification::centred);l->setColour(juce::Label::textColourId,juce::Colours::cyan);l->setInterceptsMouseClicks(false,false);}
 hem::NativeWorkspace::Callbacks workspaceCallbacks;
 workspaceCallbacks.bank=[this]() -> hem::TrackBank& {return *bank;};
 workspaceCallbacks.getSelectedTrack=[this]{return selectedTrack;};workspaceCallbacks.getSelectedSlot=[this]{return selected;};
 workspaceCallbacks.setSelectedTrack=[this](int index){selectTrack(index);};workspaceCallbacks.setSelectedSlot=[this](int slot){selected=juce::jlimit(0,7,slot);};
 workspaceCallbacks.onOpenSlot=[this](int slot){openPlugin(slot);};workspaceCallbacks.onInstallModule=[this](int module){installSelected(module);};workspaceCallbacks.onMonitor=[this]{showMonitor();};
 workspaceCallbacks.onReferenceAction=[this](int action,int track,int slot,hem::ModuleProcessor* expected){referenceAction(action,track,slot,expected);};
 workspaceCallbacks.onAmpLoad=[this](int track,int slot,hem::ModuleProcessor* expected){chooseDrumSample(-1,track,slot,expected);};
 workspaceCallbacks.onDrumLoad=[this](int layer,int track,int slot,hem::ModuleProcessor* expected){chooseDrumSample(layer,track,slot,expected);};
 workspaceCallbacks.onMoveSlot=[this](int from,int to){moveSlot(from,to);};
 workspaceCallbacks.onEditorBack=[this]{closePluginEditor();showShellPage(editorReturnPage);};
 workspaceCallbacks.onLoadVst3=[this]{chooseExternalPlugin();};workspaceCallbacks.onReloadVst3=[this]{reloadExternalPlugin();};
 workspaceCallbacks.routeTelemetry=[this]{hem::NativeWorkspace::RouteTelemetry t;t.output=outputMeters.snapshot();t.headphoneCorrection=monitoringRouter.headphoneMode();t.monitorReady=monitorRouteReady.load();t.calibrationName=calibrationName;t.sampleRate=prepared?sampleRate:0;t.blockSize=blockSize;t.limiterLatencySamples=masterLimiter.latencySamples();if(auto* d=deviceManager.getCurrentAudioDevice()){t.outputDevice=d->getName();t.hardwareOutputs=d->getActiveOutputChannels().countNumberOfSetBits();}else{t.outputDevice="NO AUDIO DEVICE / OFFLINE EDITING";t.hardwareOutputs=0;}return t;};
 workspace=std::make_unique<hem::NativeWorkspace>(std::move(workspaceCallbacks));addChildComponent(*workspace);
 addAndMakeVisible(statusBar);statusBar.setColour(juce::Label::backgroundColourId,juce::Colour(0xff061017));statusBar.setColour(juce::Label::textColourId,juce::Colour(0xffffca7c));message.setVisible(false);
 addChildComponent(utilityViewport);addChildComponent(utilityTitle);utilityViewport.setScrollBarsShown(true,true);utilityTitle.setColour(juce::Label::textColourId,juce::Colour(0xffffbb55));utilityTitle.setColour(juce::Label::backgroundColourId,juce::Colour(0xff061017));
 const std::array<const char*,6> names{"HOME","MIXER","PLUGIN","PLUGIN VAULT","ROUTING","SETTINGS"};
 for(size_t i=0;i<navigation.size();++i){auto& nav=navigation[i];addAndMakeVisible(nav);nav.setButtonText(names[i]);nav.setComponentID("navigation-"+juce::String(int(i)));nav.onClick=[this,i]{if(i==2)openPlugin(selected);else showShellPage(static_cast<ShellPage>(i));};}
 addAndMakeVisible(unloadSourcesButton);addAndMakeVisible(newSessionButton);
 unloadSourcesButton.setTooltip("Unload source audio without deleting WAV files; insert chains and gains are retained");
 unloadSourcesButton.onClick=[this]{juce::PopupMenu menu;menu.addItem(1,"Unload selected source / keep inserts and gain");menu.addItem(2,"Unload all sources / keep inserts and gains");juce::Component::SafePointer<MainComponent> safe(this);menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&unloadSourcesButton),[safe](int choice){if(safe&&(choice==1||choice==2)){const auto result=safe->clearTrackSources(choice==2);if(result.failed())safe->message.setText(result.getErrorMessage(),juce::dontSendNotification);}});};
 newSessionButton.setTooltip("Start a new empty session after confirmation; source files are not deleted");newSessionButton.onClick=[this]{requestNewSession();};
 // Off-callback startup; zero remains the default until dense qualification.
 trackScheduler.configure(juce::SystemStats::getEnvironmentVariable("HEM_TRACK_HELPERS","0").getIntValue());
 setSize(1536,864);if(startAudioDevice){deviceError=deviceManager.initialise(0,2);auto setup=deviceManager.getAudioDeviceSetup();if(auto* d=deviceManager.getCurrentAudioDevice()){
 const auto rates=d->getAvailableSampleRates();if(rates.contains(hem::defaultSampleRate))setup.sampleRate=hem::defaultSampleRate;
 const auto blocks=d->getAvailableBufferSizes();if(blocks.contains(1024))setup.bufferSize=1024;
 }
 deviceError=deviceManager.setAudioDeviceSetup(setup,true);if(!deviceManager.getCurrentAudioDevice())deviceManager.restartLastAudioDevice();}prepareOfflineSession();startTimerHz(20);selectSlot(0);timerCallback();lastSavedSessionState=sessionFingerprint(captureSession(false));
}
MainComponent::~MainComponent(){
 {const GraphControlGuard guard(*this);trackScheduler.latchFault();}
 stopTimer();shutdownInstrumentalOperations();shutdownSpectralOperations();cancelTransport();transportJob.reset();retiredTransportJobs.clear();cancelDrumSampleLoad();drumSampleJob.reset();utilityViewport.setViewedComponent(nullptr,false);utilityContent=nullptr;embeddedExport.reset();embeddedVocal.reset();embeddedMonitor.reset();embeddedAnalysis.reset();closePluginEditor();workspace.reset();exportWindow.reset();exportWorker.reset();picker.reset();profileLoader.reset();analysisWindow.reset();sessionWorker.reset();importer.reset();deviceManager.closeAudioDevice();vocalWindow.reset();pluginWindow.reset();rackWindow.reset();monitorWindow.reset();deviceSettings.reset();
 for(auto& t:bank->tracks)t.transport().releaseResources();bank.reset();diskThread.stopThread(5000);externalHost.collectGarbage();
 for(auto* c:std::array<juce::Component*,9>{&inputKnob,&outputKnob,&play,&stop,&rewind,&forward,&loop,&file,&settings})c->setLookAndFeel(nullptr);setLookAndFeel(nullptr);
}
void MainComponent::configureKnob(juce::Slider& knob,std::atomic<float>& value,const juce::String& name){
 addAndMakeVisible(knob);knob.setLookAndFeel(&overlayLook);knob.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);knob.setTextBoxStyle(juce::Slider::NoTextBox,false,0,0);
 knob.setRange(-60,6,.1);knob.setValue(0,juce::dontSendNotification);knob.setDoubleClickReturnValue(true,0);knob.setMouseDragSensitivity(250);knob.setName(name);knob.setTooltip(name+" gain / double-click 0 dB");
 auto* p=&value;auto* s=&knob;knob.onValueChange=[p,s]{p->store(float(s->getValue()),std::memory_order_relaxed);};
}
void MainComponent::prepareToPlay(int block,double rate){
 const GraphControlGuard graphGuard(*this);
 callbackSampleRate.store(rate,std::memory_order_release);
 invalidateClipAudio();invalidateSpectralAudio();referenceAuditionAudio.store(nullptr,std::memory_order_release);
 prepared=false;preparationFailed.store(false);
 try{
 // Reserve every delay's replacement storage before mutating host resources.
 // A delay allocation failure leaves all published delay cores intact.
 for(auto& t:bank->tracks)t.chain.stageDelayPreparations(rate,std::max(1,block));
 auto* inputDevice=deviceManager.getCurrentAudioDevice();hardwareOutputs=inputDevice?inputDevice->getActiveOutputChannels().countNumberOfSetBits():0;vocals.prepare(rate,std::max(1,block),inputDevice?inputDevice->getActiveInputChannels().countNumberOfSetBits():0);
 monitoringRouter.prepare(rate);analyser.prepare(rate);inputMeters.prepare(inputDevice?rate:0);outputMeters.prepare(inputDevice?rate:0);masterLimiter.prepare(rate,std::max(1,block));sampleRate=rate;blockSize=std::max(1,block);++generation;sum.setSize(2,blockSize);listeningSource.setSize(2,blockSize);monitorBuffer.setSize(2,blockSize);monitorRouteReady.store(false);monitorProcessor.prepareToPlay(rate,blockSize);inputValues.assign(size_t(blockSize),1);
 for(auto& t:bank->tracks)t.prepare(rate,blockSize);inputGain.reset(rate,hem::gain(inputDb.load()));outputGain.reset(rate,hem::gain(outputDb.load()));prepared=true;
 }catch(...){for(auto& t:bank->tracks)t.chain.cancelDelayPreparations();prepared=false;failedPreparationRate.store(rate);preparationFailed.store(true);inputMeters.prepare(0);outputMeters.prepare(0);}
}
void MainComponent::prepareOfflineSession(){
 if(prepared||preparationFailed.load()||deviceManager.getCurrentAudioDevice())return;
 const GraphControlGuard lock(*this);
 if(prepared||preparationFailed.load()||deviceManager.getCurrentAudioDevice())return;
 // No device callback exists here. Rebind playing transports without stop()'s
 // callback acknowledgement wait, retaining a common source timeline.
 for(auto& t:bank->tracks)t.transport().stop();
prepareToPlay(1024,hem::defaultSampleRate);
 for(auto& t:bank->tracks)t.chain.completePendingStopped();
 inputMeters.prepare(0);outputMeters.prepare(0);
}
void MainComponent::closePluginEditor(){closeInstrumentalOperation();closeSpectralOperation();if(editedProcessor){const GraphControlGuard guard(*this);}if(workspace)workspace->clearEditor();closeClipOperation();cancelReferenceOperation();pluginWindow.reset();editedProcessor=nullptr;editorKind=EditorKind::none;editedTrack=editedSlot=-1;}
void MainComponent::refreshTrackTrim(){
 // Poll the actual mixer gain without interrupting a drag or unfinished text edit.
 if(trackTrimGesture||trackTrim.isMouseButtonDown())return;
 for(auto* child:trackTrim.getChildren())if(auto* label=dynamic_cast<juce::Label*>(child);label&&label->isBeingEdited())return;
 trackTrim.setValue(current().trimDb.load(),juce::dontSendNotification);
}
void MainComponent::releaseResources(){const GraphControlGuard graphGuard(*this);invalidateClipAudio();invalidateSpectralAudio();referenceAuditionAudio.store(nullptr,std::memory_order_release);inputMeters.prepare(0);outputMeters.prepare(0);vocals.release();monitorProcessor.releaseResources();for(auto& t:bank->tracks)t.release();prepared=false;++generation;}
void MainComponent::getNextAudioBlock(const juce::AudioSourceChannelInfo& info){
 const auto callbackStart=juce::Time::getHighResolutionTicks();
 struct DeadlineCount {std::atomic<uint64_t>& counter;juce::int64 start,budget;~DeadlineCount(){if(budget>0&&juce::Time::getHighResolutionTicks()-start>budget)counter.fetch_add(1,std::memory_order_relaxed);}};
 const double observedRate=callbackSampleRate.load(std::memory_order_acquire);
 const DeadlineCount deadlineCount{trackRenderDeadlineMisses,callbackStart,observedRate>0?juce::int64(double(juce::Time::getHighResolutionTicksPerSecond())*info.numSamples/observedRate):0};
 unsigned expectedAdmission=0;
 if(!callbackAdmission.compare_exchange_strong(expectedAdmission,1,std::memory_order_acquire,std::memory_order_relaxed)){
  controlSilencedCallbacks.fetch_add(1,std::memory_order_relaxed);trackRenderSilencedCallbacks.fetch_add(1,std::memory_order_relaxed);controlGapPending.store(true,std::memory_order_release);info.clearActiveBufferRegion();return;
 }
 struct AdmissionExit {MainComponent& host;~AdmissionExit(){host.latchControlGap();host.callbackAdmission.fetch_and(~1u,std::memory_order_release);}} admissionExit{*this};
 latchControlGap();
 if(trackScheduler.faulted()){trackRenderSilencedCallbacks.fetch_add(1,std::memory_order_relaxed);info.clearActiveBufferRegion();return;}
 juce::ScopedNoDenormals guard;if(!info.buffer)return;if(!prepared||info.buffer->getNumChannels()==0){info.clearActiveBufferRegion();return;}
 for(size_t i=0;i<callbackSourcePositions.size();++i)callbackSourcePositions[i]=bank->tracks[i].transport().getCurrentPosition();
 bool sourcesReady=clipPreviewReady(info.numSamples)&&spectralPreviewReady(info.numSamples);
 for(auto& t:bank->tracks)if(!t.transport().availableRt(info.numSamples)){t.transport().latchUnderrun();sourcesReady=false;}
 if(!sourcesReady){transportDropouts.fetch_add(1,std::memory_order_relaxed);trackRenderFaults.fetch_add(1,std::memory_order_relaxed);trackRenderSilencedCallbacks.fetch_add(1,std::memory_order_relaxed);trackScheduler.latchFault();info.clearActiveBufferRegion();return;}
 const auto renderBudget=trackRenderTestBudget>0?trackRenderTestBudget:juce::int64(double(juce::Time::getHighResolutionTicksPerSecond())*info.numSamples/sampleRate*.9);
 const auto renderDeadline=callbackStart+renderBudget;
 const double blockBpm=projectBpm.load(std::memory_order_relaxed);
 inputGain.set(hem::gain(inputDb.load()));outputGain.set(hem::gain(outputDb.load()));float maximum=0;bool anySolo=false;for(auto& t:bank->tracks)anySolo|=t.solo.load();
 for(int offset=0;offset<info.numSamples;offset+=blockSize){const int n=std::min(blockSize,info.numSamples-offset),start=info.startSample+offset;
    vocals.capture(*info.buffer,start,n,callbackTimeline(),blockBpm);
    sum.clear();for(int i=0;i<n;++i)inputValues[size_t(i)]=inputGain.next();
    trackRenderContext={bank.get(),inputValues.data(),sampleRate,blockBpm,n,anySolo};
    const auto render=[](void* raw,int index)noexcept{
      auto& c=*static_cast<TrackRenderContext*>(raw);auto& t=c.bank->tracks[size_t(index)];
      // A looping/seeked source must not be mistaken for continuous callback time.
      const auto frame=uint64_t(std::max(0.,std::round(t.transport().getCurrentPosition()*c.rate)));
      for(int slot=0;slot<8;++slot)if(auto* reference=dynamic_cast<hem::ReferenceMatchProcessor*>(t.chain.activeProcessor(slot)))reference->sourceTimeline(frame);
      t.renderTrack(c.frames,c.solo,c.gains,c.bpm);
    };
    std::array<int,trackCount> eligible{};int count=0;
    for(int index=0;index<trackCount;++index){auto& chain=bank->tracks[size_t(index)].chain;bool parallel=trackScheduler.helperCount()>0;
      for(int slot=0;parallel&&slot<8;++slot){auto* p=chain.activeProcessor(slot);if(chain.isBusy(slot)||(p&&p->isExternal()))parallel=false;}
      if(parallel)eligible[size_t(count++)]=index;else render(&trackRenderContext,index);
    }
    if(!trackScheduler.run(&trackRenderContext,render,eligible.data(),count,renderDeadline)){
      trackRenderFaults.fetch_add(1,std::memory_order_relaxed);trackRenderSilencedCallbacks.fetch_add(1,std::memory_order_relaxed);
      info.clearActiveBufferRegion();return;
    }
    bool sourceFailed=false;for(auto& t:bank->tracks)sourceFailed=sourceFailed||t.transport().hasUnderrun()||t.transport().hasReadError();
    if(sourceFailed){transportDropouts.fetch_add(1,std::memory_order_relaxed);trackRenderFaults.fetch_add(1,std::memory_order_relaxed);trackRenderSilencedCallbacks.fetch_add(1,std::memory_order_relaxed);trackScheduler.latchFault();info.clearActiveBufferRegion();return;}
    // Original track order and original scalar additions, independent of finish order.
    for(auto& t:bank->tracks)t.addTo(sum,n);
    // Read-only input display tap: the actual summed, post-insert/post-track-trim
    // mix before the existing output gain. This is not the physical mic input.
    inputMeters.push(sum.getReadPointer(0),sum.getReadPointer(1),n);
    // Master result remains in sum. Monitoring touches only this copy.
    // An offline exporter must consume the master path, not monitorBuffer.
    for(int i=0;i<n;++i){const float g=outputGain.next();sum.setSample(0,i,sum.getSample(0,i)*g);sum.setSample(1,i,sum.getSample(1,i)*g);}
    // Analysis is tapped before all listening correction. Audio only copies
    // into a bounded queue; FFT and waveform decimation run on its worker.
    analyser.push(sum.getReadPointer(0),sum.getReadPointer(1),n);
    for(int channel=0;channel<2;++channel)monitorBuffer.copyFrom(channel,0,sum,channel,0,n);
    vocals.addMonitor(monitorBuffer,n);
    renderClipPreview(monitorBuffer,n);if(trackScheduler.faulted()){info.clearActiveBufferRegion();return;}renderReferenceAudition(monitorBuffer,n);renderSpectralPreview(monitorBuffer,n);if(trackScheduler.faulted()){info.clearActiveBufferRegion();return;}
    // Use a separate combined listening buffer as the direct/corrected source.
    for(int channel=0;channel<2;++channel)listeningSource.copyFrom(channel,0,monitorBuffer,channel,0,n);
    monitoringRouter.renderCalibration(listeningSource.getReadPointer(0),listeningSource.getReadPointer(1),monitorBuffer.getWritePointer(0),monitorBuffer.getWritePointer(1),n);
    float* channels[]{monitorBuffer.getWritePointer(0),monitorBuffer.getWritePointer(1)};
    monitorView.setDataToReferTo(channels,2,n);if(monitorRouteReady.load(std::memory_order_acquire))monitorProcessor.processBlock(monitorView,monitorMidi);
    // Last processing stage, including optional monitor EQ boosts. Only an
    // unchanged sample copy to the device follows this safety limiter.
    monitoringRouter.mixRoute(listeningSource.getReadPointer(0),listeningSource.getReadPointer(1),channels[0],channels[1],n);
    if(hardwareOutputs==1)for(int i=0;i<n;++i)channels[0][i]=channels[1][i]=.5f*(channels[0][i]+channels[1][i]);
    masterLimiter.process(channels[0],channels[1],n);
    // Exact emitted samples, including mono fold-down and limiter delay.
    outputMeters.push(channels[0],channels[1],n);
    for(int i=0;i<n;++i){const float l=monitorBuffer.getSample(0,i),r=monitorBuffer.getSample(1,i);maximum=std::max({maximum,std::abs(l),std::abs(r)});
        info.buffer->setSample(0,start+i,l);if(hardwareOutputs>1&&info.buffer->getNumChannels()>1)info.buffer->setSample(1,start+i,r);}
 }
 peak.store(maximum,std::memory_order_relaxed);
 trackRenderCompleted.fetch_add(1,std::memory_order_relaxed);
}
bool MainComponent::playing() const {for(const auto& t:bank->tracks)if(t.transport().isPlaying())return true;return false;}
double MainComponent::sessionPosition() const {
 // The longest loaded source remains the timeline anchor when shorter stems
 // finish. Muted tracks are still rendered and keep their playback positions.
 const hem::TrackEngine* anchor=nullptr;double length=-1;
 for(const auto& t:bank->tracks)if(t.loaded&&t.transport().getLengthInSeconds()>length){anchor=&t;length=t.transport().getLengthInSeconds();}
 return anchor?anchor->transport().getCurrentPosition():0;
}
void MainComponent::playAll(){
 if(trackScheduler.faulted())recoverTrackRenderFault();
 if(nativeResourceLoadBusy()){message.setText("Wait for resource loading to finish",juce::dontSendNotification);return;}
 // Transport never opens a file picker. Import requires FILE / BROWSE WAVS.
 bool loaded=false;double duration=0;for(const auto& t:bank->tracks)if(t.loaded){loaded=true;duration=std::max(duration,t.transport().getLengthInSeconds());}
 if(!deviceManager.getCurrentAudioDevice()){message.setText("No audio device: editing, sessions and export remain available",juce::dontSendNotification);return;}
 if(exportWorker&&exportWorker->isBusy()){message.setText("Finish or cancel the offline export before playback",juce::dontSendNotification);return;}

 if(vocals.recorder.busy()&&!playing()){message.setText("Start backing playback before recording, or finish the current take",juce::dontSendNotification);return;}
 if(sessionWorker&&sessionWorker->isBusy())return;
 if(playing())return;

 if(importer&&importer->hasPreparedBatch()){
    const auto result=importer->applyPreparedBatch();
    if(result.failed()){showShellPage(ShellPage::home);message.setText(result.getErrorMessage(),juce::dontSendNotification);return;}
    loaded=false;duration=0;for(const auto& t:bank->tracks)if(t.loaded){loaded=true;duration=std::max(duration,t.transport().getLengthInSeconds());}
 }
 if(!loaded){showShellPage(ShellPage::home);message.setText(importer&&importer->hasPendingWork()?"WAV import is still preparing. Press PLAY when READY.":"No audio loaded: use FILE or BROWSE WAVS to import audio",juce::dontSendNotification);return;}

 TransportRequest request;request.loop=looping;request.play=true;request.explicitPlay=true;request.generation=generation;
 {const GraphControlGuard lock(*this);double position=sessionPosition();if(position>=duration)position=0;
  for(size_t i=0;i<request.positions.size();++i)request.positions[i]=trackScheduler.faulted()?callbackSourcePositions[i]:std::min(position,bank->tracks[i].transport().getLengthInSeconds());}
 requestTransport(request);
}
void MainComponent::pauseAll(){
 double position;
 {const GraphControlGuard lock(*this);position=sessionPosition();}
 // JUCE stop() waits for its callback fade. It must run OUTSIDE the device
 // lock. Afterwards restore all stems to the same captured pause position.
 stopAll(false);seekAll(position);
}
void MainComponent::stopAll(bool rewindAll){
 spectralAudition(0);cancelTransport();
 if(trackScheduler.faulted()){recoverTrackRenderFault();if(rewindAll)seekAll(0);return;}
 if(clipOperationBusy())clipCommand(0);
 cancelReferenceCaptures();
 {const GraphControlGuard lock(*this);vocals.stopTake();}
 {const GraphControlGuard guard(*this);for(auto& t:bank->tracks)t.transport().stop();}
 if(trackScheduler.faulted())recoverTrackRenderFault();if(rewindAll)seekAll(0);
}
void MainComponent::seekAll(double position){spectralAudition(0);cancelReferenceCaptures();TransportRequest request;request.loop=looping;request.generation=generation;
 {const GraphControlGuard lock(*this);vocals.stopTake();request.play=!trackScheduler.faulted()&&playing();for(size_t i=0;i<request.positions.size();++i)request.positions[i]=std::min(position,bank->tracks[i].transport().getLengthInSeconds());}
 requestTransport(request);
}
void MainComponent::installSelected(int module){
 if(nativeResourceLoadBusy()){message.setText("Wait for resource loading to finish",juce::dontSendNotification);return;}
 if(sessionWorker&&sessionWorker->isBusy()){message.setText("Wait for session loading or saving before changing inserts",juce::dontSendNotification);return;}
 const auto& entries=hem::PluginRegistry::entries();
 if(module>=0&&module<int(entries.size())&&entries[size_t(module)].clipOperation){openClipOperation(module);return;}
 if(module>=0&&module<int(entries.size())&&entries[size_t(module)].monitoringOnly){showMonitor();message.setText("Monitor EQ is listening-only; track insert unchanged",juce::dontSendNotification);return;}
 auto& chain=current().chain;
 // An open device can remain callback-silenced after overload protection. A
 // stopped graph must not leave an insert queued for a render that cannot run.
 if(!playing()&&chain.isBusy(selected)){
  std::unique_ptr<hem::InsertChain::PreparedNode> old;
  {const GraphControlGuard lock(*this);chain.completePendingStopped();old=chain.collectRetired(selected);}
  if(old){if(editedProcessor==old->processor.get())closePluginEditor();old->processor->releaseResources();}
 }
 if(chain.isBusy(selected)){message.setText("Insert transition pending; stop playback to complete it",juce::dontSendNotification);return;}
 double rate;int block,budget;unsigned version;
 {if(!prepared){message.setText("Audio preparation is not ready; check AUDIO / I/O before inserting",juce::dontSendNotification);return;}rate=sampleRate;block=blockSize;budget=chain.slotLatency();version=generation;}
 try{auto processor=hem::createModule(module);if(processor)processor->setBlockTempo(projectBpm.load());auto node=chain.stageNode(std::move(processor),rate,block,budget);bool success=false,offline=false;
    std::unique_ptr<hem::InsertChain::PreparedNode> retired;
    if(prepared&&generation==version){
      offline=deviceManager.getCurrentAudioDevice()==nullptr||!playing();
      if(offline){const GraphControlGuard lock(*this);success=chain.publishStopped(selected,std::move(node));if(success)retired=chain.collectRetired(selected);}
      else success=chain.publish(selected,std::move(node));
    }
    if(retired){if(editedProcessor==retired->processor.get())closePluginEditor();retired->processor->releaseResources();}
    message.setText(success?"TRACK "+juce::String(selectedTrack+1)+" / SLOT "+juce::String(selected+1)+" / "+hem::InsertFactory::name(module)+(offline?" / ready":" / queued; stop playback if transition remains pending"):"Device changed; insert again",juce::dontSendNotification);
    if(workspace)workspace->refresh();
 }catch(const std::exception& e){message.setText(e.what(),juce::dontSendNotification);}
}
void MainComponent::selectTrack(int i){if(i<0||i>=trackCount)return;if(i!=selectedTrack)closePluginEditor();selectedTrack=i;trackMenu.setSelectedId(i+1,juce::dontSendNotification);trackTrim.setValue(current().trimDb.load(),juce::dontSendNotification);if(workspace)workspace->refresh();}
void MainComponent::selectSlot(int i){selected=juce::jlimit(0,7,i);showShellPage(ShellPage::home);}
void MainComponent::rememberEditorOrigin(EditorOpenIntent intent){
 // Internal processor replacements retain the original navigation destination.
 // A user opening from the rack starts a new visit, even though it is also PLUGIN.
 if(intent==EditorOpenIntent::navigation&&(shellPage!=ShellPage::plugin||!workspace->hasEditor()))editorReturnPage=shellPage;
}
void MainComponent::openPlugin(int slot,EditorOpenIntent intent){
 rememberEditorOrigin(intent);
 selected=juce::jlimit(0,7,slot);auto* p=current().chain.activeProcessor(selected);
 // Reopening the current slot retains editor, text-entry state and scroll.
 if(editorKind==EditorKind::insert&&p&&p==editedProcessor&&editedTrack==selectedTrack&&editedSlot==selected&&workspace->hasEditor()){
  showShellPage(ShellPage::plugin);return;
 }
 closePluginEditor();
 if(p){if(auto* reference=dynamic_cast<hem::ReferenceMatchProcessor*>(p))bindReferenceEditor(*reference,selectedTrack,selected);editedProcessor=p;editorKind=EditorKind::insert;editedTrack=selectedTrack;editedSlot=selected;workspace->setEditor(std::unique_ptr<juce::AudioProcessorEditor>(p->createEditor()));}
 else message.setText("Empty insert: select a module in PLUGIN VAULT",juce::dontSendNotification);
 showShellPage(ShellPage::plugin);
}
void MainComponent::moveSlot(int from,int to){
 if(nativeResourceLoadBusy()){message.setText("Wait for resource loading to finish",juce::dontSendNotification);return;}
 if(from<0||from>=8||to<0||to>=8||from==to)return;
 if(playing()){message.setText("Stop playback before changing insert order",juce::dontSendNotification);return;}
 if((sessionWorker&&sessionWorker->isBusy())||vocals.recorder.busy()){message.setText("Finish session work or vocal recording before changing insert order",juce::dontSendNotification);return;}
 auto& chain=current().chain;if(!chain.activeProcessor(from))return;
 if(chain.isBusy(from)||chain.isBusy(to)){message.setText("Wait for insert transition",juce::dontSendNotification);return;}
 closePluginEditor();bool moved;
 {const GraphControlGuard lock(*this);moved=current().chain.swapSlotsStopped(from,to);}
 if(moved)selected=to;message.setText(moved?"Insert order updated":"Wait for insert transition",juce::dontSendNotification);if(workspace)workspace->refresh();
}
void MainComponent::showRack(){closePluginEditor();workspace->setPluginExpanded(false);showShellPage(ShellPage::plugin);}
void MainComponent::timerCallback(){
 deviceManager.pollDeviceState();
 recoverTrackRenderFault();pollTransport();
 pollDrumSampleLoad();pollClipOperation();pollReferenceOperation();pollSpectralOperation();pollInstrumentalOperation();
 prepareOfflineSession();
 if(preparationFailed.load())message.setText("Audio preparation failed at "+juce::String(failedPreparationRate.load(),0)+" Hz; output silenced. Reduce delay instances or restore device settings. Staged delay allocation does not replace published delay state.",juce::dontSendNotification);
 if(auto* device=deviceManager.getCurrentAudioDevice()){
    const auto name=device->getName();
    if(name!=lastOutputDevice||!monitorRouteReady.load()){
        monitorProcessor.state.getParameter("enabled")->setValueNotifyingHost(0.f);
        monitoringRouter.setHeadphoneMode(false);calibrationName="Device restarted: reload measurement profile";
        lastOutputDevice=name;monitorRouteReady.store(true,std::memory_order_release);
    }
 }

 std::array<std::array<std::unique_ptr<hem::InsertChain::PreparedNode>,8>,trackCount> retired;
 std::array<std::array<hem::ModuleProcessor*,8>,trackCount> active{};
 bool needsRetirement=false;for(auto& track:bank->tracks)needsRetirement|=track.chain.hasRetired();
 if(needsRetirement){const GraphControlGuard lock(*this);for(int t=0;t<trackCount;++t)for(int i=0;i<8;++i)retired[size_t(t)][size_t(i)]=bank->tracks[size_t(t)].chain.collectRetired(i);}
 for(int t=0;t<trackCount;++t)for(int i=0;i<8;++i)active[size_t(t)][size_t(i)]=bank->tracks[size_t(t)].chain.activeProcessor(i);
 // Snapshot before retirement. Close any editor that still refers to an old
 // processor before the corresponding unique_ptr leaves this scope.
 if(editorKind==EditorKind::insert&&editedProcessor
    &&(editedTrack<0||editedTrack>=trackCount||editedSlot<0||editedSlot>=8
       ||active[size_t(editedTrack)][size_t(editedSlot)]!=editedProcessor)){closePluginEditor();}
 for(int t=0;t<trackCount;++t)for(int i=0;i<8;++i)if(retired[size_t(t)][size_t(i)])retired[size_t(t)][size_t(i)]->processor->releaseResources();
 auto& nodes=active[size_t(selectedTrack)];auto& chain=current().chain;auto* p=nodes[size_t(selected)];const bool running=playing();
 play.setToggleState(running,juce::dontSendNotification);loop.setToggleState(looping,juce::dontSendNotification);
 for(int i=0;i<8;++i){auto* node=nodes[size_t(i)];const auto name=juce::String(i+1)+" / "+(node?node->getName():juce::String("EMPTY"));slotButtons[size_t(i)].setButtonText(name);
    if(rackView){auto& row=rackView->rows[size_t(i)];row.name.setText((selected==i?"> ":"  ")+name,juce::dontSendNotification);row.bypass.setToggleState(chain.isBypassed(i),juce::dontSendNotification);row.up.setEnabled(!running&&i>0&&!chain.isBusy(i));row.down.setEnabled(!running&&i<7&&!chain.isBusy(i));row.remove.setEnabled(node&&!chain.isBusy(i));}}
 if(rackView){rackView->tracks.setSelectedId(selectedTrack+1,juce::dontSendNotification);rackView->status.setText("SELECTED INSERT "+juce::String(selected+1)+" / "+(running?"Stop transport to reorder":"UP / DOWN changes order")+" / "+message.getText(),juce::dontSendNotification);}
 title.setText("TRACK "+juce::String(selectedTrack+1)+" / INSERT "+juce::String(selected+1)+" / "+(p?p->getName():juce::String("EMPTY")),juce::dontSendNotification);
 bypass.setToggleState(p&&chain.isBypassed(selected),juce::dontSendNotification);bypass.setEnabled(p&&!chain.isBusy(selected));insert.setEnabled(!chain.isBusy(selected));remove.setEnabled(p&&!chain.isBusy(selected));openButton.setEnabled(p!=nullptr);
 refreshTrackTrim();
 mute.setToggleState(current().muted.load(),juce::dontSendNotification);solo.setToggleState(current().solo.load(),juce::dontSendNotification);
 track.setText(current().name+"\n"+timeText(sessionPosition())+" / "+timeText(current().transport().getLengthInSeconds()),juce::dontSendNotification);
 inValue.setText("INPUT "+juce::String(inputDb.load(),1)+" dB",juce::dontSendNotification);outValue.setText("OUTPUT "+juce::String(outputDb.load(),1)+" dB",juce::dontSendNotification);
 juce::String outputDeviceName;
 if(auto* d=deviceManager.getCurrentAudioDevice()){outputDeviceName=d->getName();telemetry.setText("AUDIO OUTPUT\n"+juce::String(d->getCurrentSampleRate()/1000,1)+" kHz\n"+juce::String(d->getCurrentBufferSizeSamples())+" frames",juce::dontSendNotification);}else telemetry.setText("NO AUDIO DEVICE",juce::dontSendNotification);
 telemetry.setTooltip(outputDeviceName+" / "+deviceError+" / output sample peak "+juce::String(masterLimiter.samplePeakDb(),2)+" dBFS / master latency "+juce::String(masterLimiter.latencySamples())+" samples");
 vocalButton.setButtonText(vocals.recorder.busy()?"VOCAL / ACTIVE":"VOCAL TRACKING");vocalButton.setToggleState(vocals.recorder.busy(),juce::dontSendNotification);
 if(workspace&&workspace->isVisible()){
  switch(workspace->page()){case hem::NativeWorkspace::Page::plugin:shellPage=ShellPage::plugin;break;case hem::NativeWorkspace::Page::mixer:shellPage=ShellPage::mixer;break;case hem::NativeWorkspace::Page::vault:shellPage=ShellPage::vault;break;case hem::NativeWorkspace::Page::routing:shellPage=ShellPage::routing;break;}
 }
 for(size_t i=0;i<navigation.size();++i)navigation[i].setToggleState(shellPage==static_cast<ShellPage>(i),juce::dontSendNotification);
 if(deviceSettings)deviceSettings->setDeviceChangesAllowed(!nativeResourceLoadBusy()&&!running&&!vocals.recorder.busy()&&!exportWorker->isBusy());
 const bool canClear=!nativeResourceLoadBusy()&&!running&&!vocals.recorder.busy()&&!sessionWorker->isBusy()&&!exportWorker->isBusy()&&!importer->hasPendingWork();unloadSourcesButton.setEnabled(canClear);newSessionButton.setEnabled(canClear);
 const bool sessionBusy=nativeResourceLoadBusy()||(sessionWorker&&sessionWorker->isBusy());
 routeButton.setButtonText(monitoringRouter.headphoneMode()?"HEADPHONE CORRECTION":"DIRECT OUTPUT");routeButton.setToggleState(monitoringRouter.headphoneMode(),juce::dontSendNotification);routeButton.setTooltip(calibrationName);
 const auto deadlines=trackRenderDeadlineMisses.load(std::memory_order_relaxed),dropouts=trackRenderSilencedCallbacks.load(std::memory_order_relaxed);
 const auto renderStatus=message.getText()+((deadlines||dropouts)?" | deadline misses "+juce::String(deadlines)+" / dropped callbacks "+juce::String(dropouts)+" / source underruns "+juce::String(transportDropouts.load())+" / control gaps "+juce::String(controlSilencedCallbacks.load()):juce::String())+(deviceManager.silentCallbacks()?" / device bridge silences "+juce::String(deviceManager.silentCallbacks()):juce::String());
 statusBar.setText(renderStatus,juce::dontSendNotification);statusBar.setTooltip(renderStatus);
 saveSessionButton.setEnabled(!sessionBusy);loadSessionButton.setEnabled(!sessionBusy);importer->setEnabled(!sessionBusy);
 juce::String externalFault;
 for(int ti=0;ti<trackCount&&externalFault.isEmpty();++ti)for(int si=0;si<8&&externalFault.isEmpty();++si)
  if(auto* node=active[size_t(ti)][size_t(si)];node&&node->isExternal()&&!node->processingReady())
   externalFault="TRACK "+juce::String(ti+1)+" / SLOT "+juce::String(si+1)+" / "+node->externalDiagnostic();
 if(externalFault!=lastExternalDiagnostic){lastExternalDiagnostic=externalFault;if(externalFault.isNotEmpty())message.setText(externalFault+" / select this slot and RELOAD CURRENT VST3",juce::dontSendNotification);}
 externalHost.collectGarbage();
 repaint();pumpCloseRequest();
}
void MainComponent::openAudio(){
 if(nativeResourceLoadBusy()){message.setText("Wait for resource loading to finish",juce::dontSendNotification);return;}if(sessionWorker&&sessionWorker->isBusy())return;const int destination=selectedTrack;picker=std::make_unique<juce::FileChooser>("Load selected track",juce::File(),"*.wav");juce::Component::SafePointer<MainComponent> safe(this);
 picker->launchAsync(juce::FileBrowserComponent::openMode|juce::FileBrowserComponent::canSelectFiles,[safe,destination](const juce::FileChooser& f){if(safe&&f.getResult().existsAsFile())safe->loadAudio(f.getResult(),destination);});
}
void MainComponent::loadAudio(const juce::File& f,int index){
 if(nativeResourceLoadBusy()){message.setText("Wait for resource loading to finish",juce::dontSendNotification);return;}
 if(sessionWorker&&sessionWorker->isBusy())return;
 juce::StringArray files;files.add(f.getFullPathName());importer->submitFiles(files,index);
}
juce::Result MainComponent::commitImport(hem::StemFileImporter::Batch& batch){
 if(nativeResourceLoadBusy())return juce::Result::fail("Wait for resource loading to finish");
 if(vocals.recorder.busy())return juce::Result::fail("Finish vocal recording before replacing backing stems");
 if(sessionWorker&&sessionWorker->isBusy())return juce::Result::fail("Wait for session operation");
 if(playing())return juce::Result::fail("Pause or stop playback, then APPLY. Current audio is unchanged.");
 cancelReferenceCaptures();
 if(batch.firstTrack<0||batch.count<1||batch.firstTrack+batch.count>trackCount)return juce::Result::fail("Invalid track mapping");
 if(batch.projectTempo!=0&&(!std::isfinite(batch.projectTempo)||batch.projectTempo<40||batch.projectTempo>240))return juce::Result::fail("Invalid instrumental tempo");
 for(int i=0;i<batch.count;++i)if(!batch.stems[size_t(i)])return juce::Result::fail("Incomplete batch; import again");
 {
    const GraphControlGuard lock(*this);
    if(!prepared||generation!=batch.context.generation||looping!=batch.context.looping)
        return juce::Result::fail("Output device or loop mode changed. CLEAR and import again.");
    if(batch.requireEmptyTargets)for(int i=0;i<batch.count;++i)
        if(bank->tracks[size_t(batch.firstTrack+i)].loaded)return juce::Result::fail("Instrumental import requires empty tracks; current sources unchanged");
    // Already prepared sources swap ownership atomically as a group. No file
    // opening, resampler preparation, reader destruction or GUI work here.
    for(int i=0;i<batch.count;++i)bank->tracks[size_t(batch.firstTrack+i)].playback.swap(batch.stems[size_t(i)]);
    if(batch.projectTempo!=0)projectBpm.store(batch.projectTempo);
 }
 if(batch.projectTempo!=0)tempoControl.setValue(batch.projectTempo,juce::dontSendNotification);
 closeSpectralOperation();
 for(int i=0;i<batch.count;++i){spectralHistory[size_t(batch.firstTrack+i)].reset();auto& t=bank->tracks[size_t(batch.firstTrack+i)];t.loaded=true;t.name=t.playback->file.getFileName();t.sourceRate=t.playback->sourceRate;}
 seekAll(0);message.setText(juce::String(batch.count)+" WAV stems applied / all tracks rewound / insert chains retained",juce::dontSendNotification);
 return juce::Result::ok();
}
void MainComponent::showMonitor(){
 monitoringRouter.setHeadphoneMode(true);
 rememberEditorOrigin(EditorOpenIntent::navigation);
 if(editedProcessor!=&monitorProcessor||!workspace->hasEditor()){
  closePluginEditor();editedProcessor=&monitorProcessor;editorKind=EditorKind::monitor;
  workspace->setEditor(std::unique_ptr<juce::AudioProcessorEditor>(monitorProcessor.createEditor()));
 }
 showShellPage(ShellPage::plugin);
}
void MainComponent::showSettings(){showShellPage(ShellPage::settings);}
juce::Rectangle<int> MainComponent::map(float x,float y,float w,float h) const {return juce::Rectangle<float>(face.getX()+x/1673*face.getWidth(),face.getY()+y/940*face.getHeight(),w/1673*face.getWidth(),h/940*face.getHeight()).toNearestInt();}
void MainComponent::resized(){
 const float scale=std::min(float(getWidth())/1673,float(getHeight())/940);face={(getWidth()-1673*scale)*.5f,(getHeight()-940*scale)*.5f,1673*scale,940*scale};
 // Only live apertures of approved host00 are covered. Header brand, chassis,
 // lower-left Ghost identity and the blank lower-right badge stay untouched.
 inputMeterView.setBounds(map(78,178,163,422));outputMeterView.setBounds(map(1436,178,166,422));
 headerMenu.setBounds(map(87,52,43,43));file.setBounds(map(136,52,45,43));headerRack.setBounds(map(197,52,47,43));headerHome.setBounds(map(261,52,69,43));settings.setBounds(map(1396,45,197,55));
 auto nav=map(345,153,983,32);for(size_t i=0;i<navigation.size();++i){const int x=nav.getX()+nav.getWidth()*int(i)/6,next=nav.getX()+nav.getWidth()*int(i+1)/6;navigation[i].setBounds(x,nav.getY(),next-x-3,nav.getHeight());}
 statusBar.setBounds(map(345,638,983,25));statusBar.setFont(juce::Font(juce::FontOptions(std::max(9.f,13*scale))));
 for(int i=0;i<8;++i)slotButtons[size_t(i)].setBounds(map(345,193.f+i*47.f,180,43));
 unloadSourcesButton.setBounds(map(345,587,85,35));newSessionButton.setBounds(map(438,587,87,35));bay.setBounds(map(535,193,793,434));
 const auto content=map(345,193,983,434);if(workspace){const bool takeover=workspace->hasEditor()&&workspace->page()==hem::NativeWorkspace::Page::plugin;workspace->setBounds(takeover?getLocalBounds():content);if(takeover)workspace->toFront(false);}if(deviceSettings)deviceSettings->setBounds(content);
 auto utility=content;utilityTitle.setBounds(utility.removeFromTop(int(28*scale)));utilityViewport.setBounds(utility);
 if(utilityContent)utilityContent->setSize(std::max(utilityMinimum.x,utility.getWidth()-utilityViewport.getScrollBarThickness()),std::max(utilityMinimum.y,utility.getHeight()-utilityViewport.getScrollBarThickness()));
 inputKnob.setBounds(map(260,746,88,84));outputKnob.setBounds(map(1322,746,88,84));
 const std::array<juce::TextButton*,6> transport{&rewind,&stop,&play,&pauseButton,&forward,&loop};for(size_t i=0;i<transport.size();++i)transport[i]->setBounds(map(503.f+106.f*float(i),768,98,46));
 // Real project tempo occupies its own lower-rail row beneath transport.
 tempoLabel.setBounds(map(617,819,110,31));tempoControl.setBounds(map(735,819,224,31));
 tempoLabel.setFont(juce::Font(juce::FontOptions(std::max(10.f,14*scale))));
 tempoControl.setTextBoxStyle(juce::Slider::TextBoxLeft,false,std::max(68,int(130*scale)),std::max(20,int(29*scale)));
 track.setBounds(map(368,751,126,95));telemetry.setBounds(map(1142,751,168,95));inValue.setBounds(map(240,832,128,18));outValue.setBounds(map(1312,832,112,18));
 const float ui=float(bay.getWidth())/793.f;auto b=bay.getLocalBounds().reduced(int(8*ui));
 title.setBounds(b.removeFromTop(int(28*ui)));auto row=b.removeFromTop(int(34*ui));trackMenu.setBounds(row.removeFromLeft(row.getWidth()/3).reduced(2));rackButton.setBounds(row.removeFromLeft(row.getWidth()/2).reduced(2));openButton.setBounds(row.reduced(2));
 row=b.removeFromTop(int(34*ui));moduleMenu.setBounds(row.removeFromLeft(row.getWidth()/2).reduced(2));insert.setBounds(row.reduced(2));
 row=b.removeFromTop(int(34*ui));remove.setBounds(row.removeFromLeft(row.getWidth()/3).reduced(2));bypass.setBounds(row.removeFromLeft(row.getWidth()/2).reduced(2));monitorButton.setBounds(row.reduced(2));
 row=b.removeFromTop(int(34*ui));mute.setBounds(row.removeFromLeft(row.getWidth()/3).reduced(2));solo.setBounds(row.removeFromLeft(row.getWidth()/2).reduced(2));outputButton.setBounds(row.reduced(2));
 row=b.removeFromTop(int(34*ui));saveSessionButton.setBounds(row.removeFromLeft(row.getWidth()/5).reduced(2));loadSessionButton.setBounds(row.removeFromLeft(row.getWidth()/4).reduced(2));analysisButton.setBounds(row.removeFromLeft(row.getWidth()/3).reduced(2));calibrationButton.setBounds(row.removeFromLeft(row.getWidth()/2).reduced(2));vocalButton.setBounds(row.reduced(2));
 row=b.removeFromTop(int(86*ui));trackTrim.setBounds(row.removeFromLeft(int(190*ui)).reduced(2));auto actions=row.withSizeKeepingCentre(row.getWidth(),int(38*ui));routeButton.setBounds(actions.removeFromLeft(actions.getWidth()*2/3).reduced(2));exportButton.setBounds(actions.reduced(2));
 importer->setBounds(b.reduced(2));message.setBounds({});
}
void MainComponent::paint(juce::Graphics& g){
 g.fillAll(juce::Colour(0xff020508));g.drawImage(faceplate,face,juce::RectanglePlacement::stretchToFit);
 // Replace the artwork's simulated window icons with one real audio-settings
 // control. The OS title bar remains the sole source of window controls.
 g.setColour(juce::Colour(0xff061017));g.fillRect(map(1396,45,197,55));
}

void MainComponent::chooseSession(bool saving){
 if(nativeResourceLoadBusy()){message.setText("Wait for resource loading to finish",juce::dontSendNotification);return;}
 if(sessionWorker->isBusy())return;
 if(!saving&&(vocals.recorder.busy()||playing()||importer->hasPendingWork())){message.setText("Pause transport and APPLY or CLEAR pending imports before recalling a session",juce::dontSendNotification);return;}
 picker=std::make_unique<juce::FileChooser>(saving?"Save HEM session":"Recall HEM session",juce::File(),"*.xml;*.json");
 juce::Component::SafePointer<MainComponent> safe(this);
 picker->launchAsync((saving?juce::FileBrowserComponent::saveMode|juce::FileBrowserComponent::warnAboutOverwriting:juce::FileBrowserComponent::openMode)|juce::FileBrowserComponent::canSelectFiles,
    [safe,saving](const juce::FileChooser& chooser){
        if(!safe||chooser.getResult()==juce::File()||safe->sessionWorker->isBusy())return;
        const auto result=saving?safe->saveSessionFile(chooser.getResult()):safe->loadSessionFile(chooser.getResult());
        if(result.failed())safe->message.setText(result.getErrorMessage(),juce::dontSendNotification);
    });
}
juce::Result MainComponent::commitSession(std::unique_ptr<hem::TrackBank>& staged,const hem::SessionSnapshot& state,hem::SessionWorker::Context context){
 if(nativeResourceLoadBusy())return juce::Result::fail("Wait for resource loading to finish");
 if(!staged||vocals.recorder.busy()||playing())return juce::Result::fail("Session unchanged: stop playback before recalling");
 cancelReferenceCaptures();
 // Close every editor borrowing a processor from the bank BEFORE retirement.
 closePluginEditor();
 {
    const GraphControlGuard lock(*this);
    if(!prepared||generation!=context.generation)return juce::Result::fail("Session unchanged: audio device changed during recall; load again");
    bank.swap(staged);inputDb.store(state.inputDb);outputDb.store(state.outputDb);projectBpm.store(state.tempoBpm);looping=state.looping;
    // A source batch prepared for the previous session must not be committed.
    ++generation;
 }
 for(auto& history:spectralHistory)history.reset();
 restoreClipEdits(state);
 tempoControl.setValue(state.tempoBpm,juce::dontSendNotification);
 // Monitor state was range-checked by the worker. Do not enable an old room or
 // headphone correction on a potentially different current output device.
 auto monitor=state.monitorParameters.createCopy();
 for(auto parameter:monitor)if(parameter["id"].toString()=="enabled")parameter.setProperty("value",0.0,nullptr);
 hem::SessionState::restoreParameters(monitorProcessor,monitor);
 inputKnob.setValue(state.inputDb,juce::dontSendNotification);outputKnob.setValue(state.outputDb,juce::dontSendNotification);
 monitoringRouter.setHeadphoneMode(false);selectTrack(0);selectSlot(0);return juce::Result::ok();
}

void MainComponent::openCalibration(){
 if(profileLoader->isBusy())return;
 picker=std::make_unique<juce::FileChooser>("Load measured headphone calibration JSON",juce::File(),"*.json");juce::Component::SafePointer<MainComponent> safe(this);
 picker->launchAsync(juce::FileBrowserComponent::openMode|juce::FileBrowserComponent::canSelectFiles,[safe](const juce::FileChooser& chooser){
    if(!safe||!chooser.getResult().existsAsFile())return;
    double rate;unsigned gen;
    {const GraphControlGuard lock(*safe.getComponent());if(!safe->prepared)return;rate=safe->sampleRate;gen=safe->generation;}
    safe->profileLoader->load(chooser.getResult(),rate,gen);
 });
}

double MainComponent::callbackTimeline() const {
 const hem::TrackEngine* anchor=nullptr;double duration=-1;
 for(const auto& t:bank->tracks){const double length=t.transport().getLengthInSeconds();if(length>duration){duration=length;anchor=&t;}}
 return anchor?anchor->transport().getCurrentPosition():0;
}
void MainComponent::showVocals(){
 if(!embeddedVocal)embeddedVocal=std::make_unique<hem::VocalTrackingPanel>(vocals,deviceManager,[this]{showSettings();},
    [this]{return bank->tracks[0].chain.latencySamples()+masterLimiter.latencySamples();},[this]{return masterLimiter.latencySamples();},
    [this]{if(nativeResourceLoadBusy())return juce::String("Wait for resource loading before recording");if(exportWorker&&exportWorker->isBusy())return juce::String("Finish or cancel the offline export before recording");if(looping)return juce::String("Disable backing loops before recording a continuous take");if(sessionWorker->isBusy())return juce::String("Wait for session recall to finish");return juce::String();},[this](const std::function<void()>& fn){const GraphControlGuard guard(*this);fn();});
 showUtility(*embeddedVocal,"LIVE VOCAL RECORDING / TRACKING");
}

void MainComponent::showExport(){
 if(!embeddedExport)embeddedExport=std::make_unique<hem::OfflineMixPanel>(*exportWorker,[this](const juce::File& file,const hem::OfflineMixOptions& options){return startExport(file,options);});
 showUtility(*embeddedExport,"OFFLINE MIX EXPORT / STEREO FLOAT32 WAV");
}
void MainComponent::showAnalysis(){if(!embeddedAnalysis)embeddedAnalysis=std::make_unique<hem::AnalysisView>(analyser);showUtility(*embeddedAnalysis,"MASTER WAVEFORM / SPECTRUM / PRE-LISTENING TAP");}
void MainComponent::showUtility(juce::Component& content,const juce::String& name){
 if(embeddedInstrumental&&utilityContent==embeddedInstrumental.get() && &content!=embeddedInstrumental.get())closeInstrumentalOperation();
 if(embeddedSpectral&&utilityContent==embeddedSpectral.get() && &content!=embeddedSpectral.get())closeSpectralOperation();
 // These host-owned panels persist until teardown. Capture each natural minimum
 // once, before viewport layout stretches it; reopening must never ratchet it up.
 const auto minimum=utilityPanelSizing.minimumFor(&content,{content.getWidth(),content.getHeight()});
 utilityContent=&content;utilityMinimum={minimum.width,minimum.height};utilityViewport.setViewedComponent(&content,false);utilityTitle.setText(name,juce::dontSendNotification);showShellPage(ShellPage::utility);
}
void MainComponent::showShellPage(ShellPage page){
 if(page!=ShellPage::utility&&embeddedInstrumental&&utilityContent==embeddedInstrumental.get())closeInstrumentalOperation();
 if(page!=ShellPage::utility&&embeddedSpectral&&utilityContent==embeddedSpectral.get())closeSpectralOperation();
 if(page==ShellPage::home)refreshTrackTrim();
 shellPage=page;bay.setVisible(page==ShellPage::home);for(auto& b:slotButtons)b.setVisible(page==ShellPage::home);
 const bool work=page==ShellPage::plugin||page==ShellPage::mixer||page==ShellPage::vault||page==ShellPage::routing;
 if(workspace){workspace->setVisible(work);if(work){const auto target=page==ShellPage::mixer?hem::NativeWorkspace::Page::mixer:page==ShellPage::vault?hem::NativeWorkspace::Page::vault:page==ShellPage::routing?hem::NativeWorkspace::Page::routing:hem::NativeWorkspace::Page::plugin;workspace->setPage(target);}}
 if(page==ShellPage::settings&&!deviceSettings){deviceSettings=std::make_unique<hem::DeviceSettingsPanel>(deviceManager);addChildComponent(*deviceSettings);}
 if(deviceSettings){deviceSettings->setVisible(page==ShellPage::settings);deviceSettings->setDeviceChangesAllowed(!nativeResourceLoadBusy()&&!playing()&&!vocals.recorder.busy()&&!exportWorker->isBusy());}
 utilityViewport.setVisible(page==ShellPage::utility);utilityTitle.setVisible(page==ShellPage::utility);
 for(size_t i=0;i<navigation.size();++i)navigation[i].setToggleState(page==static_cast<ShellPage>(i),juce::dontSendNotification);
 resized();repaint();
}
void MainComponent::requestNewSession(){
 juce::Component::SafePointer<MainComponent> safe(this);
 juce::AlertWindow::showAsync(juce::MessageBoxOptions().withIconType(juce::MessageBoxIconType::WarningIcon).withTitle("Start a new session?").withMessage("This clears unsaved track sources, inserts and mix settings. Source WAV files and saved sessions are not deleted.").withButton("Start new").withButton("Cancel"),[safe](int button){if(safe&&button==1){const auto result=safe->resetSessionConfirmed();if(result.failed())safe->message.setText(result.getErrorMessage(),juce::dontSendNotification);}});
}
juce::Result MainComponent::startExport(const juce::File& destination,const hem::OfflineMixOptions& options){
 if(nativeResourceLoadBusy())return juce::Result::fail("Wait for resource loading to finish");
 if(exportWorker->isBusy())return juce::Result::fail("An export is already running");
 if(playing()||vocals.recorder.busy())return juce::Result::fail("Pause transport and finish the take before exporting");
 if(sessionWorker->isBusy()||importer->hasPendingWork())return juce::Result::fail("Finish session work and APPLY or CLEAR pending imports first");
 try{
  auto snapshot=captureSession();
  if(!exportWorker->start(std::move(snapshot),destination,options))return juce::Result::fail(exportWorker->result());
  return juce::Result::ok();
 }catch(const std::exception& e){return juce::Result::fail(e.what());}
}

juce::Result MainComponent::saveSessionFile(const juce::File& file){
 if(nativeResourceLoadBusy())return juce::Result::fail("Wait for resource loading to finish");
 if(sessionWorker->isBusy())return juce::Result::fail("Wait for the current session operation");
 if(hasExternalInserts()&&(playing()||vocals.recorder.busy()))return juce::Result::fail("Pause transport and finish recording before saving external plug-in state");
 try{auto snapshot=captureSession();
  pendingSavedSessionState=sessionFingerprint(snapshot);
  return sessionWorker->save(std::move(snapshot),file)?juce::Result::ok():juce::Result::fail("Cannot start session save");
 }catch(const std::exception& e){return juce::Result::fail(e.what());}
}
juce::Result MainComponent::loadSessionFile(const juce::File& file){
 if(nativeResourceLoadBusy())return juce::Result::fail("Wait for resource loading to finish");
 if(sessionWorker->isBusy())return juce::Result::fail("Wait for the current session operation");
 if(vocals.recorder.busy()||playing()||importer->hasPendingWork())return juce::Result::fail("Pause and APPLY or CLEAR pending imports before recalling");
 prepareOfflineSession();hem::SessionWorker::Context context;
 {const GraphControlGuard lock(*this);context={prepared?sampleRate:0,blockSize,generation};}
 return sessionWorker->load(file,context)?juce::Result::ok():juce::Result::fail("Cannot start session recall");
}
juce::Result MainComponent::clearTrackSources(bool all){
 if(nativeResourceLoadBusy())return juce::Result::fail("Wait for resource loading to finish");
 if(playing()||vocals.recorder.busy()||sessionWorker->isBusy()||exportWorker->isBusy()||importer->hasPendingWork())return juce::Result::fail("Pause and finish pending recording, session, import or export work first");
 prepareOfflineSession();
 std::array<std::unique_ptr<hem::StemPlayback>,trackCount> replacement;
 for(int i=0;i<trackCount;++i)if(all||i==selectedTrack){replacement[size_t(i)]=std::make_unique<hem::StemPlayback>();replacement[size_t(i)]->transport->reprepare(sampleRate,blockSize);}
 {const GraphControlGuard lock(*this);
  for(int i=0;i<trackCount;++i)if(replacement[size_t(i)]){auto& t=bank->tracks[size_t(i)];t.playback.swap(replacement[size_t(i)]);t.loaded=false;t.name="EMPTY TRACK";t.sourceRate=0;t.signalMeters.prepare(sampleRate);}
  ++generation;
 }
 closeSpectralOperation();for(int i=0;i<trackCount;++i)if(all||i==selectedTrack)spectralHistory[size_t(i)].reset();
 // Retire readers here on the stopped message thread, never in the callback lock.
 message.setText(all?"All source audio unloaded / inserts and gains retained":"Selected source unloaded / inserts and gain retained",juce::dontSendNotification);
 return juce::Result::ok();
}
juce::Result MainComponent::resetSessionConfirmed(){
 if(nativeResourceLoadBusy())return juce::Result::fail("Wait for resource loading to finish");
 if(playing()||vocals.recorder.busy()||sessionWorker->isBusy()||exportWorker->isBusy()||importer->hasPendingWork())return juce::Result::fail("Pause and finish pending work before a new session");
 prepareOfflineSession();auto empty=std::make_unique<hem::TrackBank>();for(auto& t:empty->tracks)t.prepare(sampleRate,blockSize);
 closePluginEditor();
 {const GraphControlGuard lock(*this);bank.swap(empty);inputDb=0;outputDb=0;projectBpm=120;looping=false;++generation;}
 tempoControl.setValue(120,juce::dontSendNotification);
 monitoringRouter.setHeadphoneMode(false);monitorProcessor.state.getParameter("enabled")->setValueNotifyingHost(0.f);
 inputKnob.setValue(0,juce::dontSendNotification);outputKnob.setValue(0,juce::dontSendNotification);selectTrack(0);selectSlot(0);
 clipHistory.reset();for(auto& history:spectralHistory)history.reset();currentSessionFile=juce::File();lastSavedSessionState=sessionFingerprint(captureSession(false));message.setText("New empty session / source files unchanged",juce::dontSendNotification);return juce::Result::ok();
}

juce::ValueTree MainComponent::sessionFingerprint(hem::SessionSnapshot snapshot) const {
 snapshot.positionSeconds=0; // Transport motion alone is not an unsaved edit.
 for(auto& track:snapshot.tracks)for(auto& slot:track.slots)slot.external.state.reset();
 auto encoded=hem::SessionState::encode(snapshot,juce::File::getCurrentWorkingDirectory().getChildFile("__HEM_state_comparison__.xml"));
 for(auto track:encoded)if(track.hasType("TRACK")){const int ti=int(track["index"]);for(auto slot:track){const int si=int(slot["index"]);if(ti>=0&&ti<13&&si>=0&&si<8&&snapshot.tracks[size_t(ti)].slots[size_t(si)].pluginId=="external-vst3")slot.setProperty("externalRevision",juce::String(snapshot.tracks[size_t(ti)].slots[size_t(si)].externalRevision),nullptr);}}
 return encoded;
}
bool MainComponent::hasUnsavedSessionChanges(){
 try{return !lastSavedSessionState.isValid()||!sessionFingerprint(captureSession(false)).isEquivalentTo(lastSavedSessionState);}
 catch(const std::exception&){return true;} // Pending publication is conservatively dirty.
}
void MainComponent::sessionFinished(bool saving,const juce::File& file,const juce::Result& result){
 if(result.wasOk()){
  currentSessionFile=file;
  if(saving)lastSavedSessionState=pendingSavedSessionState.createCopy();
  else lastSavedSessionState=sessionFingerprint(captureSession(false));
 }
 if(saving&&closePhase==ClosePhase::saving){if(result.wasOk())closePhase=ClosePhase::complete;else abortClose("Save failed; Studio remains open: "+result.getErrorMessage());}
}
bool MainComponent::activeCloseWork() const {
 if(nativeResourceLoadBusy()||externalLoadPending||playing()||vocals.recorder.busy()||exportWorker->isBusy()||sessionWorker->isBusy()||importer->hasPendingWork())return true;
 for(const auto& t:bank->tracks)for(int i=0;i<8;++i)if(t.chain.isBusy(i))return true;
 return false;
}
void MainComponent::requestClose(std::function<void()> approved){
 if(closePhase!=ClosePhase::idle)return;
 const bool dirty=hasUnsavedSessionChanges(),active=activeCloseWork();
 if(!dirty&&!active){if(approved)approved();return;}
 closeApproved=std::move(approved);closePhase=ClosePhase::prompt;
 juce::Component::SafePointer<MainComponent> safe(this);
 auto resolved=[safe](CloseChoice choice){if(safe)safe->resolveCloseChoice(choice);};
 if(closePrompt){closePrompt(dirty,active,std::move(resolved));return;}
 if(dirty){
  juce::AlertWindow::showYesNoCancelBox(juce::MessageBoxIconType::WarningIcon,"Close Studio?",
   "This session has unsaved edits. Save before closing? Playback and recording will stop; pending import/export will be cancelled. Source WAV files and completed exports are not deleted.",
   "Save and close","Discard and close","Keep open",this,juce::ModalCallbackFunction::create([resolved](int result){resolved(result==1?CloseChoice::save:result==2?CloseChoice::discard:CloseChoice::cancel);}));
 }else{
  juce::AlertWindow::showOkCancelBox(juce::MessageBoxIconType::WarningIcon,"Close Studio?",
   "Work is active. Stop recording/playback, cancel pending import/export, finish any session write, and close? Completed files are preserved.","Stop work and close","Keep open",this,
   juce::ModalCallbackFunction::create([resolved](int result){resolved(result==1?CloseChoice::discard:CloseChoice::cancel);}));
 }
}
void MainComponent::abortClose(const juce::String& reason){closePhase=ClosePhase::idle;closeApproved={};closeNeedsSave=false;closeWasRecording=false;setEnabled(true);message.setText(reason,juce::dontSendNotification);}
void MainComponent::resolveCloseChoice(CloseChoice choice){
 if(closePhase!=ClosePhase::prompt)return;
 if(choice==CloseChoice::cancel){abortClose("Close cancelled / session unchanged");return;}
 cancelDrumSampleLoad();closePluginEditor();
 closeNeedsSave=choice==CloseChoice::save;closeWasRecording=vocals.recorder.busy();closePhase=ClosePhase::waiting;setEnabled(false);
 if(externalLoadPending){externalLoadPending=false;++externalLoadRequest;picker.reset();juce::PopupMenu::dismissAllActiveMenus();}
 prepareOfflineSession();stopAll(false);exportWorker->cancel();importer->cancelPendingWork();
 message.setText("Finishing recording/session work and cancelling pending export/import before close...",juce::dontSendNotification);
}
void MainComponent::pumpCloseRequest(){
 if(closePhase==ClosePhase::complete){auto approved=std::move(closeApproved);closePhase=ClosePhase::idle;setEnabled(true);if(approved)approved();return;}
 if(closePhase!=ClosePhase::waiting||activeCloseWork())return;
 if(closeWasRecording&&vocals.recorder.state()==hem::TakeRecorder::State::Failed){abortClose("Recording could not finalize; Studio remains open: "+vocals.recorder.result());return;}
 if(!closeNeedsSave||!hasUnsavedSessionChanges()){closePhase=ClosePhase::complete;return;}
 if(currentSessionFile!=juce::File()){
  closePhase=ClosePhase::saving;const auto result=saveSessionFile(currentSessionFile);if(result.failed())abortClose("Save failed; Studio remains open: "+result.getErrorMessage());return;
 }
 closePhase=ClosePhase::choosingSave;
 picker=std::make_unique<juce::FileChooser>("Save session before closing",juce::File(),"*.xml;*.json");juce::Component::SafePointer<MainComponent> safe(this);
 picker->launchAsync(juce::FileBrowserComponent::saveMode|juce::FileBrowserComponent::canSelectFiles|juce::FileBrowserComponent::warnAboutOverwriting,[safe](const juce::FileChooser& chooser){
  if(!safe||safe->closePhase!=ClosePhase::choosingSave)return;
  if(chooser.getResult()==juce::File()){safe->abortClose("Save cancelled; Studio remains open");return;}
  safe->closePhase=ClosePhase::saving;const auto result=safe->saveSessionFile(chooser.getResult());if(result.failed())safe->abortClose("Save failed; Studio remains open: "+result.getErrorMessage());
 });
}

bool MainComponent::hasExternalInserts() const {
 if(!bank)return false;
 for(const auto& track:bank->tracks)for(int slot=0;slot<8;++slot)
  if(auto* p=track.chain.activeProcessor(slot);p&&p->isExternal())return true;
 return false;
}
hem::SessionSnapshot MainComponent::captureSession(bool includeExternalState){
 struct CaptureGates {
  std::vector<std::pair<hem::ModuleProcessor*,bool>> held;
  ~CaptureGates(){for(auto& entry:held)entry.first->gateForStateCapture(entry.second);}
 } gates;
 if(includeExternalState&&hasExternalInserts()){
  if(playing()||vocals.recorder.busy())throw std::runtime_error("Pause playback and finish recording before capturing external VST3 state");
  gates.held.reserve(13*8);
  // Existing callback exclusion establishes that no previously-entered process
  // call remains. Expensive vendor serialization happens only after unlocking.
  {const GraphControlGuard lock(*this);
   for(auto& track:bank->tracks)for(int i=0;i<8;++i){
    if(track.chain.isBusy(i))throw std::runtime_error("Wait for insert transitions before capturing VST3 state");
    if(auto* p=track.chain.activeProcessor(i);p&&p->isExternal())gates.held.emplace_back(p,p->gateForStateCapture(true));
   }
  }
 }
 auto snapshot=hem::SessionState::capture(*bank,inputDb.load(),outputDb.load(),sessionPosition(),looping,monitorProcessor,includeExternalState);snapshot.tempoBpm=projectBpm.load();captureClipEdits(snapshot);return snapshot;
}
juce::Result MainComponent::stageExternalSession(hem::TrackBank& staged,const hem::SessionSnapshot& snapshot,hem::SessionWorker::Context context){
 if(closePhase==ClosePhase::waiting||closePhase==ClosePhase::complete)return juce::Result::fail("Session recall cancelled while closing; existing session retained");
 {const GraphControlGuard lock(*this);if(context.generation!=generation||context.sampleRate!=sampleRate||context.blockSize!=blockSize)return juce::Result::fail("Device changed during recall; existing session retained");}
 for(size_t track=0;track<snapshot.tracks.size();++track)for(int slot=0;slot<8;++slot){
  const auto& saved=snapshot.tracks[track].slots[size_t(slot)];if(saved.pluginId!="external-vst3")continue;
  juce::String error;auto& chain=staged.tracks[track].chain;
  auto processor=externalHost.stage(saved.external,context.sampleRate,context.blockSize,chain.slotLatency(),false,error);
  if(!processor)return juce::Result::fail("VST3 recall unchanged: "+error);
  auto node=chain.stageNode(std::move(processor),context.sampleRate,context.blockSize,chain.slotLatency());
  chain.installInitial(slot,std::move(node),saved.bypassed);
 }
 return juce::Result::ok();
}
juce::Result MainComponent::installExternal(const hem::ExternalPluginState& state,int trackIndex,int slotIndex,unsigned expectedGeneration){
 if(nativeResourceLoadBusy())return juce::Result::fail("Wait for resource loading to finish");
 if(trackIndex<0||trackIndex>=13||slotIndex<0||slotIndex>=8)return juce::Result::fail("Invalid VST3 destination");
 if(closePhase!=ClosePhase::idle||playing()||vocals.recorder.busy()||sessionWorker->isBusy()||exportWorker->isBusy()||importer->hasPendingWork())return juce::Result::fail("Pause and finish pending work before loading a VST3");
 auto& chain=bank->tracks[size_t(trackIndex)].chain;
 double rate;int block,budget;
 {const GraphControlGuard lock(*this);
  if(!prepared||generation!=expectedGeneration||chain.isBusy(slotIndex))return juce::Result::fail("Device/session changed or slot is transitioning; existing insert retained");
  rate=sampleRate;block=blockSize;budget=chain.slotLatency();
 }
 try{
  juce::String error;auto processor=externalHost.stage(state,rate,block,budget,false,error);
  if(!processor)return juce::Result::fail(error);
  auto node=chain.stageNode(std::move(processor),rate,block,budget);
  bool installed=false;std::unique_ptr<hem::InsertChain::PreparedNode> retired;
  {const GraphControlGuard lock(*this);
   if(prepared&&generation==expectedGeneration&&!chain.isBusy(slotIndex)){
    const bool offline=deviceManager.getCurrentAudioDevice()==nullptr;
    installed=offline?chain.publishStopped(slotIndex,std::move(node)):chain.publish(slotIndex,std::move(node));
    if(offline&&installed)retired=chain.collectRetired(slotIndex);
   }
  }
  if(retired&&editedProcessor==retired->processor.get())closePluginEditor();
  if(!installed)return juce::Result::fail("Device changed while loading VST3; existing insert retained");
  message.setText("VST3 loaded / stereo effect / "+juce::String(budget)+" sample maximum slot latency / pending transition if device active",juce::dontSendNotification);
  return juce::Result::ok();
 }catch(const std::exception& e){return juce::Result::fail(e.what());}
}
void MainComponent::chooseExternalPlugin(){
 if(nativeResourceLoadBusy()){message.setText("Wait for resource loading to finish",juce::dontSendNotification);return;}
 if(externalLoadPending||closePhase!=ClosePhase::idle)return;
 if(playing()||vocals.recorder.busy()||sessionWorker->isBusy()||exportWorker->isBusy()||importer->hasPendingWork()){
  message.setText("Pause and finish pending work before loading a trusted VST3",juce::dontSendNotification);return;
 }
 const unsigned request=++externalLoadRequest;unsigned version;{const GraphControlGuard lock(*this);version=generation;}const int targetTrack=selectedTrack,targetSlot=selected;
 externalLoadPending=true;message.setText("Select one trusted VST3. Discovery executes native code in this process; no automatic folder scan.",juce::dontSendNotification);
 picker=std::make_unique<juce::FileChooser>("Select one trusted stereo-effect VST3 / native code / 6 ms slot limit",juce::File(),"*.vst3");
 juce::Component::SafePointer<MainComponent> safe(this);
 picker->launchAsync(juce::FileBrowserComponent::openMode|juce::FileBrowserComponent::canSelectFiles|juce::FileBrowserComponent::canSelectDirectories,
  [safe,request,version,targetTrack,targetSlot](const juce::FileChooser& chooser){
   if(!safe||safe->externalLoadRequest!=request)return;
   const auto selectedFile=chooser.getResult();
   if(selectedFile==juce::File()){safe->externalLoadPending=false;safe->message.setText("VST3 selection cancelled / insert unchanged",juce::dontSendNotification);return;}
   juce::MessageManager::callAsync([safe,request,version,targetTrack,targetSlot,selectedFile]{
    if(!safe||safe->externalLoadRequest!=request)return;
    juce::String error;auto descriptions=safe->externalHost.discoverSelected(selectedFile,error);
    if(descriptions.isEmpty()){safe->externalLoadPending=false;safe->message.setText(error,juce::dontSendNotification);return;}
    auto selectedType=[safe,request,version,targetTrack,targetSlot,descriptions](int result){
     if(!safe||safe->externalLoadRequest!=request)return;
     safe->externalLoadPending=false;
     if(result<1||result>descriptions.size()){safe->message.setText("VST3 type selection cancelled / insert unchanged",juce::dontSendNotification);return;}
     hem::ExternalPluginState state;state.descriptionXml=descriptions.getReference(result-1).createXml()->toString();
     const auto installed=safe->installExternal(state,targetTrack,targetSlot,version);
     if(installed.failed())safe->message.setText(installed.getErrorMessage(),juce::dontSendNotification);
    };
    if(descriptions.size()==1){selectedType(1);return;}
    juce::PopupMenu menu;for(int i=0;i<descriptions.size();++i)menu.addItem(i+1,descriptions.getReference(i).name);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(safe->workspace.get()),std::move(selectedType));
   });
  });
}
void MainComponent::reloadExternalPlugin(){
 if(nativeResourceLoadBusy()){message.setText("Wait for resource loading to finish",juce::dontSendNotification);return;}
 if(playing()||vocals.recorder.busy()||sessionWorker->isBusy()||exportWorker->isBusy()||externalLoadPending){message.setText("Pause and finish pending work before reloading VST3",juce::dontSendNotification);return;}
 auto* processor=current().chain.activeProcessor(selected);
 if(!processor||!processor->isExternal()){message.setText("Select an existing VST3 slot to reload",juce::dontSendNotification);return;}
 try{
  auto snapshot=captureSession();unsigned version;{const GraphControlGuard lock(*this);version=generation;}const auto result=installExternal(snapshot.tracks[size_t(selectedTrack)].slots[size_t(selected)].external,selectedTrack,selected,version);
  if(result.failed())message.setText("Reload rejected; previous state retained: "+result.getErrorMessage(),juce::dontSendNotification);
 }catch(const std::exception& e){message.setText(e.what(),juce::dontSendNotification);}
}









