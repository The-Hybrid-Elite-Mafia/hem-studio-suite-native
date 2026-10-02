#pragma once
#include <JuceHeader.h>
#include <stdexcept>
namespace hem {
// Prepared entirely off the audio/message threads during a batch import.
// Ownership moves into a TrackEngine at commit; transport and reader addresses
// do not change. Destruction of replaced sources is queued back to the worker.
struct StemPlayback {
    std::unique_ptr<juce::AudioFormatReaderSource> reader;
    juce::AudioTransportSource transport;
    juce::File file;
    double sourceRate=0;
    int channels=0;
    ~StemPlayback(){transport.setSource(nullptr);transport.releaseResources();}
    void prepareFile(const juce::File& f,juce::AudioFormatManager& formats,
                     juce::TimeSliceThread& disk,double targetRate,int block,bool looping=false){
        std::unique_ptr<juce::AudioFormatReader> raw(formats.createReaderFor(f));
        if(!raw)throw std::runtime_error("Cannot decode WAV: "+f.getFileName().toStdString());
        if(raw->lengthInSamples<=0||raw->sampleRate<=0||raw->numChannels<1||raw->numChannels>2)
            throw std::runtime_error("Use a non-empty mono or stereo WAV: "+f.getFileName().toStdString());
        file=f;sourceRate=raw->sampleRate;channels=int(raw->numChannels);
        reader=std::make_unique<juce::AudioFormatReaderSource>(raw.release(),true);
        reader->setLooping(looping);
        // Source-rate correction instantiates JUCE's resampler. Read-ahead
        // keeps file reads on disk's TimeSliceThread, not the audio callback.
        transport.setSource(reader.get(),65536,&disk,sourceRate,2);
        transport.prepareToPlay(block,targetRate);
        transport.setPosition(0);
    }
};
}
