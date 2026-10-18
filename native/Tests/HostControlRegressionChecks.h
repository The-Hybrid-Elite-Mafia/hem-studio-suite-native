#pragma once

// Included after the real host helpers and the silent in-process device fixture.
// These call production button/slider callbacks and exercise the real ownership
// timer. No physical audio device or native file chooser is opened by this file.
namespace hem {
struct VocalTrackingPanelTestAccess {
 static void tick(VocalTrackingPanel& panel){panel.timerCallback();}
 static bool chooserOpen(const VocalTrackingPanel& panel){return panel.chooser!=nullptr;}
 static unsigned request(VocalTrackingPanel& panel){return ++panel.takeChooserRequest;}
 static unsigned displayedGeneration(const VocalTrackingPanel& panel){return panel.displayedInputGeneration;}
 static juce::AudioIODevice* displayedDevice(const VocalTrackingPanel& panel){return panel.displayedInputDevice;}
 static bool matches(VocalTrackingPanel& panel,unsigned generation,juce::AudioIODevice* device,int input){return panel.recordingSourceMatches(generation,device,input);}
 static void complete(VocalTrackingPanel& panel,const juce::File& file,unsigned request,unsigned generation,juce::AudioIODevice* device,int input){panel.completeTakeChoice(file,request,generation,device,input);}
 static const juce::String& notice(const VocalTrackingPanel& panel){return panel.notice;}
};
}
namespace hostControlChecks {
template<class T>T& control(juce::Component& parent,const juce::String& id){
 auto* found=dynamic_cast<T*>(findControl(parent,id));require(found!=nullptr,"Expected host control missing");return *found;
}
inline void monitorOwnership(MainComponent& host){
 auto& workspace=MainComponentTestAccess::workspace(host);int index=-1;
 for(size_t i=0;i<hem::PluginRegistry::entries().size();++i)if(hem::PluginRegistry::entries()[i].monitoringOnly)index=int(i);
 require(index>=0,"Monitor registry entry missing");
 for(bool afterInsert:{false,true})for(int entry=0;entry<3;++entry){
  MainComponentTestAccess::home(host);
  if(afterInsert){MainComponentTestAccess::install(host,0);MainComponentTestAccess::open(host);require(MainComponentTestAccess::hasEditor(host),"Insert editor precondition failed");}
  auto openMonitor=[&]{
   if(entry==0){auto* open=button(host,"MONITOR EQ");require(open!=nullptr,"Home Monitor EQ button missing");open->onClick();}
   else if(entry==1)MainComponentTestAccess::insertMenu(host,index);
   else {MainComponentTestAccess::navigate(host,3);control<juce::Button>(workspace,"vault-install-"+juce::String(index)).onClick();}
  };
  openMonitor();auto* editor=activeEditor(workspace);require(editor!=nullptr&&MainComponentTestAccess::monitorEditor(host),"Monitor was registered as a track insert editor");
  juce::Component::SafePointer<juce::AudioProcessorEditor> alive(editor);
  for(int tick=0;tick<6;++tick){MainComponentTestAccess::tick(host);require(alive&&activeEditor(workspace)==editor,"Host timer closed or retired Monitor EQ");}
  openMonitor();require(alive&&activeEditor(workspace)==editor,"Repeated Monitor EQ entry discarded the active editor");
  if(afterInsert){MainComponentTestAccess::install(host,-1);MainComponentTestAccess::tick(host);require(alive&&activeEditor(workspace)==editor,"Unrelated insert retirement closed Monitor EQ");}
  workspace.backFromEditor();require(!alive&&MainComponentTestAccess::clearEditorTarget(host),"Monitor Back retained editor ownership or stale indices");
  MainComponentTestAccess::tick(host);
 }
 MainComponentTestAccess::install(host,0);MainComponentTestAccess::open(host);MainComponentTestAccess::corruptInsertEditorIndices(host);
 MainComponentTestAccess::tick(host);require(MainComponentTestAccess::clearEditorTarget(host),"Invalid insert editor indices were not safely retired");
 MainComponentTestAccess::install(host,-1);MainComponentTestAccess::home(host);
}
inline void trackTrim(MainComponent& host){
 auto& workspace=MainComponentTestAccess::workspace(host);auto& knob=MainComponentTestAccess::trim(host);auto& track=MainComponentTestAccess::bank(host).tracks[0];
 MainComponentTestAccess::navigate(host,1);auto& fader=control<juce::Slider>(workspace,"mixer-gain-0");
 fader.setValue(-17.3,juce::sendNotificationSync);require(std::abs(track.trimDb.load()+17.3f)<.001f,"Mixer callback did not update actual trim");
 MainComponentTestAccess::navigate(host,0);require(std::abs(knob.getValue()+17.3)<.001,"Home navigation retained stale Mixer trim before first timer");
 fader.setValue(-12.4,juce::sendNotificationSync);MainComponentTestAccess::tick(host);require(std::abs(knob.getValue()+12.4)<.001,"Home polling failed to reflect the actual mixer gain");
 knob.onDragStart();track.trimDb=-7.1f;MainComponentTestAccess::tick(host);require(std::abs(knob.getValue()+12.4)<.001,"Polling overwrote an active trim drag");
 knob.setValue(-8.2,juce::sendNotificationSync);knob.onDragEnd();require(std::abs(track.trimDb.load()+8.2f)<.001f,"Trim drag completion did not preserve the real user value");
 juce::Label* text=nullptr;for(auto* child:knob.getChildren())if(auto* label=dynamic_cast<juce::Label*>(child))text=label;
 require(text!=nullptr,"Home trim text control missing");text->showEditor();auto* typing=text->getCurrentTextEditor();require(typing!=nullptr,"Home trim text editor unavailable");
 typing->setText("-6.3",false);track.trimDb=-4.2f;MainComponentTestAccess::tick(host);require(typing->getText()=="-6.3","Timer erased an unfinished trim text edit");
 text->hideEditor(false);require(std::abs(track.trimDb.load()+6.3f)<.001f,"Home trim text commit did not reach engine");
 const double previous=knob.getValue();require(knob.keyPressed(juce::KeyPress(juce::KeyPress::upKey)),"Home trim rejected keyboard increment");
 require(knob.getValue()>previous&&std::abs(track.trimDb.load()-float(knob.getValue()))<.001f,"Home trim keyboard edit did not reach engine");
 workspace.setPage(hem::NativeWorkspace::Page::mixer);workspace.refresh();require(std::abs(fader.getValue()-knob.getValue())<.001,"Mixer did not reflect Home trim edits");
 knob.setValue(0,juce::sendNotificationSync);MainComponentTestAccess::home(host);
}
inline void projectTempo(MainComponent& host,const juce::File& directory){
 auto& knob=MainComponentTestAccess::tempo(host);MainComponentTestAccess::home(host);
 knob.setValue(97.5,juce::sendNotificationSync);require(MainComponentTestAccess::bpm(host)==97.5,"Project tempo UI did not reach production atomic");
 juce::Label* text=nullptr;for(auto* child:knob.getChildren())if(auto* label=dynamic_cast<juce::Label*>(child))text=label;
 require(text!=nullptr,"Project tempo text control missing");text->showEditor();auto* typing=text->getCurrentTextEditor();require(typing!=nullptr,"Project tempo text editor missing");
 typing->setText("103.25",false);MainComponentTestAccess::tick(host);require(typing->getText()=="103.25","Host timer erased BPM text edit");
 text->hideEditor(false);require(MainComponentTestAccess::bpm(host)==103.25,"Project tempo text commit missed engine");
 const auto before=knob.getValue();require(knob.keyPressed(juce::KeyPress(juce::KeyPress::upKey)),"Project tempo keyboard increment rejected");
 require(knob.getValue()>before&&MainComponentTestAccess::bpm(host)==knob.getValue(),"Project tempo keyboard edit missed engine");
 knob.setValue(97.5,juce::sendNotificationSync);const auto captured=MainComponentTestAccess::snapshot(host);
 knob.setValue(143.25,juce::sendNotificationSync);require(captured.tempoBpm==97.5&&MainComponentTestAccess::snapshot(host).tempoBpm==143.25,"Actual host capture did not freeze project BPM");
 for(bool json:{false,true}){
  const auto file=directory.getChildFile(json?"tempo-trim.json":"tempo-trim.xml");auto saved=captured;saved.tracks[0].gainDb=-11.7f;
  if(json)hem::PresetManager::save(saved,file);else hem::SessionState::save(saved,file);require(file.existsAsFile(),"Tempo/trim fixture serialization failed");
  require(MainComponentTestAccess::load(host,file).wasOk(),"Tempo/trim recall rejected");until([&]{return !MainComponentTestAccess::sessionBusy(host);},"Tempo/trim recall timed out");
  require(MainComponentTestAccess::snapshot(host).tempoBpm==97.5&&knob.getValue()==97.5,"XML/JSON recall lost nondefault BPM/UI synchronization");
  require(std::abs(MainComponentTestAccess::trim(host).getValue()+11.7)<.001,"XML/JSON recall lost Home trim synchronization");
 }
}
inline void bypassAndOrder(MainComponent& host){
 auto& workspace=MainComponentTestAccess::workspace(host);auto& chain=MainComponentTestAccess::bank(host).tracks[0].chain;
 auto& homeBypass=control<juce::Button>(host,"home-insert-bypass");auto& pluginBypass=control<juce::Button>(workspace,"plugin-bypass-0");
 MainComponentTestAccess::slot(host,0);MainComponentTestAccess::install(host,-1);MainComponentTestAccess::tick(host);workspace.refresh();
 require(!homeBypass.isEnabled()&&!homeBypass.getToggleState()&&!pluginBypass.isEnabled()&&!pluginBypass.getToggleState(),"Empty-slot bypass presentation differs between views");
 homeBypass.onClick();pluginBypass.onClick();require(!chain.isBypassed(0),"Stale empty-slot bypass callback pre-bypassed a fresh slot");
 MainComponentTestAccess::install(host,0);MainComponentTestAccess::tick(host);require(!chain.isBypassed(0)&&homeBypass.isEnabled(),"Fresh insert inherited an empty-slot bypass click");
 homeBypass.onClick();workspace.refresh();require(chain.isBypassed(0)&&pluginBypass.getToggleState(),"Home bypass did not reach Plugin view");
 MainComponentTestAccess::install(host,-1);MainComponentTestAccess::tick(host);workspace.refresh();
 require(!homeBypass.isEnabled()&&!homeBypass.getToggleState()&&!pluginBypass.getToggleState(),"Removed-slot bypass stayed visibly active");
 homeBypass.onClick();pluginBypass.onClick();require(chain.isBypassed(0),"Empty callback changed retained per-slot state");
 MainComponentTestAccess::install(host,0);MainComponentTestAccess::tick(host);require(chain.isBypassed(0),"Replacement changed existing stored bypass semantics");
 homeBypass.onClick();require(!chain.isBypassed(0),"Populated slot failed to unbypass");
 auto pending=chain.stageNode(hem::createModule(2),48000,256,chain.slotLatency());require(chain.publish(0,std::move(pending)),"Busy insert setup failed");
 MainComponentTestAccess::tick(host);workspace.refresh();require(!homeBypass.isEnabled()&&!pluginBypass.isEnabled(),"Busy bypass controls remain enabled");
 homeBypass.onClick();pluginBypass.onClick();require(!chain.isBypassed(0),"Busy callback changed bypass state");
 MainComponentTestAccess::move(host,0,1);require(chain.activeProcessor(1)==nullptr,"Reorder accepted a busy source");
 chain.completePendingStopped();MainComponentTestAccess::tick(host);
 MainComponentTestAccess::open(host);auto* editor=activeEditor(workspace);require(editor!=nullptr,"Reorder editor precondition failed");
 juce::Component::SafePointer<juce::AudioProcessorEditor> alive(editor);MainComponentTestAccess::showRack(host);
 auto& up=control<juce::Button>(workspace,"plugin-slot-up");auto& down=control<juce::Button>(workspace,"plugin-slot-down");
 require(!alive&&!workspace.hasEditor()&&visibleWithin(host,up)&&visibleWithin(host,down),"Home INSERT RACK did not reveal actual reorder controls");
 require(!up.isEnabled()&&down.isEnabled(),"Reorder first-slot boundary/occupancy state incorrect");
 auto* hit=host.getComponentAt(host.getLocalPoint(&down,down.getLocalBounds().getCentre()));require(hit==&down||down.isParentOf(hit),"Actual rack DOWN control root target is obscured");
 auto* original=chain.activeProcessor(0);hostParameterChecks::click(down,down.getLocalBounds().getCentre());require(chain.activeProcessor(1)==original&&chain.activeProcessor(0)==nullptr&&MainComponentTestAccess::slot(host)==1,"DOWN did not move real node and selection together");
 up.onClick();require(chain.activeProcessor(0)==original&&MainComponentTestAccess::slot(host)==0,"UP did not restore real insert order");
 MainComponentTestAccess::slot(host,7);require(!down.isEnabled()&&!up.isEnabled(),"Empty/end slot reorder controls remained actionable");
 MainComponentTestAccess::move(host,-1,0);MainComponentTestAccess::move(host,0,8);require(chain.activeProcessor(0)==original,"Invalid reorder changed the chain");
 MainComponentTestAccess::slot(host,0);
 auto destinationPending=chain.stageNode(hem::createModule(0),48000,256,chain.slotLatency());require(chain.publish(1,std::move(destinationPending)),"Busy destination setup failed");
 MainComponentTestAccess::move(host,0,1);require(chain.activeProcessor(0)==original&&chain.activeProcessor(1)==nullptr,"Host reorder accepted busy destination");
 chain.completePendingStopped();MainComponentTestAccess::tick(host);auto* neighbour=chain.activeProcessor(1);require(neighbour!=nullptr,"Occupied swap setup failed");
 chain.bypass(0,false);chain.bypass(1,true);MainComponentTestAccess::move(host,0,1);
 require(chain.activeProcessor(1)==original&&chain.activeProcessor(0)==neighbour&&MainComponentTestAccess::slot(host)==1&&!chain.isBypassed(1)&&chain.isBypassed(0),"Host occupied swap separated node/bypass/selection");
 MainComponentTestAccess::move(host,1,0);require(chain.activeProcessor(0)==original&&chain.activeProcessor(1)==neighbour&&MainComponentTestAccess::slot(host)==0&&!chain.isBypassed(0)&&chain.isBypassed(1),"Host reverse occupied swap failed");
 MainComponentTestAccess::slot(host,1);MainComponentTestAccess::install(host,-1);MainComponentTestAccess::slot(host,0);MainComponentTestAccess::install(host,-1);MainComponentTestAccess::home(host);
}
inline void vocalAdmission(MainComponent& host,const juce::File& directory){
 MainComponentTestAccess::showVocals(host);auto& realPanel=MainComponentTestAccess::vocalPanel(host);
 auto& realRecord=control<juce::Button>(realPanel,"vocal-record");auto& realInput=control<juce::ComboBox>(realPanel,"vocal-input");
 require(!realRecord.isEnabled()&&!realInput.isEnabled()&&realInput.getSelectedId()==0,"Real no-device vocal panel starts with enabled recording/input controls");
 require(realInput.getTextWhenNothingSelected().contains("NO ENABLED INPUT"),"Empty vocal input has no actionable explanation");
 realRecord.onClick();require(!hem::VocalTrackingPanelTestAccess::chooserOpen(realPanel),"No-device record opened a native chooser");
 require(control<juce::Label>(realPanel,"vocal-monitoring-notice").getText().contains("INPUT / OUTPUT DEVICE"),"No-device monitor guidance falsely promises recording");
 MainComponentTestAccess::home(host);
 juce::AudioDeviceManager device;auto manager=std::make_unique<hem::LiveVocalManager>();manager->prepare(48000,256,0);int hardwareRequests=0;
 hem::VocalTrackingPanel panel(*manager,device,[&]{++hardwareRequests;},[]{return 0;},[]{return 0;},[]{return juce::String();});
 auto& input=control<juce::ComboBox>(panel,"vocal-input");auto& record=control<juce::Button>(panel,"vocal-record");auto& monitor=control<juce::ComboBox>(panel,"vocal-monitor");
 auto* insert=button(panel,"INSERT");auto* remove=button(panel,"REMOVE");auto* hardware=button(panel,"INPUT / OUTPUT DEVICE");require(insert&&remove&&hardware,"Tracking actions missing");
 require(!insert->isEnabled(),"No-device INSERT starts actionable");insert->onClick();require(manager->rack.processor(0)==nullptr,"No-device INSERT changed the rack");
 manager->rack.exchange(0,manager->rack.makeNewNode(0,48000,256));hem::VocalTrackingPanelTestAccess::tick(panel);
 require(remove->isEnabled(),"Offline REMOVE unnecessarily requires a device");remove->onClick();require(manager->rack.processor(0)==nullptr,"Offline REMOVE failed");
 hardware->onClick();require(hardwareRequests==1,"No-device recovery button did not invoke setup");
 device.addAudioDeviceType(std::make_unique<gateFactoryChecks::DeviceType>());device.setCurrentAudioDeviceType("Gate factory test",true);
 auto setup=device.getAudioDeviceSetup();setup.inputDeviceName=setup.outputDeviceName="Gate factory test";setup.sampleRate=48000;setup.bufferSize=256;setup.useDefaultInputChannels=false;setup.inputChannels.clear();
 require(device.setAudioDeviceSetup(setup,true).isEmpty()&&device.getCurrentAudioDevice(),"Silent test device failed to connect");manager->prepare(48000,256,0);hem::VocalTrackingPanelTestAccess::tick(panel);
 require(!input.isEnabled()&&!record.isEnabled()&&insert->isEnabled(),"Zero-input admission confuses recorder and effect preparation");record.onClick();require(!hem::VocalTrackingPanelTestAccess::chooserOpen(panel),"Zero-input record opened a native chooser");
 require(control<juce::Label>(panel,"vocal-status").getText().contains("No enabled input"),"Zero-input status lacks recovery guidance");
 setup.inputChannels.setBit(0);require(device.setAudioDeviceSetup(setup,true).isEmpty(),"Silent input reconnection failed");
 require(device.getCurrentAudioDevice()->getActiveInputChannels().countNumberOfSetBits()==1,"Silent input activation precondition failed");manager->prepare(48000,256,1);hem::VocalTrackingPanelTestAccess::tick(panel);
 require(input.isEnabled()&&record.isEnabled()&&input.getSelectedId()==1&&input.getNumItems()==1,"Vocal controls did not recover after enabling input");
 // Inject the chooser's exact completion seam: no OS chooser/native dialog.
 using Access=hem::VocalTrackingPanelTestAccess;
 auto* observed=device.getCurrentAudioDevice();auto generation=manager->preparationGeneration();
 require(Access::displayedGeneration(panel)==generation&&Access::displayedDevice(panel)==observed,"Vocal displayed input identity/generation stale after reconnect");
 require(Access::matches(panel,generation,observed,0),"Current valid recording source rejected");
 require(!Access::matches(panel,generation+1,observed,0)&&!Access::matches(panel,generation,nullptr,0)&&!Access::matches(panel,generation,observed,1),"Vocal source guard accepts stale generation/device/selection");
 const auto refused=directory.getChildFile("refused-take.wav");const auto request=Access::request(panel);
 Access::complete(panel,{},request,generation,observed,0);
 require(!manager->recorder.busy()&&!Access::chooserOpen(panel),"Chooser cancel started recorder or opened native chooser");
 Access::complete(panel,refused,request-1,generation,observed,0);
 require(!manager->recorder.busy()&&!refused.exists(),"Superseded chooser request started a take");
 for(int stale=0;stale<3;++stale){
  Access::complete(panel,refused,request,stale==0?generation+1:generation,stale==1?nullptr:observed,stale==2?1:0);
  require(!manager->recorder.busy()&&!refused.exists()&&Access::notice(panel).contains("Audio input changed"),"Stale chooser source did not fail safely with diagnostic");
 }
 // Same device pointer with a new preparation generation is a stale completion.
 manager->prepare(48000,256,1);Access::tick(panel);
 Access::complete(panel,refused,request,generation,observed,0);
 require(!manager->recorder.busy()&&!refused.exists(),"Reprepared device accepted pre-reconnect chooser result");
 generation=manager->preparationGeneration();require(Access::displayedGeneration(panel)==generation,"Vocal input UI did not rebind same-device generation");
 const auto take=directory.getChildFile("injected-take.wav");const auto currentRequest=Access::request(panel);
 Access::complete(panel,take,currentRequest,generation,observed,0);
 until([&]{return manager->recorder.state()==hem::TakeRecorder::State::Armed||!manager->recorder.busy();},"Injected chooser completion never armed recorder");
 require(manager->recorder.state()==hem::TakeRecorder::State::Armed&&!Access::chooserOpen(panel),"Valid injected choice did not arm without native dialog");
 juce::AudioBuffer<float> captured(1,256);captured.clear();for(int n=0;n<256;++n)captured.setSample(0,n,.025f*std::sin(float(n)*.07f));
 manager->capture(captured,0,256,0,97.5);manager->stopTake(true);
 until([&]{return !manager->recorder.busy();},"Injected take did not finalize");
 require(manager->recorder.state()==hem::TakeRecorder::State::Complete&&manager->recorder.framesWritten()==256&&take.existsAsFile(),"Injected choice did not safely produce the expected take");
 Access::tick(panel);
 for(int id=1;id<=4;++id){monitor.setSelectedId(id,juce::sendNotificationSync);hem::VocalTrackingPanelTestAccess::tick(panel);require(int(manager->monitoring())==id-1,"Vocal monitoring dropdown did not update engine");require(insert->isEnabled()==(id==1||id==4),"Tracking edit gate differs from monitor mode");}
 monitor.setSelectedId(1,juce::sendNotificationSync);insert->onClick();require(manager->rack.processor(0)!=nullptr,"Reconnected INSERT did not use actual callback");
 device.closeAudioDevice();manager->release();hem::VocalTrackingPanelTestAccess::tick(panel);require(!input.isEnabled()&&!record.isEnabled()&&!insert->isEnabled()&&input.getSelectedId()==0,"Disconnect left stale active vocal controls");
 auto* retained=manager->rack.processor(0);insert->onClick();record.onClick();require(manager->rack.processor(0)==retained&&!hem::VocalTrackingPanelTestAccess::chooserOpen(panel),"Disconnect admitted stale insert/record action");
 Access::complete(panel,refused,currentRequest,generation,observed,0);require(!manager->recorder.busy()&&!refused.exists(),"Disconnected chooser completion created a take");
 require(remove->isEnabled(),"Offline retained insert removal disabled");remove->onClick();require(manager->rack.processor(0)==nullptr,"Offline retained insert removal failed");
}
inline void hostGeometry(MainComponent& host){
 MainComponentTestAccess::home(host);
 for(auto size:{juce::Point<int>(1280,720),juce::Point<int>(1536,864),juce::Point<int>(1920,1080),juce::Point<int>(1673,940)}){
  host.setSize(size.x,size.y);const auto bay=MainComponentTestAccess::mapped(host,335,145,1003,528);const auto rail=MainComponentTestAccess::mapped(host,238,739,1195,117);
  for(auto* c:MainComponentTestAccess::homeControls(host)){require(!c->getBounds().isEmpty(),"Home control lost its bounds");require(bay.contains(host.getLocalArea(c,c->getLocalBounds())),"Home control escaped the approved center aperture");}
  const auto railControls=MainComponentTestAccess::railControls(host);for(auto* c:railControls)require(rail.contains(host.getLocalArea(c,c->getLocalBounds())),"Gain/transport/readout escaped the lower rail");
  for(size_t a=0;a<railControls.size();++a)for(size_t b=a+1;b<railControls.size();++b)require(!host.getLocalArea(railControls[a],railControls[a]->getLocalBounds()).intersects(host.getLocalArea(railControls[b],railControls[b]->getLocalBounds())),"Lower-rail controls overlap");
  require(MainComponentTestAccess::tempoLabel(host).getBounds()==MainComponentTestAccess::mapped(host,617,819,110,31),"Project BPM label lost approved lower-rail bounds");
  require(MainComponentTestAccess::tempo(host).getBounds()==MainComponentTestAccess::mapped(host,735,819,224,31),"Project BPM control lost approved lower-rail bounds");
  require(control<juce::Component>(host,"host-input-meter").getBounds()==MainComponentTestAccess::mapped(host,78,178,163,422),"Input meter does not cover every painted segment");
  require(control<juce::Component>(host,"host-output-meter").getBounds()==MainComponentTestAccess::mapped(host,1436,178,166,422),"Output meter does not cover every painted segment");
 }
 const auto& art=MainComponentTestAccess::faceplate(host);require(art.getWidth()==1673&&art.getHeight()==940,"Host did not load approved00 unchanged");
 const auto image=host.createComponentSnapshot(host.getLocalBounds());
 for(auto protectedArea:{juce::Rectangle<int>(480,15,700,90),juce::Rectangle<int>(25,742,170,140),juce::Rectangle<int>(1490,746,135,138)})
  for(int y=protectedArea.getY();y<protectedArea.getBottom();++y)for(int x=protectedArea.getX();x<protectedArea.getRight();++x)require(image.getPixelAt(x,y)==art.getPixelAt(x,y),"Live host controls repainted protected header/Ghost/chassis pixels");
 host.setSize(1536,864);
}
inline void meterTapParity(MainComponent& host){
 // Manually drive the real callback with the already loaded short stereo WAVs.
 // Independent input reference sums each real post-insert/post-trim track scratch
 // in production order. Output reference reads the exact final emitted samples.
 hem::AudioMeters expectedInput,expectedOutput;expectedInput.prepare(48000);expectedOutput.prepare(48000);
 juce::AudioBuffer<float> block(2,256),mix(2,256);MainComponentTestAccess::prepareManualMeterPass(host);juce::Thread::sleep(50);bool signal=false;
 auto same=[](const hem::AudioMeters::Snapshot& a,const hem::AudioMeters::Snapshot& b){return a.ready==b.ready&&a.processedSamples==b.processedSamples&&std::abs(a.left.peakDb-b.left.peakDb)<.0001f&&std::abs(a.right.peakDb-b.right.peakDb)<.0001f&&std::abs(a.left.rmsDb-b.left.rmsDb)<.0001f&&std::abs(a.right.rmsDb-b.right.rmsDb)<.0001f&&a.left.clipped==b.left.clipped&&a.right.clipped==b.right.clipped;};
 std::uint64_t previousInputPublication=MainComponentTestAccess::inputMeter(host).publication,previousOutputPublication=MainComponentTestAccess::outputMeter(host).publication;
 for(int pass=0;pass<64;++pass){block.clear();host.getNextAudioBlock(juce::AudioSourceChannelInfo(&block,0,256));mix.clear();
  for(auto& track:MainComponentTestAccess::bank(host).tracks)for(int channel=0;channel<2;++channel)for(int i=0;i<256;++i)mix.setSample(channel,i,mix.getSample(channel,i)+track.scratch.getSample(channel,i));
  expectedInput.push(mix.getReadPointer(0),mix.getReadPointer(1),256);expectedOutput.push(block.getReadPointer(0),block.getReadPointer(1),256);
  const auto input=MainComponentTestAccess::inputMeter(host);const auto output=MainComponentTestAccess::outputMeter(host);signal=signal||input.left.peakDb>-70;
  require(input.processedSamples==std::uint64_t(pass+1)*256&&output.processedSamples==input.processedSamples,"Host meter published incorrect processed sample count");
  require(input.generation>0&&output.generation>0&&(input.publication&1u)==0&&(output.publication&1u)==0,"Host meter snapshot is not a coherent prepared publication");
  require(input.publication>previousInputPublication&&output.publication>previousOutputPublication,"Host meter callback failed to publish even with unchanged audio values");
  previousInputPublication=input.publication;previousOutputPublication=output.publication;
  require(same(input,expectedInput.snapshot()),"Host input meter tap differs from the actual post-insert pre-output mix");
  require(same(MainComponentTestAccess::outputMeter(host),expectedOutput.snapshot()),"Host output meter tap differs from final emitted samples");
 }
 require(signal,"Meter parity pass rendered no source signal");
 require(MainComponentTestAccess::inputMeter(host).left.peakDb>MainComponentTestAccess::outputMeter(host).left.peakDb+5,"Input meter incorrectly follows output attenuation");
 MainComponentTestAccess::restoreOutput(host);MainComponentTestAccess::offline(host);require(!MainComponentTestAccess::inputMeter(host).ready&&!MainComponentTestAccess::outputMeter(host).ready,"Disconnected host meters falsely remain ready");
 std::cout<<"PASS real host input/output meter taps: independent post-insert stereo mix, pre-output attenuation separation, exact final emitted samples and no-device readiness\n";
}
inline void run(MainComponent& host,const juce::File& directory){
 hostGeometry(host);monitorOwnership(host);trackTrim(host);bypassAndOrder(host);vocalAdmission(host,directory);
 std::cout<<"PASS real host controls: three Monitor EQ entry routes before/after insert editors, repeated ticks and Back, invalid target retirement, Mixer/Home/text/keyboard/gesture trim sync, empty/busy bypass admission, reachable reorder, and silent-device vocal disconnect/zero-input/recovery\n";
}
}
