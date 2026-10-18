#include "../Source/Plugins/PluginRegistry.h"
#include "../Source/Plugins/PluginMeterReadout.h"
#include "../Source/Plugins/Editors.h"
#include "../Source/Systems/OfflineMixRenderer.h"
#include "../Source/Systems/PresetManager.h"
#include "../Source/CoreMasterStage.h"
#include "FoundationAllocationProbe.h"
#include "FoundationPlatformAllocations.h"
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <vector>
static void require(bool ok,const char* text){if(!ok)throw std::runtime_error(text);}
static constexpr std::array<const char*,5> ids{"dc-subsonic","smart-gate","vocal-rider","plosive-control","hum-buzz"};
static void set(hem::ModuleProcessor& p,const char* id,float value){auto* v=p.state.getParameter(id);require(v!=nullptr,"parameter absent");v->setValueNotifyingHost(v->convertTo0to1(value));}
static juce::Component* find(juce::Component& c,const juce::String& id){if(c.getComponentID()==id)return &c;for(auto* child:c.getChildren())if(auto* result=find(*child,id))return result;return nullptr;}
static void fill(juce::AudioBuffer<float>& b,int offset){for(int i=0;i<b.getNumSamples();++i){const float x=float(.2*std::sin(2*hem::pi*60*(i+offset)/48000)+.08*std::sin(2*hem::pi*997*(i+offset)/48000));b.setSample(0,i,x);b.setSample(1,i,-x*.37f);}}
static void preparationSafety(){
 for(const auto* id:ids){auto p=hem::PluginRegistry::create(id);juce::AudioBuffer<float> b(2,17);juce::MidiBuffer midi;
  require(!p->processingReady()&&p->actualRate.load()==0,"initial readiness fabricated");
  for(double rate:{0.,-1.,7999.,384001.,std::numeric_limits<double>::max(),std::numeric_limits<double>::infinity(),
      -std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN()}){
   p->prepareToPlay(48000,17);fill(b,0);p->processBlock(b,midi);
   bool rejected=false;try{p->prepareToPlay(rate,17);}catch(const std::invalid_argument&){rejected=true;}
   auto snapshot=p->meterSnapshot();
   require(rejected&&!p->processingReady()&&p->actualRate.load()==0&&!snapshot.isPrepared
       &&!snapshot.hasProcessedAudio&&snapshot.sampleRate==0,"invalid rate readiness/telemetry");
   fill(b,0);allocationProbe::enabled=true;p->processBlock(b,midi);allocationProbe::enabled=false;
   require(b.getMagnitude(0,17)==0&&!p->meterSnapshot().hasProcessedAudio,"failed preparation processed");
  }
  for(double rate:{8000.,44100.,48000.,96000.,384000.}){p->prepareToPlay(rate,17);
   require(p->processingReady()&&p->actualRate.load()==rate&&p->meterSnapshot().sampleRate==rate,"effective rate telemetry differs");
   set(*p,"output",6);p->prepareToPlay(rate,17);b.clear();b.setSample(0,0,std::numeric_limits<float>::max());b.setSample(1,0,.125f);
   p->processBlock(b,midi);require(b.getSample(0,0)==0&&std::isfinite(b.getSample(1,0)),"processor overflow escaped");
   fill(b,17);p->processBlock(b,midi);require(p->meterSnapshot().valueAvailable,"recovery telemetry unavailable");
  }
 }
}
static void stateAndMeter(){
 for(const char* id:ids){auto p=hem::PluginRegistry::create(id);require(hem::PluginRegistry::idFor(*p)==id,"identity mismatch");
  require(!p->meterSnapshot().hasProcessedAudio,"fabricated initial meter");
  require(hem::ui::pluginMeterReadout(*p).valueText=="NO PROCESSED AUDIO","initial meter text");
  for(int point=0;point<4;++point){for(const auto& spec:p->parameterSpecs())set(*p,spec.id,point==0?spec.minimum:point==1?spec.maximum:point==2?spec.initial:spec.minimum+(spec.maximum-spec.minimum)*.37f);
   juce::MemoryBlock bytes;p->getStateInformation(bytes);auto copy=hem::PluginRegistry::create(id);copy->setStateInformation(bytes.getData(),int(bytes.getSize()));
   require(hem::SessionState::captureParameters(*p).isEquivalentTo(hem::SessionState::captureParameters(*copy)),"APVTS recall mismatch");
   for(int invalidCase=0;invalidCase<5;++invalidCase){auto tree=p->state.copyState();
    if(invalidCase==0)for(int i=tree.getNumChildren()-1;i>=0;--i)if(tree.getChild(i)["id"].toString()=="output")tree.removeChild(i,nullptr);
    if(invalidCase==1)tree.getChild(1).setProperty("id",tree.getChild(0)["id"],nullptr);
    if(invalidCase==2)tree.getChild(0).setProperty("value","nan",nullptr);
    if(invalidCase==3)tree.getChild(0).setProperty("value",1.e20,nullptr);
    if(invalidCase==4)tree.getChild(0).setProperty("value","not-a-number",nullptr);
    juce::MemoryBlock invalid;auto xml=tree.createXml();juce::AudioProcessor::copyXmlToBinary(*xml,invalid);
    copy->setStateInformation(invalid.getData(),int(invalid.getSize()));
    require(hem::SessionState::captureParameters(*p).isEquivalentTo(hem::SessionState::captureParameters(*copy)),"corrupt direct state committed");}
   for(auto* processor:{p.get(),copy.get()}){processor->setPlayConfigDetails(2,2,48000,17);processor->prepareToPlay(48000,17);require(processor->getLatencySamples()==(juce::String(id)=="smart-gate"?288:0),"approved processor latency");}
   juce::AudioBuffer<float> a(2,257),b(2,257);juce::MidiBuffer midi;
   for(int block=0;block<40;++block){fill(a,block*257);b.makeCopyOf(a,true);
    allocationProbe::enabled=true;p->processBlock(a,midi);copy->processBlock(b,midi);allocationProbe::enabled=false;
    for(int c=0;c<2;++c)for(int i=0;i<257;++i)require(std::abs(a.getSample(c,i)-b.getSample(c,i))<=2e-6,"fresh recall render differs");}
   const auto telemetry=p->meterSnapshot();require(telemetry.isPrepared&&telemetry.hasProcessedAudio&&telemetry.blockSamples==257&&telemetry.valueAvailable,"coherent telemetry");
   if(p->meterKind()==hem::PluginMeterKind::samplePeak){float peak=0;for(int c=0;c<2;++c)for(int i=0;i<257;++i)peak=std::max(peak,std::abs(a.getSample(c,i)));require(std::abs(peak-telemetry.value)<2e-7,"output peak telemetry");}
   auto valid=hem::SessionState::captureParameters(*p);for(int error=0;error<4;++error){auto invalid=valid.createCopy();
    if(error==0)invalid.removeChild(0,nullptr);if(error==1)invalid.addChild(invalid.getChild(0).createCopy(),-1,nullptr);
    if(error==2)invalid.getChild(0).setProperty("value",1.e20,nullptr);if(error==3)invalid.getChild(0).setProperty("id","unknown",nullptr);
    bool rejected=false;try{hem::SessionState::restoreParameters(*p,invalid);}catch(...){rejected=true;}
    require(rejected&&valid.isEquivalentTo(hem::SessionState::captureParameters(*p)),"invalid state committed");}
  }
  if(juce::String(id)=="smart-gate")require(!hem::ui::pluginMeterReadout(*p).heading.contains("WET-SCALED"),"gate meter mislabeled");
 }
}
static void activeParameters(){
 for(const char* id:ids){auto model=hem::PluginRegistry::create(id);
  for(const auto& spec:model->parameterSpecs()){
   if(juce::String(id)=="hum-buzz"&&juce::String(spec.id)=="sensitivity")continue; // Measured display-only qualification has independent deterministic tests.
   auto a=hem::PluginRegistry::create(id),b=hem::PluginRegistry::create(id);
   for(auto* p:{a.get(),b.get()}){
    if(juce::String(id)=="dc-subsonic"){set(*p,"dcOn",1);set(*p,"subOn",1);}
    if(juce::String(id)=="smart-gate"){set(*p,"range",24);set(*p,"threshold",-30);set(*p,"ratio",4);set(*p,"hold",50);}
    if(juce::String(id)=="vocal-rider"){set(*p,"maxUp",12);set(*p,"maxDown",12);}
    if(juce::String(id)=="plosive-control"){set(*p,"range",24);set(*p,"threshold",-60);set(*p,"contrast",0);}
    if(juce::String(id)=="hum-buzz")set(*p,"depth",36);
   }
   set(*a,spec.id,spec.minimum);set(*b,spec.id,spec.maximum);
   a->prepareToPlay(48000,128);b->prepareToPlay(48000,128);
   juce::AudioBuffer<float> x(2,128),y(2,128);juce::MidiBuffer midi;double difference=0,meterDifference=0;
   for(int block=0;block<1500;++block){for(int i=0;i<128;++i){const int sample=block*128+i,part=(sample/12000)%4;
     const double amplitude=part==0?.3:part==1?.001:part==2?.06:0;
     const float input=float(amplitude*(.2+.5*std::sin(2*hem::pi*50*sample/48000)+.3*std::sin(2*hem::pi*400*sample/48000)));
     x.setSample(0,i,input);x.setSample(1,i,-input*.37f);}
    y.makeCopyOf(x,true);a->processBlock(x,midi);b->processBlock(y,midi);meterDifference+=std::abs(a->meter()-b->meter());
    for(int c=0;c<2;++c)for(int i=0;i<128;++i)difference+=std::abs(x.getSample(c,i)-y.getSample(c,i));}
   std::cout<<id<<" parameter="<<spec.id<<" audio_difference="<<difference<<" meter_difference="<<meterDifference<<'\n';
   require(difference>1.e-5||meterDifference>1.e-5,"parameter lacks measured active-fixture effect");
  }
 }
}
static void legacySessionMigration(){
 for(const char* id:{"smart-gate","plosive-control","hum-buzz"}){auto p=hem::PluginRegistry::create(id);const int legacy=juce::String(id)=="smart-gate"?10:juce::String(id)=="plosive-control"?8:6;
  auto old=hem::SessionState::captureParameters(*p);while(old.getNumChildren()>legacy)old.removeChild(old.getNumChildren()-1,nullptr);
  for(size_t i=size_t(legacy);i<p->parameterSpecs().size();++i)set(*p,p->parameterSpecs()[i].id,p->parameterSpecs()[i].maximum);
  hem::SessionState::restoreParameters(*p,old);for(size_t i=size_t(legacy);i<p->parameterSpecs().size();++i)require(p->state.getRawParameterValue(p->parameterSpecs()[i].id)->load()==p->parameterSpecs()[i].initial,"Legacy session retained dirty new state");
  auto malformed=old.createCopy();malformed.getChild(0).setProperty("id",p->parameterSpecs().back().id,nullptr);const auto before=hem::SessionState::captureParameters(*p);bool rejected=false;try{hem::SessionState::restoreParameters(*p,malformed);}catch(...){rejected=true;}require(rejected&&before.isEquivalentTo(hem::SessionState::captureParameters(*p)),"Session partial legacy/new set committed");
 }
}
static void chainAndTiming(){
 for(double rate:{44100.,48000.,96000.})for(int block:{17,64,128,257}){
  hem::InsertChain chain;chain.prepare(rate,block);
  for(int slot=0;slot<8;++slot){auto p=hem::PluginRegistry::create(ids[size_t(slot)%ids.size()]);
   if(slot%5==0){set(*p,"dcOn",1);set(*p,"subOn",1);}if(slot%5==1)set(*p,"range",24);if(slot%5==2){set(*p,"maxUp",6);set(*p,"maxDown",6);}if(slot%5==3)set(*p,"range",12);if(slot%5==4)set(*p,"depth",18);
   chain.installInitial(slot,chain.stageNode(std::move(p),rate,block,chain.slotLatency()),false);}
  std::vector<float> l(size_t(block),.1f),r(size_t(block),-.03f);std::array<double,500> timings{};
  for(size_t turn=0;turn<timings.size();++turn){if(turn%50==0)for(int slot=0;slot<8;++slot)chain.bypass(slot,(turn/50)%2!=0);
   std::fill(l.begin(),l.end(),.1f);std::fill(r.begin(),r.end(),-.03f);
   const auto begin=std::chrono::steady_clock::now();allocationProbe::enabled=true;chain.process(l.data(),r.data(),block);allocationProbe::enabled=false;
   timings[turn]=std::chrono::duration<double>(std::chrono::steady_clock::now()-begin).count();for(float x:l)require(std::isfinite(x),"chain non-finite");}
  std::sort(timings.begin(),timings.end());std::cout<<"chain rate="<<rate<<" block="<<block<<" p50_us="<<timings[250]*1e6<<" p99_us="<<timings[495]*1e6<<" max_deadline_fraction="<<timings.back()*rate/block<<'\n';
 }
 std::cout<<"Allocation diagnostics C++="<<allocationProbe::count<<" otherThread="<<allocationProbe::otherThreadCount<<" imports="<<platformProbe::count<<" otherThread="<<platformProbe::otherThreadCount<<'\n';
 require(allocationProbe::count==0&&platformProbe::count==0,"processBlock/eight-slot chain C++ allocation");
}
static void sessionsAndTail(){
 const auto dir=juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("hem-foundation-"+juce::Uuid().toString());require(dir.createDirectory().wasOk(),"fixture directory");
 struct Cleanup{juce::File f;~Cleanup(){f.deleteRecursively();}}cleanup{dir};
 hem::SessionSnapshot snapshot;hem::MonitorProcessor monitor;snapshot.monitorParameters=hem::SessionState::captureParameters(monitor);
 for(size_t t=0;t<13;++t)for(size_t s=0;s<8;++s){auto& slot=snapshot.tracks[t].slots[s];slot.pluginId=ids[(t*8+s)%5];slot.bypassed=s%2!=0;auto p=hem::PluginRegistry::create(slot.pluginId);
  for(const auto& spec:p->parameterSpecs())set(*p,spec.id,s%3==0?spec.minimum:s%3==1?spec.maximum:spec.initial);slot.parameters=hem::SessionState::captureParameters(*p);}
 for(bool json:{false,true}){const auto file=dir.getChildFile(json?"state.json":"state.xml");if(json)hem::PresetManager::save(snapshot,file);else hem::SessionState::save(snapshot,file);
  const auto copy=json?hem::PresetManager::load(file):hem::SessionState::load(file);
  for(size_t t=0;t<13;++t)for(size_t s=0;s<8;++s){const auto& a=snapshot.tracks[t].slots[s];const auto& b=copy.tracks[t].slots[s];require(a.pluginId==b.pluginId&&a.bypassed==b.bypassed&&a.parameters.isEquivalentTo(b.parameters),"13x8 XML/JSON state");}}
 hem::SessionSnapshot hum;hum.monitorParameters=hem::SessionState::captureParameters(monitor);auto effect=hem::PluginRegistry::create("hum-buzz");set(*effect,"depth",36);
 hum.tracks[0].slots[0].pluginId="hum-buzz";hum.tracks[0].slots[0].parameters=hem::SessionState::captureParameters(*effect);
 hem::OfflineMixOptions options;require(hem::OfflineMixRenderer::preflightTail(hum,options).failed(),"short hum tail silently accepted");options.allowTailTruncation=true;require(hem::OfflineMixRenderer::preflightTail(hum,options).wasOk(),"explicit truncation rejected");options.allowTailTruncation=false;options.tailSeconds=8;
 require(hem::OfflineMixRenderer::preflightTail(hum,options).wasOk(),"8-second hum preflight");
 const auto input=dir.getChildFile("tone.wav");{std::unique_ptr<juce::OutputStream> stream=input.createOutputStream();juce::WavAudioFormat format;
  auto writer=format.createWriterFor(stream,juce::AudioFormatWriterOptions().withSampleRate(48000).withNumChannels(2).withBitsPerSample(32).withSampleFormat(juce::AudioFormatWriterOptions::SampleFormat::floatingPoint));
  require(writer!=nullptr,"fixture writer");juce::AudioBuffer<float> data(2,48000);fill(data,0);require(writer->writeFromAudioSampleBuffer(data,0,48000)&&writer->flush(),"fixture write");}
 hum.tracks[0].source=input;const auto result=hem::OfflineMixRenderer::render(hum,dir.getChildFile("hum-eight-second-tail.wav"),options);
 require(result.status.wasOk()&&result.frames==432000,"hum eight-second-tail export");
 // Compare an independently staged live chain at the worker's exact sample
 // boundaries, both before the master and after it but before file encoding.
 for(const char* pluginId:ids)for(bool audition:{false,true})for(int profile=0;profile<(juce::String(pluginId)=="smart-gate"?3:1);++profile){if(audition&&(juce::String(pluginId)=="dc-subsonic"||juce::String(pluginId)=="vocal-rider"))continue;hem::SessionSnapshot mix;mix.tracks[0].source=input;
  auto configured=hem::PluginRegistry::create(pluginId);
  if(juce::String(pluginId)=="dc-subsonic"){set(*configured,"dcOn",1);set(*configured,"subOn",1);}
  if(juce::String(pluginId)=="smart-gate"){
   set(*configured,"range",18); // Retain the existing legacy export fixture.
   if(profile==1){set(*configured,"ratio",1);set(*configured,"hysteresis",0);}
   if(profile==2){set(*configured,"ratio",7.33f);set(*configured,"hysteresis",11.2f);set(*configured,"threshold",-12);set(*configured,"lookaheadMs",3.2f);}
  }
  if(juce::String(pluginId)=="vocal-rider"){set(*configured,"maxUp",6);set(*configured,"maxDown",6);}
  if(juce::String(pluginId)=="plosive-control"){set(*configured,"range",18);set(*configured,"threshold",-50);}
  if(juce::String(pluginId)=="hum-buzz")set(*configured,"depth",18);
  if(audition){set(*configured,juce::String(pluginId)=="smart-gate"?"keyListen":"listenRemoved",1);set(*configured,"output",-6);}
  mix.tracks[0].slots[0].pluginId=pluginId;mix.tracks[0].slots[0].parameters=hem::SessionState::captureParameters(*configured);
  hem::InsertChain live;live.prepare(48000,options.blockSize);live.installInitial(0,live.stageNode(std::move(configured),48000,options.blockSize,live.slotLatency()),false);
  hem::MasterTruePeakLimiter baseline;baseline.prepare(48000,options.blockSize,2);
  juce::AudioBuffer<float> expected(2,options.blockSize);double beforeError=0,afterError=0;
  auto compare=[&](const juce::AudioBuffer<float>& actual,juce::int64 position,int count,bool after){
   if(!after){expected.clear();for(int i=0;i<count;++i)if(position+i<48000){const double at=double(position+i);const float sample=float(.2*std::sin(2*hem::pi*60*at/48000)+.08*std::sin(2*hem::pi*997*at/48000));expected.setSample(0,i,sample);expected.setSample(1,i,-sample*.37f);}
    live.process(expected.getWritePointer(0),expected.getWritePointer(1),count);
   }else baseline.process(expected,count);
   for(int channel=0;channel<2;++channel)for(int i=0;i<count;++i){const auto error=std::abs(double(actual.getSample(channel,i))-expected.getSample(channel,i));auto& maximum=after?afterError:beforeError;maximum=std::max(maximum,error);}
  };
  const auto parity=hem::OfflineMixRenderer::render(mix,dir.getChildFile(juce::String(pluginId)+(audition?"-audition":"")+"-profile-"+juce::String(profile)+"-parity.wav"),options,{},{},nullptr,compare);
  require(parity.status.wasOk()&&beforeError<=2e-6&&afterError<=2e-6,"live/offline floating parity");
  std::cout<<pluginId<<(audition?" audition":" normal")<<" profile="<<profile<<" pre_master_max_error="<<beforeError<<" pre_encoding_max_error="<<afterError<<'\n';
 }
}
static void uiControls(){
 for(const char* id:ids){auto p=hem::PluginRegistry::create(id);std::unique_ptr<juce::AudioProcessorEditor> editor(p->createEditor());
  for(const auto& spec:p->parameterSpecs()){auto* component=find(*editor,spec.id);require(component!=nullptr,"missing native control");
   if(!p->parameterActive(spec.id)){if(juce::String(id)=="dc-subsonic")set(*p,"subOn",1);else set(*p,"gate",0);dynamic_cast<hem::ModuleEditor*>(editor.get())->refreshParameterActivity();}
   dynamic_cast<hem::ModuleEditor*>(editor.get())->refreshParameterActivity(); // Synchronous activity timer after preceding control edits.
   if(spec.toggle){auto* button=dynamic_cast<juce::ToggleButton*>(component);require(button!=nullptr,"toggle binding type");button->setToggleState(false,juce::sendNotificationSync);
    const auto now=juce::Time::getCurrentTime();const auto point=button->getLocalBounds().getCentre().toFloat();auto event=[&](int modifiers){return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(),point,juce::ModifierKeys(modifiers),1,0,0,0,0,button,button,now,point,now,1,false);};
    auto& target=static_cast<juce::Component&>(*button);target.mouseDown(event(juce::ModifierKeys::leftButtonModifier));target.mouseUp(event(0));require(p->state.getRawParameterValue(spec.id)->load()==1,"actual pointer toggle binding");}
   else{auto* slider=dynamic_cast<juce::Slider*>(component);require(slider!=nullptr,"slider binding type");for(float value:{spec.minimum,spec.maximum,spec.initial}){slider->setValue(value,juce::sendNotificationSync);require(std::abs(p->state.getRawParameterValue(spec.id)->load()-value)<std::max(.001f,spec.step),"control binding");}
    require(slider->getDoubleClickReturnValue()==spec.initial,"double click reset");
    slider->setValue(spec.minimum+(spec.maximum-spec.minimum)*.5,juce::sendNotificationSync);
    const double beforeKey=slider->getValue();require(slider->keyPressed(juce::KeyPress(juce::KeyPress::rightKey)),"keyboard adjustment unhandled");
    require(slider->getValue()>beforeKey&&std::abs(p->state.getRawParameterValue(spec.id)->load()-slider->getValue())<=spec.step,"keyboard binding");
    const auto now=juce::Time::getCurrentTime();const auto start=slider->getLocalBounds().getCentre().toFloat(),end=start+juce::Point<float>(0,-40);
    auto event=[&](juce::Point<float> point,int flags,bool dragged){return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(),point,juce::ModifierKeys(flags),1,0,0,0,0,slider,slider,now,start,now,1,dragged);};
    const auto beforeDrag=slider->getValue();slider->mouseDown(event(start,juce::ModifierKeys::leftButtonModifier,false));slider->mouseDrag(event(end,juce::ModifierKeys::leftButtonModifier,true));slider->mouseUp(event(end,0,true));
    if(!(slider->getValue()>beforeDrag))throw std::runtime_error("pointer drag binding: "+std::string(id)+"/"+spec.id);slider->mouseDoubleClick(event(start,0,false));require(std::abs(slider->getValue()-spec.initial)<.001,"actual double-click reset");}}
  if(juce::String(id)=="dc-subsonic"){set(*p,"subOn",0);require(!p->parameterActive("cutoff")&&!p->parameterActive("steep"),"sub inactive semantics");}
  if(juce::String(id)=="smart-gate"){set(*p,"gate",1);require(!p->parameterActive("ratio"),"gate inactive ratio");}
  for(auto size:{juce::Point<int>(1000,563),juce::Point<int>(1280,720),juce::Point<int>(1920,1080)})for(int scale:{1,2}){
   editor->setSize(size.x,size.y);juce::Image pixels(juce::Image::ARGB,size.x*scale,size.y*scale,true);juce::Graphics g(pixels);g.addTransform(juce::AffineTransform::scale(float(scale)));editor->paintEntireComponent(g,true);
   for(const auto& spec:p->parameterSpecs())require(editor->getLocalBounds().contains(find(*editor,spec.id)->getBounds()),"resized control outside editor");}
 }
}
int main(){allocationProbe::testThread=true;juce::ScopedJuceInitialiser_GUI gui;try{const int imports=platformProbe::install();
 allocationProbe::enabled=true;void* calibration=::operator new(17);allocationProbe::enabled=false;::operator delete(calibration);
 require(allocationProbe::count==1,"C++ allocation probe calibration");
 require(imports==0||platformProbe::count>=1,"import allocation probe calibration");
 allocationProbe::count=0;platformProbe::count=0;std::cout<<"Platform executable imports instrumented="<<imports<<"; internal DLL allocations are outside coverage.\n";preparationSafety();stateAndMeter();legacySessionMigration();activeParameters();chainAndTiming();sessionsAndTail();uiControls();
 std::cout<<"PASS foundation integration; C++ allocation count="<<allocationProbe::count<<". Internal DLL allocation paths and real device listening are NOT covered.\n";return 0;
}catch(const std::exception& e){allocationProbe::enabled=false;std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}}
