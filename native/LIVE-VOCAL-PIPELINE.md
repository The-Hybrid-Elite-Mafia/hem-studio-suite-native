> Historical recovered design/status document. Current implementation and verification take precedence in README.md, INTEGRATION-STATUS.md and VERIFICATION-2026-10-01.md.

# Live vocal pipeline 0.10.0

Driver-reported active-channel mapping; software monitoring starts off.

## DeviceSettingsPanel.h
```cpp
#pragma once
#include <JuceHeader.h>
namespace hem {
// The platform backend supplies names and channel masks. No model assumptions.
class DeviceSettingsPanel final : public juce::Component {
public:
 explicit DeviceSettingsPanel(juce::AudioDeviceManager& m):manager(m) {
  addAndMakeVisible(refresh);addAndMakeVisible(note);addAndMakeVisible(view);
  note.setText("Choose driver, input/output device and active channels. USB interfaces appear under their installed driver. Select one mono output or two stereo outputs. Changes restart the audio device.",juce::dontSendNotification);
  refresh.onClick=[this]{scan();};scan();
 }
 void resized() override {
  auto b=getLocalBounds().reduced(10);refresh.setBounds(b.removeFromTop(34));
  note.setBounds(b.removeFromTop(64));view.setBounds(b);
  if(selector)selector->setSize(std::max(200,view.getWidth()-20),std::max(600,view.getHeight()));
 }
private:
 void scan(){
  for(auto* type:manager.getAvailableDeviceTypes())type->scanForDevices();
  // AudioSourcePlayer supports at most 128 compacted channels. Individual
  // hardware channel identities remain selectable, not fixed to input 1/2.
  selector=std::make_unique<juce::AudioDeviceSelectorComponent>(manager,0,128,1,2,false,false,false,false);
  view.setViewedComponent(selector.get(),false);resized();
 }
 juce::AudioDeviceManager& manager;
 juce::TextButton refresh{"RESCAN CONNECTED AUDIO DEVICES"};juce::Label note;
 std::unique_ptr<juce::AudioDeviceSelectorComponent> selector;
 juce::Viewport view;
};
}

```

## LiveVocalManager.h
```cpp
#pragma once
#include "TakeRecorder.h"
#include "../Plugins/PluginRegistry.h"
namespace hem {
// Dedicated tracking rack: only actual processor latency, no fixed 6 ms reserve
// for an empty slot. Structural edits require monitoring OFF and recording idle.
// Parameters/bypass remain live. All publication uses the host callback lock.
class TrackingInsertChain final {
public:
    struct Node {std::unique_ptr<ModuleProcessor> processor;Delay dryDelay;Ramp wet;bool bypassed=false;};
    void prepare(double,int);
    std::unique_ptr<Node> makeNode(const juce::String&,double,int);
    std::unique_ptr<Node> exchange(int,std::unique_ptr<Node>);
    ModuleProcessor* processor(int i) const {return slots[size_t(i)]?slots[size_t(i)]->processor.get():nullptr;}
    void bypass(int i,bool b){if(slots[size_t(i)])slots[size_t(i)]->bypassed=b;}
    bool bypassed(int i) const {return slots[size_t(i)]&&slots[size_t(i)]->bypassed;}
    void process(float*,float*,int) noexcept;
    int latency() const {int n=0;for(const auto& p:slots)if(p)n+=p->processor->getLatencySamples();return n;}
private:
    std::array<std::unique_ptr<Node>,8> slots;
    std::array<std::vector<float>,2> dry;
    juce::AudioBuffer<float> view;juce::MidiBuffer midi;
    int maximumBlock=256;
};
class LiveVocalManager final {
public:
    enum class Monitor {Off,DrySoftware,InsertsSoftware,InterfaceDirect};
    void prepare(double,int,int enabledInputCount);
    void release(){recorder.stop();++generation;monitor=Monitor::Off;}
    // info buffer still contains AudioSourcePlayer's compacted active inputs.
    // Call BEFORE clearing/writing the host output region. n <= prepared block.
    void capture(const juce::AudioBuffer<float>&,int start,int n,double timelineSeconds) noexcept;
    void addMonitor(juce::AudioBuffer<float>& master,int n) noexcept;
    bool setInput(int compactChannel){if(recorder.busy()||compactChannel<0||compactChannel>=channels)return false;input.store(compactChannel);return true;}
    int selectedInput() const {return input.load();}
    void setMonitor(Monitor m){monitor.store(m);}
    Monitor monitoring() const {return monitor.load();}
    bool setPrintInserts(bool b){if(recorder.busy())return false;printInserts=b;return true;} // callback excluded, recording idle
    bool printing() const {return printInserts;}
    void setMonitorGain(float db){monitorDb.store(std::clamp(db,-60.f,6.f));}
    float inputPeak() const {return peak.load();}
    int monitorLatency() const {return monitoring()==Monitor::InsertsSoftware?rack.latency():0;}
    bool configureTake(TakeRecorder::Setup&); // callback excluded; then recorder.start(setup) OUTSIDE that lock
    void stopTake(){recorder.stop();}
    TrackingInsertChain rack;TakeRecorder recorder;
private:
    double rate=96000;int channels=0;unsigned generation=0,takeGeneration=0;
    juce::AudioBuffer<float> dry,processed;
    std::atomic<int> input{0};std::atomic<Monitor> monitor{Monitor::Off};
    std::atomic<float> monitorDb{0},peak{0};
    std::vector<Ramp> inputWeights;
    Ramp dryMix,wetMix,level;bool printInserts=false;
};
}

```

