#include <JuceHeader.h>
#include "../Source/StemFileImporter.h"
#include <future>
#include <iostream>
#include <stdexcept>
namespace hem {
struct StemFileImporterTestAccess {
 static bool chooserOpen(const StemFileImporter& importer){return importer.picker!=nullptr;}
 static bool explicitBrowseWired(const StemFileImporter& importer){return bool(importer.browseButton.onClick);}
 static void tick(StemFileImporter& importer){importer.timerCallback();}
};
}
static void require(bool b,const char* why){if(!b)throw std::runtime_error(why);}
int main(){juce::ScopedJuceInitialiser_GUI gui;juce::TimeSliceThread disk("Importer test read-ahead");disk.startThread();
 const auto folder=juce::File::getSpecialLocation(juce::File::tempDirectory).getNonexistentChildFile("hem-import-test","",false);folder.createDirectory();
 try{
    hem::StemFileImporter zone(disk,[]{return hem::StemFileImporter::Context{96000,256,1,false};},[](hem::StemFileImporter::Batch&){return juce::Result::ok();});
    require(hem::StemFileImporterTestAccess::explicitBrowseWired(zone),"Explicit Browse action lost its callback");
    for(int flags:std::array<int,3>{0,juce::ModifierKeys::leftButtonModifier,juce::ModifierKeys::rightButtonModifier}){
     for(bool dragged:{false,true}){
      const auto now=juce::Time::getCurrentTime();const juce::Point<float> start(20,20),end(70,20);
      const juce::MouseEvent event(juce::Desktop::getInstance().getMainMouseSource(),end,juce::ModifierKeys(flags),1,0,0,0,0,&zone,&zone,now,start,now,1,dragged);
      zone.mouseUp(event);
      require(!hem::StemFileImporterTestAccess::chooserOpen(zone),"Import background release opened a file chooser");
     }
    }
    juce::StringArray names;for(int i=0;i<13;++i)names.add("stem"+juce::String(i)+".WAV");require(zone.isInterestedInFileDrag(names),"13 WAV files rejected");names.add("extra.wav");require(!zone.isInterestedInFileDrag(names),"14 files accepted");require(!zone.isInterestedInFileDrag({"audio.mp3"}),"Non-WAV accepted");
    for(double rate:{44100.,48000.,96000.}){
        const auto file=folder.getChildFile(juce::String(int(rate))+".wav");
        juce::AudioBuffer<float> pcm(1,int(rate*.2));for(int i=0;i<pcm.getNumSamples();++i)pcm.setSample(0,i,.2f*float(std::sin(juce::MathConstants<double>::twoPi*1000*i/rate)));
        juce::WavAudioFormat format;std::unique_ptr<juce::OutputStream> stream=file.createOutputStream();
        std::unique_ptr<juce::AudioFormatWriter> writer(format.createWriterFor(stream.get(),rate,1,24,{},0));if(writer)stream.release();require(writer!=nullptr,"WAV writer failed");require(writer->writeFromAudioSampleBuffer(pcm,0,pcm.getNumSamples()),"WAV write failed");writer.reset();
        auto task=std::async(std::launch::async,[&disk,file]{juce::AudioFormatManager formats;formats.registerFormat(new juce::WavAudioFormat(),true);auto source=std::make_unique<hem::StemPlayback>();source->prepareFile(file,formats,disk,96000,256);return source;});
        auto source=task.get();require(source->channels==1,"Mono metadata lost");require(std::abs(source->sourceRate-rate)<1,"Source sample rate incorrect");require(std::abs(source->transport->getLengthInSeconds()-.2)<1.e-4,"Resampled duration incorrect");
        source->transport->start();juce::AudioBuffer<float> block(2,256);int crossings=0;float previous=0;double energy=0;
        for(int pos=0;pos<16384;pos+=256){juce::AudioSourceChannelInfo info(&block,0,256);source->transport->getNextAudioBlock(info);
            for(int i=0;i<256;++i)if(pos+i>=4096){const float sample=block.getSample(0,i);if(previous<=0&&sample>0)++crossings;previous=sample;energy+=sample*sample;}}
        require(crossings>=126&&crossings<=130,"Resampling changed pitch");require(energy>100,"Resampled audio silent");source.reset();
    }
    // Exercise the actual asynchronous importer, explicit publication and real
    // transport rendering. No AudioDeviceManager or hardware output is opened.
    {
      std::unique_ptr<hem::StemPlayback> live;bool permit=false;int commits=0;
      hem::StemFileImporter flow(disk,[]{return hem::StemFileImporter::Context{96000,256,1,false};},[&](hem::StemFileImporter::Batch& batch){
        if(!permit)return juce::Result::fail("Host is busy; preserve pending import");
        require(batch.count==1&&batch.firstTrack==0,"Import track mapping changed");require(batch.requireEmptyTargets&&batch.projectTempo==93.5,"Instrumental policy/tempo lost between submission and commit");live.swap(batch.stems[0]);++commits;return juce::Result::ok();
      });
      flow.submitFiles({folder.getChildFile("48000.wav").getFullPathName()},0,true,93.5);
      for(int i=0;i<3000&&!flow.hasPreparedBatch();++i){juce::Thread::sleep(5);hem::StemFileImporterTestAccess::tick(flow);}
      require(flow.hasPreparedBatch()&&!live,"Import auto-applied without an explicit command");
      require(flow.applyPreparedBatch().failed()&&flow.hasPreparedBatch()&&!live,"Rejected commit lost pending source");
      permit=true;require(flow.applyPreparedBatch().wasOk()&&live&&commits==1&&!flow.hasPreparedBatch(),"Explicit playback preparation did not publish the ready WAV");
      require(!live->transport->isPlaying(),"Import unexpectedly started playback");
      live->transport->start();juce::AudioBuffer<float> block(2,256);double energy=0;
      for(int i=0;i<40;++i){live->transport->getNextAudioBlock({&block,0,256});for(int j=0;j<256;++j)energy+=std::abs(block.getSample(0,j));}
      require(live->transport->getCurrentPosition()>0&&energy>1,"Applied import did not advance/render audible samples on explicit play");
      require(flow.applyPreparedBatch().failed()&&commits==1,"Playback preparation applied a batch twice");
      flow.shutdown();live.reset();
    }
    auto bad=folder.getChildFile("broken.wav");bad.replaceWithText("invalid wav");bool rejected=false;
    try{juce::AudioFormatManager f;f.registerFormat(new juce::WavAudioFormat(),true);hem::StemPlayback source;source.prepareFile(bad,f,disk,96000,256);}catch(const std::exception&){rejected=true;}require(rejected,"Corrupt WAV accepted");
    zone.shutdown();disk.stopThread(5000);folder.deleteRecursively();std::cout<<"PASS batch limit, extension validation, corrupt WAV, background readers and 44.1/48/96 -> 96 kHz resampling\n";return 0;
 }catch(const std::exception& e){disk.stopThread(5000);folder.deleteRecursively();std::cerr<<e.what()<<'\n';return 1;}}
