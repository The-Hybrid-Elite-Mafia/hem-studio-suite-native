#include "SpecialtyProcessors.h"
#include "SpecialtyEditors.h"
namespace hem {
DhtaProcessor::DhtaProcessor() : ModuleProcessor("DHTA","D.H.T.A. / De-Esser & Harshness Tamer",{
    {"frequency","TARGET",1000.0f,16000.0f,6800.0f,1.0f,"Hz"},
    {"q","BANDWIDTH",0.5f,8.0f,2.0f,0.01f,"Q"},
    {"threshold","THRESHOLD",-60.0f,0.0f,0.0f,0.1f,"dB"},
    {"range","MAX CUT",0.0f,24.0f,0.0f,0.1f,"dB"},
    {"attack","ATTACK",0.1f,20.0f,2.0f,0.1f,"ms"},
    {"release","RELEASE",10.0f,500.0f,80.0f,1.0f,"ms"},
    {"mix","MIX",0.0f,100.0f,100.0f,0.1f,"%"},
    {"output","OUTPUT",-24.0f,6.0f,0.0f,0.1f,"dB"},
    {"wide","WIDEBAND",0.0f,1.0f,0.0f,1.0f,"",true},
    {"delta","DELTA LISTEN",0.0f,1.0f,0.0f,1.0f,"",true},
},PluginMeterKind::gainReduction) {}
void DhtaProcessor::prepareToPlay(double rate,int) { resetMeterTelemetry(); actualRate.store(rate); setLatencySamples(0); core.prepare(rate); publishMeterPreparation(); }
void DhtaProcessor::processBlock(juce::AudioBuffer<float>& buffer,juce::MidiBuffer&) {
    juce::ScopedNoDenormals guard;
    if(buffer.getNumChannels()<2||buffer.getNumSamples()<=0) return;
    DhtaSettings p;
    p.frequency=value(0);
    p.q=value(1);
    p.threshold=value(2);
    p.range=value(3);
    p.attack=value(4);
    p.release=value(5);
    p.mix=value(6);
    p.output=value(7);
    p.wide=value(8)>.5f;
    p.delta=value(9)>.5f;
    core.process(buffer.getWritePointer(0),buffer.getWritePointer(1),buffer.getNumSamples(),p);
    publishMeterBlock(buffer);
}
juce::AudioProcessorEditor* DhtaProcessor::createEditor() { return new DhtaEditor(*this); }
TransientProcessor::TransientProcessor() : ModuleProcessor("TRANSIENT","Transient Shaper & Attack Enhancer",{
    {"attack","ATTACK AMOUNT",-12.0f,12.0f,0.0f,0.1f,"dB"},
    {"sustain","SUSTAIN AMOUNT",-12.0f,12.0f,0.0f,0.1f,"dB"},
    {"speed","ATTACK TIME",0.1f,20.0f,2.0f,0.1f,"ms"},
    {"recovery","RECOVERY",20.0f,500.0f,100.0f,1.0f,"ms"},
    {"mix","MIX",0.0f,100.0f,100.0f,0.1f,"%"},
    {"output","OUTPUT",-24.0f,6.0f,0.0f,0.1f,"dB"},
},PluginMeterKind::envelopeGain) {}
void TransientProcessor::prepareToPlay(double rate,int) { resetMeterTelemetry(); actualRate.store(rate); setLatencySamples(0); core.prepare(rate); publishMeterPreparation(); }
void TransientProcessor::processBlock(juce::AudioBuffer<float>& buffer,juce::MidiBuffer&) {
    juce::ScopedNoDenormals guard;
    if(buffer.getNumChannels()<2||buffer.getNumSamples()<=0) return;
    TransientSettings p;
    p.attack=value(0);
    p.sustain=value(1);
    p.speed=value(2);
    p.recovery=value(3);
    p.mix=value(4);
    p.output=value(5);
    core.process(buffer.getWritePointer(0),buffer.getWritePointer(1),buffer.getNumSamples(),p);
    publishMeterBlock(buffer);
}
juce::AudioProcessorEditor* TransientProcessor::createEditor() { return new TransientEditor(*this); }
SubBassProcessor::SubBassProcessor() : ModuleProcessor("SUB_BASS","Low-End 808 Sub-Bass Manager",{
    {"crossover","LOW BAND",40.0f,250.0f,100.0f,1.0f,"Hz"},
    {"lowGain","BASS LEVEL",-12.0f,12.0f,0.0f,0.1f,"dB"},
    {"subCut","SUB CUT",15.0f,50.0f,25.0f,0.1f,"Hz"},
    {"drive","LOW SATURATION",0.0f,12.0f,0.0f,0.1f,"dB"},
    {"mono","LOW MONO",0.0f,100.0f,0.0f,0.1f,"%"},
    {"mix","MIX",0.0f,100.0f,100.0f,0.1f,"%"},
    {"output","OUTPUT",-24.0f,6.0f,0.0f,0.1f,"dB"},
    {"cutEnabled","SUB CUT ENABLE",0.0f,1.0f,0.0f,1.0f,"",true},
}) {}
void SubBassProcessor::prepareToPlay(double rate,int) { resetMeterTelemetry(); actualRate.store(rate); setLatencySamples(0); core.prepare(rate); publishMeterPreparation(); }
void SubBassProcessor::processBlock(juce::AudioBuffer<float>& buffer,juce::MidiBuffer&) {
    juce::ScopedNoDenormals guard;
    if(buffer.getNumChannels()<2||buffer.getNumSamples()<=0) return;
    SubBassSettings p;
    p.crossover=value(0);
    p.lowGain=value(1);
    p.subCut=value(2);
    p.drive=value(3);
    p.mono=value(4);
    p.mix=value(5);
    p.output=value(6);
    p.cutEnabled=value(7)>.5f;
    core.process(buffer.getWritePointer(0),buffer.getWritePointer(1),buffer.getNumSamples(),p);
    publishMeterBlock(buffer);
}
juce::AudioProcessorEditor* SubBassProcessor::createEditor() { return new SubBassEditor(*this); }
RestorerProcessor::RestorerProcessor() : ModuleProcessor("RESTORER","Artifact & Spectral Restorer / Multiband DSP",{
    {"amount","MAX REDUCTION",0.0f,18.0f,0.0f,0.1f,"dB"},
    {"floor","NOISE FLOOR",-90.0f,-20.0f,-60.0f,0.1f,"dBFS"},
    {"release","RELEASE",20.0f,500.0f,100.0f,1.0f,"ms"},
    {"protect","TRANSIENT PRESERVE",0.0f,100.0f,100.0f,0.1f,"%"},
    {"mix","MIX",0.0f,100.0f,100.0f,0.1f,"%"},
    {"output","OUTPUT",-24.0f,6.0f,0.0f,0.1f,"dB"},
    {"learn","LEARN NOISE ONLY",0.0f,1.0f,0.0f,1.0f,"",true},
},PluginMeterKind::gainReduction) {}
void RestorerProcessor::prepareToPlay(double rate,int) { resetMeterTelemetry(); actualRate.store(rate); setLatencySamples(0); core.prepare(rate); publishMeterPreparation(); }
void RestorerProcessor::processBlock(juce::AudioBuffer<float>& buffer,juce::MidiBuffer&) {
    juce::ScopedNoDenormals guard;
    if(buffer.getNumChannels()<2||buffer.getNumSamples()<=0) return;
    RestorerSettings p;
    p.amount=value(0);
    p.floor=value(1);
    p.release=value(2);
    p.protect=value(3);
    p.mix=value(4);
    p.output=value(5);
    p.learn=value(6)>.5f;
    core.process(buffer.getWritePointer(0),buffer.getWritePointer(1),buffer.getNumSamples(),p);
    publishMeterBlock(buffer);
}
juce::AudioProcessorEditor* RestorerProcessor::createEditor() { return new RestorerEditor(*this); }
}