## TakeRecorder.h
```cpp
#pragma once
#include <juce_audio_formats/juce_audio_formats.h>
#include <array>
namespace hem {
// Audio thread is the only FIFO producer. The disk worker is its only reader.
// Observe a producer-quiescent callback barrier before start. Launch start()
// OUTSIDE the device lock; no old take may be active. stop/destruction require
// producer exclusion. The host generation check rejects device changes at arm.
class TakeRecorder final : private juce::Thread {
public:
    enum class State {Idle,Opening,Armed,Recording,Stopping,Complete,Failed};
    struct Setup {
        juce::File file;double rate=96000;
        int channels=1,inputLatency=0,outputLatency=0,backingLatency=0,printedLatency=0;
        juce::String inputName,inputChannelName,insertStateXml;int inputChannel=0;
        double manualOffsetMs=0;
    };
    TakeRecorder();~TakeRecorder() override;
    bool start(const Setup&); // New file only, never overwrites a take.
    void stop() noexcept;
    void push(const float*,const float*,int,double timelineSeconds) noexcept;
    State state() const noexcept {return status.load(std::memory_order_acquire);}
    bool busy() const noexcept {auto s=state();return isThreadRunning()||s==State::Opening||s==State::Armed||s==State::Recording||s==State::Stopping;}
    juce::String result() const; // Message thread only, after !busy().
    juce::File completedFile() const {return state()==State::Complete?setup.file:juce::File();}
    std::uint64_t framesWritten() const noexcept {return written.load();}
private:
    static constexpr int packetFrames=256,capacity=512;
    struct Packet {std::array<float,packetFrames> l{},r{};int frames=0;};
    juce::AbstractFifo fifo{capacity};std::array<Packet,capacity> packets;
    void run() override;
    Setup setup;juce::String outcome;juce::File partial;
    std::atomic<State> status{State::Idle};
    std::atomic<bool> stopRequested{false},overflow{false};
    std::atomic<std::uint64_t> written{0},invalidSamples{0};
    std::atomic<double> firstTimeline{-1};
};
}

```

