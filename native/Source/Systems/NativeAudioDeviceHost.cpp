#include "NativeAudioDeviceHost.h"
#include <algorithm>
#include <cmath>
#include <mutex>
namespace hem {
struct NativeAudioDeviceHost::Impl {
    explicit Impl(juce::AudioSource& s):source(s){}
    juce::AudioSource& source;
    juce::OwnedArray<juce::AudioIODeviceType> types;
    std::unique_ptr<juce::AudioIODevice> device;
    AudioDeviceSetup setup;juce::String typeName;
    juce::AudioBuffer<float> scratch;
    std::recursive_mutex control;
    int defaultInputs=0,defaultOutputs=2,preparedInputs=0,preparedOutputs=0;
    std::atomic<bool> prepared{false},accepting{false},backendStopped{false},backendError{false},startArmed{false};
    std::atomic<uint64_t> oversized{0},invalidChannels{0},errors{0},silenced{0};
    const juce::String invalidCallback{"Audio backend supplied an unsupported callback shape; reopen the device"};
    const juce::String unexpectedRestart{"Audio device restarted or changed format; reopen it from device settings"};
    std::array<std::atomic<char>,1024> errorBytes{};
    std::atomic<uint64_t> errorSequence{0};
};
static_assert(std::atomic<uint64_t>::is_always_lock_free&&std::atomic<char>::is_always_lock_free&&std::atomic<bool>::is_always_lock_free);
NativeAudioDeviceHost::NativeAudioDeviceHost(juce::AudioSource& source,bool system):impl(std::make_unique<Impl>(source)){
    if(!system)return;auto add=[this](juce::AudioIODeviceType* p){if(p)impl->types.add(p);};
#if JUCE_WINDOWS
    add(juce::AudioIODeviceType::createAudioIODeviceType_WASAPI(juce::WASAPIDeviceMode::shared));
    add(juce::AudioIODeviceType::createAudioIODeviceType_WASAPI(juce::WASAPIDeviceMode::exclusive));
    add(juce::AudioIODeviceType::createAudioIODeviceType_WASAPI(juce::WASAPIDeviceMode::sharedLowLatency));
    add(juce::AudioIODeviceType::createAudioIODeviceType_DirectSound());
    add(juce::AudioIODeviceType::createAudioIODeviceType_ASIO());
#endif
}
NativeAudioDeviceHost::~NativeAudioDeviceHost(){closeAudioDevice();}
void NativeAudioDeviceHost::addDeviceTypeForTesting(std::unique_ptr<juce::AudioIODeviceType> type){impl->types.add(type.release());}
void NativeAudioDeviceHost::recordError(const juce::String& message)noexcept{
    auto& s=*impl;auto seq=s.errorSequence.load(std::memory_order_relaxed);if(seq&1)return;
    if(!s.errorSequence.compare_exchange_strong(seq,seq+1,std::memory_order_acquire))return;
    auto text=message.getCharPointer();size_t offset=0;
    while(!text.isEmpty()&&offset+4<s.errorBytes.size()){
        const auto cp=uint32_t(text.getAndAdvance());char bytes[4]{};int count=0;
        if(cp<0x80){bytes[0]=char(cp);count=1;}
        else if(cp<0x800){bytes[0]=char(0xc0|(cp>>6));bytes[1]=char(0x80|(cp&63));count=2;}
        else if(cp<0x10000){bytes[0]=char(0xe0|(cp>>12));bytes[1]=char(0x80|((cp>>6)&63));bytes[2]=char(0x80|(cp&63));count=3;}
        else{bytes[0]=char(0xf0|(cp>>18));bytes[1]=char(0x80|((cp>>12)&63));bytes[2]=char(0x80|((cp>>6)&63));bytes[3]=char(0x80|(cp&63));count=4;}
        for(int i=0;i<count;++i)s.errorBytes[offset++].store(bytes[i],std::memory_order_relaxed);
    }
    s.errorBytes[offset].store(0,std::memory_order_relaxed);s.errorSequence.store(seq+2,std::memory_order_release);
}
juce::String NativeAudioDeviceHost::getLastError()const{
    const auto& s=*impl;const auto sequence=s.errorSequence.load(std::memory_order_acquire);if(sequence&1)return "Device status is changing";
    std::array<char,1024> text{};for(size_t i=0;i<text.size()-1;++i){text[i]=s.errorBytes[i].load(std::memory_order_relaxed);if(!text[i])break;}
    if(s.errorSequence.load(std::memory_order_acquire)!=sequence)return "Device status is changing";
    return juce::String::fromUTF8(text.data());
}
void NativeAudioDeviceHost::closeAudioDevice(){
    auto& s=*impl;std::lock_guard lock(s.control);s.accepting.store(false,std::memory_order_release);s.startArmed=false;
    // AudioIODevice::stop guarantees pending client callbacks have returned.
    if(s.device)s.device->stop();
    if(s.prepared.exchange(false)){try{s.source.releaseResources();}catch(...){recordError("Audio source release failed");}}
    if(s.device){s.device->close();s.device.reset();}
    s.scratch.setSize(0,0);s.preparedInputs=s.preparedOutputs=0;s.backendStopped=false;s.backendError=false;
}
juce::String NativeAudioDeviceHost::initialise(int inputs,int outputs){
    auto& s=*impl;std::lock_guard lock(s.control);
    if(inputs<0||inputs>maximumInputs||outputs<1||outputs>maximumOutputs)return "Select 0-128 input channels and 1-2 output channels";
    s.defaultInputs=inputs;s.defaultOutputs=outputs;
    for(auto* type:s.types){type->scanForDevices();if(!type->getDeviceNames(false).isEmpty()){s.typeName=type->getTypeName();return setAudioDeviceSetup({},true);}}
    recordError("No native audio output device is available");return getLastError();
}
juce::String NativeAudioDeviceHost::setAudioDeviceSetup(const AudioDeviceSetup& requested,bool){
    auto& s=*impl;std::lock_guard lock(s.control);
    auto fail=[this](const juce::String& error){recordError(error);return error;};
    juce::AudioIODeviceType* type=nullptr;for(auto* candidate:s.types)if(candidate->getTypeName()==s.typeName){type=candidate;break;}
    if(!type)return fail("Choose an available audio driver type");
    auto setup=requested;type->scanForDevices();const auto outputs=type->getDeviceNames(false),inputs=type->getDeviceNames(true);
    if(setup.outputDeviceName.isEmpty()){const int index=type->getDefaultDeviceIndex(false);if(!juce::isPositiveAndBelow(index,outputs.size()))return fail("No default output device is available");setup.outputDeviceName=outputs[index];}
    if(!outputs.contains(setup.outputDeviceName))return fail("Selected output device is unavailable: "+setup.outputDeviceName);
    const bool wantInput=setup.useDefaultInputChannels?s.defaultInputs>0:!setup.inputChannels.isZero();
    if(wantInput&&setup.inputDeviceName.isEmpty()){const int index=type->getDefaultDeviceIndex(true);if(!juce::isPositiveAndBelow(index,inputs.size()))return fail("No default input device is available");setup.inputDeviceName=inputs[index];}
    if(wantInput&&!inputs.contains(setup.inputDeviceName))return fail("Selected input device is unavailable: "+setup.inputDeviceName);
    if(!wantInput)setup.inputDeviceName.clear();
    std::unique_ptr<juce::AudioIODevice> next(type->createDevice(setup.outputDeviceName,setup.inputDeviceName));
    if(!next)return fail("Cannot create selected native audio device");
    const auto inNames=next->getInputChannelNames(),outNames=next->getOutputChannelNames();
    if(setup.useDefaultInputChannels){setup.inputChannels.clear();setup.inputChannels.setRange(0,std::min(s.defaultInputs,inNames.size()),true);}
    if(setup.useDefaultOutputChannels){setup.outputChannels.clear();setup.outputChannels.setRange(0,std::min(s.defaultOutputs,outNames.size()),true);}
    const int nIn=setup.inputChannels.countNumberOfSetBits(),nOut=setup.outputChannels.countNumberOfSetBits();
    if(nIn>maximumInputs||nOut<1||nOut>maximumOutputs||setup.inputChannels.isNegative()||setup.outputChannels.isNegative()
       ||setup.inputChannels.getHighestBit()>=inNames.size()||setup.outputChannels.getHighestBit()>=outNames.size())return fail("Selected channel mask is outside the device or supported channel count");
    const auto rates=next->getAvailableSampleRates();const auto blocks=next->getAvailableBufferSizes();
    if(setup.sampleRate==0)setup.sampleRate=rates.isEmpty()||rates.contains(48000.)?48000.:rates.getFirst();
    if(setup.bufferSize==0)setup.bufferSize=next->getDefaultBufferSize();
    if(!std::isfinite(setup.sampleRate)||setup.sampleRate<=0||(!rates.isEmpty()&&!rates.contains(setup.sampleRate)))return fail("Selected sample rate is not supported by this device");
    if(setup.bufferSize<=0||setup.bufferSize>maximumFrames||(!blocks.isEmpty()&&!blocks.contains(setup.bufferSize)))return fail("Selected buffer size is not supported by this device");
    closeAudioDevice();
    const auto error=next->open(setup.inputChannels,setup.outputChannels,setup.sampleRate,setup.bufferSize);
    if(error.isNotEmpty()){next->close();return fail(error);}
    const int actualInputs=next->getActiveInputChannels().countNumberOfSetBits(),actualOutputs=next->getActiveOutputChannels().countNumberOfSetBits();
    const double actualRate=next->getCurrentSampleRate();const int actualBlock=next->getCurrentBufferSizeSamples();
    if(actualInputs!=nIn||actualOutputs!=nOut||next->getActiveInputChannels()!=setup.inputChannels||next->getActiveOutputChannels()!=setup.outputChannels||!std::isfinite(actualRate)||actualRate<=0||actualBlock<=0||actualBlock>maximumFrames){next->close();return fail("Device opened with an unsupported channel or processing configuration");}
    setup.sampleRate=actualRate;setup.bufferSize=actualBlock;s.device=std::move(next);s.setup=setup;s.preparedInputs=nIn;s.preparedOutputs=nOut;
    try{s.scratch.setSize(std::max(2,nIn),maximumFrames);s.scratch.clear();s.prepared.store(true,std::memory_order_release);s.source.prepareToPlay(actualBlock,actualRate);}
    catch(...){closeAudioDevice();return fail("Audio processing preparation failed");}
    s.backendStopped=false;s.backendError=false;recordError({});s.startArmed.store(true,std::memory_order_release);
    try{s.device->start(this);}catch(...){closeAudioDevice();return fail("Native audio device start failed");}
    if(!s.device->isPlaying()||!s.accepting.load(std::memory_order_acquire)){auto text=s.device->getLastError();closeAudioDevice();return fail(text.isNotEmpty()?text:"Native audio device did not start");}
    return {};
}
juce::String NativeAudioDeviceHost::restartLastAudioDevice(){auto& s=*impl;std::lock_guard lock(s.control);return setAudioDeviceSetup(s.setup,false);}
NativeAudioDeviceHost::AudioDeviceSetup NativeAudioDeviceHost::getAudioDeviceSetup()const{return impl->setup;}
juce::String NativeAudioDeviceHost::setCurrentAudioDeviceType(const juce::String& name,bool chosen){auto& s=*impl;std::lock_guard lock(s.control);bool found=false;for(auto* t:s.types)found|=t->getTypeName()==name;if(!found){recordError("Audio driver is unavailable: "+name);return getLastError();}closeAudioDevice();s.typeName=name;auto setup=s.setup;setup.inputDeviceName.clear();setup.outputDeviceName.clear();return setAudioDeviceSetup(setup,chosen);}
juce::String NativeAudioDeviceHost::getCurrentAudioDeviceType()const{return impl->typeName;}
const juce::OwnedArray<juce::AudioIODeviceType>& NativeAudioDeviceHost::getAvailableDeviceTypes()const noexcept{return impl->types;}
juce::AudioIODevice* NativeAudioDeviceHost::getCurrentAudioDevice()const noexcept{return impl->device.get();}
bool NativeAudioDeviceHost::pollDeviceState(){auto& s=*impl;std::lock_guard lock(s.control);if(!s.device)return false;if(s.backendStopped.load(std::memory_order_acquire)||s.backendError.load(std::memory_order_acquire)||!s.device->isOpen()||!s.device->isPlaying()){if(getLastError().isEmpty()){const auto error=s.device->getLastError();recordError(error.isNotEmpty()?error:"Audio device stopped or disconnected");}closeAudioDevice();return true;}return false;}
void NativeAudioDeviceHost::audioDeviceAboutToStart(juce::AudioIODevice* d){auto& s=*impl;const bool valid=s.startArmed.exchange(false,std::memory_order_acq_rel)&&d==s.device.get()&&s.prepared.load(std::memory_order_acquire)&&d->getCurrentSampleRate()==s.setup.sampleRate&&d->getCurrentBufferSizeSamples()==s.setup.bufferSize;s.accepting.store(valid,std::memory_order_release);if(!valid){s.errors.fetch_add(1,std::memory_order_relaxed);recordError(s.unexpectedRestart);s.backendError.store(true,std::memory_order_release);}}
void NativeAudioDeviceHost::audioDeviceStopped(){impl->accepting.store(false,std::memory_order_release);impl->backendStopped.store(true,std::memory_order_release);}
void NativeAudioDeviceHost::audioDeviceError(const juce::String& message){impl->errors.fetch_add(1,std::memory_order_relaxed);recordError(message);impl->accepting.store(false,std::memory_order_release);impl->backendError.store(true,std::memory_order_release);}
void NativeAudioDeviceHost::audioDeviceIOCallbackWithContext(const float* const* input,int nInput,float* const* output,int nOutput,int frames,const juce::AudioIODeviceCallbackContext&){
    auto& s=*impl;const auto silence=[&]{int cleared=0;if(output&&frames>0)for(int c=0;c<std::clamp(nOutput,0,4096)&&cleared<maximumOutputs;++c)if(output[c]){std::fill_n(output[c],std::min(frames,maximumFrames),0.f);++cleared;}s.silenced.fetch_add(1,std::memory_order_relaxed);};
    const auto fatalShape=[&]{s.errors.fetch_add(1,std::memory_order_relaxed);recordError(s.invalidCallback);s.accepting.store(false,std::memory_order_release);s.backendError.store(true,std::memory_order_release);silence();};
    if(frames<0||frames>maximumFrames){s.oversized.fetch_add(1,std::memory_order_relaxed);fatalShape();return;}
    if(nInput<0||nOutput<0||nInput>4096||nOutput>4096||(nInput>0&&!input)||(nOutput>0&&!output)){s.invalidChannels.fetch_add(1,std::memory_order_relaxed);fatalShape();return;}
    if(!s.accepting.load(std::memory_order_acquire)){silence();return;}
    std::array<const float*,maximumInputs> ins{};std::array<float*,maximumOutputs> outs{};int inputs=0,outputs=0;
    for(int c=0;c<nInput;++c)if(input[c]){if(inputs==maximumInputs){s.invalidChannels.fetch_add(1,std::memory_order_relaxed);fatalShape();return;}ins[size_t(inputs++)]=input[c];}
    for(int c=0;c<nOutput;++c)if(output[c]){if(outputs==maximumOutputs){s.invalidChannels.fetch_add(1,std::memory_order_relaxed);fatalShape();return;}outs[size_t(outputs++)]=output[c];}
    if(inputs!=s.preparedInputs||outputs!=s.preparedOutputs){s.invalidChannels.fetch_add(1,std::memory_order_relaxed);fatalShape();return;}
    if(frames==0)return;
    juce::ScopedNoDenormals noDenormals;
    for(int c=0;c<s.scratch.getNumChannels();++c){auto* dest=s.scratch.getWritePointer(c);if(c<inputs)std::copy_n(ins[size_t(c)],frames,dest);else std::fill_n(dest,frames,0.f);}
    try{s.source.getNextAudioBlock({&s.scratch,0,frames});}
    catch(...){s.errors.fetch_add(1,std::memory_order_relaxed);s.accepting=false;s.backendError=true;silence();return;}
    for(int c=0;c<outputs;++c)std::copy_n(s.scratch.getReadPointer(c),frames,outs[size_t(c)]);
}
uint64_t NativeAudioDeviceHost::oversizedCallbacks()const noexcept{return impl->oversized.load(std::memory_order_relaxed);}
uint64_t NativeAudioDeviceHost::invalidChannelCallbacks()const noexcept{return impl->invalidChannels.load(std::memory_order_relaxed);}
uint64_t NativeAudioDeviceHost::callbackErrors()const noexcept{return impl->errors.load(std::memory_order_relaxed);}
uint64_t NativeAudioDeviceHost::silentCallbacks()const noexcept{return impl->silenced.load(std::memory_order_relaxed);}
}
