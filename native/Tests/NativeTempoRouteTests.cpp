#include "../Source/Systems/LiveVocalManager.h"
#include "../Source/Plugins/ModulationEditors.h"
#include <iostream>
#include <new>
#include <cstdlib>
static thread_local bool measuring=false;
static thread_local unsigned allocations=0,deletions=0;
void*operator new(std::size_t n){if(measuring)++allocations;if(auto*p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void*operator new[](std::size_t n){return ::operator new(n);}
void operator delete(void*p)noexcept{if(measuring&&p)++deletions;std::free(p);}
void operator delete[](void*p)noexcept{::operator delete(p);}
void operator delete(void*p,std::size_t)noexcept{::operator delete(p);}
void operator delete[](void*p,std::size_t)noexcept{::operator delete(p);}
static unsigned checks=0;
void check(bool b,const char*m){++checks;if(!b)throw std::runtime_error(m);}
void set(hem::ModuleProcessor&p,const char*id,float v){auto*q=p.state.getParameter(id);q->setValueNotifyingHost(q->convertTo0to1(v));}
struct Host:juce::AudioPlayHead{juce::Optional<PositionInfo>getPosition()const override{++reads;PositionInfo p;p.setBpm(93);return p;}mutable unsigned reads=0;};
struct Probe:hem::ModuleProcessor{
 Probe():ModuleProcessor("tempo-probe","Tempo Probe",{}){}
 void prepareToPlay(double,int)override{setLatencySamples(300);}
 void setBlockTempo(double v)noexcept override{bpm=v;++sets;}
 void processBlock(juce::AudioBuffer<float>& b,juce::MidiBuffer&)override{seen=bpm;++calls;frames+=b.getNumSamples();}
 juce::AudioProcessorEditor*createEditor()override{return nullptr;}
 float meter()const noexcept override{return 0;}
 juce::String meterLabel()const override{return {};}
 double bpm=0,seen=0;unsigned sets=0,calls=0,frames=0;
};
void modulation(){for(int number:{23,24}){
 std::unique_ptr<hem::ModulationProcessor>p;if(number==23)p=std::make_unique<hem::ChorusFlangerPhaserProcessor>();else p=std::make_unique<hem::RotaryTremoloAutopanProcessor>();
 p->prepareToPlay(48000,128);set(*p,"sync",1);set(*p,number==23?"rate":"speed",1);if(number==24)set(*p,"mode",1);
 juce::AudioBuffer<float>b(2,2400);juce::MidiBuffer midi;
 auto run=[&]{b.clear();measuring=true;p->processBlock(b,midi);measuring=false;};
 run();check(!p->modulationDisplay().tempoAvailable,"unset tempo falsely valid");
 Host host;p->setPlayHead(&host);run();check(p->modulationDisplay().bpm==93&&p->modulationDisplay().syncActive,"93 BPM playhead fallback lost");
 for(double bpm:{20.,73.25,120.,187.5,400.}){measuring=true;p->setBlockTempo(bpm);measuring=false;auto reads=host.reads;run();auto d=p->modulationDisplay();check(d.bpm==float(bpm)&&d.syncActive,"native BPM missing");check(std::abs(d.rateHz-float(bpm/60))<1.e-5f,"native synced rate incorrect");check(reads==host.reads,"native tempo queried playhead");}
 for(double bpm:{0.,-1.,19.9,400.1,std::numeric_limits<double>::quiet_NaN(),std::numeric_limits<double>::infinity()}){p->setBlockTempo(bpm);run();auto d=p->modulationDisplay();check(!d.tempoAvailable&&!d.syncActive&&d.bpm==0&&std::abs(d.rateHz-1)<1.e-5f,"invalid native tempo accepted or fell through to playhead");}
 p->configureFrozenExport(144,false);run();check(p->modulationDisplay().bpm==144,"frozen export tempo not inherited");p->setPlayHead(nullptr);
 }
 std::cout<<"PASS both modulation processors: unset, playhead 93, native priority, boundaries, invalid values, export\n";
}
void configurationOnlyUi(){
 hem::ChorusFlangerPhaserProcessor p;p.prepareToPlay(48000,128);set(p,"sync",1);p.setBlockTempo(73.25);
 hem::ModulationEditor editor(p,23);editor.refreshParameterActivity();
 juce::Slider* rate=nullptr;for(auto* child:editor.getChildren())if(child->getComponentID()=="rate")rate=dynamic_cast<juce::Slider*>(child);
 check(rate&&rate->getTextValueSuffix()==" c/b","native configuration not reflected before audio");
 check(!p.meterSnapshot().hasProcessedAudio&&!p.modulationDisplay().tempoAvailable,"configuration fabricated audio telemetry");
 p.setBlockTempo(0);editor.refreshParameterActivity();check(rate->getTextValueSuffix()==" Hz","invalid native config falsely syncs UI");
}
void vocal(){auto m=std::make_unique<hem::LiveVocalManager>();m->prepare(48000,128,1);
 auto node=std::make_unique<hem::TrackingInsertChain::Node>();auto probe=std::make_unique<Probe>();auto*p=probe.get();probe->prepareToPlay(48000,128);node->processor=std::move(probe);node->dryDelay.prepare(300);node->wet.reset(48000,1);m->rack.exchange(0,std::move(node));
 auto modulationNode=m->rack.makeNode("chorus-flanger-phaser",48000,128);auto*mod=dynamic_cast<hem::ModulationProcessor*>(modulationNode->processor.get());check(mod!=nullptr,"tracking modulation factory");set(*mod,"sync",1);set(*mod,"rate",1);m->rack.exchange(1,std::move(modulationNode));
 juce::AudioBuffer<float>b(2,128);b.clear();
 auto capture=[&](double bpm){measuring=true;m->capture(b,0,128,0,bpm);measuring=false;};
 capture(87);check(mod->modulationDisplay().bpm==87&&mod->modulationDisplay().syncActive,"tracking modulation did not consume callback tempo");check(p->seen==87&&p->sets==p->calls,"capture BPM did not precede DSP");m->rack.bypass(0,true);capture(161);check(p->seen==161,"bypassed rack BPM stale");
 measuring=true;m->capture(b,0,128,0);measuring=false;check(p->seen==120,"legacy capture default changed");
 m->setPrintInserts(true);hem::TakeRecorder::Setup setup;setup.file=juce::File::getSpecialLocation(juce::File::tempDirectory).getNonexistentChildFile("hem-tempo-route-"+juce::Uuid().toString(),".wav");check(m->configureTake(setup),"configure take failed");check(m->recorder.start(setup),"start take failed");
 for(int i=0;i<2000&&m->recorder.state()!=hem::TakeRecorder::State::Armed;++i)juce::Thread::sleep(1);check(m->recorder.state()==hem::TakeRecorder::State::Armed,"recorder not armed");capture(173.5);auto frames=p->frames, calls=p->calls;m->stopTake(true);check(p->frames-frames==300&&p->calls-calls==3,"drain duration changed");check(p->seen==173.5&&p->sets==p->calls,"drain lost final callback BPM");for(int i=0;i<2000&&m->recorder.busy();++i)juce::Thread::sleep(1);check(!m->recorder.busy(),"recorder stop timed out");check(m->recorder.state()==hem::TakeRecorder::State::Complete,"recording did not publish");
 std::cout<<"PASS tracking capture/default/bypass and 300-frame end-of-stream drain with final BPM; fixture "<<setup.file.getFullPathName()<<'\n';
}
int main(){try{juce::ScopedJuceInitialiser_GUI gui;modulation();configurationOnlyUi();vocal();check(!allocations&&!deletions,"callback allocation/free");std::cout<<"PASS "<<checks<<" assertions; zero callback allocations/frees\n";return 0;}catch(const std::exception&e){measuring=false;std::cerr<<"FAIL "<<e.what()<<" allocations="<<allocations<<" frees="<<deletions<<'\n';return 1;}}