## VocalTrackingPanel.h
```cpp
#pragma once
#include "LiveVocalManager.h"
#include "../StudioWindow.h"
namespace hem {
class VocalTrackingPanel final:public juce::Component,private juce::Timer {
public:
    VocalTrackingPanel(LiveVocalManager&,juce::AudioDeviceManager&,std::function<void()> setup,std::function<int()> backingLatency,std::function<juce::String()> mayRecord);
    ~VocalTrackingPanel() override {stopTimer();editor.reset();setLookAndFeel(nullptr);}
    void resized() override;
private:
    void updateMonitoringNotice();
    void timerCallback() override;void chooseTake();void insert(int,bool remove=false);void open(int);
    LiveVocalManager& manager;juce::AudioDeviceManager& device;
    std::function<void()> setupDevice;std::function<int()> backingDelay;std::function<juce::String()> recordCheck;
    HardwareLookAndFeel look;
    juce::ComboBox input,monitor,plugin;
    juce::TextButton hardware{"INPUT / OUTPUT DEVICE"},record{"RECORD NEW TAKE"},stop{"STOP / FINALIZE TAKE"};
    juce::ToggleButton print{"PRINT INSERTS TO STEREO TAKE"};juce::Slider level,offset;
    juce::Label status,help,monitoringNotice;
    struct Row{juce::TextButton name,load{"INSERT"},remove{"REMOVE"},bypass{"BYP"};};
    std::array<Row,8> rows;
    std::unique_ptr<juce::FileChooser> chooser;std::unique_ptr<StudioWindow> editor;
    juce::String lastInputs,notice;int edited=-1;
};
}

```

## LiveVocalManager.cpp
```cpp
#include "LiveVocalManager.h"
#include <stdexcept>
namespace hem {
void TrackingInsertChain::prepare(double rate,int block){maximumBlock=std::max(1,block);for(auto& b:dry)b.assign(size_t(maximumBlock),0);
 for(auto& p:slots)if(p){p->processor->setPlayConfigDetails(2,2,rate,block);p->processor->prepareToPlay(rate,block);p->dryDelay.prepare(p->processor->getLatencySamples());p->wet.reset(rate,p->bypassed?0.f:1.f);}}
std::unique_ptr<TrackingInsertChain::Node> TrackingInsertChain::makeNode(const juce::String& id,double rate,int block){
 if(id.isEmpty())return {};auto p=PluginRegistry::create(id);if(!p||p->monitorOnly())throw std::runtime_error("Invalid vocal insert");
 auto node=std::make_unique<Node>();node->processor=std::move(p);node->processor->setPlayConfigDetails(2,2,rate,block);node->processor->prepareToPlay(rate,block);node->dryDelay.prepare(node->processor->getLatencySamples());node->wet.reset(rate,1);return node;
}
std::unique_ptr<TrackingInsertChain::Node> TrackingInsertChain::exchange(int index,std::unique_ptr<Node> p){if(index<0||index>=8)throw std::out_of_range("Vocal insert");slots[size_t(index)].swap(p);return p;}
void TrackingInsertChain::process(float* l,float* r,int n) noexcept {
 for(auto& node:slots)if(node){std::copy_n(l,n,dry[0].data());std::copy_n(r,n,dry[1].data());node->dryDelay.process(dry[0].data(),dry[1].data(),n);
    float* c[]{l,r};view.setDataToReferTo(c,2,n);midi.clear();node->processor->processBlock(view,midi);node->wet.set(node->bypassed?0.f:1.f);
    for(int i=0;i<n;++i){const float w=node->wet.next();l[i]=dry[0][size_t(i)]+w*(l[i]-dry[0][size_t(i)]);r[i]=dry[1][size_t(i)]+w*(r[i]-dry[1][size_t(i)]);}}
}
void LiveVocalManager::prepare(double sr,int block,int inputs){recorder.stop();++generation;rate=sr;channels=std::clamp(inputs,0,128);input=0;inputWeights.resize(size_t(channels));for(int c=0;c<channels;++c)inputWeights[size_t(c)].reset(sr,c==0?1.f:0.f);dry.setSize(2,block);processed.setSize(2,block);rack.prepare(sr,block);dryMix.reset(sr,0);wetMix.reset(sr,0);level.reset(sr,1);monitor=Monitor::Off;}
bool LiveVocalManager::configureTake(TakeRecorder::Setup& setup){if(recorder.busy())return false;if(channels<=0||input.load()<0||input.load()>=channels)return false;setup.rate=rate;setup.channels=printInserts?2:1;setup.printedLatency=printInserts?rack.latency():0;takeGeneration=generation;return true;}
void LiveVocalManager::capture(const juce::AudioBuffer<float>& buffer,int start,int n,double position) noexcept {
 const int index=input.load();const bool valid=index>=0&&index<channels&&index<buffer.getNumChannels();float max=0;
 for(int c=0;c<channels;++c)inputWeights[size_t(c)].set(valid&&c==index?1.f:0.f);
 for(int i=0;i<n;++i){float raw=0;for(int c=0;c<channels;++c){const float weight=inputWeights[size_t(c)].next();if(c<channels&&c<buffer.getNumChannels())raw+=weight*buffer.getSample(c,start+i);}const float sample=std::isfinite(raw)?raw:0.f;dry.setSample(0,i,sample);dry.setSample(1,i,sample);max=std::max(max,std::abs(sample));}
 peak.store(max);for(int c=0;c<2;++c)processed.copyFrom(c,0,dry,c,0,n);rack.process(processed.getWritePointer(0),processed.getWritePointer(1),n);
 if(takeGeneration!=generation)recorder.stop();
 const auto& tap=printInserts?processed:dry;recorder.push(tap.getReadPointer(0),tap.getReadPointer(1),n,position);
}
void LiveVocalManager::addMonitor(juce::AudioBuffer<float>& bus,int n) noexcept {
 const auto m=monitor.load();dryMix.set(m==Monitor::DrySoftware?1.f:0.f);wetMix.set(m==Monitor::InsertsSoftware?1.f:0.f);level.set(gain(monitorDb.load()));
 auto* l=bus.getWritePointer(0);auto* r=bus.getWritePointer(1);
 for(int i=0;i<n;++i){const float d=dryMix.next(),w=wetMix.next(),g=level.next();l[i]+=g*(d*dry.getSample(0,i)+w*processed.getSample(0,i));r[i]+=g*(d*dry.getSample(1,i)+w*processed.getSample(1,i));}
}
}

```

