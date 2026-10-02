#include <JuceHeader.h>
#include "../Source/StemFileImporter.h"
#include <future>
#include <iostream>
#include <stdexcept>
static void require(bool b,const char* why){if(!b)throw std::runtime_error(why);}
int main(){juce::ScopedJuceInitialiser_GUI gui;juce::TimeSliceThread disk("Importer test read-ahead");disk.startThread();
 const auto folder=juce::File::getSpecialLocation(juce::File::tempDirectory).getNonexistentChildFile("hem-import-test","",false);folder.createDirectory();
 try{
    hem::StemFileImporter zone(disk,[]{return hem::StemFileImporter::Context{96000,256,1,false};},[](hem::StemFileImporter::Batch&){return juce::Result::ok();});
    juce::StringArray names;for(int i=0;i<13;++i)names.add("stem"+juce::String(i)+".WAV");require(zone.isInterestedInFileDrag(names),"13 WAV files rejected");names.add("extra.wav");require(!zone.isInterestedInFileDrag(names),"14 files accepted");require(!zone.isInterestedInFileDrag({"audio.mp3"}),"Non-WAV accepted");
    for(double rate:{44100.,48000.,96000.}){
        const auto file=folder.getChildFile(juce::String(int(rate))+".wav");
        juce::AudioBuffer<float> pcm(1,int(rate*.2));for(int i=0;i<pcm.getNumSamples();++i)pcm.setSample(0,i,.2f*float(std::sin(juce::MathConstants<double>::twoPi*1000*i/rate)));
        juce::WavAudioFormat format;std::unique_ptr<juce::OutputStream> stream=file.createOutputStream();
        std::unique_ptr<juce::AudioFormatWriter> writer(format.createWriterFor(stream.get(),rate,1,24,{},0));if(writer)stream.release();require(writer!=nullptr,"WAV writer failed");require(writer->writeFromAudioSampleBuffer(pcm,0,pcm.getNumSamples()),"WAV write failed");writer.reset();
        auto task=std::async(std::launch::async,[&disk,file]{juce::AudioFormatManager formats;formats.registerFormat(new juce::WavAudioFormat(),true);auto source=std::make_unique<hem::StemPlayback>();source->prepareFile(file,formats,disk,96000,256);return source;});
        auto source=task.get();require(source->channels==1,"Mono metadata lost");require(std::abs(source->sourceRate-rate)<1,"Source sample rate incorrect");require(std::abs(source->transport.getLengthInSeconds()-.2)<1.e-4,"Resampled duration incorrect");
        source->transport.start();juce::AudioBuffer<float> block(2,256);int crossings=0;float previous=0;double energy=0;
        for(int pos=0;pos<16384;pos+=256){juce::AudioSourceChannelInfo info(&block,0,256);source->transport.getNextAudioBlock(info);
            for(int i=0;i<256;++i)if(pos+i>=4096){const float sample=block.getSample(0,i);if(previous<=0&&sample>0)++crossings;previous=sample;energy+=sample*sample;}}
        require(crossings>=126&&crossings<=130,"Resampling changed pitch");require(energy>100,"Resampled audio silent");source.reset();
    }
    auto bad=folder.getChildFile("broken.wav");bad.replaceWithText("invalid wav");bool rejected=false;
    try{juce::AudioFormatManager f;f.registerFormat(new juce::WavAudioFormat(),true);hem::StemPlayback source;source.prepareFile(bad,f,disk,96000,256);}catch(const std::exception&){rejected=true;}require(rejected,"Corrupt WAV accepted");
    zone.shutdown();disk.stopThread(5000);folder.deleteRecursively();std::cout<<"PASS batch limit, extension validation, corrupt WAV, background readers and 44.1/48/96 -> 96 kHz resampling\n";return 0;
 }catch(const std::exception& e){disk.stopThread(5000);folder.deleteRecursively();std::cerr<<e.what()<<'\n';return 1;}}
