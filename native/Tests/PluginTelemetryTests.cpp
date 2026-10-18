#include "../Source/Plugins/FinishingProcessors.h"
#include "../Source/Plugins/SpecialtyProcessors.h"
#include "BaselineTransientCore.h"
#include "../Source/UI/PluginTelemetryView.h"
#include <iostream>
#include <stdexcept>
#include <thread>
#include <cstdlib>
#include <new>
static thread_local bool checking=false;static std::atomic<int> allocations{0};
void* operator new(std::size_t n){if(checking)++allocations;if(auto* p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void* operator new[](std::size_t n){return ::operator new(n);}
void operator delete(void* p) noexcept{std::free(p);}void operator delete[](void* p) noexcept{std::free(p);}
void operator delete(void* p,std::size_t) noexcept{std::free(p);}void operator delete[](void* p,std::size_t) noexcept{std::free(p);}
static void require(bool b,const char* m){if(!b)throw std::runtime_error(m);}
static void set(hem::ModuleProcessor& p,const char* id,float v){auto* a=p.state.getParameter(id);require(a!=nullptr,"parameter absent");a->setValueNotifyingHost(a->convertTo0to1(v));}
static hem::PluginAnalysis::Snapshot wait(hem::PluginAnalysis& w,std::uint64_t end){hem::PluginAnalysis::Snapshot s;for(int i=0;i<600;++i){if(w.copyLatest(s)&&s.frameEnd>=end)return s;juce::Thread::sleep(5);}throw std::runtime_error("worker timeout");}
template<class P,class C,class S,class Configure>void parity(Configure configure){
 for(double rate:{44100.,48000.,96000.,192000.})for(int block:{1,32,127,256,511,1024,4096}){
  P p;C core;S settings;p.prepareToPlay(rate,block);core.prepare(rate);juce::AudioBuffer<float>b(2,block),ref(2,block);juce::MidiBuffer midi;
  for(int pass=0;pass<4;++pass){configure(p,settings,pass);
   for(int i=0;i<block;++i){const float envelope=((i+pass*block)%700)<200?1.f:.15f;b.setSample(0,i,envelope*.3f*float(std::sin((i+pass*block)*.137)));b.setSample(1,i,envelope*.2f*float(std::cos((i+pass*block)*.091)));}
   const std::array<float,2> inputPeaks{b.getMagnitude(0,0,block),b.getMagnitude(1,0,block)};
   ref.makeCopyOf(b);{juce::ScopedNoDenormals sameProcessorMode;core.process(ref.getWritePointer(0),ref.getWritePointer(1),block,settings);}
   checking=true;p.processBlock(b,midi);checking=false;
   hem::PluginTelemetry::Levels measured;require(p.pluginTelemetry()->read(measured)&&measured.valid,"processor level capture missing");
   for(int ch=0;ch<2;++ch){require(measured.peak[ch]==inputPeaks[ch],"input tap is not pre-FX");require(measured.peak[ch+2]==b.getMagnitude(ch,0,block),"output tap is not post-FX");}
   if(p.meterKind()==hem::PluginMeterKind::envelopeGain)require(std::abs(p.meter())==std::max(std::abs(measured.gains[0]),std::abs(measured.gains[1])),"transient gain publication mismatch");
   for(int ch=0;ch<2;++ch)if(std::memcmp(b.getReadPointer(ch),ref.getReadPointer(ch),size_t(block)*sizeof(float))!=0){
    std::cerr<<"PARITY "<<p.getName()<<" rate="<<rate<<" block="<<block<<" pass="<<pass<<" channel="<<ch<<"\n";
    for(int i=0;i<block;++i)if(b.getSample(ch,i)!=ref.getSample(ch,i)){std::cerr.precision(12);std::cerr<<"sample "<<i<<" actual="<<b.getSample(ch,i)<<" reference="<<ref.getSample(ch,i)<<"\n";break;}
    for(const auto& spec:p.parameterSpecs())std::cerr<<spec.id<<"="<<p.state.getRawParameterValue(spec.id)->load()<<" ";std::cerr<<"\n";
    require(false,"DSP output parity failed");
   }
  }p.releaseResources();
 }
}
static void save(juce::Component& c,const juce::File& folder,const juce::String& name){auto image=c.createComponentSnapshot(c.getLocalBounds(),true);auto out=folder.getChildFile(name+".png").createOutputStream();require(out!=nullptr,"render open");juce::PNGImageFormat png;require(png.writeImageToStream(image,*out),"render encode");}
int main(int argc,char** argv){try{juce::ScopedJuceInitialiser_GUI gui;
 auto t=std::make_unique<hem::PluginTelemetry>();std::array<float,4096> l,r,ol,orr;l.fill(.5f);r.fill(.25f);ol.fill(.25f);orr.fill(.125f);
 for(double rate:{8000.,44100.,48000.,96000.,192000.})for(int block:{1,32,127,256,511,1024,4096}){
  t->prepare(rate);int total=int(rate*.1),done=0;
  while(done<total){int n=std::min(block,total-done);checking=true;t->input(l.data(),r.data(),n);t->output(ol.data(),orr.data(),n);checking=false;done+=n;}
  hem::PluginTelemetry::Levels v;require(t->read(v)&&v.valid,"level snapshot invalid");
  require(v.peak[0]==.5f&&v.peak[1]==.25f&&v.peak[2]==.25f&&v.peak[3]==.125f,"input/output peaks wrong");
  const double response=std::sqrt(1-std::exp(-total/(rate*.3)));
  for(int ch=0;ch<4;++ch)require(std::abs(v.rms[ch]-v.peak[ch]*response)<1.e-6,"RMS time constant/block invariance wrong");
 }
 std::vector<float> a(16384),b(16384);for(int i=0;i<16384;++i){a[i]=.5f*float(std::sin(juce::MathConstants<double>::twoPi*86*i/8192));b[i]=-.25f*float(std::sin(juce::MathConstants<double>::twoPi*86*i/8192));}
 t->prepare(48000);juce::Thread::sleep(80);t->input(a.data(),b.data(),16384);t->output(a.data(),b.data(),16384);auto f=wait(t->analysis,16384);
 require(std::abs(f.db[86]+6.0206f)<.02,"FFT max-channel calibration");
 require(std::abs(f.dbL[86]+6.0206f)<.02&&std::abs(f.dbR[86]+12.0412f)<.02,"independent stereo FFT calibration");
 for(int i=0;i<1024;++i)require(f.phaseR[i]==-.5f*f.phaseL[i],"goniometer paired samples corrupted");
 for(int bin=0;bin<256;++bin){float lo=1,hi=-1;for(int i=0;i<32;++i){lo=std::min(lo,a[8192+bin*32+i]);hi=std::max(hi,a[8192+bin*32+i]);}require(lo==f.wave[bin].loL&&hi==f.wave[bin].hiL,"waveform extrema/time order");}
 for(int i=0;i<100;++i)t->analysis.push(a.data(),b.data(),16384);juce::Thread::sleep(150);t->analysis.push(a.data(),b.data(),16384);juce::Thread::sleep(150);require(t->analysis.copyLatest(f)&&f.droppedFrames>0,"overflow unreported");
 t->suspend();require(!t->analysis.copyLatest(f),"suspended history leaked");
 t->prepare(48000);std::vector<float> zero(16384,0);t->input(zero.data(),zero.data(),16384);t->output(zero.data(),zero.data(),16384);require(wait(t->analysis,16384).silent,"silent waveform/FFT invalid");
 float invalid=std::numeric_limits<float>::quiet_NaN(),validSample=0;t->input(&invalid,&validSample,1);t->output(&validSample,&validSample,1);hem::PluginTelemetry::Levels invalidLevel;require(t->read(invalidLevel)&&!invalidLevel.valid,"invalid sample presented as valid");
 std::atomic<bool> done{false};std::thread reader([&]{hem::PluginAnalysis::Snapshot v;while(!done){t->analysis.copyLatest(v);std::this_thread::yield();}});
 for(int i=0;i<30;++i){t->prepare(i%2?44100:192000);t->input(a.data(),b.data(),16384);t->output(a.data(),b.data(),16384);}done=true;reader.join();
 std::cout<<"PASS input/output stereo peaks and 300ms RMS: 5 rates x 7 blocks; FFT, paired phase samples, exact waveform extrema/time order, drops/epochs/concurrent snapshots\n";
 t->prepare(48000);for(int block=1;block<=1200;++block){checking=true;t->input(l.data(),r.data(),256);t->output(ol.data(),orr.data(),256,{},block*.01f,block*.02f);checking=false;}
 hem::PluginTelemetry::MeterPoint point;require(t->readMeterPoint(600,point)&&point.value==1200*.01f&&point.auxiliary==1200*.02f&&point.endSample==307200,"reduction history payload/time calibration");
 require(!t->readMeterPoint(1,point),"overwritten reduction point accepted");t->prepare(48000);require(!t->readMeterPoint(600,point),"old reduction epoch accepted");
 std::cout<<"PASS reduction history values, sample-time, 512-time-bucket wrap and prepare epoch; independent L/R FFT calibration\n";
 parity<hem::SipaProcessor,hem::StereoCore,hem::StereoParameters>([](auto& p,auto& s,int n){s.width=100+n*10;s.output=-float(n);s.invertR=n>1;set(p,"width",s.width);set(p,"output",s.output);set(p,"invertR",s.invertR?1.f:0.f);});
 parity<hem::TapeProcessor,hem::TapeCore,hem::TapeSettings>([](auto& p,auto& s,int n){s.drive=float(n);s.mix=100-n*10.f;s.output=-float(n);set(p,"drive",s.drive);set(p,"mix",s.mix);set(p,"output",s.output);});
 parity<hem::SubBassProcessor,hem::SubBassCore,hem::SubBassSettings>([](auto& p,auto& s,int n){s.lowGain=float(n);s.drive=float(n);s.output=-float(n);set(p,"lowGain",s.lowGain);set(p,"drive",s.drive);set(p,"output",s.output);});
 parity<hem::TransientProcessor,hem::BaselineTransientCore,hem::TransientSettings>([](auto& p,auto& s,int n){s.attack=n*2.f;s.sustain=-n*2.f;s.mix=100-n*20.f;s.output=-float(n);set(p,"attack",s.attack);set(p,"sustain",s.sustain);set(p,"mix",s.mix);set(p,"output",s.output);});
 require(allocations==0,"audio callback allocated C++ new/new[]");
 std::cout<<"PASS all four processors sample-exact vs unchanged cores (frozen original Transient): 4 rates x 7 blocks x 4 settings; zero callback new/new[]\n";
 hem::TransientCore gains;gains.prepare(48000);hem::TransientSettings gs;gs.attack=12;gs.sustain=-12;gs.mix=50;gs.output=-6;
 bool attack=false,sustain=false;float phase=0;
 for(int block=0;block<500;++block){for(int i=0;i<256;++i){float v=block%60<15?.4f:.002f;l[i]=r[i]=v*std::sin(phase);phase+=.17f;}gains.process(l.data(),r.data(),256,gs);
  const auto& v=gains.gainTelemetry;if(block>80){for(int branch=0;branch<2;++branch){float expected=20*std::log10(1+.5f*(std::pow(10.f,v[branch]/20)-1));require(std::abs(expected-v[branch+2])<.002,"applied gain not actual mix multiplier");}attack|=v[0]>1;sustain|=v[1]<-1;}}
 require(attack&&sustain,"attack/sustain branch fixtures did not activate");std::cout<<"PASS attack/sustain detector vs actual mixed signed gain, excludes output trim\n";
 juce::File folder(argc>1?argv[1]:"telemetry-renders");folder.createDirectory();
 for(int type=0;type<4;++type){std::unique_ptr<hem::ModuleProcessor> p;juce::String name;
  if(type==0){p=std::make_unique<hem::TapeProcessor>();name="tape";}if(type==1){p=std::make_unique<hem::SipaProcessor>();name="sipa";}if(type==2){p=std::make_unique<hem::TransientProcessor>();name="transient";}if(type==3){p=std::make_unique<hem::SubBassProcessor>();name="sub-bass";}
  p->prepareToPlay(48000,256);auto editor=std::unique_ptr<juce::AudioProcessorEditor>(p->createEditor());save(*editor,folder,name+"-waiting");
  juce::AudioBuffer<float> audio(2,16384);juce::MidiBuffer midi;audio.copyFrom(0,0,a.data(),16384);audio.copyFrom(1,0,b.data(),16384);p->processBlock(audio,midi);wait(p->pluginTelemetry()->analysis,16384);hem::ui::PluginTelemetryView observer;observer.poll(p->pluginTelemetry());require(observer.isLive(),"live telemetry marked stale");save(*editor,folder,name+"-live");
  editor->setSize(1152,648);save(*editor,folder,name+"-minimum");juce::Thread::sleep(800);observer.poll(p->pluginTelemetry());require(!observer.isLive(),"old audio displayed as live");save(*editor,folder,name+"-stale");
  audio.clear();p->processBlock(audio,midi);audio.clear();p->processBlock(audio,midi);wait(p->pluginTelemetry()->analysis,49152);save(*editor,folder,name+"-silence");
  if(type==2){p->prepareToPlay(48000,256);set(*p,"attack",12);set(*p,"sustain",-12);set(*p,"mix",50);set(*p,"output",-6);
   for(int i=0;i<16384;++i){float env=i%1000<250?1.f:.02f;audio.setSample(0,i,env*a[i]);audio.setSample(1,i,env*b[i]);}
   p->processBlock(audio,midi);wait(p->pluginTelemetry()->analysis,16384);save(*editor,folder,name+"-active-gain");}
  // Separate broad musical-range fixture; the calibrated single-bin tests above remain unchanged.
  p->prepareToPlay(48000,256);editor->setSize(1280,720);std::uint32_t random=0x7a103u;
  for(int i=0;i<16384;++i){random=1664525u*random+1013904223u;const float noise=float((random>>8)&0xffffff)/8388608.f-1.f;
   const float seconds=float(i)/48000.f,envelope=.22f+.78f*std::exp(-float(i%4096)/900.f);
   const float low=.23f*std::sin(juce::MathConstants<float>::twoPi*61*seconds)+.13f*std::sin(juce::MathConstants<float>::twoPi*143*seconds);
   audio.setSample(0,i,envelope*(low+.17f*noise+.12f*std::sin(juce::MathConstants<float>::twoPi*1271*seconds)));
   audio.setSample(1,i,envelope*(low*.75f-.12f*noise+.11f*std::sin(juce::MathConstants<float>::twoPi*2707*seconds)));}
  p->processBlock(audio,midi);wait(p->pluginTelemetry()->analysis,16384);save(*editor,folder,name+"-broadband");
  editor.reset();for(int i=0;i<12;++i){p->releaseResources();p->prepareToPlay(i%2?44100:96000,256);editor.reset(p->createEditor());p->processBlock(audio,midi);editor.reset();}
 }
 std::cout<<"PASS actual native renders for four editors: waiting/live/minimum/stale/silence, 48 editor epoch/lifecycle cycles\n";
}catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}return 0;}
