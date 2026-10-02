#include "MainComponent.h"
#include <BinaryData.h>
#include "UI/StudioBranding.h"
namespace {
juce::String timeText(double t){int n=int(std::max(0.0,t));return juce::String(n/60)+":"+juce::String(n%60).paddedLeft('0',2);}
}
MainComponent::MainComponent(){
 setLookAndFeel(&look);setOpaque(true);addAndMakeVisible(outputMeterView);
 faceplate=juce::ImageCache::getFromMemory(BinaryData::studio_suite_master_png,BinaryData::studio_suite_master_pngSize);
 ghostBrand=juce::ImageCache::getFromMemory(BinaryData::ghost_brand_reference_png,BinaryData::ghost_brand_reference_pngSize);
 formats.registerBasicFormats();diskThread.startThread();configureKnob(inputKnob,inputDb,"INPUT");configureKnob(outputKnob,outputDb,"MASTER HEADROOM / OUTPUT");
 for(auto* b:std::array<juce::TextButton*,7>{&play,&stop,&rewind,&forward,&loop,&file,&settings}){addAndMakeVisible(*b);b->setLookAndFeel(&overlayLook);}
 play.setButtonText("PLAY");stop.setButtonText("STOP");rewind.setButtonText("REWIND");forward.setButtonText("+10 SEC");loop.setButtonText("LOOP");file.setButtonText("FILE");settings.setButtonText("I/O");
 play.setTooltip("Play all loaded tracks");stop.setTooltip("Stop all and rewind");rewind.setTooltip("Rewind all");forward.setTooltip("Forward 10 seconds");loop.setTooltip("Loop tracks individually");file.setTooltip("Load selected track");settings.setTooltip("Audio devices");
 file.onClick=[this]{openAudio();};settings.onClick=[this]{showSettings();};play.onClick=[this]{playAll();};stop.onClick=[this]{stopAll(true);};
 rewind.onClick=[this]{seekAll(0);};forward.onClick=[this]{seekAll(sessionPosition()+10);};
 loop.onClick=[this]{
    if(vocals.recorder.busy()){message.setText("Finish the take before changing loop mode",juce::dontSendNotification);return;}
    if(sessionWorker&&sessionWorker->isBusy())return;
    const bool resume=playing();const double commonPosition=sessionPosition();stopAll(false);looping=!looping;
    for(auto& t:bank->tracks)if(t.loaded){const auto position=t.transport().getCurrentPosition();t.transport().setSource(nullptr);t.playback->reader->setLooping(looping);t.transport().setSource(t.playback->reader.get(),32768,&diskThread,t.sourceRate,2);t.transport().setPosition(position);}
    seekAll(commonPosition);if(resume)playAll();repaint();
 };
 for(int i=0;i<8;++i){addAndMakeVisible(slotButtons[size_t(i)]);slotButtons[size_t(i)].onClick=[this,i]{selectSlot(i);openPlugin(i);};}
 addAndMakeVisible(bay);
 importer=std::make_unique<hem::StemFileImporter>(diskThread,[this]{
    const juce::ScopedLock lock(deviceManager.getAudioCallbackLock());
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
 analysisButton.onClick=[this]{showAnalysis();};
 routeButton.onClick=[this]{monitoringRouter.setHeadphoneMode(!monitoringRouter.headphoneMode());};
 calibrationButton.onClick=[this]{openCalibration();};
 profileLoader=std::make_unique<hem::MonitorProfileLoader>(
    [this](const hem::MonitorProfile& p,const hem::MonitoringRouter::Curve& curve,unsigned gen){
        bool installed=false;
        {const juce::ScopedLock lock(deviceManager.getAudioCallbackLock());if(prepared&&generation==gen)installed=monitoringRouter.installPrepared(curve);}
        if(installed){calibrationName=p.name+" / "+p.device+" / "+p.connection+" / "+p.deviceMode;message.setText("Calibration loaded; select HEADPHONE CORRECTION to audition",juce::dontSendNotification);}
        else message.setText("Device changed or profile transition busy; load calibration again",juce::dontSendNotification);
    },[this](const juce::String& error){message.setText(error,juce::dontSendNotification);});
 saveSessionButton.onClick=[this]{chooseSession(true);};loadSessionButton.onClick=[this]{chooseSession(false);};
 for(auto* c:std::array<juce::Component*,15>{&pauseButton,&outputButton,&monitorButton,&moduleMenu,&trackMenu,&insert,&remove,&bypass,&rackButton,&openButton,&mute,&solo,&trackTrim,&title,&message})bay.addAndMakeVisible(*c);
 const auto& registry=hem::PluginRegistry::entries();for(size_t i=0;i<registry.size();++i)moduleMenu.addItem(registry[i].name,int(i)+1);moduleMenu.setSelectedId(1);
 for(int i=0;i<trackCount;++i)trackMenu.addItem("TRACK "+juce::String(i+1),i+1);trackMenu.setSelectedId(1);
 trackMenu.onChange=[this]{selectTrack(trackMenu.getSelectedId()-1);};
 insert.onClick=[this]{installSelected(moduleMenu.getSelectedId()-1);};remove.onClick=[this]{installSelected(-1);};bypass.onClick=[this]{current().chain.bypass(selected,!current().chain.isBypassed(selected));};
 pauseButton.onClick=[this]{pauseAll();};outputButton.onClick=[this]{showSettings();};
 monitorButton.onClick=[this]{showMonitor();};
 rackButton.onClick=[this]{showRack();};openButton.onClick=[this]{openPlugin(selected);};
 mute.onClick=[this]{current().muted.store(!current().muted.load());};solo.onClick=[this]{current().solo.store(!current().solo.load());};
 trackTrim.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);trackTrim.setTextBoxStyle(juce::Slider::TextBoxBelow,false,100,24);trackTrim.setRange(-60,6,.1);trackTrim.setValue(0);trackTrim.setTextValueSuffix(" dB / TRACK");trackTrim.setDoubleClickReturnValue(true,0);
 trackTrim.onValueChange=[this]{current().trimDb.store(float(trackTrim.getValue()));};
 for(auto* l:std::array<juce::Label*,4>{&track,&telemetry,&inValue,&outValue}){addAndMakeVisible(*l);l->setJustificationType(juce::Justification::centred);l->setColour(juce::Label::textColourId,juce::Colours::cyan);l->setInterceptsMouseClicks(false,false);}
 hem::NativeWorkspace::Callbacks workspaceCallbacks;
 workspaceCallbacks.bank=[this]() -> hem::TrackBank& {return *bank;};
 workspaceCallbacks.getSelectedTrack=[this]{return selectedTrack;};workspaceCallbacks.getSelectedSlot=[this]{return selected;};
 workspaceCallbacks.setSelectedTrack=[this](int index){selectTrack(index);};workspaceCallbacks.setSelectedSlot=[this](int slot){selected=juce::jlimit(0,7,slot);};
 workspaceCallbacks.onOpenSlot=[this](int slot){openPlugin(slot);};workspaceCallbacks.onInstallModule=[this](int module){installSelected(module);};workspaceCallbacks.onMonitor=[this]{showMonitor();};
 workspaceCallbacks.onLoadVst3=[this]{chooseExternalPlugin();};workspaceCallbacks.onReloadVst3=[this]{reloadExternalPlugin();};
 workspaceCallbacks.routeTelemetry=[this]{const juce::ScopedLock lock(deviceManager.getAudioCallbackLock());hem::NativeWorkspace::RouteTelemetry t;t.output=outputMeters.snapshot();t.headphoneCorrection=monitoringRouter.headphoneMode();t.monitorReady=monitorRouteReady.load();t.calibrationName=calibrationName;t.sampleRate=prepared?sampleRate:0;t.blockSize=blockSize;t.limiterLatencySamples=masterLimiter.latencySamples();if(auto* d=deviceManager.getCurrentAudioDevice()){t.outputDevice=d->getName();t.hardwareOutputs=d->getActiveOutputChannels().countNumberOfSetBits();}else{t.outputDevice="NO AUDIO DEVICE / OFFLINE EDITING";t.hardwareOutputs=0;}return t;};
 workspace=std::make_unique<hem::NativeWorkspace>(std::move(workspaceCallbacks));addChildComponent(*workspace);
 addAndMakeVisible(statusBar);statusBar.setColour(juce::Label::backgroundColourId,juce::Colour(0xff061017));statusBar.setColour(juce::Label::textColourId,juce::Colour(0xffffca7c));message.setVisible(false);
 addChildComponent(utilityViewport);addChildComponent(utilityTitle);utilityViewport.setScrollBarsShown(true,true);utilityTitle.setColour(juce::Label::textColourId,juce::Colour(0xffffbb55));utilityTitle.setColour(juce::Label::backgroundColourId,juce::Colour(0xff061017));
 const std::array<const char*,6> names{"HOME","MIXER","PLUGIN","PLUGIN VAULT","ROUTING","SETTINGS"};
 for(size_t i=0;i<navigation.size();++i){auto& nav=navigation[i];addAndMakeVisible(nav);nav.setButtonText(names[i]);nav.setComponentID("navigation-"+juce::String(int(i)));nav.onClick=[this,i]{if(i==2)openPlugin(selected);else showShellPage(static_cast<ShellPage>(i));};}
 addAndMakeVisible(unloadSourcesButton);addAndMakeVisible(newSessionButton);
 unloadSourcesButton.setTooltip("Unload source audio without deleting WAV files; insert chains and gains are retained");
 unloadSourcesButton.onClick=[this]{juce::PopupMenu menu;menu.addItem(1,"Unload selected source / keep inserts and gain");menu.addItem(2,"Unload all sources / keep inserts and gains");juce::Component::SafePointer<MainComponent> safe(this);menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&unloadSourcesButton),[safe](int choice){if(safe&&(choice==1||choice==2)){const auto result=safe->clearTrackSources(choice==2);if(result.failed())safe->message.setText(result.getErrorMessage(),juce::dontSendNotification);}});};
 newSessionButton.setTooltip("Start a new empty session after confirmation; source files are not deleted");newSessionButton.onClick=[this]{requestNewSession();};
 setSize(1536,864);setAudioChannels(0,2);auto setup=deviceManager.getAudioDeviceSetup();if(auto* d=deviceManager.getCurrentAudioDevice()){
 const auto rates=d->getAvailableSampleRates();if(rates.contains(hem::defaultSampleRate))setup.sampleRate=hem::defaultSampleRate;
 const auto blocks=d->getAvailableBufferSizes();if(blocks.contains(256))setup.bufferSize=256;
 }
 deviceError=deviceManager.setAudioDeviceSetup(setup,true);if(!deviceManager.getCurrentAudioDevice())deviceManager.restartLastAudioDevice();prepareOfflineSession();startTimerHz(20);selectSlot(0);lastSavedSessionState=sessionFingerprint(captureSession(false));
}
MainComponent::~MainComponent(){
 stopTimer();utilityViewport.setViewedComponent(nullptr,false);utilityContent=nullptr;embeddedExport.reset();embeddedVocal.reset();embeddedMonitor.reset();embeddedAnalysis.reset();closePluginEditor();workspace.reset();exportWindow.reset();exportWorker.reset();picker.reset();profileLoader.reset();analysisWindow.reset();sessionWorker.reset();importer.reset();shutdownAudio();vocalWindow.reset();pluginWindow.reset();rackWindow.reset();monitorWindow.reset();deviceSettings.reset();
 for(auto& t:bank->tracks){t.transport().setSource(nullptr);t.playback->reader.reset();}diskThread.stopThread(5000);bank.reset();externalHost.collectGarbage();
 for(auto* c:std::array<juce::Component*,9>{&inputKnob,&outputKnob,&play,&stop,&rewind,&forward,&loop,&file,&settings})c->setLookAndFeel(nullptr);setLookAndFeel(nullptr);
}
void MainComponent::configureKnob(juce::Slider& knob,std::atomic<float>& value,const juce::String& name){
 addAndMakeVisible(knob);knob.setLookAndFeel(&overlayLook);knob.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);knob.setTextBoxStyle(juce::Slider::NoTextBox,false,0,0);
 knob.setRange(-60,6,.1);knob.setValue(0,juce::dontSendNotification);knob.setDoubleClickReturnValue(true,0);knob.setMouseDragSensitivity(250);knob.setName(name);knob.setTooltip(name+" gain / double-click 0 dB");
 auto* p=&value;auto* s=&knob;knob.onValueChange=[p,s]{p->store(float(s->getValue()),std::memory_order_relaxed);};
}
void MainComponent::prepareToPlay(int block,double rate){
 auto* inputDevice=deviceManager.getCurrentAudioDevice();hardwareOutputs=inputDevice?inputDevice->getActiveOutputChannels().countNumberOfSetBits():0;vocals.prepare(rate,std::max(1,block),inputDevice?inputDevice->getActiveInputChannels().countNumberOfSetBits():0);
 monitoringRouter.prepare(rate);analyser.prepare(rate);outputMeters.prepare(inputDevice?rate:0);masterLimiter.prepare(rate,std::max(1,block));sampleRate=rate;blockSize=std::max(1,block);++generation;sum.setSize(2,blockSize);listeningSource.setSize(2,blockSize);monitorBuffer.setSize(2,blockSize);monitorRouteReady.store(false);monitorProcessor.prepareToPlay(rate,blockSize);inputValues.assign(size_t(blockSize),1);
 for(auto& t:bank->tracks)t.prepare(rate,blockSize);inputGain.reset(rate,hem::gain(inputDb.load()));outputGain.reset(rate,hem::gain(outputDb.load()));prepared=true;
}
void MainComponent::prepareOfflineSession(){
 const juce::ScopedLock lock(deviceManager.getAudioCallbackLock());
 if(prepared||deviceManager.getCurrentAudioDevice())return;
 // No device callback exists here. Rebind playing transports without stop()'s
 // callback acknowledgement wait, retaining a common source timeline.
 const double position=sessionPosition();
 for(auto& t:bank->tracks)if(t.loaded&&t.transport().isPlaying()){
  t.transport().setSource(t.playback->reader.get(),65536,&diskThread,t.sourceRate,2);
  t.transport().setPosition(std::min(position,t.transport().getLengthInSeconds()));
 }
 prepareToPlay(256,hem::defaultSampleRate);
 for(auto& t:bank->tracks)t.chain.completePendingStopped();
 outputMeters.prepare(0);
}
void MainComponent::closePluginEditor(){if(workspace)workspace->clearEditor();pluginWindow.reset();editedProcessor=nullptr;}
void MainComponent::releaseResources(){outputMeters.prepare(0);vocals.release();monitorProcessor.releaseResources();for(auto& t:bank->tracks)t.release();prepared=false;++generation;}
void MainComponent::getNextAudioBlock(const juce::AudioSourceChannelInfo& info){
 juce::ScopedNoDenormals guard;if(!info.buffer)return;if(!prepared||info.buffer->getNumChannels()==0){info.clearActiveBufferRegion();return;}
 inputGain.set(hem::gain(inputDb.load()));outputGain.set(hem::gain(outputDb.load()));float maximum=0;bool anySolo=false;for(auto& t:bank->tracks)anySolo|=t.solo.load();
 for(int offset=0;offset<info.numSamples;offset+=blockSize){const int n=std::min(blockSize,info.numSamples-offset),start=info.startSample+offset;
    vocals.capture(*info.buffer,start,n,callbackTimeline());
    sum.clear();for(int i=0;i<n;++i)inputValues[size_t(i)]=inputGain.next();
    for(auto& t:bank->tracks)t.render(sum,n,anySolo,inputValues.data());
    // Master result remains in sum. Monitoring touches only this copy.
    // An offline exporter must consume the master path, not monitorBuffer.
    for(int i=0;i<n;++i){const float g=outputGain.next();sum.setSample(0,i,sum.getSample(0,i)*g);sum.setSample(1,i,sum.getSample(1,i)*g);}
    // Analysis is tapped before all listening correction. Audio only copies
    // into a bounded queue; FFT and waveform decimation run on its worker.
    analyser.push(sum.getReadPointer(0),sum.getReadPointer(1),n);
    for(int channel=0;channel<2;++channel)monitorBuffer.copyFrom(channel,0,sum,channel,0,n);
    vocals.addMonitor(monitorBuffer,n);
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
 if(!deviceManager.getCurrentAudioDevice()){message.setText("No audio device: editing, sessions and export remain available",juce::dontSendNotification);return;}
 if(exportWorker&&exportWorker->isBusy()){message.setText("Finish or cancel the offline export before playback",juce::dontSendNotification);return;}

 if(vocals.recorder.busy()&&!playing()){message.setText("Start backing playback before recording, or finish the current take",juce::dontSendNotification);return;}
 if(sessionWorker&&sessionWorker->isBusy())return;
 if(playing())return;
 bool loaded=false;double duration=0;for(const auto& t:bank->tracks)if(t.loaded){loaded=true;duration=std::max(duration,t.transport().getLengthInSeconds());}
 if(!loaded){openAudio();return;}
 const juce::ScopedLock lock(deviceManager.getAudioCallbackLock());
 double position=sessionPosition();if(position>=duration)position=0;
 for(auto& t:bank->tracks)if(t.loaded){t.transport().setPosition(std::min(position,t.transport().getLengthInSeconds()));t.transport().start();}
}
void MainComponent::pauseAll(){
 double position;
 {const juce::ScopedLock lock(deviceManager.getAudioCallbackLock());position=sessionPosition();}
 // JUCE stop() waits for its callback fade. It must run OUTSIDE the device
 // lock. Afterwards restore all stems to the same captured pause position.
 stopAll(false);seekAll(position);
}
void MainComponent::stopAll(bool rewindAll){
 {const juce::ScopedLock lock(deviceManager.getAudioCallbackLock());vocals.stopTake();}
 // stop() can wait for the callback, so never hold the device lock here.
 for(auto& t:bank->tracks)t.transport().stop();if(rewindAll)seekAll(0);
}
void MainComponent::seekAll(double position){const juce::ScopedLock lock(deviceManager.getAudioCallbackLock());vocals.stopTake();for(auto& t:bank->tracks)if(t.loaded)t.transport().setPosition(std::min(position,t.transport().getLengthInSeconds()));}
void MainComponent::installSelected(int module){
 if(sessionWorker&&sessionWorker->isBusy())return;
 const auto& entries=hem::PluginRegistry::entries();
 if(module>=0&&module<int(entries.size())&&entries[size_t(module)].monitoringOnly){showMonitor();message.setText("Monitor EQ is listening-only; track insert unchanged",juce::dontSendNotification);return;}
 auto& chain=current().chain;if(chain.isBusy(selected)){message.setText("Insert transition in progress",juce::dontSendNotification);return;}
 double rate;int block,budget;unsigned version;
 {const juce::ScopedLock lock(deviceManager.getAudioCallbackLock());if(!prepared)return;rate=sampleRate;block=blockSize;budget=chain.slotLatency();version=generation;}
 try{auto node=chain.stageNode(hem::createModule(module),rate,block,budget);bool success=false,offline=false;
    std::unique_ptr<hem::InsertChain::PreparedNode> retired;
    {const juce::ScopedLock lock(deviceManager.getAudioCallbackLock());if(prepared&&generation==version){
      offline=deviceManager.getCurrentAudioDevice()==nullptr;
      success=offline?chain.publishStopped(selected,std::move(node)):chain.publish(selected,std::move(node));
      if(offline&&success)retired=chain.collectRetired(selected);
    }}
    if(retired){if(editedProcessor==retired->processor.get())closePluginEditor();retired->processor->releaseResources();}
    message.setText(success?"TRACK "+juce::String(selectedTrack+1)+" / SLOT "+juce::String(selected+1)+" / "+(module>=0&&module<int(entries.size())?juce::String(entries[size_t(module)].name):juce::String("EMPTY"))+(offline?" / updated offline":" / queued; open after transition"):"Device changed; insert again",juce::dontSendNotification);
 }catch(const std::exception& e){message.setText(e.what(),juce::dontSendNotification);}
}
void MainComponent::selectTrack(int i){if(i<0||i>=trackCount)return;if(i!=selectedTrack)closePluginEditor();selectedTrack=i;trackMenu.setSelectedId(i+1,juce::dontSendNotification);trackTrim.setValue(current().trimDb.load(),juce::dontSendNotification);if(workspace)workspace->refresh();}
void MainComponent::selectSlot(int i){selected=juce::jlimit(0,7,i);showShellPage(ShellPage::home);}
void MainComponent::openPlugin(int slot){
 selected=juce::jlimit(0,7,slot);auto* p=current().chain.activeProcessor(selected);closePluginEditor();
 if(p){editedProcessor=p;editedTrack=selectedTrack;editedSlot=selected;workspace->setEditor(std::unique_ptr<juce::AudioProcessorEditor>(p->createEditor()));}
 else message.setText("Empty insert: select a module in PLUGIN VAULT",juce::dontSendNotification);
 showShellPage(ShellPage::plugin);
}
void MainComponent::moveSlot(int from,int to){
 if(playing()){message.setText("Stop playback before changing insert order",juce::dontSendNotification);return;}
 closePluginEditor();bool moved;
 {const juce::ScopedLock lock(deviceManager.getAudioCallbackLock());moved=current().chain.swapSlotsStopped(from,to);}
 if(moved)selected=to;message.setText(moved?"Insert order updated":"Wait for insert transition",juce::dontSendNotification);
}
void MainComponent::showRack(){openPlugin(selected);}
void MainComponent::timerCallback(){
 prepareOfflineSession();
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
 {const juce::ScopedLock lock(deviceManager.getAudioCallbackLock());for(int t=0;t<trackCount;++t)for(int i=0;i<8;++i){active[size_t(t)][size_t(i)]=bank->tracks[size_t(t)].chain.activeProcessor(i);retired[size_t(t)][size_t(i)]=bank->tracks[size_t(t)].chain.collectRetired(i);}}
 // Snapshot before retirement. Close any editor that still refers to an old
 // processor before the corresponding unique_ptr leaves this scope.
 if(editedProcessor&&active[size_t(editedTrack)][size_t(editedSlot)]!=editedProcessor){closePluginEditor();}
 for(int t=0;t<trackCount;++t)for(int i=0;i<8;++i)if(retired[size_t(t)][size_t(i)])retired[size_t(t)][size_t(i)]->processor->releaseResources();
 auto& nodes=active[size_t(selectedTrack)];auto& chain=current().chain;auto* p=nodes[size_t(selected)];const bool running=playing();
 play.setToggleState(running,juce::dontSendNotification);loop.setToggleState(looping,juce::dontSendNotification);
 for(int i=0;i<8;++i){auto* node=nodes[size_t(i)];const auto name=juce::String(i+1)+" / "+(node?node->getName():juce::String("EMPTY"));slotButtons[size_t(i)].setButtonText(name);
    if(rackView){auto& row=rackView->rows[size_t(i)];row.name.setText((selected==i?"> ":"  ")+name,juce::dontSendNotification);row.bypass.setToggleState(chain.isBypassed(i),juce::dontSendNotification);row.up.setEnabled(!running&&i>0&&!chain.isBusy(i));row.down.setEnabled(!running&&i<7&&!chain.isBusy(i));row.remove.setEnabled(node&&!chain.isBusy(i));}}
 if(rackView){rackView->tracks.setSelectedId(selectedTrack+1,juce::dontSendNotification);rackView->status.setText("SELECTED INSERT "+juce::String(selected+1)+" / "+(running?"Stop transport to reorder":"UP / DOWN changes order")+" / "+message.getText(),juce::dontSendNotification);}
 title.setText("TRACK "+juce::String(selectedTrack+1)+" / INSERT "+juce::String(selected+1)+" / "+(p?p->getName():juce::String("EMPTY")),juce::dontSendNotification);
 bypass.setToggleState(chain.isBypassed(selected),juce::dontSendNotification);insert.setEnabled(!chain.isBusy(selected));remove.setEnabled(p&&!chain.isBusy(selected));openButton.setEnabled(p!=nullptr);
 mute.setToggleState(current().muted.load(),juce::dontSendNotification);solo.setToggleState(current().solo.load(),juce::dontSendNotification);
 track.setText(current().name+"\n"+timeText(sessionPosition())+" / "+timeText(current().transport().getLengthInSeconds()),juce::dontSendNotification);
 inValue.setText("INPUT "+juce::String(inputDb.load(),1)+" dB",juce::dontSendNotification);outValue.setText("OUTPUT "+juce::String(outputDb.load(),1)+" dB",juce::dontSendNotification);
 if(auto* d=deviceManager.getCurrentAudioDevice())telemetry.setText("AUDIO OUTPUT\n"+d->getName()+"\n"+juce::String(d->getCurrentSampleRate()/1000,1)+" kHz / "+juce::String(d->getCurrentBufferSizeSamples())+" frames",juce::dontSendNotification);else telemetry.setText("NO AUDIO DEVICE",juce::dontSendNotification);
 telemetry.setTooltip(deviceError+" / Task 2B master / output sample peak "+juce::String(masterLimiter.samplePeakDb(),2)+" dBFS / latency "+juce::String(masterLimiter.latencySamples())+" samples / reconstructed true-peak certification pending");
 vocalButton.setButtonText(vocals.recorder.busy()?"VOCAL / ACTIVE":"VOCAL TRACKING");vocalButton.setToggleState(vocals.recorder.busy(),juce::dontSendNotification);
 if(workspace&&workspace->isVisible()){
  switch(workspace->page()){case hem::NativeWorkspace::Page::plugin:shellPage=ShellPage::plugin;break;case hem::NativeWorkspace::Page::mixer:shellPage=ShellPage::mixer;break;case hem::NativeWorkspace::Page::vault:shellPage=ShellPage::vault;break;case hem::NativeWorkspace::Page::routing:shellPage=ShellPage::routing;break;}
 }
 for(size_t i=0;i<navigation.size();++i)navigation[i].setToggleState(shellPage==static_cast<ShellPage>(i),juce::dontSendNotification);
 if(deviceSettings)deviceSettings->setDeviceChangesAllowed(!running&&!vocals.recorder.busy()&&!exportWorker->isBusy());
 const bool canClear=!running&&!vocals.recorder.busy()&&!sessionWorker->isBusy()&&!exportWorker->isBusy()&&!importer->hasPendingWork();unloadSourcesButton.setEnabled(canClear);newSessionButton.setEnabled(canClear);
 const bool sessionBusy=sessionWorker&&sessionWorker->isBusy();
 routeButton.setButtonText(monitoringRouter.headphoneMode()?"HEADPHONE CORRECTION":"DIRECT OUTPUT");routeButton.setToggleState(monitoringRouter.headphoneMode(),juce::dontSendNotification);routeButton.setTooltip(calibrationName);
 statusBar.setText(message.getText(),juce::dontSendNotification);statusBar.setTooltip(message.getText());
 saveSessionButton.setEnabled(!sessionBusy);loadSessionButton.setEnabled(!sessionBusy);importer->setEnabled(!sessionBusy);
 juce::String externalFault;
 for(int ti=0;ti<trackCount&&externalFault.isEmpty();++ti)for(int si=0;si<8&&externalFault.isEmpty();++si)
  if(auto* node=active[size_t(ti)][size_t(si)];node&&node->isExternal()&&!node->processingReady())
   externalFault="TRACK "+juce::String(ti+1)+" / SLOT "+juce::String(si+1)+" / "+node->externalDiagnostic();
 if(externalFault!=lastExternalDiagnostic){lastExternalDiagnostic=externalFault;if(externalFault.isNotEmpty())message.setText(externalFault+" / select this slot and RELOAD CURRENT VST3",juce::dontSendNotification);}
 externalHost.collectGarbage();
 repaint();pumpCloseRequest();
}
void MainComponent::openAudio(){if(sessionWorker&&sessionWorker->isBusy())return;const int destination=selectedTrack;picker=std::make_unique<juce::FileChooser>("Load selected track",juce::File(),"*.wav");juce::Component::SafePointer<MainComponent> safe(this);
 picker->launchAsync(juce::FileBrowserComponent::openMode|juce::FileBrowserComponent::canSelectFiles,[safe,destination](const juce::FileChooser& f){if(safe&&f.getResult().existsAsFile())safe->loadAudio(f.getResult(),destination);});
}
void MainComponent::loadAudio(const juce::File& f,int index){
 if(sessionWorker&&sessionWorker->isBusy())return;
 juce::StringArray files;files.add(f.getFullPathName());importer->submitFiles(files,index);
}
juce::Result MainComponent::commitImport(hem::StemFileImporter::Batch& batch){
 if(vocals.recorder.busy())return juce::Result::fail("Finish vocal recording before replacing backing stems");
 if(sessionWorker&&sessionWorker->isBusy())return juce::Result::fail("Wait for session operation");
 if(playing())return juce::Result::fail("Pause or stop playback, then APPLY. Current audio is unchanged.");
 if(batch.firstTrack<0||batch.count<1||batch.firstTrack+batch.count>trackCount)return juce::Result::fail("Invalid track mapping");
 for(int i=0;i<batch.count;++i)if(!batch.stems[size_t(i)])return juce::Result::fail("Incomplete batch; import again");
 {
    const juce::ScopedLock lock(deviceManager.getAudioCallbackLock());
    if(!prepared||generation!=batch.context.generation||looping!=batch.context.looping)
        return juce::Result::fail("Output device or loop mode changed. CLEAR and import again.");
    // Already prepared sources swap ownership atomically as a group. No file
    // opening, resampler preparation, reader destruction or GUI work here.
    for(int i=0;i<batch.count;++i)bank->tracks[size_t(batch.firstTrack+i)].playback.swap(batch.stems[size_t(i)]);
 }
 for(int i=0;i<batch.count;++i){auto& t=bank->tracks[size_t(batch.firstTrack+i)];t.loaded=true;t.name=t.playback->file.getFileName();t.sourceRate=t.playback->sourceRate;}
 seekAll(0);message.setText(juce::String(batch.count)+" WAV stems applied / all tracks rewound / insert chains retained",juce::dontSendNotification);
 return juce::Result::ok();
}
void MainComponent::showMonitor(){
 monitoringRouter.setHeadphoneMode(true);if(!embeddedMonitor)embeddedMonitor.reset(monitorProcessor.createEditor());
 showUtility(*embeddedMonitor,"LISTENING-ONLY MONITOR EQ / EXCLUDED FROM EXPORT");
}
void MainComponent::showSettings(){showShellPage(ShellPage::settings);}
juce::Rectangle<int> MainComponent::map(float x,float y,float w,float h) const {return juce::Rectangle<float>(face.getX()+x/1536*face.getWidth(),face.getY()+y/864*face.getHeight(),w/1536*face.getWidth(),h/864*face.getHeight()).toNearestInt();}
void MainComponent::resized(){const float scale=std::min(float(getWidth())/1536,float(getHeight())/864);face={(getWidth()-1536*scale)*.5f,(getHeight()-864*scale)*.5f,1536*scale,864*scale};
 outputMeterView.setBounds(map(1310,280,190,350));statusBar.setBounds(map(304,627,994,28));statusBar.setFont(juce::Font(juce::FontOptions(std::max(10.f,13*scale))));
 inputKnob.setBounds(map(338,696,94,98));outputKnob.setBounds(map(1100,696,102,98));rewind.setBounds(map(502,725,101,55));stop.setBounds(map(612,725,90,55));play.setBounds(map(716,725,102,55));forward.setBounds(map(829,725,99,55));loop.setBounds(map(944,725,87,55));file.setBounds(map(53,25,62,44));settings.setBounds(map(196,25,91,44));
 track.setBounds(map(52,700,236,85));telemetry.setBounds(map(1257,700,231,85));inValue.setBounds(map(311,797,147,22));outValue.setBounds(map(1078,797,147,22));
 for(int i=0;i<8;++i)slotButtons[size_t(i)].setBounds(map(43,145.f+i*56.f,195,45));bay.setBounds(map(304,136,994,479));
 auto nav=map(304,96,994,34);for(size_t i=0;i<navigation.size();++i){const int x=nav.getX()+nav.getWidth()*int(i)/6,next=nav.getX()+nav.getWidth()*int(i+1)/6;navigation[i].setBounds(x,nav.getY(),next-x-3,nav.getHeight());}
 unloadSourcesButton.setBounds(map(43,105,95,32));newSessionButton.setBounds(map(143,105,95,32));
 const auto content=map(43,141,1255,474);if(workspace)workspace->setBounds(content);if(deviceSettings)deviceSettings->setBounds(content);
 auto utility=content;utilityTitle.setBounds(utility.removeFromTop(28));utilityViewport.setBounds(utility);
 if(utilityContent)utilityContent->setSize(std::max(utilityMinimum.x,utility.getWidth()-utilityViewport.getScrollBarThickness()),std::max(utilityMinimum.y,utility.getHeight()-utilityViewport.getScrollBarThickness()));
 const float ui=float(bay.getWidth())/994.f;
 auto b=bay.getLocalBounds().reduced(int(12*ui));title.setBounds(b.removeFromTop(int(36*ui)));auto row=b.removeFromTop(int(44*ui));trackMenu.setBounds(row.removeFromLeft(row.getWidth()/3).reduced(3));rackButton.setBounds(row.removeFromLeft(row.getWidth()/2).reduced(3));openButton.setBounds(row.reduced(3));
 row=b.removeFromTop(int(44*ui));moduleMenu.setBounds(row.removeFromLeft(row.getWidth()/2).reduced(3));insert.setBounds(row.reduced(3));row=b.removeFromTop(int(44*ui));remove.setBounds(row.removeFromLeft(row.getWidth()/3).reduced(3));bypass.setBounds(row.removeFromLeft(row.getWidth()/2).reduced(3));monitorButton.setBounds(row.reduced(3));
 row=b.removeFromTop(int(44*ui));mute.setBounds(row.removeFromLeft(row.getWidth()/4).reduced(3));solo.setBounds(row.removeFromLeft(row.getWidth()/3).reduced(3));pauseButton.setBounds(row.removeFromLeft(row.getWidth()/2).reduced(3));outputButton.setBounds(row.reduced(3));row=b.removeFromTop(int(38*ui));saveSessionButton.setBounds(row.removeFromLeft(row.getWidth()/5).reduced(3));loadSessionButton.setBounds(row.removeFromLeft(row.getWidth()/4).reduced(3));analysisButton.setBounds(row.removeFromLeft(row.getWidth()/3).reduced(3));calibrationButton.setBounds(row.removeFromLeft(row.getWidth()/2).reduced(3));vocalButton.setBounds(row.reduced(3));
 row=b.removeFromTop(int(32*ui));exportButton.setBounds(row.removeFromRight(row.getWidth()/3).reduced(3));routeButton.setBounds(row.reduced(3));message.setBounds({});importer->setBounds(b.removeFromBottom(int(100*ui)));trackTrim.setBounds(b);
}
void MainComponent::paint(juce::Graphics& g){g.fillAll(juce::Colour(0xff020508));g.drawImage(faceplate,face,juce::RectanglePlacement::stretchToFit);
 // The archived artwork had an unwired Edit label. Hide it rather than promise an editor.
 g.setColour(juce::Colour(0xff061117));g.fillRoundedRectangle(map(125,25,70,44).toFloat(),4);
 // The native title bar owns working minimize/maximize/close actions. Hide their
 // decorative duplicates printed on the historical artwork.
 g.fillRoundedRectangle(map(1323,24,189,49).toFloat(),4);
 hem::ui::StudioBranding::draw(g,face,ghostBrand);
 if(bay.isVisible()){g.setColour(juce::Colour(0xe0080c12));g.fillRoundedRectangle(bay.getBounds().toFloat(),12);g.setColour(juce::Colour(0xff00cdda));g.drawRoundedRectangle(bay.getBounds().toFloat(),12,1.5f);}
}

void MainComponent::chooseSession(bool saving){
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
 if(!staged||vocals.recorder.busy()||playing())return juce::Result::fail("Session unchanged: stop playback before recalling");
 // Close every editor borrowing a processor from the bank BEFORE retirement.
 closePluginEditor();
 {
    const juce::ScopedLock lock(deviceManager.getAudioCallbackLock());
    if(!prepared||generation!=context.generation)return juce::Result::fail("Session unchanged: audio device changed during recall; load again");
    bank.swap(staged);inputDb.store(state.inputDb);outputDb.store(state.outputDb);looping=state.looping;
    // A source batch prepared for the previous session must not be committed.
    ++generation;
 }
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
    {const juce::ScopedLock lock(safe->deviceManager.getAudioCallbackLock());if(!safe->prepared)return;rate=safe->sampleRate;gen=safe->generation;}
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
    [this]{if(exportWorker&&exportWorker->isBusy())return juce::String("Finish or cancel the offline export before recording");if(looping)return juce::String("Disable backing loops before recording a continuous take");if(sessionWorker->isBusy())return juce::String("Wait for session recall to finish");return juce::String();});
 showUtility(*embeddedVocal,"LIVE VOCAL RECORDING / TRACKING");
}

