#include "../Source/SessionState.h"
#include "../Source/Plugins/DelayEditors.h"
#include "../Source/Systems/OfflineMixRenderer.h"
#include "../Source/CoreMasterStage.h"
#include "DelayAllocationProbe.h"
#include "../Source/Plugins/DelayPresentation.h"
#include "DelayParameterView.h"
#include <iostream>
#include <ApprovedArt.h>
#include "../Source/UI/SegmentedMeterModel.h"
#include <stdexcept>
using namespace hem;
static void require(bool b,const char* reason){if(!b)throw std::runtime_error(reason);}
static void parameter(ModuleProcessor& p,const char* id,float value){auto* v=p.state.getParameter(id);require(v!=nullptr,"Missing parameter");v->setValueNotifyingHost(v->convertTo0to1(value));}
static juce::Component* find(juce::Component& parent,const juce::String& id){if(parent.getComponentID()==id)return &parent;for(auto* child:parent.getChildren())if(auto* found=find(*child,id))return found;return nullptr;}
static void verifyReviewFixes(DelayProcessor& p){
 const bool tape=p.delayKind()==delay::Kind::tape;const char* time=tape?"time_ms":"tap1.time_ms";
 const auto saved=SessionState::captureParameters(p);
 for(const char* value:{"4000.00001","0.99999999999","4000","1"}){
  const bool valid=juce::String(value)=="4000"||juce::String(value)=="1";
  auto session=saved.createCopy();session.getChild(0).setProperty("value",value,nullptr);
  require(p.validateStoredParameters(session)==valid,"Serialized double boundary validation failed");
  auto tree=p.state.copyState();tree.getChildWithProperty("id",time).setProperty("value",value,nullptr);
  juce::MemoryBlock bytes;auto xml=tree.createXml();juce::AudioProcessor::copyXmlToBinary(*xml,bytes);
  p.setStateInformation(bytes.getData(),int(bytes.getSize()));
  if(valid)require(p.state.getRawParameterValue(time)->load()==float(std::strtod(value,nullptr)),"Exact state boundary rejected");
  else require(saved.isEquivalentTo(SessionState::captureParameters(p)),"Out-of-range double committed after float rounding");
  SessionState::restoreParameters(p,saved);
 }
 p.setBlockTempo(20);parameter(p,"sync",1);parameter(p,tape?"division":"tap1.division",19);if(!tape)parameter(p,"tap1.on",1);
 DelayEditor editor(p);
 for(int mode=0;mode<3;++mode){parameter(p,"mix",mode==0?0.f:100.f);parameter(p,"bypass",mode==1?1.f:0.f);editor.refreshBindings();
  const auto status=delayUi::syncStatus(p.settings(),p.delayKind(),20);require(status.unavailable,"Unavailable sync setting warning lost");
  require(status.blocksAudibleExport==(mode==2),"Sync UI disagrees with local wet audibility");
  require(p.capturedTail(48000,20).valid==(mode!=2),"Sync UI disagrees with captured tail");
  require(editor.statusText().contains("export allowed")== (mode!=2),"Inactive wet path falsely reports blocked export");
 }
 SessionState::restoreParameters(p,saved);p.setBlockTempo(120);
 delayUi::PublicationFreshness fresh(p.meterPublication());
 p.prepareToPlay(48000,256);require(!fresh.observe(p.meterPublication(),p.meterSnapshot().hasProcessedAudio,0),"Prepare fabricated fresh meter data");
 juce::AudioBuffer<float> block(2,256);block.clear();juce::MidiBuffer midi;p.processBlock(block,midi);auto token=p.meterPublication();
 require(fresh.observe(token,true,10),"New publication not fresh");require(!fresh.observe(token,true,261),"Stopped callbacks left apparently live meters");
 p.processBlock(block,midi);require(p.meterPublication()!=token&&fresh.observe(p.meterPublication(),true,262),"Same-block-size restart did not restore freshness");
 p.releaseResources();const auto released=p.meterSnapshot();require(!released.isPrepared&&!released.hasProcessedAudio&&!released.valueAvailable&&!p.processingReady()&&!p.waveform().valid,"Release retained apparently live telemetry");
 require(!fresh.observe(p.meterPublication(),released.valueAvailable,263),"Release did not invalidate freshness");
 p.prepareToPlay(48000,256);require(!p.meterSnapshot().hasProcessedAudio,"Reprepare retained old meter value");
 p.processBlock(block,midi);require(fresh.observe(p.meterPublication(),true,264),"Same-size reprepare callback failed freshness");
}
static void verifyPointer(juce::Slider& slider){
 const auto bounds=slider.getBounds();slider.setSize(100,100);
 const auto initial=slider.getValue();
 for(double value:{slider.getMinimum(),initial,slider.getMaximum()}){
  slider.setValue(value,juce::sendNotificationSync);const double fraction=slider.valueToProportionOfLength(slider.getValue());
  juce::Image raster(juce::Image::ARGB,100,100,true);
  {juce::Graphics g(raster);slider.getLookAndFeel().drawRotarySlider(g,0,0,100,100,float(fraction),0,0,slider);}
  // Independent expected coordinates: min at (left), middle at noon, max right.
  const double angle=hem::pi+fraction*hem::pi;const int x=int(std::round(50+std::cos(angle)*35)),y=int(std::round(50+std::sin(angle)*35));
  bool white=false;for(int dy=-2;dy<=2;++dy)for(int dx=-2;dx<=2;++dx){const auto c=raster.getPixelAt(x+dx,y+dy);white|=c.getRed()>200&&c.getGreen()>200&&c.getBlue()>200;}
  require(white,"Rendered pointer position disagrees with physical parameter range");
 }
 if(slider.getMinimum()<0&&slider.getMaximum()>0)require(std::abs(slider.valueToProportionOfLength(0)-.5)<1e-12,"Bipolar zero is not noon");
 slider.setValue(initial,juce::sendNotificationSync);slider.setBounds(bounds);
}
static void verifyControls(DelayProcessor& p){
 DelayEditor editor(p);const bool tape=p.delayKind()==delay::Kind::tape;
 for(int tap=0;tap<(tape?1:8);++tap){editor.selectTap(tap);
  for(const auto& spec:p.parameterSpecs()){
   const juce::String id=spec.id;if(id.endsWith("division"))continue;
   if(!tape&&id.startsWith("tap")&&!id.startsWith("tap"+juce::String(tap+1)+"."))continue;
   auto* component=find(editor,id);require(component!=nullptr,"Reference control has no live binding");
   if(spec.toggle){auto* toggle=dynamic_cast<juce::ToggleButton*>(component);require(toggle!=nullptr,"Toggle is not independently interactive");toggle->setToggleState(!toggle->getToggleState(),juce::sendNotificationSync);require(p.state.getRawParameterValue(id)->load()==float(toggle->getToggleState()),"Toggle/APVTS binding mismatch");toggle->setToggleState(spec.initial>.5f,juce::sendNotificationSync);}
   else{auto* slider=dynamic_cast<juce::Slider*>(component);require(slider!=nullptr,"Rotary is not independently interactive");verifyPointer(*slider);slider->setValue(spec.maximum,juce::sendNotificationSync);require(std::abs(p.state.getRawParameterValue(id)->load()-spec.maximum)<.11f,"Rotary/APVTS binding mismatch");require(editor.layoutIssues().isEmpty(),"Maximum value fails native geometry/text fit");slider->setValue(spec.initial,juce::sendNotificationSync);}
   require(editor.getLocalBounds().contains(component->getBounds()),"Control outside reference layout");
  }
 }
 parameter(p,"sync",1);editor.refreshBindings();require(find(editor,tape?"division":"tap1.division")!=nullptr,"Time control did not bind separate division state");parameter(p,"sync",0);editor.refreshBindings();require(find(editor,tape?"time_ms":"tap1.time_ms")!=nullptr,"Manual time binding was lost");
 if(tape)require(find(editor,"tap-tempo")!=nullptr,"Original Tap Tempo button missing");
 // Raster checks are authored but remain unexecuted until approval. No GUI
 // window/device is required; original neutral lighting is fixed in the image.
 for(auto size:{juce::Point<int>(1003,565),juce::Point<int>(1338,753),juce::Point<int>(1672,941),juce::Point<int>(2006,1129)}){editor.setSize(size.x,size.y);require(editor.layoutIssues().isEmpty(),"Resized native geometry/text fit failed");juce::Image image(juce::Image::ARGB,size.x,size.y,true);juce::Graphics g(image);editor.paintEntireComponent(g,true);require(image.isValid(),"Editor failed to render");}
}
static void renderEvidence(){
 const auto dir=juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("hem-delay-r2-renders-"+juce::Uuid().toString());require(dir.createDirectory().wasOk(),"Cannot create native render evidence directory");
 std::cout<<"Native render evidence: "<<dir.getFullPathName()<<std::endl;
 juce::String report;
 for(bool tape:{false,true}){
  std::unique_ptr<DelayProcessor> p=tape?std::unique_ptr<DelayProcessor>(new TapeDelayProcessor):std::unique_ptr<DelayProcessor>(new MultitapDelayProcessor);DelayEditor editor(*p);
  for(int mode=0;mode<5;++mode){
   if(mode==1){parameter(*p,"mix",100);parameter(*p,"sync",1);parameter(*p,tape?"division":"tap1.division",19);if(!tape)parameter(*p,"tap1.on",1);p->setBlockTempo(20);}
   if(mode==2){parameter(*p,"mix",0);p->releaseResources();}
   if(mode==3){parameter(*p,"sync",0);parameter(*p,"mix",65);parameter(*p,"output",-3);p->setBlockTempo(120);parameter(*p,tape?"time_ms":"tap1.time_ms",750);parameter(*p,tape?"feedback":"tap1.feedback",35);p->prepareToPlay(48000,256);}
   if(mode==4)juce::Thread::sleep(300); // Message-side stale-publication evidence; no DSP clock.
   editor.refreshBindings();
   for(auto size:{juce::Point<int>(1003,565),juce::Point<int>(1338,753),juce::Point<int>(1672,941),juce::Point<int>(2006,1129)}){
    editor.setSize(size.x,size.y);const auto name=juce::String(tape?"tape":"multitap")+"-state"+juce::String(mode)+"-"+juce::String(size.x)+"x"+juce::String(size.y);
    const auto issues=editor.layoutIssues();report+=name+": "+(issues.isEmpty()?"PASS geometry/text fit":issues.joinIntoString("; "))+"\n";
    if(mode==3){juce::AudioBuffer<float> block(2,256);for(int c=0;c<2;++c)for(int n=0;n<256;++n)block.setSample(c,n,float(.25*std::sin(2*hem::pi*997*n/48000)));juce::MidiBuffer midi;p->processBlock(block,midi);}
    juce::Image image(juce::Image::ARGB,size.x,size.y,true);{juce::Graphics g(image);editor.paintEntireComponent(g,true);}
    if(mode==4)require(editor.statusText().contains("NO LIVE AUDIO DATA"),"Native stale render still claims live telemetry");
    auto stream=dir.getChildFile(name+".png").createOutputStream();require(stream&&juce::PNGImageFormat().writeImageToStream(image,*stream),"Cannot save native render evidence");
    require(dir.getChildFile("geometry-text-fit.txt").replaceWithText(report),"Cannot save incremental geometry/text report");
    require(issues.isEmpty(),"Native evidence geometry/text fit failed");
   }
  }
 }
 require(dir.getChildFile("geometry-text-fit.txt").replaceWithText(report),"Cannot save geometry/text report");
}
static void verifyMeterScale(DelayProcessor& p){
 const bool tape=p.delayKind()==delay::Kind::tape;
 DelayEditor e(p);require(e.getWidth()==1280&&e.getHeight()==720,"Host initial geometry changed");
 require(std::abs(e.getConstrainer()->getFixedAspectRatio()-1672.0/941.0)<1e-10,"Authored aspect ratio not locked");
 e.setSize(1672,941);parameter(p,"mix",0);parameter(p,"output",0);p.prepareToPlay(48000,256);
 const auto art=juce::ImageCache::getFromMemory(tape?ApprovedArt::approved_22_png:ApprovedArt::approved_21_png,tape?ApprovedArt::approved_22_pngSize:ApprovedArt::approved_21_pngSize);
 const auto area=tape?juce::Rectangle<float>(1499,194,98,259):juce::Rectangle<float>(1459,94,154,218);
 const float inset=tape?12.f:22.f;const auto bar=juce::Rectangle<float>(area.getX()+inset,area.getY()+25,21,area.getHeight()-50);
 juce::AudioBuffer<float> block(2,256);juce::MidiBuffer midi;
 for(float amplitude:{0.f,.001f,.25f,1.f,2.f}){
  const float right=amplitude*.125f;
  for(int c=0;c<2;++c)for(int n=0;n<256;++n)block.setSample(c,n,c?right:amplitude);
  p.processBlock(block,midi);juce::Image image(juce::Image::ARGB,1672,941,true);{juce::Graphics g(image);e.paintEntireComponent(g,true);}
  for(int side=0;side<2;++side)for(int ch=0;ch<2;++ch){
   const auto panel=side?area:(tape?juce::Rectangle<float>(74,194,100,259):juce::Rectangle<float>(68,94,146,218));
   const float cx=ch?panel.getRight()-inset-10.5f:panel.getX()+inset+10.5f;
   const int expected=int(std::floor(ui::meterAmplitudePosition(ch?right:amplitude)*24+1e-5));int lit=0;
   for(int i=0;i<24;++i){const int y=int(bar.getBottom()-(i+.5f)*bar.getHeight()/24);auto c=image.getPixelAt(int(cx),y);lit+=c.getRed()>40&&c.getGreen()>80;}
   require(lit==expected,"Independent stereo input/output segmented peak calibration mismatch");
  }
  // Sample immutable rings and chassis, outside live glass and opaque inner bodies.
  for(auto pt:tape?std::array<juce::Point<int>,4>{{{111,621},{173,683},{500,100},{900,900}}}:std::array<juce::Point<int>,4>{{{76,402},{114,441},{500,100},{900,920}}})
   require(image.getPixelAt(pt.x,pt.y)==art.getPixelAt(pt.x,pt.y),"Authored ring/chassis altered");
 }
 juce::Thread::sleep(300);juce::Image frozen(juce::Image::ARGB,1672,941,true);{juce::Graphics g(frozen);e.paintEntireComponent(g,true);}
 require(e.statusText().contains("NO LIVE AUDIO DATA"),"Frozen telemetry claimed live data");
 for(int ch=0;ch<2;++ch)for(int i=0;i<24;++i){auto c=frozen.getPixelAt(int(ch?area.getRight()-inset-10.5f:bar.getCentreX()),int(bar.getBottom()-(i+.5f)*bar.getHeight()/24));require(c.getRed()<40&&c.getGreen()<80,"Frozen stereo peak remains lit");}
 p.releaseResources();juce::Image idle(juce::Image::ARGB,1672,941,true);{juce::Graphics g(idle);e.paintEntireComponent(g,true);}
 require(e.statusText().contains("NO LIVE AUDIO DATA"),"Released processor claimed live data");
 for(int i=0;i<24;++i){auto c=idle.getPixelAt(int(bar.getCentreX()),int(bar.getBottom()-(i+.5f)*bar.getHeight()/24));require(c.getRed()<40&&c.getGreen()<80,"Baked/live meter remains after release");}
 for(int y=tape?430:303;y<(tape?435:308);++y)for(int x=335;x<1380;++x){auto c=idle.getPixelAt(x,y);require(c.getGreen()<100&&c.getRed()<100,"Authored signal glow survived below graph boundary");}
 if(tape)for(int y=395;y<405;++y)for(int x=280;x<1390;++x){auto c=idle.getPixelAt(x,y);require(c.getGreen()<100&&c.getRed()<100,"Baked lower waveform survived graph clearing");}
 // The original lower-left pointer must not coexist with a noon live pointer.
 auto* dial=dynamic_cast<juce::Slider*>(find(e,tape?"time_ms":"tap1.time_ms"));require(dial,"Missing pointer test dial");
 dial->setValue(dial->proportionOfLengthToValue(.5),juce::sendNotificationSync);
 dial->setSize(100,100);juce::Image face(juce::Image::ARGB,100,100,true);{juce::Graphics g(face);dial->getLookAndFeel().drawRotarySlider(g,0,0,100,100,.5,0,0,*dial);}
 require(face.getPixelAt(28,72).getAlpha()==255,"Inner body does not cover baked pointer");
}
static void offlineParity(){
 const auto dir=juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("hem-delay-parity-"+juce::Uuid().toString());require(dir.createDirectory().wasOk(),"Cannot create parity fixture directory");
 // Deliberately retain narrowly named test artifacts for approval-time inspection.
 const auto source=dir.getChildFile("source.wav");juce::AudioBuffer<float> input(2,4800);
 for(int i=0;i<input.getNumSamples();++i){const float x=float(.05*std::sin(2*hem::pi*997*i/48000));input.setSample(0,i,x);input.setSample(1,i,-x*.6f);}
 {juce::WavAudioFormat wav;std::unique_ptr<juce::OutputStream> stream=source.createOutputStream();auto writer=wav.createWriterFor(stream,juce::AudioFormatWriterOptions().withSampleRate(48000).withNumChannels(2).withBitsPerSample(32).withSampleFormat(juce::AudioFormatWriterOptions::SampleFormat::floatingPoint));require(writer&&writer->writeFromAudioSampleBuffer(input,0,4800)&&writer->flush(),"Cannot write parity source");}
 for(const char* id:{"multitap-delay","tape-delay"}){
  auto processor=PluginRegistry::create(id);const bool tape=juce::String(id)=="tape-delay";
  parameter(*processor,"mix",65);parameter(*processor,"sync",1);if(!tape)parameter(*processor,"tap1.on",1);else parameter(*processor,"drive",6);
  SessionSnapshot snapshot;MonitorProcessor monitor;snapshot.monitorParameters=SessionState::captureParameters(monitor);snapshot.tempoBpm=96;snapshot.tracks[0].source=source;
  snapshot.tracks[0].slots[0].pluginId=id;snapshot.tracks[0].slots[0].parameters=SessionState::captureParameters(*processor);
  InsertChain live;live.prepare(48000,128);processor->setBlockTempo(96);auto node=live.stageNode(std::move(processor),48000,128,live.slotLatency());live.installInitial(0,std::move(node),false);
  CoreMasterStage master;master.prepare(48000,128);juce::AudioBuffer<float> expected(2,128);double beforeError=0,afterError=0;
  auto compare=[&](const juce::AudioBuffer<float>& actual,juce::int64 position,int count,bool after){
   if(!after){expected.clear();for(int c=0;c<2;++c)for(int n=0;n<count;++n)if(position+n<4800)expected.setSample(c,n,input.getSample(c,int(position+n)));
    allocationProbe::enabled=true;live.process(expected.getWritePointer(0),expected.getWritePointer(1),count,96);allocationProbe::enabled=false;
   }else master.process(expected.getWritePointer(0),expected.getWritePointer(1),count);
   for(int c=0;c<2;++c)for(int n=0;n<count;++n){auto& error=after?afterError:beforeError;error=std::max(error,std::abs(double(expected.getSample(c,n))-actual.getSample(c,n)));}
  };
  OfflineMixOptions options;options.blockSize=128;options.tailSeconds=.65;
  const auto result=OfflineMixRenderer::render(snapshot,dir.getChildFile(juce::String(id)+".wav"),options,{},{},nullptr,compare);
  require(result.status.wasOk(),"Frozen delay export failed");require(beforeError<2e-6&&afterError<2e-6,"Live/offline delay or BPM parity failed");
  require(result.frames==36000,"Delay export frame/tail accounting changed");
 }
 std::cout<<"Offline fixture artifacts: "<<dir.getFullPathName()<<'\n';
}
int main(int argc,char** argv){try{
 allocationProbe::testThread=true;
 require(allocationProbe::calibrate(),"C++ new allocation detector failed eight positive controls");
 juce::ScopedJuceInitialiser_GUI gui;
 if(argc==2&&juce::String(argv[1])=="--render-only"){renderEvidence();return 0;}
 require(argc==1,"Unknown test arguments");
 const std::array<const char*,35> expectedIds{{"sipa","surgical-eq","vca","true-peak","dhta","transient","sub-bass","spectral-restorer","vocal-presence","triode-tube","tape","monitor-eq","dc-subsonic","smart-gate","vocal-rider","plosive-control","hum-buzz","loudness-lab","spectrum-spectrogram","delta-monitor","convolution-reverb","algorithmic-reverb","chorus-flanger-phaser","rotary-tremolo-autopan","multitap-delay","tape-delay","dynamic-eq-8","mid-side-eq","multiband-dynamics","multiband-imager","parallel-distortion","exciter","dynamic-resonance-suppressor","breath-mouth-control","clip-repair"}};
 require(PluginRegistry::entries().size()==expectedIds.size(),"Integrated registry size changed");
 for(size_t i=0;i<expectedIds.size();++i)require(juce::String(PluginRegistry::entries()[i].id)==expectedIds[i],"Stable registry prefix or recovered family suffix changed");
 MultitapDelayProcessor multi;TapeDelayProcessor tape;
 for(auto* p:std::array<DelayProcessor*,2>{&multi,&tape}){
    const auto parameters=SessionState::captureParameters(*p);require(p->validateStoredParameters(parameters),"Valid captured state rejected");
    auto invalid=parameters.createCopy();invalid.getChild(1).setProperty("value",12.5,nullptr);require(!p->validateStoredParameters(invalid),"Fractional division accepted");
    bool rejected=false;try{SessionState::restoreParameters(*p,invalid);}catch(...){rejected=true;}require(rejected,"Session accepted fractional division");
    juce::MemoryBlock state;p->getStateInformation(state);p->setStateInformation(state.getData(),int(state.getSize()));require(parameters.isEquivalentTo(SessionState::captureParameters(*p)),"State roundtrip changed controls");
    for(int bad=0;bad<7;++bad){auto tree=p->state.copyState();
     if(bad==0)tree.setProperty("delayStateVersion",2,nullptr);
     if(bad==1)tree.getChild(0).setProperty("value","nan",nullptr);
     if(bad==2)tree.getChild(0).setProperty("id","unknown",nullptr);
     if(bad==3)tree.removeChild(0,nullptr);
     if(bad==4)tree.getChild(1).setProperty("id",tree.getChild(0)["id"],nullptr);
     if(bad==5)tree.getChild(0).setProperty("unexpected",1,nullptr);
     if(bad==6)tree.getChild(0).setProperty("value","250junk",nullptr);
     juce::MemoryBlock invalidState;auto xml=tree.createXml();juce::AudioProcessor::copyXmlToBinary(*xml,invalidState);p->setStateInformation(invalidState.getData(),int(invalidState.getSize()));
     require(parameters.isEquivalentTo(SessionState::captureParameters(*p)),"Malformed direct state partially committed");
    }
    p->prepareToPlay(48000,256);juce::AudioBuffer<float> buffer(2,256);buffer.clear();juce::MidiBuffer midi;allocationProbe::enabled=true;p->processBlock(buffer,midi);allocationProbe::enabled=false;require(p->meterSnapshot().hasProcessedAudio,"Real meter telemetry was not published");
    auto editor=std::unique_ptr<juce::AudioProcessorEditor>(p->createEditor());require(editor->getNumChildComponents()>12,"Original-layout controls missing");
 }
 DelayEditor selected(multi);selected.selectTap(7);require(selected.selectedTap()==7,"Shared filter selection failed");
 parameter(tape,"sync",1);parameter(tape,"division",19);parameter(tape,"mix",100);
 require(!tape.capturedTail(48000,20).valid,"Unrepresentable audible sync accepted");
 require(tape.capturedTail(48000,20,false).valid,"Muted path blocked sync export");
 parameter(tape,"sync",0);require(!tape.tapTempo(1000)&&tape.tapTempo(1500),"Tap tempo interval handling failed");require(tape.settings().taps[0].time.manualMilliseconds==500,"Tap tempo did not set milliseconds");
 SessionSnapshot snapshot;MonitorProcessor monitor;snapshot.monitorParameters=SessionState::captureParameters(monitor);
 const auto file=juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("hem-delay-session-reference.xml");
 snapshot.tempoBpm=83.25;auto encoded=SessionState::encode(snapshot,file);require(SessionState::decode(encoded,file).tempoBpm==83.25,"BPM persistence failed");
 encoded.removeProperty("tempoBpm",nullptr);require(SessionState::decode(encoded,file).tempoBpm==120,"Legacy BPM fallback failed");
 encoded.setProperty("tempoBpm","120junk",nullptr);bool rejected=false;try{SessionState::decode(encoded,file);}catch(...){rejected=true;}require(rejected,"Malformed BPM accepted");
 snapshot.tracks[0].source=file; // preflight checks metadata; parity test below supplies a real WAV
 auto& slot=snapshot.tracks[0].slots[0];slot.pluginId="tape-delay";parameter(tape,"feedback",0);parameter(tape,"time_ms",250);slot.parameters=SessionState::captureParameters(tape);
 OfflineMixOptions options;options.tailSeconds=.251;require(OfflineMixRenderer::preflightTail(snapshot,options).wasOk(),"One-shot export required live safety cap");
 options.tailSeconds=.1;require(OfflineMixRenderer::preflightTail(snapshot,options).failed(),"Short one-shot tail accepted without truncation");options.allowTailTruncation=true;require(OfflineMixRenderer::preflightTail(snapshot,options).wasOk(),"Explicit tail truncation was ignored");
 parameter(tape,"feedback",95);parameter(tape,"time_ms",4000);slot.parameters=SessionState::captureParameters(tape);snapshot.tracks[0].slots[1]=slot;
 options.tailSeconds=3600;options.allowTailTruncation=false;require(OfflineMixRenderer::preflightTail(snapshot,options).failed(),"Serial long tails silently clamped to export limit");options.allowTailTruncation=true;require(OfflineMixRenderer::preflightTail(snapshot,options).wasOk(),"Explicit serial-tail truncation rejected");
 parameter(tape,"sync",1);parameter(tape,"division",19);snapshot.tempoBpm=20;slot.parameters=SessionState::captureParameters(tape);snapshot.tracks[0].slots[1]=slot;
 require(OfflineMixRenderer::preflightTail(snapshot,options).failed(),"Truncation wrongly permits unresolved audible sync");
 snapshot.tracks[0].muted=true;require(OfflineMixRenderer::preflightTail(snapshot,options).wasOk(),"Muted sync conflict blocked export");snapshot.tracks[0].muted=false;
 snapshot.tracks[0].source=juce::File();require(OfflineMixRenderer::preflightTail(snapshot,options).wasOk(),"Fresh source-free delay path blocked export");
 InsertChain chain;chain.prepare(48000,256);auto node=chain.stageNode(std::make_unique<TapeDelayProcessor>(),48000,256,chain.slotLatency());auto* original=node->processor.get();chain.installInitial(0,std::move(node),false);
 bool failedStage=false;try{chain.stageNode(std::make_unique<TapeDelayProcessor>(),std::numeric_limits<double>::quiet_NaN(),256,chain.slotLatency());}catch(const std::exception&){failedStage=true;}require(failedStage,"Invalid staged preparation unexpectedly succeeded");
 require(chain.activeProcessor(0)==original&&original->processingReady(),"Failed staging replaced the live chain");
 allocationProbe::failAboveBytes=65536;bool allocationFailed=false;try{chain.stageDelayPreparations(384000,256);}catch(const std::exception&){allocationFailed=true;}allocationProbe::failAboveBytes=0;
 require(allocationFailed&&chain.activeProcessor(0)==original&&original->actualRate.load()==48000&&original->processingReady(),"Allocation failure committed staged delay rate or ownership");
 std::array<float,256> l{},r{};chain.process(l.data(),r.data(),256,91.5);require(static_cast<DelayProcessor*>(original)->bpm()==91.5,"Immutable host block tempo not supplied");
 TapeDelayProcessor frozen;parameter(frozen,"mix",100);parameter(frozen,"sync",1);frozen.configureFrozenExport(120,true);frozen.prepareToPlay(8000,64);frozen.setBlockTempo(60);
 juce::AudioBuffer<float> impulse(2,4100);impulse.clear();impulse.setSample(0,0,1);juce::MidiBuffer midi;frozen.processBlock(impulse,midi);require(std::abs(impulse.getSample(0,4016)-1)<1e-5,"Frozen BPM changed during export");
 MultitapDelayProcessor controlMulti;TapeDelayProcessor controlTape;verifyReviewFixes(controlMulti);verifyReviewFixes(controlTape);verifyControls(controlMulti);verifyControls(controlTape);verifyMeterScale(controlMulti);verifyMeterScale(controlTape);offlineParity();renderEvidence();
 require(allocationProbe::count==0,"Delay processor/chain allocated in processBlock");
 std::cout<<"PASS delay state, BPM, controls, preflight, staging and frozen context checks\n";return 0;
 }catch(const std::exception& e){allocationProbe::enabled=false;allocationProbe::failAboveBytes=0;std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}}
