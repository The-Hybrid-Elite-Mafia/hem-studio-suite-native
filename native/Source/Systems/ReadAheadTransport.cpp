#include "ReadAheadTransport.h"
#include <array>
#include <cmath>
#include <limits>
#include <mutex>
#include <stdexcept>
namespace hem {
namespace {
void requireTransport(bool ok,const char* text){if(!ok)throw std::runtime_error(text);}
class WorkerReader final:public juce::AudioSource {
public:
    std::unique_ptr<juce::AudioFormatReader> reader;
    juce::int64 position=0;bool looping=false,failed=false;std::function<void()> beforeRead;
    explicit WorkerReader(std::unique_ptr<juce::AudioFormatReader> r):reader(std::move(r)){}
    void prepareToPlay(int,double)override{}
    void releaseResources()override{}
    void getNextAudioBlock(const juce::AudioSourceChannelInfo& info)override{
        info.clearActiveBufferRegion();int offset=0;
        while(offset<info.numSamples&&!failed){
            const auto length=reader->lengthInSamples;
            if(looping)position%=length;
            if(position>=length)break;
            const int count=int(std::min<juce::int64>(info.numSamples-offset,length-position));
            if(beforeRead)beforeRead();
            if(!reader->read(info.buffer,info.startSample+offset,count,position,true,true)){failed=true;break;}
            for(int c=0;c<2;++c)for(int n=0;n<count;++n)
                if(!std::isfinite(info.buffer->getSample(c,info.startSample+offset+n))){failed=true;break;}
            position+=count;offset+=count;
        }
        if(failed)info.clearActiveBufferRegion();
    }
};
}
struct ReadAheadTransport::Impl final:juce::TimeSliceClient {
    struct Bank {
        std::array<std::array<float,capacityFrames>,2> pcm{};
        alignas(64) std::atomic<uint64_t> read{0};
        alignas(64) std::atomic<uint64_t> written{0};
        std::unique_ptr<WorkerReader> source;
        std::unique_ptr<juce::ResamplingAudioSource> resampler;
        juce::AudioBuffer<float> scratch;
        uint64_t token=0,startFrame=0,frames=0;bool looping=false;
        std::atomic<bool> failed{false};
    };
    std::array<Bank,2> banks;
    std::mutex control;
    juce::TimeSliceThread* disk=nullptr;juce::File file;
    juce::AudioFormatManager formats;
    double rate=0,sourceRate=0;juce::int64 sourceFrames=0;int maximumBlock=0;
    std::atomic<int> active{0},pendingBank{-1};
    std::atomic<uint64_t> activeEpoch{0},pending{0},underruns{0};uint64_t nextEpoch=0;
    std::atomic<bool> ready{false},requestedPlay{false},fault{false},readError{false},loopState{false};
    std::atomic<double> position{0},duration{0},sourceRateSnapshot{0};std::atomic<int> sourceChannels{0};
    bool audioPlaying=false,registered=false;std::function<void()> readHook;
    Impl(){formats.registerBasicFormats();}
    void detach(){if(registered&&disk){disk->removeTimeSliceClient(this);registered=false;}}
    void attach(){if(!registered&&disk&&ready.load()){disk->addTimeSliceClient(this);registered=true;}}
    void clearBank(Bank& b){b.resampler.reset();b.source.reset();b.scratch.setSize(0,0);b.read=0;b.written=0;b.failed=false;b.token=0;}
    void createBank(Bank& b,double seconds,bool loop,uint64_t token){
        clearBank(b);
        std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
        requireTransport(reader&&reader->lengthInSamples==sourceFrames&&reader->sampleRate==sourceRate&&int(reader->numChannels)==sourceChannels.load(),"Source changed during transport preparation");
        b.source=std::make_unique<WorkerReader>(std::move(reader));b.source->looping=loop;b.source->beforeRead=readHook;
        const auto total=uint64_t(std::ceil(static_cast<long double>(sourceFrames)*rate/sourceRate));
        // Exact EOF must use ceil-sized output length, not floor(duration * rate).
        b.startFrame=seconds>=duration.load()?total:std::min(total,uint64_t(std::floor(std::clamp(seconds,0.,duration.load())*rate)));
        b.source->position=std::min(sourceFrames,juce::int64(std::floor(static_cast<long double>(b.startFrame)*sourceRate/rate)));
        b.looping=loop;b.frames=total-b.startFrame;b.token=token;
        b.scratch.setSize(2,pageFrames);
        b.resampler=std::make_unique<juce::ResamplingAudioSource>(b.source.get(),false,2);
        b.resampler->setResamplingRatio(sourceRate/rate);b.resampler->prepareToPlay(pageFrames,rate);
    }
    bool fill(Bank& b){
        if(!b.source||b.failed.load(std::memory_order_relaxed))return false;
        const auto w=b.written.load(std::memory_order_relaxed),r=b.read.load(std::memory_order_acquire);
        if(w<r||w-r>capacityFrames||w>std::numeric_limits<uint64_t>::max()-capacityFrames){b.failed=true;return false;}
        const auto free=uint64_t(capacityFrames)-(w-r);
        if(free<pageFrames)return false;
        if(!b.looping&&w>=b.frames)return false;
        const int count=int(std::min<uint64_t>(pageFrames,b.looping?uint64_t(pageFrames):b.frames-w));
        b.resampler->getNextAudioBlock({&b.scratch,0,count});
        if(b.source->failed){b.failed.store(true,std::memory_order_release);return false;}
        const int index=int(w%capacityFrames),first=std::min(count,capacityFrames-index);
        for(int c=0;c<2;++c){std::copy_n(b.scratch.getReadPointer(c),first,b.pcm[size_t(c)].data()+index);
            if(first<count)std::copy_n(b.scratch.getReadPointer(c)+first,count-first,b.pcm[size_t(c)].data());}
        b.written.store(w+uint64_t(count),std::memory_order_release);return true;
    }
    int useTimeSlice()override{
        if(!ready.load(std::memory_order_acquire))return 10;
        auto& b=banks[size_t(active.load(std::memory_order_acquire))];return fill(b)?1:5;
    }
    void prime(Bank& b,const std::function<bool()>& cancel){
        const auto wanted=b.looping?uint64_t(startupFrames):std::min<uint64_t>(startupFrames,b.frames);
        while(b.written.load()<wanted){requireTransport(!cancel||!cancel(),"Transport preparation cancelled");requireTransport(fill(b),"Transport read failed during priming");}
    }
};
static_assert(std::atomic<uint64_t>::is_always_lock_free&&std::atomic<double>::is_always_lock_free&&std::atomic<int>::is_always_lock_free&&std::atomic<bool>::is_always_lock_free);
ReadAheadTransport::ReadAheadTransport():impl(std::make_unique<Impl>()){}
ReadAheadTransport::~ReadAheadTransport(){releaseResources();}
void ReadAheadTransport::prepareFile(const juce::File& f,juce::AudioFormatManager&,juce::TimeSliceThread& disk,double rate,int block,bool loop,double seconds){
    auto& s=*impl;std::lock_guard lock(s.control);s.detach();s.ready=false;s.requestedPlay=false;s.audioPlaying=false;
    requireTransport(std::isfinite(rate)&&rate>=8000&&rate<=384000&&block>0&&block<=capacityFrames,"Unsupported transport rate or block size");
    requireTransport(std::isfinite(seconds),"Non-finite transport position");
    std::unique_ptr<juce::AudioFormatReader> reader(s.formats.createReaderFor(f));
    requireTransport(reader&&reader->lengthInSamples>0&&reader->sampleRate>=8000&&reader->sampleRate<=384000&&reader->numChannels>=1&&reader->numChannels<=2,"Use a nonempty mono/stereo audio source");
    requireTransport(static_cast<long double>(reader->lengthInSamples)*rate/reader->sampleRate<std::numeric_limits<juce::int64>::max()/2,"Transport source is too long");
    s.file=f;s.disk=&disk;s.rate=rate;s.maximumBlock=block;s.sourceRate=reader->sampleRate;s.sourceFrames=reader->lengthInSamples;s.duration=double(s.sourceFrames)/s.sourceRate;s.sourceRateSnapshot=s.sourceRate;s.sourceChannels=int(reader->numChannels);
    requireTransport(s.nextEpoch<std::numeric_limits<uint64_t>::max()-1,"Transport epoch exhausted");
    s.createBank(s.banks[0],seconds,loop,++s.nextEpoch);s.prime(s.banks[0],{});s.clearBank(s.banks[1]);
    s.active=0;s.activeEpoch=s.banks[0].token;s.pending=0;s.pendingBank=-1;s.position=double(s.banks[0].startFrame)/rate;s.loopState=loop;s.fault=false;s.readError=false;s.ready=true;s.attach();
}
juce::Result ReadAheadTransport::prepareEpoch(double seconds,bool loop,uint64_t& token,const std::function<bool()>& cancel){
    token=0;auto& s=*impl;std::lock_guard lock(s.control);
    if(!s.ready.load()||!std::isfinite(seconds))return juce::Result::fail("Transport is not prepared or seek is non-finite");
    if(s.pending.load())return juce::Result::fail("A transport epoch is already pending");
    s.detach();const int index=1-s.active.load(std::memory_order_acquire);
    // Only the retired bank needed producer quiescence. Keep the independent
    // active decoder refilling while the control worker primes the next epoch.
    s.attach();
    try{requireTransport(s.nextEpoch<std::numeric_limits<uint64_t>::max()-1,"Transport epoch exhausted");
        auto& b=s.banks[size_t(index)];s.createBank(b,seconds,loop,++s.nextEpoch);s.prime(b,cancel);
        requireTransport(!cancel||!cancel(),"Transport preparation cancelled");s.pendingBank.store(index,std::memory_order_relaxed);token=b.token;s.pending.store(token,std::memory_order_release);s.attach();return juce::Result::ok();
    }catch(const std::exception& e){s.clearBank(s.banks[size_t(index)]);s.attach();return juce::Result::fail(e.what());}
}
bool ReadAheadTransport::commitPreparedEpoch(uint64_t token,bool play)noexcept{
    auto& s=*impl;constexpr auto claimed=std::numeric_limits<uint64_t>::max();
    if(!token||token==claimed)return false;auto expected=token;
    if(!s.pending.compare_exchange_strong(expected,claimed,std::memory_order_acq_rel))return false;
    const int index=s.pendingBank.load(std::memory_order_relaxed);if(index<0||index>1){s.pending.store(0,std::memory_order_release);return false;}auto& b=s.banks[size_t(index)];
    s.audioPlaying=false;s.position.store(double(b.startFrame)/s.rate,std::memory_order_relaxed);s.loopState=b.looping;s.fault=false;s.readError=false;s.requestedPlay=play;
    s.active.store(index,std::memory_order_release);s.activeEpoch.store(token,std::memory_order_release);s.pendingBank=-1;s.pending.store(0,std::memory_order_release);return true;
}
void ReadAheadTransport::discardPreparedEpoch(uint64_t token){auto& s=*impl;std::lock_guard lock(s.control);constexpr auto claimed=std::numeric_limits<uint64_t>::max();auto expected=token;if(token&&token!=claimed&&s.pending.compare_exchange_strong(expected,claimed,std::memory_order_acq_rel)){s.pendingBank=-1;s.pending.store(0,std::memory_order_release);}}
void ReadAheadTransport::releaseResources(){auto& s=*impl;std::lock_guard lock(s.control);s.detach();s.ready=false;s.requestedPlay=false;s.audioPlaying=false;s.pending=0;s.pendingBank=-1;for(auto& b:s.banks)s.clearBank(b);}
void ReadAheadTransport::reprepare(double rate,int block){auto& s=*impl;juce::File f;juce::TimeSliceThread* disk=nullptr;double position=0;bool loop=false;{std::lock_guard lock(s.control);f=s.file;disk=s.disk;position=s.position.load();loop=s.loopState.load();}if(f==juce::File()||!disk)return;prepareFile(f,s.formats,*disk,rate,block,loop,position);}
void ReadAheadTransport::setReadHookForTesting(std::function<void()> hook){auto& s=*impl;std::lock_guard lock(s.control);s.detach();s.readHook=std::move(hook);for(auto& b:s.banks)if(b.source)b.source->beforeRead=s.readHook;s.attach();}
void ReadAheadTransport::start()noexcept{auto& s=*impl;if(s.ready.load()&&!s.fault.load())s.requestedPlay=true;}
void ReadAheadTransport::stop()noexcept{impl->requestedPlay=false;}
bool ReadAheadTransport::isPlaying()const noexcept{return impl->requestedPlay.load(std::memory_order_relaxed)&&!impl->fault.load(std::memory_order_relaxed);}
bool ReadAheadTransport::availableRt(int n)const noexcept{
    const auto& s=*impl;if(n<0||n>capacityFrames)return false;if(!s.ready.load(std::memory_order_acquire))return !isPlaying();
    if(s.fault.load(std::memory_order_relaxed))return false;
    if(!s.requestedPlay.load(std::memory_order_relaxed)&&!s.audioPlaying)return true;
    const auto& b=s.banks[size_t(s.active.load(std::memory_order_acquire))];if(b.failed.load(std::memory_order_acquire))return false;
    const auto r=b.read.load(std::memory_order_relaxed),w=b.written.load(std::memory_order_acquire);
    const auto required=b.looping?uint64_t(n):std::min<uint64_t>(uint64_t(n),b.frames-std::min(r,b.frames));return w>=r&&w-r>=required;
}
void ReadAheadTransport::latchUnderrun()noexcept{auto& s=*impl;if(s.banks[size_t(s.active.load(std::memory_order_acquire))].failed.load(std::memory_order_acquire))s.readError=true;if(!s.fault.exchange(true,std::memory_order_relaxed))s.underruns.fetch_add(1,std::memory_order_relaxed);s.requestedPlay=false;}
bool ReadAheadTransport::renderRt(const juce::AudioSourceChannelInfo& info)noexcept{
    auto& s=*impl;if(!info.buffer||info.startSample<0||info.numSamples<0||info.startSample>info.buffer->getNumSamples()-info.numSamples)return false;
    info.clearActiveBufferRegion();if(!s.ready.load(std::memory_order_acquire))return true;
    const bool play=s.requestedPlay.load(std::memory_order_relaxed);if(!play&&!s.audioPlaying)return !s.fault.load();
    if(!availableRt(info.numSamples)){const auto& b=s.banks[size_t(s.active.load())];if(b.failed.load())s.readError=true;latchUnderrun();s.audioPlaying=false;return false;}
    auto& b=s.banks[size_t(s.active.load(std::memory_order_acquire))];const auto r=b.read.load(std::memory_order_relaxed);
    const int count=int(b.looping?uint64_t(info.numSamples):std::min<uint64_t>(info.numSamples,b.frames-std::min(r,b.frames)));
    const int index=int(r%capacityFrames),first=std::min(count,capacityFrames-index);
    for(int c=0;c<std::min(2,info.buffer->getNumChannels());++c){auto* out=info.buffer->getWritePointer(c,info.startSample);std::copy_n(b.pcm[size_t(c)].data()+index,first,out);if(first<count)std::copy_n(b.pcm[size_t(c)].data(),count-first,out+first);}
    if(!play){const int fade=std::min(256,info.numSamples);for(int c=0;c<info.buffer->getNumChannels();++c)info.buffer->applyGainRamp(c,info.startSample,fade,1.f,0.f);if(info.numSamples>fade)info.buffer->clear(info.startSample+fade,info.numSamples-fade);}
    b.read.store(r+uint64_t(count),std::memory_order_release);
    double seconds=(double(b.startFrame)+double(r)+double(count))/s.rate;if(b.looping)seconds=std::fmod(seconds,s.duration.load(std::memory_order_relaxed));s.position.store(std::min(seconds,s.duration.load(std::memory_order_relaxed)),std::memory_order_relaxed);
    if(!b.looping&&r+uint64_t(count)>=b.frames)s.requestedPlay=false;
    s.audioPlaying=play&&s.requestedPlay.load(std::memory_order_relaxed);return true;
}
double ReadAheadTransport::getCurrentPosition()const noexcept{return impl->position.load(std::memory_order_relaxed);}
double ReadAheadTransport::getLengthInSeconds()const noexcept{return impl->duration.load(std::memory_order_relaxed);}
double ReadAheadTransport::getSourceSampleRate()const noexcept{return impl->sourceRateSnapshot.load(std::memory_order_relaxed);}
int ReadAheadTransport::getSourceChannels()const noexcept{return impl->sourceChannels.load(std::memory_order_relaxed);}
bool ReadAheadTransport::isLooping()const noexcept{return impl->loopState.load(std::memory_order_relaxed);}
bool ReadAheadTransport::hasUnderrun()const noexcept{return impl->fault.load(std::memory_order_relaxed);}
bool ReadAheadTransport::hasReadError()const noexcept{return impl->readError.load(std::memory_order_relaxed);}
uint64_t ReadAheadTransport::underrunCount()const noexcept{return impl->underruns.load(std::memory_order_relaxed);}
uint64_t ReadAheadTransport::epoch()const noexcept{return impl->activeEpoch.load(std::memory_order_acquire);}
uint64_t ReadAheadTransport::preparedEpoch()const noexcept{return impl->pending.load(std::memory_order_acquire);}
}