## TakeRecorder.cpp
```cpp
#include "TakeRecorder.h"
#include <stdexcept>
namespace hem {
TakeRecorder::TakeRecorder():juce::Thread("HEM vocal take writer"){}
TakeRecorder::~TakeRecorder(){stop();waitForThreadToExit(-1);}
bool TakeRecorder::start(const Setup& s){
    if(busy())return false;
    if(!std::isfinite(s.rate)||s.rate<8000||s.rate>384000||s.channels<1||s.channels>2||s.file==juce::File()||!std::isfinite(s.manualOffsetMs))return false;
    setup=s;outcome.clear();partial=juce::File();fifo.reset();written=0;invalidSamples=0;firstTimeline=-1;overflow=false;stopRequested=false;status=State::Opening;
    if(!startThread()){outcome="Could not start recording worker";status=State::Failed;return false;}return true;
}
void TakeRecorder::stop() noexcept {stopRequested.store(true,std::memory_order_release);}
void TakeRecorder::push(const float* l,const float* r,int count,double timeline) noexcept {
    if(stopRequested.load(std::memory_order_acquire))return;
    auto s=state();if(s!=State::Armed&&s!=State::Recording)return;
    if(s==State::Armed){firstTimeline.store(timeline);status.store(State::Recording,std::memory_order_release);}
    for(int offset=0;offset<count;offset+=packetFrames){
        const int n=std::min(packetFrames,count-offset);int a,n1,b,n2;fifo.prepareToWrite(1,a,n1,b,n2);
        if(n1+n2==0){overflow.store(true);stopRequested.store(true,std::memory_order_release);return;}
        auto& p=packets[size_t(n1?a:b)];p.frames=n;
        for(int i=0;i<n;++i){auto clean=[this](float x){if(std::isfinite(x))return x;invalidSamples.fetch_add(1,std::memory_order_relaxed);return 0.f;};p.l[size_t(i)]=clean(l[offset+i]);if(setup.channels==2)p.r[size_t(i)]=clean(r[offset+i]);}
        fifo.finishedWrite(1);
    }
}
juce::String TakeRecorder::result() const {return busy()?juce::String("Recording / finalizing"):outcome;}
void TakeRecorder::run(){
    try{
        if(setup.file.exists())throw std::runtime_error("Take destination already exists; choose a new name");
        partial=setup.file.getSiblingFile(setup.file.getFileNameWithoutExtension()+".partial-"+juce::Uuid().toString()+".wav");
        std::unique_ptr<juce::OutputStream> stream=partial.createOutputStream();if(!stream)throw std::runtime_error("Cannot create take file");
        auto* fileStream=dynamic_cast<juce::FileOutputStream*>(stream.get());
        juce::WavAudioFormat wav;
        auto options=juce::AudioFormatWriterOptions().withSampleRate(setup.rate).withNumChannels(setup.channels).withBitsPerSample(32).withSampleFormat(juce::AudioFormatWriterOptions::SampleFormat::floatingPoint);
        auto writer=wav.createWriterFor(stream,options);if(!writer)throw std::runtime_error("Cannot create float WAV writer");
        status.store(State::Armed,std::memory_order_release);
        bool diskError=false;
        while(true){
            int a,n1,b,n2;fifo.prepareToRead(1,a,n1,b,n2);
            if(n1+n2){const auto& p=packets[size_t(n1?a:b)];const float* channels[]{p.l.data(),p.r.data()};
                if(!writer->writeFromFloatArrays(channels,setup.channels,p.frames)){diskError=true;stopRequested=true;}
                else written.fetch_add(unsigned(p.frames));fifo.finishedRead(1);
                // Prevent legacy RIFF size overflow; close a valid partial take.
                if(written.load()*unsigned(setup.channels)*4>=0xff000000ULL){overflow=true;stopRequested=true;}
                if(diskError)break;continue;
            }
            if(stopRequested.load(std::memory_order_acquire))break;
            wait(3);
        }
        status.store(State::Stopping,std::memory_order_release);
        const bool flushed=writer->flush();const bool streamOK=fileStream&&fileStream->getStatus().wasOk();writer.reset();
        if(diskError||!flushed||!streamOK)throw std::runtime_error("Disk write/flush failed; partial WAV retained");
        if(overflow.load())throw std::runtime_error("Recording stopped: disk queue or RIFF capacity exceeded; partial WAV retained without skipping timeline samples");
        if(written.load()==0){partial.deleteFile();throw std::runtime_error("No input frames recorded");}
        if(setup.file.exists()||!partial.moveFileTo(setup.file))throw std::runtime_error("Cannot publish take; partial WAV retained");
        juce::DynamicObject::Ptr metadata=new juce::DynamicObject();
        metadata->setProperty("sampleRate",setup.rate);metadata->setProperty("frames",juce::int64(written.load()));metadata->setProperty("channels",setup.channels);
        metadata->setProperty("inputDevice",setup.inputName);metadata->setProperty("activeInputChannelIndex",setup.inputChannel);metadata->setProperty("inputChannelName",setup.inputChannelName);metadata->setProperty("insertStateAtArmXml",setup.insertStateXml);
        metadata->setProperty("firstCallbackTimelineSeconds",firstTimeline.load());
        const double delay=(setup.inputLatency+setup.outputLatency+setup.backingLatency+setup.printedLatency)/setup.rate;
        metadata->setProperty("estimatedPlacementSeconds",firstTimeline.load()-delay+setup.manualOffsetMs*.001);
        metadata->setProperty("inputLatencySamples",setup.inputLatency);metadata->setProperty("outputLatencySamples",setup.outputLatency);metadata->setProperty("backingDspLatencySamples",setup.backingLatency);metadata->setProperty("printedInsertLatencySamples",setup.printedLatency);
        metadata->setProperty("manualOffsetMs",setup.manualOffsetMs);metadata->setProperty("nonFiniteSamplesReplaced",juce::int64(invalidSamples.load()));
        metadata->setProperty("alignmentNote","Driver-reported estimate; verify with a loopback test. Positive manual offset places audio later.");
        const auto sidecar=setup.file.getSiblingFile(setup.file.getFileName()+".take.json");
        const bool saved=sidecar.replaceWithText(juce::JSON::toString(juce::var(metadata.get()),false));
        outcome="Saved "+setup.file.getFileName()+(saved?" with placement metadata":"; metadata save failed");status.store(State::Complete,std::memory_order_release);
    }catch(const std::exception& e){outcome=e.what();if(partial.existsAsFile())outcome+=" / "+partial.getFullPathName();status.store(State::Failed,std::memory_order_release);}
}
}

```

