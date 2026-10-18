#include "../Source/Plugins/AnalysisEditors.h"
#include <iostream>
#include <stdexcept>
#include <thread>
static void require(bool b,const juce::String&s){if(!b)throw std::runtime_error(s.toStdString());}
static juce::Component*find(juce::Component&c,const juce::String&id){if(c.getComponentID()==id)return &c;for(int i=0;i<c.getNumChildComponents();++i)if(auto*p=find(*c.getChildComponent(i),id))return p;return nullptr;}
static int cyanPixels(const juce::Image&image,juce::Rectangle<int>area){
 int pixels=0;for(int y=area.getY();y<area.getBottom();++y)for(int x=area.getX();x<area.getRight();++x){auto c=image.getPixelAt(x,y);if(c.getGreen()>150&&c.getBlue()>150&&c.getRed()<80)++pixels;}return pixels;
}
static int orangePixels(const juce::Image&image,juce::Rectangle<int>area){
 int pixels=0;for(int y=area.getY();y<area.getBottom();++y)for(int x=area.getX();x<area.getRight();++x){auto c=image.getPixelAt(x,y);if(c.getRed()>180&&c.getGreen()>90&&c.getGreen()<210&&c.getBlue()<80)++pixels;}return pixels;
}
static hem::LoudnessDisplay readLoudness(hem::LoudnessLabProcessor&p){
 hem::LoudnessDisplay d;for(int i=0;i<500;++i){if(p.readAnalysis(d))return d;std::this_thread::sleep_for(std::chrono::milliseconds(4));}throw std::runtime_error("loudness display unavailable");
}
static void checkWarmupHistory(hem::LoudnessLabProcessor&p,const juce::Image&image){
 const auto d=readLoudness(p);require(d.measurement.momentaryReady&&!d.measurement.shortReady,"0.533-second window readiness");
 int finiteM=0,missingM=0,missingI=0;
 for(unsigned j=0;j<d.historyCount;++j){const auto i=(d.historyCursor+600-d.historyCount+j)%600;finiteM+=std::isfinite(d.momentary[i])?1:0;missingM+=std::isnan(d.momentary[i])?1:0;missingI+=std::isnan(d.integrated[i])?1:0;require(std::isnan(d.shortTerm[i]),"unready S stored a fake floor value");}
 require(finiteM>0&&missingM>0&&missingI>0,"warmup M/I availability not preserved");
 require(orangePixels(image,{704,265,267,141})==0,"unready S panel drew an orange trace");
 require(orangePixels(image,{651,664,583,124})==0,"unready S history drew an orange trace");
 require(cyanPixels(image,{1013,265,272,141})>5,"ready M panel lacks measured pixels");
 require(cyanPixels(image,{1014,267,145,135})==0,"M trace joined startup placeholders to measured values");
}
static void checkStartupOverrun(){
 auto worker=std::make_unique<hem::LoudnessAnalysisWorker>();
 worker->prepare(8000);worker->stop();
 std::vector<float>tone(65536);for(size_t i=0;i<tone.size();++i)tone[i]=.1f*float(std::sin(2*hem::pi*997*i/8000));
 // The actual consumer is stopped: exactly 128 x 256 samples fit. The entire
 // overrun happens before FIRST consumption, and there is NO later push/gap.
 worker->push(tone.data(),tone.data(),int(tone.size()));
 require(worker->droppedSamples()==32768,"startup drop count was not captured at producer boundary");
 require(worker->startPrepared(),"prepared loudness worker restart failed");
 hem::LoudnessDisplay d;bool measured=false;
 for(int i=0;i<1500;++i){if(worker->copy(d)&&d.measurement.frameEnd>=28000){measured=true;break;}std::this_thread::sleep_for(std::chrono::milliseconds(4));}
 require(measured,"startup overflow fixture did not consume accepted audio");
 require(d.dropped==32768&&!d.measurement.complete&&!d.measurement.integratedReady&&!d.measurement.lraReady,"startup tail loss was falsely complete");
 require(std::isnan(d.integrated[(d.historyCursor+599)%600]),"startup incomplete I was drawn as measured");
 // A reset with OLD packets/drops still pending must not inherit old counts or
 // lose new-epoch drops. No consumer is running for either producer operation.
 worker->prepare(8000);worker->stop();worker->push(tone.data(),tone.data(),int(tone.size()));
 require(worker->droppedSamples()==32768,"old epoch overflow fixture");worker->reset();
 require(worker->droppedSamples()==0,"reset inherited old epoch drops");
 worker->push(tone.data(),tone.data(),4096);
 require(worker->droppedSamples()==4096,"new epoch tail drops were hidden by pending old packets");
 require(worker->startPrepared(),"reset consumer restart failed");
 std::this_thread::sleep_for(std::chrono::milliseconds(50));
 require(!worker->copy(d),"old packets appeared as new reset measurements");
 require(worker->droppedSamples()==4096,"idle consumer changed the epoch drop count");
 worker->stop();
}
static void checkGapHistory(hem::LoudnessLabProcessor&p,juce::AudioProcessorEditor&editor){
 juce::MidiBuffer midi;juce::AudioBuffer<float>burst(2,327680);burst.clear();p.processBlock(burst,midi);
 hem::LoudnessDisplay d;bool incomplete=false;
 for(int i=0;i<750;++i){if(p.readAnalysis(d)&&d.dropped>0&&!d.measurement.complete){incomplete=true;break;}std::this_thread::sleep_for(std::chrono::milliseconds(4));}
 require(incomplete,"forced transport loss was not reported");
 require(!d.measurement.integratedReady&&std::isnan(d.integrated[(d.historyCursor+599)%600]),"incomplete I history was presented as measured");
 // Let the bounded pre-gap queue drain, then supply a new contiguous window.
 std::this_thread::sleep_for(std::chrono::milliseconds(1200));
 juce::AudioBuffer<float>audio(2,256);for(int block=0;block<140;++block){for(int c=0;c<2;++c)for(int i=0;i<256;++i)audio.setSample(c,i,.1f*std::sin(float((block*256+i)*2*hem::pi*1000/48000)));p.processBlock(audio,midi);std::this_thread::sleep_for(std::chrono::milliseconds(5));}
 std::this_thread::sleep_for(std::chrono::milliseconds(150));d=readLoudness(p);
 require(!d.measurement.complete&&!d.measurement.integratedReady,"I became complete after a missing capture range");
 int gapStart=-1,gapEnd=-1;bool hadFinite=false;
 for(unsigned j=0;j<d.historyCount;++j){const auto i=(d.historyCursor+600-d.historyCount+j)%600;if(std::isfinite(d.momentary[i])){if(gapStart>=0){gapEnd=int(j);break;}hadFinite=true;}else if(hadFinite&&gapStart<0)gapStart=int(j);}
 require(gapStart>=0&&gapEnd-gapStart>=2,"M history lacks an unavailable gap between measured windows");
 auto image=editor.createComponentSnapshot(editor.getLocalBounds(),true);const float middle=float(gapStart+gapEnd-1)*.5f;const int x=int(1010+279*middle/float(d.historyCount-1));
 require(cyanPixels(image,{x-1,266,3,140})==0,"M trace bridged an unavailable capture gap");
 p.resetAnalysis();audio.clear();for(int block=0;block<100;++block){p.processBlock(audio,midi);std::this_thread::sleep_for(std::chrono::milliseconds(5));}std::this_thread::sleep_for(std::chrono::milliseconds(150));d=readLoudness(p);
 require(d.measurement.momentaryReady&&std::isfinite(d.momentary[(d.historyCursor+599)%600])&&d.momentary[(d.historyCursor+599)%600]==hem::analysis::floorDb,"ready silence lost its real floor measurement");
}
int main(int argc,char**argv){juce::ScopedJuceInitialiser_GUI gui;try{checkStartupOverrun();for(int number:{29,30,31}){std::unique_ptr<hem::AnalysisProcessor>p;if(number==29)p=std::make_unique<hem::LoudnessLabProcessor>();if(number==30)p=std::make_unique<hem::SpectrumSpectrogramProcessor>();if(number==31)p=std::make_unique<hem::DeltaMonitorProcessor>();p->prepareToPlay(48000,256);auto editor=std::unique_ptr<juce::AudioProcessorEditor>(p->createEditor());auto*moduleEditor=dynamic_cast<hem::ModuleEditor*>(editor.get());require(moduleEditor,"host editor contract");for(const auto&spec:p->parameterSpecs()){auto*c=find(*editor,spec.id);require(c,juce::String("missing actual control ")+spec.id);require(c->getName()==spec.name,juce::String("accessible name ")+spec.id);auto*parameter=p->state.getParameter(spec.id);auto*raw=p->state.getRawParameterValue(spec.id);require(raw&&std::abs(raw->load()-spec.initial)<.0001,"startup");if(c->getProperties().contains("analysisChoiceParameter")){auto*initialButton=dynamic_cast<juce::Button*>(find(*editor,spec.initial==0?juce::String(spec.id):juce::String(spec.id)+"_"+juce::String(int(spec.initial))));require(initialButton&&initialButton->getToggleState(),"choice default unavailable before first paint");for(int value=int(spec.minimum);value<=int(spec.maximum);++value){auto*b=dynamic_cast<juce::Button*>(find(*editor,value==0?juce::String(spec.id):juce::String(spec.id)+"_"+juce::String(value)));require(b&&bool(b->onClick),"actual selector option");b->onClick();require(raw->load()==float(value),"choice click did not reach DSP");moduleEditor->refreshParameterActivity();require(b->getToggleState(),"choice feedback");}}else if(auto*combo=dynamic_cast<juce::ComboBox*>(c)){for(int value=int(spec.minimum);value<=int(spec.maximum);++value){combo->setSelectedItemIndex(value,juce::sendNotificationSync);require(raw->load()==float(value),"combo selection did not reach DSP");}}else if(auto*slider=dynamic_cast<juce::Slider*>(c)){require(slider->getDoubleClickReturnValue()==spec.initial,"dial reset");require(slider->getTextValueSuffix().trim()==spec.unit,"dial unit");for(float v:{spec.minimum,spec.maximum,spec.initial}){slider->setValue(v,juce::sendNotificationSync);require(std::abs(raw->load()-v)<=std::max(.0001f,spec.step*.51f),"dial binding");}}else if(auto*button=dynamic_cast<juce::ToggleButton*>(c)){button->setToggleState(true,juce::sendNotificationSync);require(raw->load()==1,"toggle on");button->setToggleState(false,juce::sendNotificationSync);require(raw->load()==0,"toggle off");}else require(false,"unknown control type");parameter->setValueNotifyingHost(parameter->convertTo0to1(spec.initial));require(editor->getLocalBounds().contains(c->getBounds()),"control bounds");}for(auto size:{juce::Point<int>(1000,563),juce::Point<int>(1280,720),juce::Point<int>(1672,941)}){editor->setSize(size.x,size.y);auto image=editor->createComponentSnapshot(editor->getLocalBounds(),true);require(image.isValid(),"raster failed");if(size.x==1672&&(number==30||number==31)){const auto approved=hem::ui::approvedFaceplate(number);const std::vector<juce::Point<int>>centres=number==30?std::vector<juce::Point<int>>{{952,629},{1218,629},{1489,629}}:std::vector<juce::Point<int>>{{224,664},{1448,664}};const int inner=number==30?48:66,outer=number==30?66:96;for(auto centre:centres)for(int y=centre.y-outer;y<=centre.y+outer;++y)for(int x=centre.x-outer;x<=centre.x+outer;++x){const int dx=x-centre.x,dy=y-centre.y;const int distance=dx*dx+dy*dy;if(distance<inner*inner||distance>outer*outer)continue;/* Readout wells start below the preserved dial hardware. */if((number==30&&y>=680)||(number==31&&y>=729))continue;require(image.getPixelAt(x,y)==approved.getPixelAt(x,y),"authored outer dial ring/ticks changed");}}if(argc>1){auto directory=juce::File(argv[1]);directory.createDirectory();auto stream=directory.getChildFile(juce::String(number)+"-"+juce::String(size.x)+".png").createOutputStream();require(stream!=nullptr,"raster open");juce::PNGImageFormat png;stream->setPosition(0);stream->truncate();require(png.writeImageToStream(image,*stream),"raster write");}}juce::MidiBuffer midi;juce::AudioBuffer<float>audio(2,256),reference(2,256);for(int block=0;block<(number==29?100:130);++block){for(int c=0;c<2;++c)for(int i=0;i<256;++i){const float sample=.1f*std::sin(float((block*256+i)*2*hem::pi*1000/48000));reference.setSample(c,i,sample);audio.setSample(c,i,number==31?sample*1.4f:sample);}if(number==31)static_cast<hem::DeltaMonitorProcessor&>(*p).setReferenceBlock({reference.getReadPointer(0),reference.getReadPointer(1),256,48000,std::uint64_t(block*256),true});p->processBlock(audio,midi);std::this_thread::sleep_for(std::chrono::milliseconds(5));}std::this_thread::sleep_for(std::chrono::milliseconds(150));moduleEditor->refreshParameterActivity();auto live=editor->createComponentSnapshot(editor->getLocalBounds(),true);if(number==29)checkWarmupHistory(static_cast<hem::LoudnessLabProcessor&>(*p),live);if(number==30){int pixels=0;for(int y=220;y<474;++y)for(int x=135;x<890;++x){auto colour=live.getPixelAt(x,y);if(colour.getGreen()>150&&colour.getBlue()>150&&colour.getRed()<80)++pixels;}require(pixels>50,"actual live FFT trace missing");}if(argc>1){auto stream=juce::File(argv[1]).getChildFile(juce::String(number)+"-live.png").createOutputStream();juce::PNGImageFormat png;require(stream!=nullptr,"live raster open");stream->setPosition(0);stream->truncate();require(png.writeImageToStream(live,*stream),"live raster");}if(number==29)checkGapHistory(static_cast<hem::LoudnessLabProcessor&>(*p),*editor);editor.reset();p->releaseResources();}std::cout<<"PASS 3 analysis editors: actual combobox/button/dial routes, APVTS defaults/reset, units, names, bounds 9 idle + 3 live native rasters, truthful M/S/I warmup/gap/silence history and startup/reset-epoch overflow\n";return 0;}catch(const std::exception&e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