void MainComponent::showExport(){
 if(!embeddedExport)embeddedExport=std::make_unique<hem::OfflineMixPanel>(*exportWorker,[this](const juce::File& file,const hem::OfflineMixOptions& options){return startExport(file,options);});
 showUtility(*embeddedExport,"OFFLINE MIX EXPORT / STEREO FLOAT32 WAV");
}
void MainComponent::showAnalysis(){if(!embeddedAnalysis)embeddedAnalysis=std::make_unique<hem::AnalysisView>(analyser);showUtility(*embeddedAnalysis,"MASTER WAVEFORM / SPECTRUM / PRE-LISTENING TAP");}
void MainComponent::showUtility(juce::Component& content,const juce::String& name){
 utilityContent=&content;utilityMinimum={std::max(600,content.getWidth()),std::max(350,content.getHeight())};utilityViewport.setViewedComponent(&content,false);utilityTitle.setText(name,juce::dontSendNotification);showShellPage(ShellPage::utility);
}
void MainComponent::showShellPage(ShellPage page){
 shellPage=page;bay.setVisible(page==ShellPage::home);for(auto& b:slotButtons)b.setVisible(page==ShellPage::home);
 const bool work=page==ShellPage::plugin||page==ShellPage::mixer||page==ShellPage::vault||page==ShellPage::routing;
 if(workspace){workspace->setVisible(work);if(work){const auto target=page==ShellPage::mixer?hem::NativeWorkspace::Page::mixer:page==ShellPage::vault?hem::NativeWorkspace::Page::vault:page==ShellPage::routing?hem::NativeWorkspace::Page::routing:hem::NativeWorkspace::Page::plugin;workspace->setPage(target);}}
 if(page==ShellPage::settings&&!deviceSettings){deviceSettings=std::make_unique<hem::DeviceSettingsPanel>(deviceManager);addChildComponent(*deviceSettings);}
 if(deviceSettings){deviceSettings->setVisible(page==ShellPage::settings);deviceSettings->setDeviceChangesAllowed(!playing()&&!vocals.recorder.busy()&&!exportWorker->isBusy());}
 utilityViewport.setVisible(page==ShellPage::utility);utilityTitle.setVisible(page==ShellPage::utility);
 for(size_t i=0;i<navigation.size();++i)navigation[i].setToggleState(page==static_cast<ShellPage>(i),juce::dontSendNotification);
 resized();repaint();
}
void MainComponent::requestNewSession(){
 juce::Component::SafePointer<MainComponent> safe(this);
 juce::AlertWindow::showAsync(juce::MessageBoxOptions().withIconType(juce::MessageBoxIconType::WarningIcon).withTitle("Start a new session?").withMessage("This clears unsaved track sources, inserts and mix settings. Source WAV files and saved sessions are not deleted.").withButton("Start new").withButton("Cancel"),[safe](int button){if(safe&&button==1){const auto result=safe->resetSessionConfirmed();if(result.failed())safe->message.setText(result.getErrorMessage(),juce::dontSendNotification);}});
}
juce::Result MainComponent::startExport(const juce::File& destination,const hem::OfflineMixOptions& options){
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
 if(sessionWorker->isBusy())return juce::Result::fail("Wait for the current session operation");
 if(hasExternalInserts()&&(playing()||vocals.recorder.busy()))return juce::Result::fail("Pause transport and finish recording before saving external plug-in state");
 try{auto snapshot=captureSession();
  pendingSavedSessionState=sessionFingerprint(snapshot);
  return sessionWorker->save(std::move(snapshot),file)?juce::Result::ok():juce::Result::fail("Cannot start session save");
 }catch(const std::exception& e){return juce::Result::fail(e.what());}
}
juce::Result MainComponent::loadSessionFile(const juce::File& file){
 if(sessionWorker->isBusy())return juce::Result::fail("Wait for the current session operation");
 if(vocals.recorder.busy()||playing()||importer->hasPendingWork())return juce::Result::fail("Pause and APPLY or CLEAR pending imports before recalling");
 prepareOfflineSession();hem::SessionWorker::Context context;
 {const juce::ScopedLock lock(deviceManager.getAudioCallbackLock());context={prepared?sampleRate:0,blockSize,generation};}
 return sessionWorker->load(file,context)?juce::Result::ok():juce::Result::fail("Cannot start session recall");
}
juce::Result MainComponent::clearTrackSources(bool all){
 if(playing()||vocals.recorder.busy()||sessionWorker->isBusy()||exportWorker->isBusy()||importer->hasPendingWork())return juce::Result::fail("Pause and finish pending recording, session, import or export work first");
 prepareOfflineSession();
 std::array<std::unique_ptr<hem::StemPlayback>,trackCount> replacement;
 for(int i=0;i<trackCount;++i)if(all||i==selectedTrack){replacement[size_t(i)]=std::make_unique<hem::StemPlayback>();replacement[size_t(i)]->transport.prepareToPlay(blockSize,sampleRate);}
 {const juce::ScopedLock lock(deviceManager.getAudioCallbackLock());
  for(int i=0;i<trackCount;++i)if(replacement[size_t(i)]){auto& t=bank->tracks[size_t(i)];t.playback.swap(replacement[size_t(i)]);t.loaded=false;t.name="EMPTY TRACK";t.sourceRate=0;t.signalMeters.prepare(sampleRate);}
  ++generation;
 }
 // Retire readers here on the stopped message thread, never in the callback lock.
 message.setText(all?"All source audio unloaded / inserts and gains retained":"Selected source unloaded / inserts and gain retained",juce::dontSendNotification);
 return juce::Result::ok();
}
juce::Result MainComponent::resetSessionConfirmed(){
 if(playing()||vocals.recorder.busy()||sessionWorker->isBusy()||exportWorker->isBusy()||importer->hasPendingWork())return juce::Result::fail("Pause and finish pending work before a new session");
 prepareOfflineSession();auto empty=std::make_unique<hem::TrackBank>();for(auto& t:empty->tracks)t.prepare(sampleRate,blockSize);
 closePluginEditor();
 {const juce::ScopedLock lock(deviceManager.getAudioCallbackLock());bank.swap(empty);inputDb=0;outputDb=0;looping=false;++generation;}
 monitoringRouter.setHeadphoneMode(false);monitorProcessor.state.getParameter("enabled")->setValueNotifyingHost(0.f);
 inputKnob.setValue(0,juce::dontSendNotification);outputKnob.setValue(0,juce::dontSendNotification);selectTrack(0);selectSlot(0);
 currentSessionFile=juce::File();lastSavedSessionState=sessionFingerprint(captureSession(false));message.setText("New empty session / source files unchanged",juce::dontSendNotification);return juce::Result::ok();
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
 if(externalLoadPending||playing()||vocals.recorder.busy()||exportWorker->isBusy()||sessionWorker->isBusy()||importer->hasPendingWork())return true;
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
  {const juce::ScopedLock lock(deviceManager.getAudioCallbackLock());
   for(auto& track:bank->tracks)for(int i=0;i<8;++i){
    if(track.chain.isBusy(i))throw std::runtime_error("Wait for insert transitions before capturing VST3 state");
    if(auto* p=track.chain.activeProcessor(i);p&&p->isExternal())gates.held.emplace_back(p,p->gateForStateCapture(true));
   }
  }
 }
 return hem::SessionState::capture(*bank,inputDb.load(),outputDb.load(),sessionPosition(),looping,monitorProcessor,includeExternalState);
}
juce::Result MainComponent::stageExternalSession(hem::TrackBank& staged,const hem::SessionSnapshot& snapshot,hem::SessionWorker::Context context){
 if(closePhase==ClosePhase::waiting||closePhase==ClosePhase::complete)return juce::Result::fail("Session recall cancelled while closing; existing session retained");
 {const juce::ScopedLock lock(deviceManager.getAudioCallbackLock());if(context.generation!=generation||context.sampleRate!=sampleRate||context.blockSize!=blockSize)return juce::Result::fail("Device changed during recall; existing session retained");}
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
 if(trackIndex<0||trackIndex>=13||slotIndex<0||slotIndex>=8)return juce::Result::fail("Invalid VST3 destination");
 if(closePhase!=ClosePhase::idle||playing()||vocals.recorder.busy()||sessionWorker->isBusy()||exportWorker->isBusy()||importer->hasPendingWork())return juce::Result::fail("Pause and finish pending work before loading a VST3");
 auto& chain=bank->tracks[size_t(trackIndex)].chain;
 double rate;int block,budget;
 {const juce::ScopedLock lock(deviceManager.getAudioCallbackLock());
  if(!prepared||generation!=expectedGeneration||chain.isBusy(slotIndex))return juce::Result::fail("Device/session changed or slot is transitioning; existing insert retained");
  rate=sampleRate;block=blockSize;budget=chain.slotLatency();
 }
 try{
  juce::String error;auto processor=externalHost.stage(state,rate,block,budget,false,error);
  if(!processor)return juce::Result::fail(error);
  auto node=chain.stageNode(std::move(processor),rate,block,budget);
  bool installed=false;std::unique_ptr<hem::InsertChain::PreparedNode> retired;
  {const juce::ScopedLock lock(deviceManager.getAudioCallbackLock());
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
 if(externalLoadPending||closePhase!=ClosePhase::idle)return;
 if(playing()||vocals.recorder.busy()||sessionWorker->isBusy()||exportWorker->isBusy()||importer->hasPendingWork()){
  message.setText("Pause and finish pending work before loading a trusted VST3",juce::dontSendNotification);return;
 }
 const unsigned request=++externalLoadRequest;unsigned version;{const juce::ScopedLock lock(deviceManager.getAudioCallbackLock());version=generation;}const int targetTrack=selectedTrack,targetSlot=selected;
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
 if(playing()||vocals.recorder.busy()||sessionWorker->isBusy()||exportWorker->isBusy()||externalLoadPending){message.setText("Pause and finish pending work before reloading VST3",juce::dontSendNotification);return;}
 auto* processor=current().chain.activeProcessor(selected);
 if(!processor||!processor->isExternal()){message.setText("Select an existing VST3 slot to reload",juce::dontSendNotification);return;}
 try{
  auto snapshot=captureSession();unsigned version;{const juce::ScopedLock lock(deviceManager.getAudioCallbackLock());version=generation;}const auto result=installExternal(snapshot.tracks[size_t(selectedTrack)].slots[size_t(selected)].external,selectedTrack,selected,version);
  if(result.failed())message.setText("Reload rejected; previous state retained: "+result.getErrorMessage(),juce::dontSendNotification);
 }catch(const std::exception& e){message.setText(e.what(),juce::dontSendNotification);}
}
