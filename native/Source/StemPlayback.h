#pragma once
#include <JuceHeader.h>
#include <stdexcept>
#include "Systems/ReadAheadTransport.h"
namespace hem {
// Prepared entirely off the audio/message threads during a batch import.
// Ownership moves into a TrackEngine at commit; transport and reader addresses
// do not change. Destruction of replaced sources is queued back to the worker.
struct StemPlayback {
    std::shared_ptr<ReadAheadTransport> transport=std::make_shared<ReadAheadTransport>();
    juce::File file;
    double sourceRate=0;
    int channels=0;
    void prepareFile(const juce::File& f,juce::AudioFormatManager& formats,
                     juce::TimeSliceThread& disk,double targetRate,int block,bool looping=false,double position=0){
        transport->prepareFile(f,formats,disk,targetRate,block,looping,position);
        file=f;sourceRate=transport->getSourceSampleRate();channels=transport->getSourceChannels();
    }
};
}
