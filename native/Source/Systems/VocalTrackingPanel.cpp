#include "VocalTrackingPanel.h"
namespace hem {
VocalTrackingPanel::VocalTrackingPanel(LiveVocalManager& m,juce::AudioDeviceManager& d,std::function<void()> setup,std::function<int()> delay,std::function<int()> master,std::function<juce::String()> check):manager(m),device(d),setupDevice(std::move(setup)),backingDelay(std::move(delay)),masterDelay(std::move(master)),recordCheck(std::move(check)){
 setLookAndFeel(&look);for(auto* c:std::array<juce::Component*,12>{&input,&monitor,&plugin,&hardware,&record,&stop,&print,&level,&offset,&status,&help,&monitoringNotice})addAndMakeVisible(*c);
 monitor.addItem("MONITOR OFF",1);monitor.addItem("DRY SOFTWARE MONITOR",2);monitor.addItem("INSERT SOFTWARE MONITOR",3);monitor.addItem("INTERFACE DIRECT / SOFTWARE OFF",4);monitor.setSelectedId(1);
 monitor.onChange=[this]{manager.setMonitor(LiveVocalManager::Monitor(monitor.getSelectedId()-1));updateMonitoringNotice();};
 input.onChange=[this]{const juce::ScopedLock lock(device.getAudioCallbackLock());if(!manager.recorder.busy())manager.setInput(input.getSelectedId()-1);};
 hardware.onClick=setupDevice;record.onClick=[this]{chooseTake();};stop.onClick=[this]{const juce::ScopedLock lock(device.getAudioCallbackLock());manager.stopTake();};
 print.onClick=[this]{const juce::ScopedLock lock(device.getAudioCallbackLock());if(!manager.recorder.busy())manager.setPrintInserts(print.getToggleState());};
 level.setRange(-60,6,.1);level.setValue(0);level.setTextValueSuffix(" dB / vocal monitor");level.onValueChange=[this]{manager.setMonitorGain(float(level.getValue()));};
 offset.setRange(-5000,5000,.1);offset.setValue(0);offset.setTextValueSuffix(" ms / placement correction");
 const auto& entries=PluginRegistry::entries();for(size_t i=0;i<entries.size();++i)if(!entries[i].monitoringOnly)plugin.addItem(entries[i].name,int(i)+1);plugin.setSelectedId(1);
 for(int i=0;i<8;++i){auto& row=rows[size_t(i)];for(auto* b:{&row.name,&row.load,&row.remove,&row.bypass})addAndMakeVisible(*b);row.name.onClick=[this,i]{open(i);};row.load.onClick=[this,i]{insert(i);};row.remove.onClick=[this,i]{insert(i,true);};row.bypass.onClick=[this,i]{const juce::ScopedLock lock(device.getAudioCallbackLock());manager.rack.bypass(i,!manager.rack.bypassed(i));};}
 help.setText("Interface Direct disables only this app's vocal return. Enable direct monitoring on your interface itself. Dry software monitoring still has input/output latency plus the final master limiter. Takes are raw mono by default; PRINT records the stereo insert output. Use headphones to avoid mic/speaker feedback.",juce::dontSendNotification);
 monitoringNotice.setColour(juce::Label::backgroundColourId,juce::Colour(0xff101923));
 monitoringNotice.setColour(juce::Label::textColourId,juce::Colour(0xffffbb55));
 monitoringNotice.setColour(juce::Label::outlineColourId,juce::Colour(0xff00cddd));
 monitoringNotice.setFont(juce::Font(juce::FontOptions(16.0f)));
 monitoringNotice.setJustificationType(juce::Justification::centredLeft);
 monitoringNotice.setBorderSize(juce::BorderSize<int>(10));
 monitoringNotice.setMinimumHorizontalScale(1.0f);
 updateMonitoringNotice();
 setSize(1050,830);startTimerHz(15);
}
// Always-visible guidance follows the actual engine state, including device resets.
void VocalTrackingPanel::updateMonitoringNotice(){
 juce::String text;
 switch(manager.monitoring()){
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
void VocalTrackingPanel::chooseTake(){
 const auto problem=recordCheck();if(problem.isNotEmpty()){notice=problem;return;}
 if(manager.recorder.busy())return;
 chooser=std::make_unique<juce::FileChooser>("New vocal take  -  choose a new WAV filename",juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("HEM-Vocal-"+juce::Time::getCurrentTime().formatted("%Y%m%d-%H%M%S")+".wav"),"*.wav");
 juce::Component::SafePointer<VocalTrackingPanel> safe(this);
 chooser->launchAsync(juce::FileBrowserComponent::saveMode|juce::FileBrowserComponent::canSelectFiles,[safe](const juce::FileChooser& f){
    if(!safe||f.getResult()==juce::File())return;const auto problem=safe->recordCheck();if(problem.isNotEmpty()){safe->notice=problem;return;}
    TakeRecorder::Setup options;options.file=f.getResult().withFileExtension("wav");options.manualOffsetMs=safe->offset.getValue();options.inputChannelName=safe->input.getText();
    juce::ValueTree rackState("VOCAL_INSERTS");for(int i=0;i<8;++i){juce::ValueTree slot("SLOT");slot.setProperty("index",i,nullptr);slot.setProperty("bypassed",safe->manager.rack.bypassed(i),nullptr);if(auto* p=safe->manager.rack.processor(i)){slot.setProperty("pluginId",PluginRegistry::idFor(*p),nullptr);slot.addChild(p->state.copyState(),-1,nullptr);}rackState.addChild(slot,-1,nullptr);}options.insertStateXml=rackState.toXmlString();
    bool started=false;{const juce::ScopedLock lock(safe->device.getAudioCallbackLock());if(auto* dev=safe->device.getCurrentAudioDevice()){
        options.inputName=safe->device.getAudioDeviceSetup().inputDeviceName;if(options.inputName.isEmpty())options.inputName=dev->getName();options.inputChannel=safe->manager.selectedInput();
        options.inputLatency=dev->getInputLatencyInSamples();options.outputLatency=dev->getOutputLatencyInSamples();options.backingLatency=safe->backingDelay();started=safe->manager.configureTake(options);}}
    // Thread creation and file preparation must not hold the device lock.
    if(started)started=safe->manager.recorder.start(options);
    safe->notice=started?juce::String():juce::String("No enabled input or recorder busy");
 });
}
void VocalTrackingPanel::insert(int slot,bool remove){
 if(manager.recorder.busy()||(manager.monitoring()!=LiveVocalManager::Monitor::Off&&manager.monitoring()!=LiveVocalManager::Monitor::InterfaceDirect))return;
 double rate=0;int block=0;juce::AudioIODevice* observed=nullptr;
 {const juce::ScopedLock lock(device.getAudioCallbackLock());observed=device.getCurrentAudioDevice();if(!observed)return;rate=observed->getCurrentSampleRate();block=observed->getCurrentBufferSizeSamples();}
 try{auto node=manager.rack.makeNode(remove?juce::String():juce::String(PluginRegistry::entries()[size_t(plugin.getSelectedId()-1)].id),rate,block);
    editor.reset();edited=-1;std::unique_ptr<TrackingInsertChain::Node> retired;
    {const juce::ScopedLock lock(device.getAudioCallbackLock());auto* dev=device.getCurrentAudioDevice();if(dev!=observed||!dev||dev->getCurrentSampleRate()!=rate||dev->getCurrentBufferSizeSamples()!=block)return;retired=manager.rack.exchange(slot,std::move(node));}
    // Ownership deletion takes place on the message thread after callback lock.
 }catch(const std::exception& e){notice=e.what();}
}
void VocalTrackingPanel::open(int i){auto* p=manager.rack.processor(i);if(!p)return;editor.reset();edited=i;editor=std::make_unique<StudioWindow>("LIVE VOCAL / "+p->getName(),p->createEditor());}
void VocalTrackingPanel::timerCallback(){
 updateMonitoringNotice();
 const bool busy=manager.recorder.busy();input.setEnabled(!busy);print.setEnabled(!busy);record.setEnabled(!busy);offset.setEnabled(!busy);hardware.setEnabled(!busy);
 monitor.setSelectedId(int(manager.monitoring())+1,juce::dontSendNotification);
 juce::StringArray names;int inputLatency=0,outputLatency=0;double rate=0;
 {const juce::ScopedLock lock(device.getAudioCallbackLock());if(auto* d=device.getCurrentAudioDevice()){auto all=d->getInputChannelNames();auto active=d->getActiveInputChannels();for(int i=0;i<all.size();++i)if(active[i])names.add(juce::String(i+1)+" / "+all[i]);rate=d->getCurrentSampleRate();inputLatency=d->getInputLatencyInSamples();outputLatency=d->getOutputLatencyInSamples();}}
 const auto key=device.getCurrentAudioDeviceType()+"/"+device.getAudioDeviceSetup().inputDeviceName+"/"+names.joinIntoString("|");if(key!=lastInputs){lastInputs=key;input.clear(juce::dontSendNotification);for(int i=0;i<names.size();++i)input.addItem(names[i],i+1);input.setSelectedId(names.isEmpty()?0:1,juce::dontSendNotification);const juce::ScopedLock lock(device.getAudioCallbackLock());if(!busy)manager.setInput(0);}
 input.setSelectedId(names.isEmpty()?0:manager.selectedInput()+1,juce::dontSendNotification);
 const bool editable=!busy&&(manager.monitoring()==LiveVocalManager::Monitor::Off||manager.monitoring()==LiveVocalManager::Monitor::InterfaceDirect);
 for(int i=0;i<8;++i){auto& row=rows[size_t(i)];auto* p=manager.rack.processor(i);row.name.setButtonText(juce::String(i+1)+" / "+(p?p->getName():juce::String("EMPTY")));row.load.setEnabled(editable);row.remove.setEnabled(editable&&p);row.bypass.setEnabled(p);row.bypass.setToggleState(manager.rack.bypassed(i),juce::dontSendNotification);}
 juce::String state;
 switch(manager.recorder.state()){case TakeRecorder::State::Idle:state="READY";break;case TakeRecorder::State::Opening:state="OPENING";break;case TakeRecorder::State::Armed:state="ARMED";break;case TakeRecorder::State::Recording:state="RECORDING";break;case TakeRecorder::State::Stopping:state="FINALIZING";break;default:state=manager.recorder.result();break;}
 const auto latency=rate>0?1000.0*(inputLatency+outputLatency+manager.monitorLatency()+masterDelay())/rate:0;
 status.setText((notice.isEmpty()?juce::String():notice+" / ")+state+" / MIC "+juce::String(decibels(manager.inputPeak()),1)+" dBFS / frames "+juce::String(juce::int64(manager.recorder.framesWritten()))+" / software path estimate "+juce::String(latency,1)+" ms (driver + DSP)",juce::dontSendNotification);
}
void VocalTrackingPanel::resized(){auto b=getLocalBounds().reduced(16);monitoringNotice.setBounds(b.removeFromTop(76).reduced(3));auto row=b.removeFromTop(42);hardware.setBounds(row.removeFromLeft(row.getWidth()/3).reduced(3));input.setBounds(row.removeFromLeft(row.getWidth()/2).reduced(3));monitor.setBounds(row.reduced(3));
 row=b.removeFromTop(42);record.setBounds(row.removeFromLeft(row.getWidth()/3).reduced(3));stop.setBounds(row.removeFromLeft(row.getWidth()/2).reduced(3));print.setBounds(row.reduced(3));
 row=b.removeFromTop(42);level.setBounds(row.removeFromLeft(row.getWidth()/2).reduced(3));offset.setBounds(row.reduced(3));plugin.setBounds(b.removeFromTop(40).reduced(3));
 help.setBounds(b.removeFromBottom(90));status.setBounds(b.removeFromBottom(65));for(auto& controls:rows){row=b.removeFromTop(std::min(42,b.getHeight()));controls.name.setBounds(row.removeFromLeft(row.getWidth()/2).reduced(2));controls.load.setBounds(row.removeFromLeft(row.getWidth()/3).reduced(2));controls.remove.setBounds(row.removeFromLeft(row.getWidth()/2).reduced(2));controls.bypass.setBounds(row.reduced(2));}}
}
