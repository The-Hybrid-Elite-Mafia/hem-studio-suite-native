#include <JuceHeader.h>
#include "../Source/Systems/ReadAheadTransport.h"
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <future>
#include <chrono>
#include <cstdlib>
#include <new>
#include <malloc.h>
namespace callbackAudit {thread_local bool enabled=false;thread_local size_t allocated=0,freed=0;}
void* operator new(size_t n){if(callbackAudit::enabled)++callbackAudit::allocated;if(auto* p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void* operator new[](size_t n){return ::operator new(n);}
void operator delete(void* p)noexcept{if(p&&callbackAudit::enabled)++callbackAudit::freed;std::free(p);}
void operator delete[](void* p)noexcept{::operator delete(p);}
void operator delete(void* p,size_t)noexcept{::operator delete(p);}
void operator delete[](void* p,size_t)noexcept{::operator delete(p);}
void* operator new(size_t n,std::align_val_t a){if(callbackAudit::enabled)++callbackAudit::allocated;if(auto* p=_aligned_malloc(n?n:1,size_t(a)))return p;throw std::bad_alloc();}
void* operator new[](size_t n,std::align_val_t a){return ::operator new(n,a);}
void operator delete(void* p,std::align_val_t)noexcept{if(p&&callbackAudit::enabled)++callbackAudit::freed;_aligned_free(p);}
void operator delete[](void* p,std::align_val_t a)noexcept{::operator delete(p,a);}
void operator delete(void* p,size_t,std::align_val_t a)noexcept{::operator delete(p,a);}
void operator delete[](void* p,size_t,std::align_val_t a)noexcept{::operator delete(p,a);}

namespace hem { struct ReadAheadTransportTestAccess {static void hook(ReadAheadTransport& t,std::function<void()> f){t.setReadHookForTesting(std::move(f));}}; }
static void check(bool value,const char* why){if(!value)throw std::runtime_error(why);}
static size_t auditedBlocks=0;
static bool auditedRender(hem::ReadAheadTransport& t,const juce::AudioSourceChannelInfo& info){
 const auto a=callbackAudit::allocated,f=callbackAudit::freed;callbackAudit::enabled=true;
 (void)t.availableRt(info.numSamples);(void)t.getCurrentPosition();const bool result=t.renderRt(info);
 callbackAudit::enabled=false;++auditedBlocks;check(a==callbackAudit::allocated&&f==callbackAudit::freed,"callback allocated or freed memory");return result;
}
static void auditedToggle(hem::ReadAheadTransport& t,bool playing){const auto a=callbackAudit::allocated,f=callbackAudit::freed;callbackAudit::enabled=true;if(playing)t.start();else t.stop();callbackAudit::enabled=false;check(a==callbackAudit::allocated&&f==callbackAudit::freed,"transport toggle allocated or freed");}
static bool auditedCommit(hem::ReadAheadTransport& t,uint64_t token,bool play){const auto a=callbackAudit::allocated,f=callbackAudit::freed;callbackAudit::enabled=true;const bool result=t.commitPreparedEpoch(token,play);callbackAudit::enabled=false;check(a==callbackAudit::allocated&&f==callbackAudit::freed,"epoch commit allocated or freed");return result;}
int main(){juce::ScopedJuceInitialiser_GUI gui;juce::TimeSliceThread disk("Transport test disk");disk.startThread();
 try{callbackAudit::enabled=true;auto* ordinary=::operator new(31);::operator delete(ordinary);auto* aligned=::operator new(64,std::align_val_t(64));::operator delete(aligned,std::align_val_t(64));callbackAudit::enabled=false;check(callbackAudit::allocated==2&&callbackAudit::freed==2,"allocation hooks not active");callbackAudit::allocated=callbackAudit::freed=0;
 juce::AudioFormatManager formats;formats.registerBasicFormats();auto folder=juce::File::getCurrentWorkingDirectory().getChildFile("performance-dense-work/transport-design/fixtures");folder.createDirectory();
 for(int length:{1,17,257,48007}){
  auto file=folder.getChildFile("stereo-"+juce::String(length)+".wav");file.deleteFile();juce::WavAudioFormat wav;auto stream=file.createOutputStream();std::unique_ptr<juce::AudioFormatWriter> writer(wav.createWriterFor(stream.get(),48000,2,24,{},0));check(bool(writer),"writer");stream.release();
  juce::AudioBuffer<float> samples(2,length);for(int n=0;n<length;++n){samples.setSample(0,n,float(n%97)*.001f);samples.setSample(1,n,-float(n%79)*.002f);}check(writer->writeFromAudioSampleBuffer(samples,0,length),"write");writer.reset();
  std::unique_ptr<juce::AudioFormatReader> decoder(formats.createReaderFor(file));juce::AudioBuffer<float> expected(2,length);check(decoder->read(&expected,0,length,0,true,true),"decodeoracle");
  for(int block:{1,63,255,256,513}){hem::ReadAheadTransport transport;transport.prepareFile(file,formats,disk,48000,256,false);check(!transport.isPlaying(),"prepareautoplay");check(transport.getSourceChannels()==2&&transport.getSourceSampleRate()==48000,"metadata");auditedToggle(transport,true);juce::AudioBuffer<float> output(2,block);int cursor=0;
   while(transport.isPlaying()){for(int wait=0;wait<2000&&!transport.availableRt(block);++wait)juce::Thread::sleep(1);check(transport.availableRt(block),"unexpectedstarvation");check(auditedRender(transport,{&output,0,block}),"renderfailure");for(int c=0;c<2;++c)for(int n=0;n<block;++n){const float target=cursor+n<length?expected.getSample(c,cursor+n):0;check(output.getSample(c,n)==target,"exactidentityorEOF");}cursor+=block;}
   check(std::abs(transport.getCurrentPosition()-double(length)/48000)<1e-12,"emittedEOFcursor");check(!transport.hasUnderrun(),"EOFcountedasunderrun");
   uint64_t token=0;const int seek=length/2;check(transport.prepareEpoch(double(seek)/48000,false,token).wasOk(),"seekprepare");check(auditedCommit(transport,token,true),"seekcommit");const int actualSeek=int(std::floor(double(seek)/48000*48000));check(auditedRender(transport,{&output,0,block}),"seekrender");check(output.getSample(0,0)==expected.getSample(0,actualSeek),"seekfirstsample");
   check(transport.prepareEpoch(0,true,token).wasOk(),"loopprepare");check(auditedCommit(transport,token,true),"loopcommit");check(auditedRender(transport,{&output,0,block}),"looprender");for(int n=0;n<block;++n)check(output.getSample(0,n)==expected.getSample(0,n%length),"loopwrap");
   auditedToggle(transport,false);check(auditedRender(transport,{&output,0,block}),"stop fade failed");
   check(auditedRender(transport,{&output,0,block})&&output.getMagnitude(0,block)==0,"stopped render failed");
   auditedToggle(transport,true);check(auditedRender(transport,{&output,0,block}),"restart render failed");
   transport.latchUnderrun();transport.latchUnderrun();check(transport.underrunCount()==1&&!transport.isPlaying(),"stickycount");check(transport.prepareEpoch(0,false,token).wasOk(),"recoveryprepare");transport.discardPreparedEpoch(token);check(!auditedCommit(transport,token,true),"staletoken");check(transport.hasUnderrun(),"discardclearedfault");
   check(transport.prepareEpoch(0,false,token).wasOk()&&auditedCommit(transport,token,false),"recoverycommit");check(!transport.hasUnderrun()&&!transport.isPlaying(),"recoveryclearsfaultstopped");
  }
 }
 {
  hem::ReadAheadTransport transport;transport.prepareFile(folder.getChildFile("stereo-48007.wav"),formats,disk,48000,256,true);
  juce::WaitableEvent entered,release;std::atomic<bool> held{true};
  struct Unblock {hem::ReadAheadTransport& t;std::atomic<bool>& held;juce::WaitableEvent& release;~Unblock(){held=false;release.signal();hem::ReadAheadTransportTestAccess::hook(t,{});}} unblock{transport,held,release};
  hem::ReadAheadTransportTestAccess::hook(transport,[&]{if(held.load()){entered.signal();release.wait();}});
  check(entered.wait(3000),"decoderhooknotentered");auditedToggle(transport,true);juce::AudioBuffer<float> output(2,256);int consumed=0;
  while(transport.availableRt(256)&&consumed<65536){check(auditedRender(transport,{&output,0,256}),"cachedduringheldread");consumed+=256;}
  check(consumed>=16384&&consumed<=65536,"cachedcapacitybound");
  const auto before=std::chrono::steady_clock::now();check(!auditedRender(transport,{&output,0,256}),"starvationnotreported");
  check(std::chrono::steady_clock::now()-before<std::chrono::milliseconds(100),"callbackwaitedfordecoder");check(output.getMagnitude(0,256)==0&&transport.underrunCount()==1,"starvationnotzerosticky");
  held=false;release.signal();hem::ReadAheadTransportTestAccess::hook(transport,{});
 }
 std::cout<<"PASS callback allocation/free audit blocks="<<auditedBlocks<<" ordinary+aligned hooks; exact stereo identity, emitted EOF, zero remainder, seek rounding, short loops, partitions, sticky underrun and token discard\n";
 }catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';disk.stopThread(5000);return 1;}disk.stopThread(5000);return 0;
}
