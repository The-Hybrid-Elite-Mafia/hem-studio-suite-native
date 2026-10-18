#include "../Source/DSP/ConvolutionReverbCore.h"
#include <iostream>
#include <random>
#include <thread>
#include <new>
#include <cstdlib>
#include <stdexcept>
static thread_local bool measure=false;static thread_local size_t allocations=0,deletions=0;
void* operator new(std::size_t n){if(measure)++allocations;if(auto*p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void operator delete(void*p)noexcept{if(measure&&p)++deletions;std::free(p);}void operator delete(void*p,std::size_t)noexcept{::operator delete(p);}void*operator new[](std::size_t n){return ::operator new(n);}void operator delete[](void*p)noexcept{::operator delete(p);}void operator delete[](void*p,std::size_t)noexcept{::operator delete(p);}
static size_t assertions=0;static void check(bool x,const char*m){++assertions;if(!x)throw std::runtime_error(m);}
static void process(hem::ConvolutionReverbCore&c,float*l,float*r,int n,const hem::ConvolutionReverbSettings&p){measure=true;c.process(l,r,n,p);measure=false;}
static std::vector<std::array<float,2>> render(hem::ConvolutionReverbCore&c,const std::vector<std::array<float,2>>&in,hem::ConvolutionReverbSettings p,int block){auto out=in;for(size_t i=0;i<out.size();i+=size_t(block)){int n=int(std::min(size_t(block),out.size()-i));std::vector<float>l(size_t(n),0),r(size_t(n),0);for(int j=0;j<n;++j){l[size_t(j)]=out[i+size_t(j)][0];r[size_t(j)]=out[i+size_t(j)][1];}process(c,l.data(),r.data(),n,p);for(int j=0;j<n;++j)out[i+size_t(j)]={l[size_t(j)],r[size_t(j)]};}return out;}
static juce::AudioBuffer<float> testIR(int channels=2,int frames=1001){juce::AudioBuffer<float>ir(channels,frames);ir.clear();for(int ch=0;ch<channels;++ch){ir.setSample(ch,0,ch?.3f:.7f);if(frames>31)ir.setSample(ch,31,ch?-.13f:.2f);if(frames>256)ir.setSample(ch,256,ch?.21f:-.1f);if(frames>777)ir.setSample(ch,777,ch?-.05f:.03f);if(frames>1000)ir.setSample(ch,1000,ch?.02f:-.04f);}return ir;}
static void referenceTests(){for(double rate:{8000.,44100.,48000.,96000.,192000.})for(int block:{1,17,64,127,512,1024}){
    auto ir=testIR();hem::ConvolutionReverbCore c;check(c.loadOffThread(ir,rate).wasOk(),"valid IR rejected");c.prepare(rate,block);check(c.latency()==int(std::ceil(rate*.004))&&c.latency()<=int(std::ceil(rate*.006)),"convolution latency budget changed");
    std::vector<std::array<float,2>>in(2400);std::mt19937 rng(12);std::uniform_real_distribution<float>noise(-.2f,.2f);for(int i=0;i<400;++i)in[size_t(i)]={noise(rng),noise(rng)};
    hem::ConvolutionReverbSettings p;p.mix=100;const auto out=render(c,in,p,block);double maxError=0;
    for(int i=0;i<int(in.size());++i)for(int ch=0;ch<2;++ch){double expected=0;for(int j=0;j<std::min(ir.getNumSamples(),i-c.latency()+1);++j)expected+=double(ir.getSample(ch,j))*in[size_t(i-c.latency()-j)][size_t(ch)];maxError=std::max(maxError,std::abs(expected-out[size_t(i)][size_t(ch)]));}
    check(maxError<2.e-5,"partitioned convolution differs from direct FIR reference");
    c.reset();auto repeated=render(c,in,p,block);check(repeated==out,"reset did not reset convolution deterministically");
    check(std::abs(c.tailSeconds()-1001./rate)<2./rate,"tail length incorrect");
 }
 std::cout<<"PASS direct-FIR reference, 30 rate/block pairs, partition boundaries, declared latency, deterministic reset\n";
}
static void partitionBoundaryTests(){for(int frames:{63,64,65,255,256,257,1023,1024,1025,2047,2048,2049,4095,4096,4097,8191,8192,8193,10001,100003}){juce::AudioBuffer<float>ir(2,frames);std::mt19937 rng(94);std::uniform_real_distribution<float>noise(-.005f,.005f);for(int c=0;c<2;++c)for(int i=0;i<frames;++i)ir.setSample(c,i,noise(rng));hem::ConvolutionReverbCore core;core.loadOffThread(ir,48000);core.prepare(48000,128);hem::ConvolutionReverbSettings p;p.mix=100;std::vector<std::array<float,2>>in(size_t(frames+270+192));in[0]={1,.5f};in[17]={-.7f,1};in[267]={.2f,-.3f};auto out=render(core,in,p,173);for(int i=0;i<int(out.size());++i)for(int c=0;c<2;++c){double expected=0;for(int tap:{0,17,267})if(i>=tap+192&&i-tap-192<frames)expected+=in[size_t(tap)][size_t(c)]*ir.getSample(c,i-tap-192);check(std::abs(out[size_t(i)][size_t(c)]-expected)<2.e-5,"FFT-tier boundary/sparse-input reference mismatch");}}std::cout<<"PASS all 64/256/1024/4096 tier edges, dense IR through 100003 taps, independent three-impulse FIR reference\n";}
static void mixesAndChannels(){auto ir=testIR();for(float mix:{0.f,25.f,100.f}){
 hem::ConvolutionReverbCore c;c.loadOffThread(ir,48000);c.prepare(48000,128);hem::ConvolutionReverbSettings p;p.mix=mix;std::vector<std::array<float,2>>in(2400);in[0]={1,0};auto out=render(c,in,p,293);
 for(int i=0;i<int(in.size());++i){float wet=i>=192&&i-192<ir.getNumSamples()?ir.getSample(0,i-192):0;check(std::abs(out[size_t(i)][0]-((i==192?1.f:0.f)*(1-mix*.01f)+wet*mix*.01f))<2.e-5f,"dry/wet mix incorrect");check(std::abs(out[size_t(i)][1])<2.e-6f,"stereo channels crossed");}
 }
 hem::ConvolutionReverbCore mono;auto one=testIR(1);mono.loadOffThread(one,48000);mono.prepare(48000,512);hem::ConvolutionReverbSettings p;p.mix=100;std::array<float,2048>l{};l[0]=1;process(mono,l.data(),nullptr,int(l.size()),p);for(int i=0;i<one.getNumSamples();++i)check(std::abs(l[size_t(i+192)]-one.getSample(0,i))<2.e-5f,"mono input/IR wrong");
 hem::ConvolutionReverbCore empty;empty.prepare(48000,128);p.soloWet=true;p.mix=100;std::array<float,512>a{},b{};a.fill(.25f);b.fill(-.2f);process(empty,a.data(),b.data(),512,p);check(a[192]==.25f&&b[192]==-.2f&&a[0]==0&&b[0]==0,"No-IR state is not truthful dry-safe");
 std::cout<<"PASS dry/wet, solo/no-IR, mono IR, independent stereo, oversized host blocks\n";
}
static double toneEnergy(bool lowOn,bool highOn,float hz){hem::ConvolutionReverbCore c;auto ir=testIR(1,1);ir.setSample(0,0,1);c.loadOffThread(ir,48000);c.prepare(48000,128);hem::ConvolutionReverbSettings p;p.mix=100;p.lowCutOn=lowOn;p.highCutOn=highOn;p.lowCut=1000;p.highCut=1000;double energy=0;std::array<float,128>l{},r{};for(int b=0;b<300;++b){for(int i=0;i<128;++i)l[size_t(i)]=r[size_t(i)]=.2f*std::sin(float(juce::MathConstants<double>::twoPi*hz*(b*128+i)/48000));process(c,l.data(),r.data(),128,p);if(b>200)for(float v:l)energy+=v*v;}return energy;}
static void controlTests(){check(toneEnergy(true,false,100)<toneEnergy(false,false,100)*.001,"low-cut filter ineffective");check(toneEnergy(false,true,10000)<toneEnergy(false,false,10000)*.001,"high-cut filter ineffective");
 for(float delay:{0.f,10.f,250.f}){hem::ConvolutionReverbCore c;auto ir=testIR(1,1);ir.setSample(0,0,1);c.loadOffThread(ir,48000);c.prepare(48000,128);hem::ConvolutionReverbSettings p;p.mix=100;p.predelay=delay;std::vector<std::array<float,2>>in(13000);in[0]={1,1};auto out=render(c,in,p,177);int peak=0;for(int i=1;i<int(out.size());++i)if(out[size_t(i)][0]>out[size_t(peak)][0])peak=i;check(peak==int(delay*48)+192,"predelay sample position incorrect");check(std::abs(out[size_t(peak)][0]-1)<1.e-5,"predelay amplitude incorrect");}
 hem::ConvolutionReverbCore c;c.loadOffThread(testIR(),48000);c.prepare(48000,128);hem::ConvolutionReverbSettings p;p.mix=0;p.output=6;std::array<float,128>l{},r{};l.fill(.2f);r.fill(-.3f);for(int block=0;block<3;++block){l.fill(.2f);r.fill(-.3f);process(c,l.data(),r.data(),128,p);}check(std::abs(l.back()-.2f*std::pow(10.f,.3f))<1.e-6,"output gain ineffective");p.bypass=true;for(int i=0;i<20;++i){l.fill(.2f);r.fill(-.3f);process(c,l.data(),r.data(),128,p);}check(l.back()==.2f&&r.back()==-.3f,"settled bypass not exact dry");
 std::cout<<"PASS predelay 0/10/250 ms, wet low/high filters, output gain, settled click-free bypass\n";
}
static std::vector<double> impulseResampleOracle(int sourceFrames,int sourceImpulse,double effectiveRate,double targetRate){const int latency=int(std::ceil(targetRate*.004));const double ratio=targetRate/effectiveRate;if(std::abs(ratio-1)<1.e-12){std::vector<double>result(size_t(sourceFrames+latency));result[size_t(sourceImpulse+latency)]=1;return result;}const double radius=16*std::max(1.,ratio),band=1/std::max(1.,ratio);std::vector<double>result(size_t(std::ceil((sourceFrames-1)*ratio+latency+radius))+1);double sum=0;for(size_t i=0;i<result.size();++i){const double d=double(i)-latency-sourceImpulse*ratio;if(std::abs(d)>=radius)continue;const double sinc=std::abs(d)<1.e-14?1:std::sin(juce::MathConstants<double>::pi*d*band)/(juce::MathConstants<double>::pi*d*band);result[i]=band*sinc*(.42+.5*std::cos(juce::MathConstants<double>::pi*d/radius)+.08*std::cos(2*juce::MathConstants<double>::pi*d/radius));sum+=result[i];}for(auto&v:result)v/=sum;return result;}
static void resampleTests(){for(double inputRate:{22050.,44100.,48000.,96000.})for(double outputRate:{44100.,48000.,96000.})for(float size:{25.f,100.f,200.f}){hem::ConvolutionReverbCore c;juce::AudioBuffer<float>ir(1,int(inputRate*.03));ir.clear();const int sourceImpulse=int(inputRate*.01);ir.setSample(0,sourceImpulse,1);check(c.loadOffThread(ir,inputRate,size).wasOk(),"resampling IR load failed");c.prepare(outputRate,128);hem::ConvolutionReverbSettings p;p.mix=100;const auto reference=impulseResampleOracle(ir.getNumSamples(),sourceImpulse,inputRate/(size*.01),outputRate);std::vector<std::array<float,2>>in(reference.size()+128);in[0]={1,1};auto out=render(c,in,p,67);double maxError=0;for(size_t i=0;i<out.size();++i){const double expected=i<reference.size()?reference[i]:0;maxError=std::max(maxError,std::abs(out[i][0]-expected));}check(maxError<2.e-6,"resampled coefficients differ from independent analytic sinc oracle");check(std::abs(c.tailSeconds()-(reference.size()-size_t(c.latency()))/outputRate)<1.e-8,"resampled complete tail length wrong");}
 for(auto rates:{std::array<double,2>{48000,96000},std::array<double,2>{48000,192000},std::array<double,2>{96000,44100},std::array<double,2>{8000,192000}})for(int frames:{1,512})for(int impulse:{0,frames-1}){hem::ConvolutionReverbCore c;juce::AudioBuffer<float>ir(1,frames);ir.clear();ir.setSample(0,impulse,1);c.loadOffThread(ir,rates[0]);c.prepare(rates[1],128);hem::ConvolutionReverbSettings p;p.mix=100;const auto expected=impulseResampleOracle(frames,impulse,rates[0],rates[1]);std::vector<std::array<float,2>>in(expected.size()+128);in[0]={1,1};auto out=render(c,in,p,113);double sum=0,moment=0,maxError=0;for(size_t i=0;i<out.size();++i){sum+=out[i][0];moment+=i*out[i][0];maxError=std::max(maxError,std::abs(out[i][0]-(i<expected.size()?expected[i]:0)));}check(std::abs(sum-1)<2.e-5,"IR resampling lost boundary DC gain");check(std::abs(moment/sum-(c.latency()+impulse*rates[1]/rates[0]))<.02,"resampled group delay is not the declared 4 ms");check(maxError<2.e-6,"onset/end IR boundary support differs from analytic reference");}
 std::cout<<"PASS 36 analytic resampling references plus 16 onset/end/single-tap boundary cases: unit DC gain and exact reported 4 ms group delay\n";
}
static void crossfadeTests(){hem::ConvolutionReverbCore c;c.prepare(48000,128);juce::AudioBuffer<float>ir(1,1);ir.setSample(0,0,1);hem::ConvolutionReverbSettings p;p.mix=100;std::array<float,128>l{},r{};for(int warm=0;warm<3;++warm){l.fill(.2f);r.fill(-.3f);process(c,l.data(),r.data(),128,p);}for(int phase=0;phase<2;++phase){if(phase==0)c.loadOffThread(ir,48000);else{c.reclaimOffThread();c.clearOffThread();}for(int block=0;block<30;++block){l.fill(.2f);r.fill(-.3f);process(c,l.data(),r.data(),128,p);for(int n=0;n<128;++n)check(std::abs(l[size_t(n)]-.2f)<2.e-6&&std::abs(r[size_t(n)]+.3f)<2.e-6,"identity IR transition has a gain hole");}}c.reclaimOffThread();p.mix=0;for(int warm=0;warm<3;++warm){l.fill(-0.f);r.fill(-0.f);process(c,l.data(),r.data(),128,p);}check(std::signbit(l[0])&&std::signbit(r[0]),"neutral signed zero not preserved");std::cout<<"PASS unity-IR load/clear crossfades without gain holes and bit-exact signed-zero dry endpoint\n";}
// Audio progress and off-audio retirement are separate parts of the mailbox
// protocol. These tests use exact sample budgets, never thread timing or sleeps.
static void clearRetirementProtocolTests(){
 for(double rate:{8000.,44100.,48000.,96000.,192000.})for(int block:{1,17,128,1024}){
    hem::ConvolutionReverbCore c;juce::AudioBuffer<float>ir(1,1);ir.setSample(0,0,.5f);
    check(c.loadOffThread(ir,rate).wasOk(),"protocol initial IR load");c.prepare(rate,block);
    ir.setSample(0,0,.25f);check(c.loadOffThread(ir,rate).wasOk(),"protocol replacement IR load");
    hem::ConvolutionReverbSettings p;p.mix=100;p.soloWet=true;
    std::array<float,1024>l{},r{};
    auto advance=[&](int frames){while(frames>0){const int n=std::min(frames,block);l.fill(.2f);r.fill(-.3f);process(c,l.data(),r.data(),n,p);frames-=n;}};
    const int transitionFrames=c.latency()+std::max(1,int(rate*.05));
    advance(1); // Adopt B: A is fading, not yet retired.
    c.reclaimOffThread();c.clearOffThread(); // A cannot be reclaimed here yet.
    check(!c.displaySnapshot().loaded,"clear did not remove selected resource");
    advance(transitionFrames-1); // A enters the retired mailbox exactly here.
    check(c.hasActiveIR(),"queued clear bypassed retirement ownership barrier");
    advance(transitionFrames+block); // More audio cannot reclaim retired A.
    check(c.hasActiveIR(),"audio cleared an unreclaimed engine");
    check(std::abs(l[0]-.05f)<1.e-6f&&std::abs(r[0]+.075f)<1.e-6f,
          "pending clear corrupted the valid active IR while retirement blocked");
    c.reclaimOffThread(); // Production worker service, outside measured process.
    advance(1);check(!c.hasActiveIR(),"clear not adopted after retirement service");
    advance(transitionFrames-1);advance(block);
    for(int i=0;i<block;++i)check(l[size_t(i)]==.2f&&r[size_t(i)]==-.3f,
                                "serviced clear is not bit-exact dry-safe in solo wet");
    check(c.tailSeconds()==0,"cleared transition retained a reported tail");
    c.reclaimOffThread();
 }
 std::cout<<"PASS deterministic pending-clear retirement barrier and explicit off-audio reclamation at five rates/four block sizes; exact dry-safe endpoint\n";
}
static void loadingTests(){hem::ConvolutionReverbCore c;c.prepare(48000,128);auto good=testIR();auto bad=testIR();bad.setSample(0,3,std::numeric_limits<float>::quiet_NaN());check(c.loadOffThread(bad,48000).failed(),"NaN IR accepted");check(c.loadOffThread(good,0).failed(),"invalid IR rate accepted");juce::AudioBuffer<float>empty(1,1);empty.clear();check(c.loadOffThread(empty,48000).failed(),"silent IR accepted");juce::AudioBuffer<float>surround(4,10);surround.clear();check(c.loadOffThread(surround,48000).failed(),"surround IR accepted");
 hem::ConvolutionReverbSettings p;p.mix=100;std::array<float,128>l{},r{};
 std::atomic<bool>done{false};std::thread worker([&]{for(int i=0;i<40;++i){c.loadOffThread(good,48000,i%2?50.f:150.f);c.reclaimOffThread();}done.store(true);});int blocks=0;while(!done.load()||blocks<600){for(int i=0;i<128;++i)l[size_t(i)]=r[size_t(i)]=.1f*std::sin(float(blocks*128+i));p.predelay=blocks%5*30.f;p.lowCutOn=blocks%2;p.highCutOn=blocks%3;p.mix=float(blocks%101);process(c,l.data(),r.data(),128,p);for(float v:l)check(std::isfinite(v)&&std::abs(v)<2,"runtime IR/automation unstable");++blocks;}worker.join();
 c.reclaimOffThread();c.clearOffThread();
 // The load worker has stopped, so explicitly service its reclamation duty.
 // At most one old fade remains. Finish that fade, reclaim off audio, then
 // finish the queued clear. Each phase is bounded by 4 ms warmup + 50 ms fade.
 const int transitionFrames=c.latency()+int(48000*.05);
 for(int phase=0;phase<2;++phase){for(int remaining=transitionFrames;remaining>0;){const int n=std::min(remaining,128);l.fill(.2f);r.fill(.2f);process(c,l.data(),r.data(),n,p);remaining-=n;}c.reclaimOffThread();}
 l.fill(.2f);r.fill(.2f);process(c,l.data(),r.data(),128,p);
 check(!c.hasActiveIR(),"clear did not remove active IR");check(std::abs(l.back()-.2f)<1.e-5f,"clear resource failed dry-safe");c.reclaimOffThread();
 c.prepare(96000,17);c.reset();l.fill(std::numeric_limits<float>::infinity());process(c,l.data(),r.data(),128,p);for(float v:l)check(std::isfinite(v),"nonfinite input not contained");
 std::cout<<"PASS bounded validation, concurrent runtime IR replacement, retirement, resize/reset/lifecycle, nonfinite containment\n";
}
int main(){try{referenceTests();partitionBoundaryTests();mixesAndChannels();controlTests();resampleTests();crossfadeTests();clearRetirementProtocolTests();loadingTests();check(allocations==0,"audio callback allocated");check(deletions==0,"audio callback deleted resources");std::cout<<"PASS "<<assertions<<" assertions; zero audio-thread allocations or frees\n";return 0;}catch(const std::exception&e){measure=false;std::cerr<<"FAIL "<<e.what()<<" allocations="<<allocations<<" frees="<<deletions<<'\n';return 1;}}
