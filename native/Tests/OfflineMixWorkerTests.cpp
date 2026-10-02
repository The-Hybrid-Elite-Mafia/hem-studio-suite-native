#include "../Source/Systems/OfflineMixPanel.h"
#include <iostream>
#include <stdexcept>

namespace {
void require(bool value,const char* why){if(!value)throw std::runtime_error(why);}
void wait(hem::OfflineMixWorker& worker){
    for(int i=0;worker.isBusy()&&i<2000;++i){juce::Thread::sleep(5);juce::Timer::callPendingTimersSynchronously();}
    require(!worker.isBusy(),"Worker completion failed to reach message thread");
}
}
int main(){juce::ScopedJuceInitialiser_GUI gui;try{
    const auto dir=juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("HEM-export-worker-"+juce::Uuid().toString());
    require(dir.createDirectory().wasOk(),"Test directory failed");
    struct Clean{juce::File path;~Clean(){path.deleteRecursively();}} clean{dir};
    int reports=0;juce::String last;
    hem::OfflineMixWorker worker([&](const juce::String& value){++reports;last=value;});
    hem::OfflineMixOptions options;options.tailSeconds=0;
    require(worker.start({},dir.getChildFile("empty.wav"),options),"Empty snapshot was not admitted");
    require(worker.isBusy(),"Worker not immediately busy");
    require(!worker.start({},dir.getChildFile("duplicate.wav"),options),"Concurrent export admitted");
    wait(worker);require(reports==1&&last.contains("Export unchanged"),"Failure was not reported exactly once");
    require(!dir.getChildFile("empty.wav").exists()&&!dir.getChildFile("duplicate.wav").exists(),"Failed export created final file");

    const auto source=dir.getChildFile("source.wav");
    std::unique_ptr<juce::OutputStream> stream=source.createOutputStream();
    juce::WavAudioFormat format;
    const auto writerOptions=juce::AudioFormatWriterOptions().withSampleRate(48000).withNumChannels(2).withBitsPerSample(32).withSampleFormat(juce::AudioFormatWriterOptions::SampleFormat::floatingPoint);
    auto writer=format.createWriterFor(stream,writerOptions);require(writer!=nullptr,"Fixture writer failed");
    juce::AudioBuffer<float> data(2,4800);data.clear();data.setSample(0,1000,.05f);data.setSample(1,1000,-.025f);
    require(writer->writeFromAudioSampleBuffer(data,0,data.getNumSamples()),"Fixture write failed");writer.reset();
    hem::SessionSnapshot snapshot;snapshot.tracks[0].source=source;
    require(worker.start(snapshot,dir.getChildFile("complete.wav"),options),"Worker could not restart after failure");
    wait(worker);require(reports==2&&last.startsWith("Saved complete.wav"),"Success did not reach UI");
    require(dir.getChildFile("complete.wav").existsAsFile(),"Successful worker has no output");

    options.tailSeconds=30;
    require(worker.start(snapshot,dir.getChildFile("cancelled.wav"),options),"Cancel test was not admitted");worker.cancel();
    wait(worker);require(reports==3&&last.containsIgnoreCase("cancel"),"Cancellation did not reach UI");
    require(!dir.getChildFile("cancelled.wav").exists(),"Cancelled worker published final audio");
    {hem::OfflineMixWorker interrupted([](const juce::String&){});require(interrupted.start(snapshot,dir.getChildFile("closed.wav"),options),"Shutdown test not admitted");}
    require(!dir.getChildFile("closed.wav").exists(),"Shutdown published interrupted audio");

    hem::OfflineMixPanel panel(worker,[](const juce::File&,const hem::OfflineMixOptions&){return juce::Result::ok();});
    for(const auto size:std::array<juce::Point<int>,2>{{{800,470},{1600,940}}}){
        panel.setSize(size.x,size.y);juce::Image image(juce::Image::ARGB,size.x,size.y,true);juce::Graphics graphics(image);panel.paintEntireComponent(graphics,true);
        for(int i=0;i<panel.getNumChildComponents();++i){auto* child=panel.getChildComponent(i);require(child->getWidth()>0&&child->getHeight()>0&&panel.getLocalBounds().contains(child->getBounds()),"Export control exceeds panel bounds");}
        require(image.getPixelAt(0,0).getAlpha()==255,"Export panel background is not opaque");
    }
    std::cout<<"PASS offline worker busy exclusion, UI-thread completion, restart, cancel, shutdown cleanup and actual export panel raster at 1x/2x\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
