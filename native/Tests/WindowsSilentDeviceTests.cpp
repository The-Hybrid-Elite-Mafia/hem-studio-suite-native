#include <JuceHeader.h>
#include <atomic>
#include <iostream>
#include <windows.h>

// Opens a private default output stream and emits silence only. No input,
// preferences file, global default-device change, or user media is involved.
struct SilentProbe final : juce::AudioIODeviceCallback {
 std::atomic<int> callbacks{0},stops{0},errors{0};
 std::atomic<long long> frames{0};
 void audioDeviceIOCallbackWithContext(const float* const*,int,float* const* output,int channels,int samples,const juce::AudioIODeviceCallbackContext&) override {
  for(int c=0;c<channels;++c)if(output[c])juce::FloatVectorOperations::clear(output[c],samples);
  frames.fetch_add(samples,std::memory_order_relaxed);callbacks.fetch_add(1,std::memory_order_relaxed);
 }
 void audioDeviceAboutToStart(juce::AudioIODevice*) override {}
 void audioDeviceStopped() override {++stops;}
 void audioDeviceError(const juce::String&) override {++errors;}
};
int main(){juce::ScopedJuceInitialiser_GUI gui;juce::AudioDeviceManager manager;SilentProbe probe;
 const auto error=manager.initialise(0,2,nullptr,true);
 if(error.isNotEmpty()){std::cerr<<"FAIL default output open: "<<error<<'\n';return 1;}
 auto* device=manager.getCurrentAudioDevice();
 if(!device){std::cerr<<"FAIL no default output device\n";return 1;}
 if(device->getAvailableBufferSizes().contains(256)){
  auto setup=manager.getAudioDeviceSetup();setup.bufferSize=256;
  const auto setupError=manager.setAudioDeviceSetup(setup,false);
  if(setupError.isNotEmpty()){std::cerr<<"FAIL application buffer setup: "<<setupError<<'\n';return 1;}
  device=manager.getCurrentAudioDevice();
  if(!device){std::cerr<<"FAIL output lost after buffer setup\n";return 1;}
 }
 std::cout<<"Backend: "<<manager.getCurrentAudioDeviceType()<<"\nDevice: "<<device->getName()
          <<"\nCurrent rate: "<<device->getCurrentSampleRate()<<" / buffer: "<<device->getCurrentBufferSizeSamples()
          <<"\nOutput channels: "<<device->getActiveOutputChannels().countNumberOfSetBits()<<"\nAvailable rates:";
 for(auto rate:device->getAvailableSampleRates())std::cout<<' '<<rate;
 std::cout<<"\nAvailable buffers:";for(auto size:device->getAvailableBufferSizes())std::cout<<' '<<size;std::cout<<'\n';
 manager.addAudioCallback(&probe);
 const auto until=juce::Time::getMillisecondCounterHiRes()+3000;
 while(juce::Time::getMillisecondCounterHiRes()<until){MSG message;while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){TranslateMessage(&message);DispatchMessageW(&message);}juce::Thread::sleep(10);}
 manager.removeAudioCallback(&probe);manager.closeAudioDevice();
 std::cout<<"Callbacks: "<<probe.callbacks<<" / frames: "<<probe.frames<<" / errors: "<<probe.errors<<'\n';
 if(probe.callbacks<2||probe.frames<=0||probe.errors){std::cerr<<"FAIL silent callback stream\n";return 1;}
 std::cout<<"PASS default Windows output opened, delivered silent callbacks and closed. No audible playback, microphone, device switching, or physical listening validation.\n";return 0;
}