## VocalTrackingPanel.cpp
```cpp
#include "VocalTrackingPanel.h"
namespace hem {
VocalTrackingPanel::VocalTrackingPanel(LiveVocalManager& m,juce::AudioDeviceManager& d,std::function<void()> setup,std::function<int()> delay,std::function<juce::String()> check):manager(m),device(d),setupDevice(std::move(setup)),backingDelay(std::move(delay)),recordCheck(std::move(check)){
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
 help.setText("Interface Direct disables only this app's vocal return. Enable direct monitoring on your interface itself. Dry software monitoring still has input/output latency plus the final 5 ms limiter. Takes are raw mono by default; PRINT records the stereo insert output. Use headphones to avoid mic/speaker feedback.",juce::dontSendNotification);
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
  text="MIC MONITORING OFF — Recording still works. Choose dry/insert software monitoring, or enable Direct Monitor on your audio interface for hardware monitoring.";break;
 case LiveVocalManager::Monitor::DrySoftware:
  text="DRY SOFTWARE MONITORING — You hear the mic through the app. Delay includes input/output buffers and the final 5 ms limiter. Turn interface Direct Monitor off to avoid a doubled vocal.";break;
 case LiveVocalManager::Monitor::InsertsSoftware:
  text="INSERT SOFTWARE MONITORING — You hear the mic through its effects. Delay includes device buffers, plugin latency and the final 5 ms limiter. Turn interface Direct Monitor off to avoid a doubled vocal.";break;
 case LiveVocalManager::Monitor::InterfaceDirect:
  text="ACTION REQUIRED: Enable Direct Monitor on your audio interface. This mode only turns OFF the app's vocal return; it cannot enable hardware monitoring. Recording remains available.";break;
 }
 if(monitoringNotice.getText()!=text)monitoringNotice.setText(text,juce::dontSendNotification);
}
void VocalTrackingPanel::chooseTake(){
 const auto problem=recordCheck();if(problem.isNotEmpty()){notice=problem;return;}
 if(manager.recorder.busy())return;
 chooser=std::make_unique<juce::FileChooser>("New vocal take — choose a new WAV filename",juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("HEM-Vocal-"+juce::Time::getCurrentTime().formatted("%Y%m%d-%H%M%S")+".wav"),"*.wav");
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
 const auto latency=rate>0?1000*(inputLatency+outputLatency+manager.monitorLatency())/rate+5:0;
 status.setText((notice.isEmpty()?juce::String():notice+" / ")+state+" / MIC "+juce::String(decibels(manager.inputPeak()),1)+" dBFS / frames "+juce::String(juce::int64(manager.recorder.framesWritten()))+" / software path estimate "+juce::String(latency,1)+" ms (driver + DSP)",juce::dontSendNotification);
}
void VocalTrackingPanel::resized(){auto b=getLocalBounds().reduced(16);monitoringNotice.setBounds(b.removeFromTop(76).reduced(3));auto row=b.removeFromTop(42);hardware.setBounds(row.removeFromLeft(row.getWidth()/3).reduced(3));input.setBounds(row.removeFromLeft(row.getWidth()/2).reduced(3));monitor.setBounds(row.reduced(3));
 row=b.removeFromTop(42);record.setBounds(row.removeFromLeft(row.getWidth()/3).reduced(3));stop.setBounds(row.removeFromLeft(row.getWidth()/2).reduced(3));print.setBounds(row.reduced(3));
 row=b.removeFromTop(42);level.setBounds(row.removeFromLeft(row.getWidth()/2).reduced(3));offset.setBounds(row.reduced(3));plugin.setBounds(b.removeFromTop(40).reduced(3));
 help.setBounds(b.removeFromBottom(90));status.setBounds(b.removeFromBottom(65));for(auto& controls:rows){row=b.removeFromTop(std::min(42,b.getHeight()));controls.name.setBounds(row.removeFromLeft(row.getWidth()/2).reduced(2));controls.load.setBounds(row.removeFromLeft(row.getWidth()/3).reduced(2));controls.remove.setBounds(row.removeFromLeft(row.getWidth()/2).reduced(2));controls.bypass.setBounds(row.reduced(2));}}
}

```
