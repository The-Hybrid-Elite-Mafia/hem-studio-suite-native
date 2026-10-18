#include "VocalTrackingPanel.h"
#include "../Plugins/InsertFactory.h"
namespace hem {
VocalTrackingPanel::VocalTrackingPanel(LiveVocalManager& m,NativeAudioDeviceHost& d,std::function<void()> setup,std::function<int()> delay,std::function<int()> master,std::function<juce::String()> check,std::function<void(const std::function<void()>&)> control):manager(m),device(d),setupDevice(std::move(setup)),backingDelay(std::move(delay)),masterDelay(std::move(master)),recordCheck(std::move(check)),withGraphControl(std::move(control)){
 if(!withGraphControl)throw std::invalid_argument("Vocal panel requires graph control admission");
 setLookAndFeel(&look);for(auto* c:std::array<juce::Component*,12>{&input,&monitor,&plugin,&hardware,&record,&stop,&print,&level,&offset,&status,&help,&monitoringNotice})addAndMakeVisible(*c);
 monitor.addItem("MONITOR OFF",1);monitor.addItem("DRY SOFTWARE MONITOR",2);monitor.addItem("INSERT SOFTWARE MONITOR",3);monitor.addItem("INTERFACE DIRECT / SOFTWARE OFF",4);monitor.setSelectedId(1);
 monitor.onChange=[this]{manager.setMonitor(LiveVocalManager::Monitor(monitor.getSelectedId()-1));updateMonitoringNotice();};
 input.setTextWhenNothingSelected("NO ENABLED INPUT / OPEN DEVICE SETTINGS");input.setTextWhenNoChoicesAvailable("Enable an input in INPUT / OUTPUT DEVICE");
 input.setComponentID("vocal-input");record.setComponentID("vocal-record");monitor.setComponentID("vocal-monitor");status.setComponentID("vocal-status");monitoringNotice.setComponentID("vocal-monitoring-notice");
 input.onChange=[this]{
  auto* d=device.getCurrentAudioDevice();const int choice=input.getSelectedId()-1;
  // A queued ComboBox choice belongs to the device generation that populated it.
  if(!manager.recorder.busy()&&d&&d==displayedInputDevice&&d->isOpen()
     &&manager.preparationGeneration()==displayedInputGeneration&&choice>=0
     &&choice<d->getActiveInputChannels().countNumberOfSetBits())manager.setInput(choice);
 };
 hardware.onClick=setupDevice;record.onClick=[this]{chooseTake();};stop.onClick=[this]{withGraphControl([this]{manager.stopTake();});};
 print.onClick=[this]{if(!manager.recorder.busy())manager.setPrintInserts(print.getToggleState());};
 level.setRange(-60,6,.1);level.setValue(0);level.setTextValueSuffix(" dB / vocal monitor");level.onValueChange=[this]{manager.setMonitorGain(float(level.getValue()));};
 offset.setRange(-5000,5000,.1);offset.setValue(0);offset.setTextValueSuffix(" ms / placement correction");
 const auto& entries=PluginRegistry::entries();for(size_t i=0;i<entries.size();++i)if(!entries[i].monitoringOnly&&entries[i].trackingAllowed&&entries[i].realtimeInsert)plugin.addItem(InsertFactory::name(int(i)),int(i)+1);plugin.addItem(InsertFactory::name(InsertFactory::legacyGateChoice()),InsertFactory::legacyGateChoice()+1);plugin.setSelectedId(1);
 for(int i=0;i<8;++i){auto& row=rows[size_t(i)];for(auto* b:{&row.name,&row.load,&row.remove,&row.bypass})addAndMakeVisible(*b);row.name.onClick=[this,i]{open(i);};row.load.onClick=[this,i]{insert(i);};row.remove.onClick=[this,i]{insert(i,true);};row.bypass.onClick=[this,i]{manager.rack.bypass(i,!manager.rack.bypassed(i));};}
 help.setText("Interface Direct disables only this app's vocal return. Enable direct monitoring on your interface itself. Dry software monitoring still has input/output latency plus the final master limiter. Takes are raw mono by default; PRINT records the stereo insert output. Use headphones to avoid mic/speaker feedback.",juce::dontSendNotification);
 monitoringNotice.setColour(juce::Label::backgroundColourId,juce::Colour(0xff101923));
 monitoringNotice.setColour(juce::Label::textColourId,juce::Colour(0xffffbb55));
 monitoringNotice.setColour(juce::Label::outlineColourId,juce::Colour(0xff00cddd));
 monitoringNotice.setFont(juce::Font(juce::FontOptions(16.0f)));
 monitoringNotice.setJustificationType(juce::Justification::centredLeft);
 monitoringNotice.setBorderSize(juce::BorderSize<int>(10));
 monitoringNotice.setMinimumHorizontalScale(1.0f);
 timerCallback();
 setSize(1050,830);startTimerHz(15);
}
juce::String VocalTrackingPanel::inputProblem() const {
 auto* d=device.getCurrentAudioDevice();
 if(!d||!d->isOpen()||d->getCurrentSampleRate()<=0||d->getCurrentBufferSizeSamples()<=0)return "No audio device: open INPUT / OUTPUT DEVICE to connect one";
 if(d->getActiveInputChannels().isZero())return "No enabled input: enable a microphone channel in INPUT / OUTPUT DEVICE";
 return {};
}
// Always-visible guidance follows the actual engine state, including device resets.
void VocalTrackingPanel::updateMonitoringNotice(){
 juce::String text=inputProblem();
 if(text.isEmpty())switch(manager.monitoring()){
 case LiveVocalManager::Monitor::Off:
  text="MIC MONITORING OFF  -  Recording still works. Choose dry/insert software monitoring, or enable Direct Monitor on your audio interface for hardware monitoring.";break;
 case LiveVocalManager::Monitor::DrySoftware:
  text="DRY SOFTWARE MONITORING  -  You hear the mic through the app. Delay includes input/output buffers and the final master limiter. Turn interface Direct Monitor off to avoid a doubled vocal.";break;
 case LiveVocalManager::Monitor::InsertsSoftware:
  text="INSERT SOFTWARE MONITORING  -  You hear the mic through its effects. Delay includes device buffers, plugin latency and the final master limiter. Turn interface Direct Monitor off to avoid a doubled vocal.";break;
 case LiveVocalManager::Monitor::InterfaceDirect:
  text="ACTION REQUIRED: Enable Direct Monitor on your audio interface. This mode only turns OFF the app's vocal return; it cannot enable hardware monitoring. Recording remains available.";break;
 }
 if(monitoringNotice.getText()!=text)monitoringNotice.setText(text,juce::dontSendNotification);
}
bool VocalTrackingPanel::recordingSourceMatches(unsigned expectedGeneration,juce::AudioIODevice* expectedDevice,int expectedInput) const {
 auto* d=device.getCurrentAudioDevice();
 return d&&d==expectedDevice&&d->isOpen()&&d->getCurrentSampleRate()>0&&d->getCurrentBufferSizeSamples()>0
     &&manager.preparationGeneration()==expectedGeneration&&manager.selectedInput()==expectedInput
     &&expectedInput>=0&&expectedInput<d->getActiveInputChannels().countNumberOfSetBits();
}
void VocalTrackingPanel::chooseTake(){
 // Admission is checked before opening a chooser and again after it returns.
 if(inputProblem().isNotEmpty()){timerCallback();return;}
 const auto problem=recordCheck();if(problem.isNotEmpty()){notice=problem;return;}
 if(manager.recorder.busy())return;
 unsigned version;juce::AudioIODevice* observed;int selectedInput;
 {version=manager.preparationGeneration();observed=device.getCurrentAudioDevice();selectedInput=manager.selectedInput();}
 if(!recordingSourceMatches(version,observed,selectedInput)){timerCallback();return;}
 const unsigned request=++takeChooserRequest;
 chooser=std::make_unique<juce::FileChooser>("New vocal take  -  choose a new WAV filename",juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("HEM-Vocal-"+juce::Time::getCurrentTime().formatted("%Y%m%d-%H%M%S")+".wav"),"*.wav");
 juce::Component::SafePointer<VocalTrackingPanel> safe(this);
 chooser->launchAsync(juce::FileBrowserComponent::saveMode|juce::FileBrowserComponent::canSelectFiles,[safe,request,version,observed,selectedInput](const juce::FileChooser& f){
    if(safe)safe->completeTakeChoice(f.getResult(),request,version,observed,selectedInput);
 });
}
void VocalTrackingPanel::completeTakeChoice(const juce::File& file,unsigned request,unsigned expectedGeneration,juce::AudioIODevice* expectedDevice,int expectedInput){
 if(request!=takeChooserRequest||file==juce::File())return;
 if(!recordingSourceMatches(expectedGeneration,expectedDevice,expectedInput)){
  notice="Audio input changed; choose RECORD NEW TAKE again";timerCallback();return;
 }
 const auto problem=recordCheck();if(problem.isNotEmpty()){notice=problem;return;}
 if(manager.recorder.busy())return;
 TakeRecorder::Setup options;options.file=file.withFileExtension("wav");options.manualOffsetMs=offset.getValue();options.inputChannelName=input.getText();
 juce::ValueTree rackState("VOCAL_INSERTS");for(int i=0;i<8;++i){juce::ValueTree slot("SLOT");slot.setProperty("index",i,nullptr);slot.setProperty("bypassed",manager.rack.bypassed(i),nullptr);if(auto* p=manager.rack.processor(i)){slot.setProperty("pluginId",PluginRegistry::idFor(*p),nullptr);slot.addChild(p->state.copyState(),-1,nullptr);}rackState.addChild(slot,-1,nullptr);}options.insertStateXml=rackState.toXmlString();
 bool started=false;
 withGraphControl([&]{
  if(recordingSourceMatches(expectedGeneration,expectedDevice,expectedInput)){
   auto* dev=device.getCurrentAudioDevice();options.inputName=device.getAudioDeviceSetup().inputDeviceName;if(options.inputName.isEmpty())options.inputName=dev->getName();options.inputChannel=expectedInput;
   options.inputLatency=dev->getInputLatencyInSamples();options.outputLatency=dev->getOutputLatencyInSamples();options.backingLatency=backingDelay();started=manager.configureTake(options);
  }
 });
 // Thread creation and file preparation stay outside graph-control admission.
 if(started)started=manager.recorder.start(options);
 notice=started?juce::String():juce::String("No enabled input or recorder busy");
}

void VocalTrackingPanel::insert(int slot,bool remove){
 if(slot<0||slot>=8||manager.recorder.busy()||(manager.monitoring()!=LiveVocalManager::Monitor::Off&&manager.monitoring()!=LiveVocalManager::Monitor::InterfaceDirect))return;
 double rate=0;int block=0;unsigned generation=0;bool deviceReady=false;juce::AudioIODevice* observed=nullptr;
 {observed=device.getCurrentAudioDevice();generation=manager.preparationGeneration();
  if(observed){rate=observed->getCurrentSampleRate();block=observed->getCurrentBufferSizeSamples();deviceReady=observed->isOpen()&&rate>0&&block>0;}}
 // Removing an existing node needs no device preparation and remains available offline.
 if(!remove&&!deviceReady){timerCallback();return;}
 if(!remove&&plugin.getSelectedId()<=0)return;
 try{auto node=manager.rack.makeNewNode(remove?-1:plugin.getSelectedId()-1,rate,block);
    ++bypassBindingEpoch;editor.reset();edited=-1;std::unique_ptr<TrackingInsertChain::Node> retired;
    withGraphControl([&]{auto* dev=device.getCurrentAudioDevice();
     if(manager.preparationGeneration()!=generation||manager.recorder.busy()||(manager.monitoring()!=LiveVocalManager::Monitor::Off&&manager.monitoring()!=LiveVocalManager::Monitor::InterfaceDirect))return;
     if(!remove&&(dev!=observed||!dev||!dev->isOpen()||dev->getCurrentSampleRate()!=rate||dev->getCurrentBufferSizeSamples()!=block)){notice="Audio device changed; try INSERT again";return;}
     retired=manager.rack.exchange(slot,std::move(node));});
    // Ownership deletion takes place on the message thread after graph admission.
 }catch(const std::exception& e){notice=e.what();}
}
void VocalTrackingPanel::attachHostBypass(ModuleEditor& module,int slot){
 const juce::Component::SafePointer<VocalTrackingPanel> safe(this);
 const juce::Component::SafePointer<ModuleEditor> alive(&module);
 const auto epoch=++bypassBindingEpoch;auto* expected=module.getAudioProcessor();unsigned generation=0;
 {generation=manager.preparationGeneration();}
 module.setHostBypassBinding({
  [safe,alive,slot,expected,epoch,generation]() -> std::optional<bool> {
   if(safe==nullptr||alive==nullptr||slot<0||slot>=8)return std::nullopt;
   
   if(safe->bypassBindingEpoch!=epoch||safe->manager.preparationGeneration()!=generation||safe->manager.rack.processor(slot)!=expected)return std::nullopt;
   return safe->manager.rack.bypassed(slot);
  },
  [safe,alive,slot,expected,epoch,generation](bool state){
   if(safe==nullptr||alive==nullptr||slot<0||slot>=8)return;
   
   if(safe->bypassBindingEpoch!=epoch||safe->manager.preparationGeneration()!=generation||safe->manager.rack.processor(slot)!=expected)return;
   safe->manager.rack.bypass(slot,state);
  }
 });
}
void VocalTrackingPanel::open(int i){if(i<0||i>=8)return;auto* p=manager.rack.processor(i);if(!p)return;++bypassBindingEpoch;editor.reset();edited=i;
 auto next=std::unique_ptr<juce::AudioProcessorEditor>(p->createEditor());if(auto* module=dynamic_cast<ModuleEditor*>(next.get()))attachHostBypass(*module,i);
 editor=std::make_unique<StudioWindow>("LIVE VOCAL / "+p->getName(),next.release());}
void VocalTrackingPanel::timerCallback(){
 updateMonitoringNotice();
 const bool busy=manager.recorder.busy();const auto problem=inputProblem();input.setEnabled(!busy&&problem.isEmpty());print.setEnabled(!busy);record.setEnabled(!busy&&problem.isEmpty());stop.setEnabled(busy);offset.setEnabled(!busy);hardware.setEnabled(!busy);
 record.setTooltip(problem.isEmpty()?"Record to a new WAV file":problem);input.setTooltip(problem);
 monitor.setSelectedId(int(manager.monitoring())+1,juce::dontSendNotification);
 juce::StringArray names;int inputLatency=0,outputLatency=0;double rate=0;bool deviceReady=false;
 {displayedInputGeneration=manager.preparationGeneration();displayedInputDevice=device.getCurrentAudioDevice();if(auto* d=displayedInputDevice){auto all=d->getInputChannelNames();auto active=d->getActiveInputChannels();for(int i=0;i<all.size();++i)if(active[i])names.add(juce::String(i+1)+" / "+all[i]);rate=d->getCurrentSampleRate();deviceReady=d->isOpen()&&rate>0&&d->getCurrentBufferSizeSamples()>0;inputLatency=d->getInputLatencyInSamples();outputLatency=d->getOutputLatencyInSamples();}}
 const auto key=device.getCurrentAudioDeviceType()+"/"+device.getAudioDeviceSetup().inputDeviceName+"/"+names.joinIntoString("|");if(key!=lastInputs){lastInputs=key;input.clear(juce::dontSendNotification);for(int i=0;i<names.size();++i)input.addItem(names[i],i+1);input.setSelectedId(names.isEmpty()?0:1,juce::dontSendNotification);if(!busy)manager.setInput(0);}
 if(!busy&&!names.isEmpty()&&(manager.selectedInput()<0||manager.selectedInput()>=names.size())){if(manager.preparationGeneration()==displayedInputGeneration)manager.setInput(0);}
 input.setSelectedId(names.isEmpty()?0:manager.selectedInput()+1,juce::dontSendNotification);
 const bool editable=!busy&&(manager.monitoring()==LiveVocalManager::Monitor::Off||manager.monitoring()==LiveVocalManager::Monitor::InterfaceDirect);
 for(int i=0;i<8;++i){auto& row=rows[size_t(i)];auto* p=manager.rack.processor(i);row.name.setButtonText(juce::String(i+1)+" / "+(p?p->getName():juce::String("EMPTY")));row.name.setEnabled(p!=nullptr);row.load.setEnabled(editable&&deviceReady);row.load.setTooltip(deviceReady?"Insert the selected effect":"Connect an audio device in INPUT / OUTPUT DEVICE to prepare inserts");row.remove.setEnabled(editable&&p);row.bypass.setEnabled(p);row.bypass.setToggleState(p&&manager.rack.bypassed(i),juce::dontSendNotification);}
 juce::String state;
 switch(manager.recorder.state()){case TakeRecorder::State::Idle:state=problem.isEmpty()?juce::String("READY"):problem;break;case TakeRecorder::State::Opening:state="OPENING";break;case TakeRecorder::State::Armed:state="ARMED";break;case TakeRecorder::State::Recording:state="RECORDING";break;case TakeRecorder::State::Stopping:state="FINALIZING";break;default:state=manager.recorder.result();break;}
 const auto latency=rate>0?1000.0*(inputLatency+outputLatency+manager.monitorLatency()+masterDelay())/rate:0;
 status.setText((notice.isEmpty()?juce::String():notice+" / ")+state+" / MIC "+juce::String(decibels(manager.inputPeak()),1)+" dBFS / frames "+juce::String(juce::int64(manager.recorder.framesWritten()))+" / software path estimate "+juce::String(latency,1)+" ms (driver + DSP)",juce::dontSendNotification);
}
void VocalTrackingPanel::resized(){auto b=getLocalBounds().reduced(16);monitoringNotice.setBounds(b.removeFromTop(76).reduced(3));auto row=b.removeFromTop(42);hardware.setBounds(row.removeFromLeft(row.getWidth()/3).reduced(3));input.setBounds(row.removeFromLeft(row.getWidth()/2).reduced(3));monitor.setBounds(row.reduced(3));
 row=b.removeFromTop(42);record.setBounds(row.removeFromLeft(row.getWidth()/3).reduced(3));stop.setBounds(row.removeFromLeft(row.getWidth()/2).reduced(3));print.setBounds(row.reduced(3));
 row=b.removeFromTop(42);level.setBounds(row.removeFromLeft(row.getWidth()/2).reduced(3));offset.setBounds(row.reduced(3));plugin.setBounds(b.removeFromTop(40).reduced(3));
 help.setBounds(b.removeFromBottom(90));status.setBounds(b.removeFromBottom(65));for(auto& controls:rows){row=b.removeFromTop(std::min(42,b.getHeight()));controls.name.setBounds(row.removeFromLeft(row.getWidth()/2).reduced(2));controls.load.setBounds(row.removeFromLeft(row.getWidth()/3).reduced(2));controls.remove.setBounds(row.removeFromLeft(row.getWidth()/2).reduced(2));controls.bypass.setBounds(row.reduced(2));}}
}
