#include <JuceHeader.h>
#include "../Source/TrackEngine.h"
#include <iostream>
#include <stdexcept>
static void require(bool ok,const char* reason){if(!ok)throw std::runtime_error(reason);}
int main(){juce::ScopedJuceInitialiser_GUI gui;try{
 constexpr int count=13,capacity=128;constexpr double rate=96000;
 juce::AudioBuffer<float> input(2,96000);for(int ch=0;ch<2;++ch)for(int i=0;i<input.getNumSamples();++i)input.setSample(ch,i,.01f);
 std::array<std::unique_ptr<juce::MemoryAudioSource>,count> sources;
 std::array<hem::TrackEngine,count> stems;
 juce::AudioBuffer<float> sum(2,capacity);std::array<float,capacity> gain{};gain.fill(1);
 for(int i=0;i<count;++i){sources[size_t(i)]=std::make_unique<juce::MemoryAudioSource>(input,true,false);auto& t=stems[size_t(i)];t.transport().setSource(sources[size_t(i)].get(),0,nullptr,rate,2);t.prepare(rate,capacity);t.transport().start();}
 auto render=[&](int requested){
    bool anySolo=false;for(auto& t:stems)anySolo|=t.solo.load();
    for(int offset=0;offset<requested;offset+=capacity){int n=std::min(capacity,requested-offset);sum.clear();for(auto& t:stems)t.render(sum,n,anySolo,gain.data());}
 };
 for(int i=0;i<30;++i)render(512);
 require(std::abs(sum.getSample(0,127)-.13f)<1.e-5f,"13-stem unity sum incorrect");
 for(const auto& stem:stems){const auto meter=stem.signalMeters.snapshot();require(meter.ready&&std::abs(meter.left.peakDb+40.f)<.002f&&std::abs(meter.right.peakDb+40.f)<.002f,"Per-track meters do not measure actual post-trim stereo contribution");}
 stems[0].muted.store(true);for(int i=0;i<10;++i)render(512);
 require(std::abs(sum.getSample(0,127)-.12f)<1.e-5f,"Mute summing incorrect");
 for(auto& t:stems)require(std::abs(t.transport().getCurrentPosition()-stems[0].transport().getCurrentPosition())<1.e-9,"Muted stem lost sync");
 stems[1].solo.store(true);for(int i=0;i<10;++i)render(512);
 require(std::abs(sum.getSample(0,127)-.01f)<1.e-5f,"Solo exclusion incorrect");
 for(auto& t:stems)require(std::abs(t.transport().getCurrentPosition()-stems[1].transport().getCurrentPosition())<1.e-9,"Solo-excluded stem lost sync");
 // JUCE 8.0.10 ResamplingAudioSource requests round(n * ratio) + 3 source
 // samples after flush. AudioTransportSource::getCurrentPosition reports that
 // source read head, not the sample currently emitted by the resampler.
 for(auto& t:stems){t.transport().setPosition(.5);require(std::abs(t.transport().getCurrentPosition()-.5)<1.e-9,"Common seek boundary failed");}
 render(128);
 constexpr int unityRatioSourceReadAhead=3;
 for(auto& t:stems)require(std::abs(t.transport().getCurrentPosition()-(.5+(128+unityRatioSourceReadAhead)/rate))<1.e-9,"Common source read head failed");
 const auto firstPosition=stems[0].transport().getCurrentPosition();render(128);
 for(auto& t:stems)require(std::abs(t.transport().getCurrentPosition()-(firstPosition+128/rate))<1.e-9,"Common steady rendered-frame advancement failed");
 for(auto& t:stems){t.transport().setSource(nullptr);t.release();}
 std::cout<<"PASS 13-stem sum, bounded chunking, mute, solo, continuous source positions and shared seek\n";return 0;
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
