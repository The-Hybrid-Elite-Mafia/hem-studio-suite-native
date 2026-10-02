#include "TakeRecorder.h"
#include "NoReplaceFile.h"
#include <stdexcept>
namespace hem {
TakeRecorder::TakeRecorder():juce::Thread("HEM vocal take writer"){}
TakeRecorder::~TakeRecorder(){stop();waitForThreadToExit(-1);}
bool TakeRecorder::start(const Setup& s){
    if(busy())return false;
    if(!std::isfinite(s.rate)||s.rate<8000||s.rate>384000||s.channels<1||s.channels>2||s.file==juce::File()||!std::isfinite(s.manualOffsetMs))return false;
    setup=s;outcome.clear();partial=juce::File();fifo.reset();written=0;invalidSamples=0;firstTimeline=-1;overflow=false;stopRequested=false;status=State::Opening;
    if(!startThread()){outcome="Could not start recording worker";status=State::Failed;return false;}return true;
}
void TakeRecorder::stop() noexcept {stopRequested.store(true,std::memory_order_release);}
void TakeRecorder::push(const float* l,const float* r,int count,double timeline) noexcept {
    if(stopRequested.load(std::memory_order_acquire))return;
    auto s=state();if(s!=State::Armed&&s!=State::Recording)return;
    if(s==State::Armed){firstTimeline.store(timeline);status.store(State::Recording,std::memory_order_release);}
    for(int offset=0;offset<count;offset+=packetFrames){
        const int n=std::min(packetFrames,count-offset);int a,n1,b,n2;fifo.prepareToWrite(1,a,n1,b,n2);
        if(n1+n2==0){overflow.store(true);stopRequested.store(true,std::memory_order_release);return;}
        auto& p=packets[size_t(n1?a:b)];p.frames=n;
        for(int i=0;i<n;++i){auto clean=[this](float x){if(std::isfinite(x))return x;invalidSamples.fetch_add(1,std::memory_order_relaxed);return 0.f;};p.l[size_t(i)]=clean(l[offset+i]);if(setup.channels==2)p.r[size_t(i)]=clean(r[offset+i]);}
        fifo.finishedWrite(1);
    }
}
juce::String TakeRecorder::result() const {return busy()?juce::String("Recording / finalizing"):outcome;}
void TakeRecorder::run(){
    bool wavPublished=false;
    juce::File metadataPartial;
    try{
        std::error_code inspectError;
        if(!noReplaceFile::available(setup.file,inspectError)){
            if(inspectError)throw std::runtime_error("Cannot inspect take destination: "+inspectError.message());
            throw std::runtime_error("Take destination already exists; choose a new name");
        }
        partial=setup.file.getSiblingFile(setup.file.getFileNameWithoutExtension()+".partial-"+juce::Uuid().toString()+".wav");
        if(!noReplaceFile::available(partial,inspectError))throw std::runtime_error("Temporary take path is already in use or inaccessible");
        std::unique_ptr<juce::OutputStream> stream=partial.createOutputStream();if(!stream)throw std::runtime_error("Cannot create take file");
        auto* fileStream=dynamic_cast<juce::FileOutputStream*>(stream.get());
        juce::WavAudioFormat wav;
        auto options=juce::AudioFormatWriterOptions().withSampleRate(setup.rate).withNumChannels(setup.channels).withBitsPerSample(32).withSampleFormat(juce::AudioFormatWriterOptions::SampleFormat::floatingPoint);
        auto writer=wav.createWriterFor(stream,options);if(!writer)throw std::runtime_error("Cannot create float WAV writer");
        status.store(State::Armed,std::memory_order_release);
        bool diskError=false;
        while(true){
            int a,n1,b,n2;fifo.prepareToRead(1,a,n1,b,n2);
            if(n1+n2){const auto& p=packets[size_t(n1?a:b)];const float* channels[]{p.l.data(),p.r.data()};
                if(!writer->writeFromFloatArrays(channels,setup.channels,p.frames)){diskError=true;stopRequested=true;}
                else written.fetch_add(unsigned(p.frames));fifo.finishedRead(1);
                // Prevent legacy RIFF size overflow; close a valid partial take.
                if(written.load()*unsigned(setup.channels)*4>=0xff000000ULL){overflow=true;stopRequested=true;}
                if(diskError)break;continue;
            }
            if(stopRequested.load(std::memory_order_acquire))break;
            wait(3);
        }
        status.store(State::Stopping,std::memory_order_release);
        const bool flushed=writer->flush();if(fileStream)fileStream->flush();const bool streamOK=fileStream&&fileStream->getStatus().wasOk();writer.reset();
        if(diskError||!flushed||!streamOK)throw std::runtime_error("Disk write/flush failed; partial WAV retained");
        if(overflow.load())throw std::runtime_error("Recording stopped: disk queue or RIFF capacity exceeded; partial WAV retained without skipping timeline samples");
        if(written.load()==0){partial.deleteFile();throw std::runtime_error("No input frames recorded");}
        if(const auto error=noReplaceFile::publish(partial,setup.file))
            throw std::runtime_error("Cannot publish take without overwriting; partial WAV retained (destination may exist or hard links may be unsupported): "+error.message());
        // This is the audio commit point. A later metadata error must never
        // claim that the recorded WAV failed or hide it from completedFile().
        wavPublished=true;
        partial.deleteFile(); // A failed cleanup only leaves a recoverable duplicate.
        juce::DynamicObject::Ptr metadata=new juce::DynamicObject();
        metadata->setProperty("sampleRate",setup.rate);metadata->setProperty("frames",juce::int64(written.load()));metadata->setProperty("channels",setup.channels);
        metadata->setProperty("inputDevice",setup.inputName);metadata->setProperty("activeInputChannelIndex",setup.inputChannel);metadata->setProperty("inputChannelName",setup.inputChannelName);metadata->setProperty("insertStateAtArmXml",setup.insertStateXml);
        metadata->setProperty("firstCallbackTimelineSeconds",firstTimeline.load());
        const double delay=(setup.inputLatency+setup.outputLatency+setup.backingLatency+setup.printedLatency)/setup.rate;
        metadata->setProperty("estimatedPlacementSeconds",firstTimeline.load()-delay+setup.manualOffsetMs*.001);
        metadata->setProperty("inputLatencySamples",setup.inputLatency);metadata->setProperty("outputLatencySamples",setup.outputLatency);metadata->setProperty("backingDspLatencySamples",setup.backingLatency);metadata->setProperty("printedInsertLatencySamples",setup.printedLatency);
        metadata->setProperty("manualOffsetMs",setup.manualOffsetMs);metadata->setProperty("nonFiniteSamplesReplaced",juce::int64(invalidSamples.load()));
        metadata->setProperty("alignmentNote","Driver-reported estimate; verify with a loopback test. Positive manual offset places audio later.");
        const auto sidecar=setup.file.getSiblingFile(setup.file.getFileName()+".take.json");
        metadataPartial=sidecar.getSiblingFile(sidecar.getFileName()+".partial-"+juce::Uuid().toString());
        if(!noReplaceFile::available(metadataPartial,inspectError))throw std::runtime_error("Temporary metadata path is already in use or inaccessible");
        {
            auto metadataStream=metadataPartial.createOutputStream();
            if(!metadataStream||!metadataStream->getStatus().wasOk())throw std::runtime_error("Cannot create placement metadata");
            if(!metadataStream->writeText(juce::JSON::toString(juce::var(metadata.get()),false),false,false,"\n"))
                throw std::runtime_error("Cannot write placement metadata");
            metadataStream->flush();
            if(metadataStream->getStatus().failed())throw std::runtime_error("Cannot flush placement metadata");
        }
        if(const auto error=noReplaceFile::publish(metadataPartial,sidecar))
            throw std::runtime_error("Cannot publish placement metadata without overwriting (destination may exist or hard links may be unsupported): "+error.message());
        metadataPartial.deleteFile();
        outcome="Saved "+setup.file.getFileName()+" with placement metadata";
        if(partial.existsAsFile())outcome+="; duplicate partial WAV retained: "+partial.getFullPathName();
        if(metadataPartial.existsAsFile())outcome+="; duplicate partial metadata retained: "+metadataPartial.getFullPathName();
        status.store(State::Complete,std::memory_order_release);
    }catch(const std::exception& e){
        outcome=wavPublished?"Saved "+setup.file.getFileName()+"; placement metadata not saved: "+juce::String(e.what()):juce::String(e.what());
        if(partial.existsAsFile())outcome+=" / "+partial.getFullPathName();
        if(metadataPartial.existsAsFile())outcome+=" / metadata retained: "+metadataPartial.getFullPathName();
        status.store(wavPublished?State::Complete:State::Failed,std::memory_order_release);
    }
}
}
