#include "../Source/Plugins/RestorationProcessors.h"
#include "FoundationAllocationProbe.h"
#include <iostream>
#include <stdexcept>
namespace {
uint64_t checks=0;
void check(bool result,const char*what){++checks;if(!result)throw std::runtime_error(what);}
void set(hem::ModuleProcessor&p,const char*id,float value){auto*parameter=p.state.getParameter(id);check(parameter!=nullptr,"parameter missing");parameter->setValueNotifyingHost(parameter->convertTo0to1(value));}
struct CallbackProbe {CallbackProbe(){allocationProbe::testThread=true;allocationProbe::enabled=true;}~CallbackProbe(){allocationProbe::enabled=false;allocationProbe::testThread=false;}};
void run(hem::RestorationProcessor&p,juce::AudioBuffer<float>&b){juce::MidiBuffer midi;CallbackProbe probe;p.processBlock(b,midi);}
void checkUnavailable(hem::RestorationProcessor&p,bool prepared){
    check(p.processingReady()==prepared,"ready mismatch");const auto meter=p.meterSnapshot();check(meter.isPrepared==prepared,"isPrepared mismatch");check(!meter.hasProcessedAudio&&!meter.signalPresent&&!meter.valueAvailable&&meter.value==0&&meter.processedBlocks==0&&meter.blockSamples==0,"stale shared meter survives lifecycle change");check(p.meter()==0,"stale raw output peak");
    check(p.pluginTelemetry()!=nullptr,"shared levels missing");hem::PluginTelemetry::Levels levels;check(!p.pluginTelemetry()->read(levels)||!levels.valid,"stale shared levels survive lifecycle change");
    const auto family=p.restorationDisplay();check(!family.valid&&family.count==0&&family.frames==0&&family.events==0&&family.repaired==0&&family.skipped==0&&family.spectralCount==0,"stale family display survives lifecycle change");
    for(const auto&lane:family.history)for(float value:lane)check(value==0,"unavailable family exposed old waveform");
    for(const auto&column:family.spectrogram)for(float value:column)check(value==0,"unavailable family exposed old spectrum");
}
void fill(juce::AudioBuffer<float>&b,int start,double rate){
    for(int i=0;i<b.getNumSamples();++i){const auto t=double(start+i)/rate;const float x=float(.71*std::sin(2*hem::pi*713*t)+.31*std::sin(2*hem::pi*1813*t));b.setSample(0,i,std::clamp(x,-.63f,.63f));b.setSample(1,i,float(.073*std::cos(2*hem::pi*389*t)));}
}
void silence(const juce::AudioBuffer<float>&b){for(int c=0;c<b.getNumChannels();++c)for(int i=0;i<b.getNumSamples();++i)check(b.getSample(c,i)==0,"unprepared/released/cleared DSP output nonzero");}
void configure(hem::DynamicResonanceSuppressorProcessor&p,int mode){if(mode){set(p,"maxReduction",18);set(p,"sensitivity",91);set(p,"selectivity",78);set(p,"attack",1.5f);set(p,"release",45);set(p,"output",-3);set(p,"profile",1);}if(mode==2)set(p,"bypass",1);if(mode==3)set(p,"soloDelta",1);}
void configure(hem::BreathMouthControlProcessor&p,int mode){if(mode){set(p,"breathReduction",16);set(p,"mouthReduction",19);set(p,"breathDetection",97);set(p,"mouthDetection",98);set(p,"mouthLength",1.3f);set(p,"low",1000);set(p,"high",13000);}if(mode==2)set(p,"bypass",1);if(mode==3){set(p,"listenBreath",1);set(p,"listenMouth",1);}}
void configure(hem::ClipRepairProcessor&p,int mode){if(mode){set(p,"threshold",-5);set(p,"amount",85);set(p,"length",2.4f);set(p,"output",-3);}if(mode==2)set(p,"bypass",1);if(mode==3)set(p,"listenRepair",1);}
// This reproduces only the documented meter accumulation, independently of the
// wrapper. The reference DSP is the actual recovered core, never a reimplementation.
struct LevelReference {
    double memory;std::array<double,4>energy{};
    explicit LevelReference(double rate):memory(std::exp(-1./(.3*rate))){}
    void compare(hem::RestorationProcessor&p,const juce::AudioBuffer<float>&input,const juce::AudioBuffer<float>&output){
        std::array<float,4>peak{};
        for(int side=0;side<2;++side){const auto&b=side?output:input;for(int i=0;i<b.getNumSamples();++i)for(int c=0;c<2;++c){const size_t lane=size_t(2*side+c);const float x=b.getSample(c,i);peak[lane]=std::max(peak[lane],std::abs(x));energy[lane]=memory*energy[lane]+(1-memory)*double(x)*x;}}
        hem::PluginTelemetry::Levels measured;check(p.pluginTelemetry()->read(measured)&&measured.valid,"processed levels unavailable");
        for(size_t lane=0;lane<4;++lane){check(measured.peak[lane]==peak[lane],"shared stereo peak not actual input/output");check(std::abs(measured.rms[lane]-float(std::sqrt(energy[lane])))<1.e-7f,"shared stereo RMS not actual input/output");}
        const auto meter=p.meterSnapshot();check(meter.isPrepared&&meter.hasProcessedAudio&&meter.valueAvailable,"processed readiness flags missing");check(meter.value==std::max(peak[2],peak[3]),"output sample peak not actual audible block");check(meter.signalPresent==(meter.value>0),"audible signal flag mismatch");
    }
};
template<class Processor,class Core>void correspondence(const char*name){
    size_t cases=0;
    for(double rate:{8000.,44100.,48000.,96000.,192000.,384000.})for(int block:{1,31,256,1025})for(int mode=0;mode<4;++mode){
        Processor wrapper;configure(wrapper,mode);wrapper.prepareToPlay(rate,block);Core core;core.prepare(rate,wrapper.settings());LevelReference levels(rate);
        juce::AudioBuffer<float>input(2,block),actual(2,block),expected(2,block);
        // Cover detector warm-up / latency at every rate and all four block sizes.
        const int frames=std::max(2048,int(std::ceil(rate*.045)));
        for(int start=0;start<frames;start+=block){fill(input,start,rate);actual.makeCopyOf(input,true);expected.makeCopyOf(input,true);run(wrapper,actual);{CallbackProbe probe;core.process(expected.getWritePointer(0),expected.getWritePointer(1),block,wrapper.settings());}
            for(int c=0;c<2;++c)for(int i=0;i<block;++i)check(actual.getSample(c,i)==expected.getSample(c,i),"wrapper differs from recovered core");levels.compare(wrapper,input,actual);
        }
        ++cases;
    }
    std::cout<<"PASS "<<name<<": "<<cases<<" sample-exact wrapper/core and stereo peak/RMS cases (neutral/active/bypass/audition)\n";
}
template<class Processor>void lifecycle(const char*name){
    Processor p;check(p.actualRate==0,"constructor has a fabricated actual rate");checkUnavailable(p,false);juce::AudioBuffer<float>b(2,257);fill(b,0,48000);run(p,b);silence(b);checkUnavailable(p,false);
    for(double rate:{8000.,44100.,48000.,96000.,192000.,384000.}){
        p.prepareToPlay(rate,128);checkUnavailable(p,true);check(p.actualRate==rate&&p.meterSnapshot().sampleRate==rate,"prepared rate mismatch");const int latency=p.getLatencySamples();
        for(int start=0;start<int(rate*.04)+latency;start+=257){fill(b,start,rate);run(p,b);}check(p.restorationDisplay().valid,"family never received real signal");
        {CallbackProbe probe;p.reset();}checkUnavailable(p,true);check(p.actualRate==rate&&p.getLatencySamples()==latency,"reset changed preparation contract");
        for(int start=0;start<latency+514;start+=257){b.clear();run(p,b);silence(b);}check(p.meterSnapshot().hasProcessedAudio&&!p.meterSnapshot().signalPresent,"silence not marked as processed silence");
        fill(b,5000,rate);run(p,b);p.releaseResources();checkUnavailable(p,false);check(p.actualRate==0,"release retained actual sample rate");fill(b,0,rate);run(p,b);silence(b);checkUnavailable(p,false);{CallbackProbe probe;p.reset();}checkUnavailable(p,false);
        // Re-preparing the reused object must exactly match a fresh object.
        p.prepareToPlay(rate,257);Processor fresh;fresh.prepareToPlay(rate,257);juce::AudioBuffer<float>reference(2,257);
        for(int start=0;start<latency+1028;start+=257){fill(b,start,rate);reference.makeCopyOf(b,true);run(p,b);run(fresh,reference);for(int c=0;c<2;++c)for(int i=0;i<257;++i)check(b.getSample(c,i)==reference.getSample(c,i),"reprepare differs from fresh processor");}
    }
    for(double rate:{0.,-1.,7999.,384001.,std::numeric_limits<double>::quiet_NaN(),std::numeric_limits<double>::infinity(),-std::numeric_limits<double>::infinity()}){p.prepareToPlay(rate,257);checkUnavailable(p,false);check(p.actualRate==0&&p.getLatencySamples()==0,"invalid prepare published rate/latency");fill(b,0,48000);run(p,b);silence(b);checkUnavailable(p,false);}
    p.prepareToPlay(48000,257);checkUnavailable(p,true);fill(b,0,48000);run(p,b);check(p.meterSnapshot().hasProcessedAudio,"valid prepare after invalid did not recover");
    std::cout<<"PASS "<<name<<": construction/prepared/reset/released/reprepared/invalid-rate recovery and stale telemetry rejection\n";
}
}
int main(){try{juce::ScopedJuceInitialiser_GUI gui;
    {CallbackProbe probe;void*p=::operator new(31);::operator delete(p);void*a=::operator new(127,std::align_val_t(64));::operator delete(a,std::align_val_t(64));}check(allocationProbe::count==2,"scalar/aligned probe positive controls");allocationProbe::count=0;
    lifecycle<hem::DynamicResonanceSuppressorProcessor>("02");lifecycle<hem::BreathMouthControlProcessor>("06");lifecycle<hem::ClipRepairProcessor>("09");
    correspondence<hem::DynamicResonanceSuppressorProcessor,hem::DynamicResonanceCore>("02");correspondence<hem::BreathMouthControlProcessor,hem::BreathMouthCore>("06");correspondence<hem::ClipRepairProcessor,hem::ClipRepairCore>("09");
    check(allocationProbe::count==0,"callback/reset allocated");std::cout<<"PASS "<<checks<<" assertions; zero measured callback/reset allocations\n";return 0;
}catch(const std::exception&e){allocationProbe::enabled=false;allocationProbe::testThread=false;std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
