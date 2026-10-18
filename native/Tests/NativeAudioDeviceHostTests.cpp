#include <JuceHeader.h>
#include "../Source/Systems/NativeAudioDeviceHost.h"
#include <iostream>
#include <stdexcept>
#include <thread>
#include <future>
#include <mutex>
#include <condition_variable>
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
static void check(bool ok,const char* text){if(!ok)throw std::runtime_error(text);}
namespace hem {struct NativeAudioDeviceHostTestAccess {static void add(NativeAudioDeviceHost& h,std::unique_ptr<juce::AudioIODeviceType> t){h.addDeviceTypeForTesting(std::move(t));}};}
struct FakeDevice final:juce::AudioIODevice {
 bool failOpen=false;explicit FakeDevice(bool fail=false):AudioIODevice("Fake output","Test native"),failOpen(fail){}
 juce::BigInteger in,out;double rate=48000;int block=256;bool opened=false;juce::AudioIODeviceCallback* callback=nullptr;std::mutex mutex;std::condition_variable done;int running=0;
 juce::StringArray getOutputChannelNames()override{return {"Out1","Out2","Out3","Out4"};}
 juce::StringArray getInputChannelNames()override{juce::StringArray names;for(int i=0;i<130;++i)names.add("In"+juce::String(i+1));return names;}
 juce::Array<double> getAvailableSampleRates()override{return {44100,48000,96000};}juce::Array<int> getAvailableBufferSizes()override{return {128,256,512};}int getDefaultBufferSize()override{return 256;}
 juce::String open(const juce::BigInteger& i,const juce::BigInteger& o,double r,int b)override{if(failOpen)return "Injected open failure";in=i;out=o;rate=r;block=b;opened=true;return {};}
 void close()override{stop();opened=false;}bool isOpen()override{return opened;}
 void start(juce::AudioIODeviceCallback* cb)override{cb->audioDeviceAboutToStart(this);std::lock_guard lock(mutex);callback=cb;}
 void stop()override{juce::AudioIODeviceCallback* old=nullptr;{std::unique_lock lock(mutex);old=callback;callback=nullptr;done.wait(lock,[&]{return running==0;});}if(old)old->audioDeviceStopped();}
 bool isPlaying()override{std::lock_guard lock(mutex);return callback!=nullptr;}juce::String getLastError()override{return {};}
 int getCurrentBufferSizeSamples()override{return block;}double getCurrentSampleRate()override{return rate;}int getCurrentBitDepth()override{return 32;}
 juce::BigInteger getActiveOutputChannels()const override{return out;}juce::BigInteger getActiveInputChannels()const override{return in;}
 int getOutputLatencyInSamples()override{return 11;}int getInputLatencyInSamples()override{return 7;}
 void deliver(const float* const* i,int ni,float* const* o,int no,int n){juce::AudioIODeviceCallback* cb;{std::lock_guard lock(mutex);cb=callback;if(cb)++running;}if(!cb)return;const auto a=callbackAudit::allocated,f=callbackAudit::freed;callbackAudit::enabled=true;cb->audioDeviceIOCallbackWithContext(i,ni,o,no,n,{});callbackAudit::enabled=false;{std::lock_guard lock(mutex);--running;done.notify_all();}check(a==callbackAudit::allocated&&f==callbackAudit::freed,"callback allocation/free");}
 void error(const juce::String& message){auto* cb=callback;callbackAudit::enabled=true;cb->audioDeviceError(message);callbackAudit::enabled=false;}
};
struct FakeType final:juce::AudioIODeviceType {
 bool failOpen=false;FakeType():AudioIODeviceType("Test native"){}void scanForDevices()override{}juce::StringArray getDeviceNames(bool input=false)const override{return {input?"Fake input":"Fake output"};}
 int getDefaultDeviceIndex(bool)const override{return 0;}int getIndexOfDevice(juce::AudioIODevice*,bool)const override{return 0;}bool hasSeparateInputsAndOutputs()const override{return true;}
 juce::AudioIODevice* createDevice(const juce::String&,const juce::String&)override{return new FakeDevice(failOpen);}
};
struct Source final:juce::AudioSource {
 bool failPrepare=false;int prepares=0,releases=0,renders=0,channels=0;double rate=0;int block=0;std::atomic<bool> hold{false},entered{false};
 void prepareToPlay(int b,double r)override{++prepares;rate=r;block=b;if(failPrepare)throw 7;}
 void releaseResources()override{++releases;}
 void getNextAudioBlock(const juce::AudioSourceChannelInfo& i)override{++renders;channels=i.buffer->getNumChannels();entered=true;while(hold.load(std::memory_order_acquire))std::this_thread::yield();for(int c=0;c<2;++c)for(int n=0;n<i.numSamples;++n)i.buffer->setSample(c,n,i.buffer->getSample(c,n)*2);}
};
int main(){juce::ScopedJuceInitialiser_GUI gui;try{
 callbackAudit::enabled=true;auto* p=::operator new(17);::operator delete(p);p=::operator new(64,std::align_val_t(64));::operator delete(p,std::align_val_t(64));callbackAudit::enabled=false;check(callbackAudit::allocated==2&&callbackAudit::freed==2,"inactive allocation hooks");callbackAudit::allocated=callbackAudit::freed=0;
 Source source;hem::NativeAudioDeviceHost host(source,false);auto type=std::make_unique<FakeType>();auto* fakeType=type.get();hem::NativeAudioDeviceHostTestAccess::add(host,std::move(type));check(host.initialise(0,2).isEmpty(),"initialise");check(source.prepares==1&&source.block==256&&source.rate==48000,"prepare contract");
 auto setup=host.getAudioDeviceSetup();setup.useDefaultInputChannels=false;setup.inputDeviceName="Fake input";setup.inputChannels.setBit(1);setup.inputChannels.setBit(3);setup.useDefaultOutputChannels=false;setup.outputChannels.clear();setup.outputChannels.setBit(1);setup.outputChannels.setBit(3);setup.sampleRate=96000;setup.bufferSize=128;check(host.setAudioDeviceSetup(setup,true).isEmpty(),"configure mask/rate/block");check(source.prepares==2&&source.releases==1&&source.rate==96000&&source.block==128,"restart lifecycle");
 auto* device=dynamic_cast<FakeDevice*>(host.getCurrentAudioDevice());check(device,"owned fake device");std::vector<float> l(65537,.25f),r(65537,-.5f),ol(65537,7),orr(65537,7);const float* ins[]{nullptr,l.data(),nullptr,r.data()};float* outs[]{nullptr,ol.data(),nullptr,orr.data()};
 for(int n:{1,128,257,4096,65536}){device->deliver(ins,4,outs,4,n);check(ol[size_t(n-1)]==.5f&&orr[size_t(n-1)]==-1&&source.channels==2,"compacted mapping");}
 const int rendered=source.renders;device->deliver(ins,4,outs,4,65537);check(source.renders==rendered&&host.oversizedCallbacks()==1&&ol[65535]==0&&orr[65535]==0&&ol.back()==7&&orr.back()==7,"bounded oversize silence");check(host.pollDeviceState(),"oversize did not latch fatal");check(host.restartLastAudioDevice().isEmpty(),"restart after oversize");device=dynamic_cast<FakeDevice*>(host.getCurrentAudioDevice());
 float* badOutputs[]{ol.data(),orr.data(),l.data()};device->deliver(ins,4,badOutputs,3,128);check(host.invalidChannelCallbacks()==1&&source.renders==rendered,"invalid channel rejection");check(host.pollDeviceState(),"invalid channels not fatal");check(host.restartLastAudioDevice().isEmpty(),"restart invalid channels");device=dynamic_cast<FakeDevice*>(host.getCurrentAudioDevice());
 std::vector<float*> manyOutputs(4097,nullptr);manyOutputs[0]=ol.data();manyOutputs[1]=orr.data();manyOutputs[2]=l.data();manyOutputs[4096]=r.data();l[0]=9;r[0]=8;device->deliver(ins,4,manyOutputs.data(),4097,128);check(l[0]==9&&r[0]==8&&host.invalidChannelCallbacks()==2,"malformed channel write limit");check(host.pollDeviceState(),"malformed pointer count not fatal");check(host.restartLastAudioDevice().isEmpty(),"restart malformed pointers");device=dynamic_cast<FakeDevice*>(host.getCurrentAudioDevice());
 auto bad=setup;bad.outputChannels.setBit(2);check(host.setAudioDeviceSetup(bad,true).isNotEmpty()&&host.getCurrentAudioDevice()==device,"invalid setup changed live device");
 const juce::String error=juce::String::fromUTF8("Test device failure \xc3\xa9");const auto errorsBefore=host.callbackErrors();const int releasesBeforeError=source.releases;const auto allocated=callbackAudit::allocated,freed=callbackAudit::freed;device->error(error);check(host.callbackErrors()==errorsBefore+1,"backend error not counted");check(callbackAudit::allocated==allocated&&callbackAudit::freed==freed,"error notification allocated");check(host.getLastError()==error,"error text lost");device->deliver(ins,4,outs,4,128);check(ol[0]==0&&orr[0]==0,"error did not silence");check(host.pollDeviceState()&&!host.getCurrentAudioDevice()&&source.releases==releasesBeforeError+1,"deferred release after error");
 check(host.restartLastAudioDevice().isEmpty(),"restart last setup");device=dynamic_cast<FakeDevice*>(host.getCurrentAudioDevice());source.entered=false;source.hold=true;
 auto render=std::async(std::launch::async,[&]{device->deliver(ins,4,outs,4,128);});for(int i=0;i<3000&&!source.entered.load();++i)juce::Thread::sleep(1);check(source.entered,"held render entry");
 const int beforeRelease=source.releases;auto close=std::async(std::launch::async,[&]{host.closeAudioDevice();});const bool waited=close.wait_for(std::chrono::milliseconds(20))==std::future_status::timeout;const bool preserved=source.releases==beforeRelease;source.hold=false;render.get();close.get();check(waited&&preserved&&source.releases==beforeRelease+1,"stop did not preserve callback lifetime");
 check(host.restartLastAudioDevice().isEmpty(),"restart before backend notification");device=dynamic_cast<FakeDevice*>(host.getCurrentAudioDevice());callbackAudit::enabled=true;device->callback->audioDeviceAboutToStart(device);callbackAudit::enabled=false;check(host.pollDeviceState()&&!host.getCurrentAudioDevice(),"unsolicited backend restart accepted stale graph");
 source.failPrepare=true;const int releasesBeforeFailure=source.releases;check(host.restartLastAudioDevice().isNotEmpty()&&!host.getCurrentAudioDevice()&&source.releases==releasesBeforeFailure+1,"failed prepare did not release partial source");source.failPrepare=false;
 fakeType->failOpen=true;const int preparesBeforeOpen=source.prepares;check(host.restartLastAudioDevice()=="Injected open failure"&&!host.getCurrentAudioDevice()&&source.prepares==preparesBeforeOpen,"open failure lost error or prepared source");
 check(callbackAudit::allocated==0&&callbackAudit::freed==0,"callback allocation totals");std::cout<<"PASS direct owned-device lifecycle, physical masks/compaction, rate/block, variable frames, bounded malformed silence/fatal recovery, counted errors, stop flush; zero callback ordinary/aligned allocations/frees. Fake device only.\n";
 }catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}return 0;}
