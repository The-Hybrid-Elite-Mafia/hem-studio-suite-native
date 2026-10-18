#include "../Source/Plugins/FinishingEditors.h"
#include <iostream>
#include <stdexcept>
#include <thread>
#include <cstdlib>
#include <new>
static thread_local bool checking=false;
static std::atomic<int> allocations{0};
void* operator new(std::size_t n){if(checking)++allocations;if(auto* p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void* operator new[](std::size_t n){return ::operator new(n);}
void operator delete(void* p) noexcept{std::free(p);}
void operator delete[](void* p) noexcept{std::free(p);}
void operator delete(void* p,std::size_t) noexcept{std::free(p);}
void operator delete[](void* p,std::size_t) noexcept{std::free(p);}
static void require(bool b,const char* s){if(!b)throw std::runtime_error(s);}
using Spectrum=hem::VocalSpectrum;
static Spectrum::Snapshot waitFor(Spectrum& w,double rate,std::uint64_t end){
 Spectrum::Snapshot s;for(int i=0;i<600;++i){if(w.copyLatest(s)&&s.sampleRate==rate&&s.frameEnd>=end)return s;juce::Thread::sleep(5);}throw std::runtime_error("No spectrum frame");
}
static void tone(std::vector<float>& l,std::vector<float>& r,int mode=0){
 for(size_t i=0;i<l.size();++i){l[i]=.5f*float(std::sin(juce::MathConstants<double>::twoPi*86*i/Spectrum::size));r[i]=mode?-l[i]:.25f*float(std::sin(juce::MathConstants<double>::twoPi*172*i/Spectrum::size));}
}
static juce::Image save(juce::Component& c,const juce::File& folder,const char* name){
 auto image=c.createComponentSnapshot(c.getLocalBounds(),true);juce::PNGImageFormat png;
 auto out=folder.getChildFile(name).createOutputStream();require(out!=nullptr,"PNG open");require(png.writeImageToStream(image,*out),"PNG write");return image;
}
static int tracePixels(const juce::Image& image){int n=0;for(int y=160;y<239;++y)for(int x=809;x<1092;++x){auto c=image.getPixelAt(x,y);if(c.getGreen()>160&&c.getBlue()>160&&c.getRed()<125)++n;}return n;}
int main(int argc,char** argv){try{
 juce::ScopedJuceInitialiser_GUI gui;
 auto worker=std::make_unique<Spectrum>();require(worker->operational(),"worker startup");
 std::vector<float> l(Spectrum::size*2),r(l.size());tone(l,r);
 for(double rate:{8000.,22050.,44100.,48000.,88200.,96000.,176400.,192000.})for(int block:{1,32,127,256,511,1024,4096}){
  worker->prepare(rate);checking=true;for(int offset=0;offset<int(l.size());offset+=block)worker->push(l.data()+offset,r.data()+offset,std::min(block,int(l.size())-offset));checking=false;
  auto s=waitFor(*worker,rate,l.size());
  require(std::abs(s.db[86]+6.0206f)<.02f,"L amplitude calibration");require(std::abs(s.db[172]+12.0412f)<.02f,"R amplitude calibration");
  require(!s.silent,"tone flagged silence");
 }
 worker->prepare(48000);for(size_t i=0;i<l.size();++i){l[i]=.5f;r[i]=i%2?.25f:-.25f;}
 worker->push(l.data(),r.data(),int(l.size()));auto edge=waitFor(*worker,48000,l.size());
 require(std::abs(edge.db[0]+6.0206f)<.02f&&std::abs(edge.db[Spectrum::bins-1]+12.0412f)<.02f,"DC/Nyquist calibration");
 worker->prepare(48000);tone(l,r,1);worker->push(l.data(),r.data(),int(l.size()));
 require(std::abs(waitFor(*worker,48000,l.size()).db[86]+6.0206f)<.02f,"antiphase cancellation");
 std::fill(l.begin(),l.end(),0);std::fill(r.begin(),r.end(),0);worker->prepare(48000);worker->push(l.data(),r.data(),int(l.size()));
 auto zero=waitFor(*worker,48000,l.size());require(zero.silent&&zero.db[86]==-160,"silence floor");
 worker->suspend();require(!worker->copyLatest(zero),"suspended old frame exposed");
 tone(l,r);for(int i=0;i<100;++i){worker->prepare(48000);worker->push(l.data(),r.data(),int(l.size()));}
 juce::Thread::sleep(150);worker->push(l.data(),r.data(),int(l.size()));auto dropped=waitFor(*worker,48000,l.size()*2);
 require(dropped.droppedFrames>0,"overflow missing");
 std::atomic<bool> done{false},bad{false};
 std::thread reader([&]{Spectrum::Snapshot s;while(!done.load()){if(worker->copyLatest(s)&&(!std::isfinite(s.db[86])||s.sampleRate<=0))bad=true;std::this_thread::yield();}});
 for(int i=0;i<60;++i){worker->prepare(i%2?44100:192000);worker->push(l.data(),r.data(),int(l.size()));juce::Thread::sleep(2);}
 done=true;reader.join();require(!bad,"concurrent snapshot invalid");
 std::cout<<"PASS FFT calibration: 8 rates x 7 block sizes, DC/Nyquist, stereo max, antiphase, silence, epochs, overflow, concurrent snapshots\n";
 for(double rate:{44100.,48000.,96000.,192000.})for(int block:{1,32,127,256,511,1024,4096}){
  hem::VocalProcessor p;hem::VocalCore reference;hem::VocalSettings settings;
  p.prepareToPlay(rate,block);reference.prepare(rate);juce::AudioBuffer<float> b(2,block),expected(2,block);juce::MidiBuffer midi;
  for(int pass=0;pass<4;++pass){
   settings.body=float(pass);settings.presence=float(-pass);settings.output=float(-pass);settings.cutEnabled=pass>1;
   for(auto pair:{std::pair<const char*,float>{"body",settings.body},{"presence",settings.presence},{"output",settings.output},{"cutEnabled",settings.cutEnabled?1.f:0.f}}){auto* a=p.state.getParameter(pair.first);a->setValueNotifyingHost(a->convertTo0to1(pair.second));}
   for(int i=0;i<block;++i){b.setSample(0,i,.2f*float(std::sin((pass*block+i)*.123)));b.setSample(1,i,.1f*float(std::cos((pass*block+i)*.137)));}
   expected.makeCopyOf(b);reference.process(expected.getWritePointer(0),expected.getWritePointer(1),block,settings);
   checking=true;p.processBlock(b,midi);checking=false;
   for(int ch=0;ch<2;++ch)require(std::memcmp(b.getReadPointer(ch),expected.getReadPointer(ch),size_t(block)*sizeof(float))==0,"output differs from untouched core");
  }
  p.releaseResources();
 }
 require(allocations.load()==0,"callback C++ new allocation");
 std::cout<<"PASS sample-exact core parity: 4 rates x 7 blocks x 4 settings; zero callback C++ new/new[] calls\n";
 hem::VocalProcessor processor;processor.prepareToPlay(48000,256);juce::MidiBuffer midi;
 const juce::File folder(argc>1?argv[1]:"spectrum-renders");folder.createDirectory();
 auto editor=std::unique_ptr<juce::AudioProcessorEditor>(processor.createEditor());editor->setSize(1280,720);save(*editor,folder,"vocal-waiting.png");
 tone(l,r);juce::AudioBuffer<float> buffer(2,int(l.size()));buffer.copyFrom(0,0,l.data(),int(l.size()));buffer.copyFrom(1,0,r.data(),int(r.size()));
 processor.processBlock(buffer,midi);waitFor(processor.vocalSpectrum(),48000,l.size());auto renderedTone=save(*editor,folder,"vocal-tone.png");require(tracePixels(renderedTone)>20,"live trace absent from actual editor raster");
 editor->setSize(1000,563);save(*editor,folder,"vocal-minimum.png");editor->setSize(1920,1080);save(*editor,folder,"vocal-large.png");editor->setSize(1280,720);
 juce::Thread::sleep(800);require(tracePixels(save(*editor,folder,"vocal-stale.png"))==0,"stale trace was not hidden");
 buffer.clear();processor.processBlock(buffer,midi);waitFor(processor.vocalSpectrum(),48000,l.size()*2);require(tracePixels(save(*editor,folder,"vocal-silence.png"))==0,"silence displayed a false trace");
 auto* output=processor.state.getParameter("output");output->setValueNotifyingHost(output->convertTo0to1(-12.f));
 tone(l,r);buffer.copyFrom(0,0,l.data(),int(l.size()));buffer.copyFrom(1,0,r.data(),int(r.size()));processor.processBlock(buffer,midi);
 auto attenuated=waitFor(processor.vocalSpectrum(),48000,l.size()*3);require(std::abs(attenuated.db[86]+18.0618f)<.05f,"post-FX tap did not follow output gain");
 std::cout<<"PASS actual post-FX spectrum follows -12 dB output gain\n";
 editor.reset();for(int i=0;i<25;++i){processor.prepareToPlay(i%2?44100:96000,256);editor.reset(processor.createEditor());processor.processBlock(buffer,midi);editor.reset();processor.releaseResources();}
 std::cout<<"PASS actual editor renders waiting/tone/minimum/large/stale/silence; 25 editor lifecycle cycles\n";
}catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}return 0;}
