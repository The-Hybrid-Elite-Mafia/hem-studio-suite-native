#include "FinishingProcessors.h"
#include "FinishingEditors.h"
namespace hem {
VocalProcessor::VocalProcessor() : ModuleProcessor("VPR_ARCHITECT","Vocal Presence & Resonance Architect",{
    {"cutoff","LOW CUT",20.0f,300.0f,80.0f,1.0f,"Hz"},
    {"body","BODY / 150 Hz",-12.0f,12.0f,0.0f,0.1f,"dB"},
    {"mud","MUD / 350 Hz",-12.0f,12.0f,0.0f,0.1f,"dB"},
    {"presence","PRESENCE / 2.5 kHz",-12.0f,12.0f,0.0f,0.1f,"dB"},
    {"brightness","BRIGHTNESS / 5 kHz",-12.0f,12.0f,0.0f,0.1f,"dB"},
    {"air","AIR / 12 kHz",-12.0f,12.0f,0.0f,0.1f,"dB"},
    {"resonance","RESONANCE TARGET",1000.0f,10000.0f,2800.0f,1.0f,"Hz"},
    {"q","RESONANCE Q",0.5f,8.0f,2.0f,0.01f,"Q"},
    {"threshold","DYNAMIC THRESHOLD",-60.0f,0.0f,0.0f,0.1f,"dB"},
    {"range","MAX RESONANCE CUT",0.0f,18.0f,0.0f,0.1f,"dB"},
    {"attack","ATTACK",0.1f,30.0f,5.0f,0.1f,"ms"},
    {"release","RELEASE",10.0f,500.0f,120.0f,1.0f,"ms"},
    {"output","OUTPUT",-24.0f,6.0f,0.0f,0.1f,"dB"},
    {"cutEnabled","LOW CUT ENABLE",0.0f,1.0f,0.0f,1.0f,"",true},
}) {}
void VocalProcessor::prepareToPlay(double rate,int) { resetMeterTelemetry(); actualRate.store(rate); core.prepare(rate); setLatencySamples(0); publishMeterPreparation(); }
void VocalProcessor::processBlock(juce::AudioBuffer<float>& buffer,juce::MidiBuffer&) {
    juce::ScopedNoDenormals guard;
    if(buffer.getNumChannels()<2||buffer.getNumSamples()<=0) return;
    VocalSettings p;
    p.cutoff=value(0);
    p.body=value(1);
    p.mud=value(2);
    p.presence=value(3);
    p.brightness=value(4);
    p.air=value(5);
    p.resonance=value(6);
    p.q=value(7);
    p.threshold=value(8);
    p.range=value(9);
    p.attack=value(10);
    p.release=value(11);
    p.output=value(12);
    p.cutEnabled=value(13)>.5f;
    core.process(buffer.getWritePointer(0),buffer.getWritePointer(1),buffer.getNumSamples(),p);
    publishMeterBlock(buffer);
}
juce::AudioProcessorEditor* VocalProcessor::createEditor() { return new VocalEditor(*this); }
TubeProcessor::TubeProcessor() : ModuleProcessor("TRIODE_TUBE","Triode Tube Saturator / 4x",{
    {"drive","DRIVE",0.0f,24.0f,0.0f,0.1f,"dB"},
    {"bias","ASYMMETRY",-50.0f,50.0f,0.0f,0.1f,"%"},
    {"tone","TONE / 6 kHz",-6.0f,6.0f,0.0f,0.1f,"dB"},
    {"mix","MIX",0.0f,100.0f,100.0f,0.1f,"%"},
    {"output","OUTPUT",-24.0f,6.0f,0.0f,0.1f,"dB"},
}) {}
void TubeProcessor::prepareToPlay(double rate,int) { resetMeterTelemetry(); actualRate.store(rate); core.prepare(rate); setLatencySamples(core.latency()); publishMeterPreparation(); }
void TubeProcessor::processBlock(juce::AudioBuffer<float>& buffer,juce::MidiBuffer&) {
    juce::ScopedNoDenormals guard;
    if(buffer.getNumChannels()<2||buffer.getNumSamples()<=0) return;
    TubeSettings p;
    p.drive=value(0);
    p.bias=value(1);
    p.tone=value(2);
    p.mix=value(3);
    p.output=value(4);
    core.process(buffer.getWritePointer(0),buffer.getWritePointer(1),buffer.getNumSamples(),p);
    publishMeterBlock(buffer);
}
juce::AudioProcessorEditor* TubeProcessor::createEditor() { return new TubeEditor(*this); }
TapeProcessor::TapeProcessor() : ModuleProcessor("TAPE","Tape Saturator / 4x",{
    {"drive","DRIVE",0.0f,24.0f,0.0f,0.1f,"dB"},
    {"warmth","HEAD BUMP / 80 Hz",-6.0f,6.0f,0.0f,0.1f,"dB"},
    {"rolloff","HF LOSS / 10 kHz",-12.0f,0.0f,0.0f,0.1f,"dB"},
    {"mix","MIX",0.0f,100.0f,100.0f,0.1f,"%"},
    {"output","OUTPUT",-24.0f,6.0f,0.0f,0.1f,"dB"},
}) {}
void TapeProcessor::prepareToPlay(double rate,int) { resetMeterTelemetry(); actualRate.store(rate); core.prepare(rate); setLatencySamples(core.latency()); publishMeterPreparation(); }
void TapeProcessor::processBlock(juce::AudioBuffer<float>& buffer,juce::MidiBuffer&) {
    juce::ScopedNoDenormals guard;
    if(buffer.getNumChannels()<2||buffer.getNumSamples()<=0) return;
    TapeSettings p;
    p.drive=value(0);
    p.warmth=value(1);
    p.rolloff=value(2);
    p.mix=value(3);
    p.output=value(4);
    core.process(buffer.getWritePointer(0),buffer.getWritePointer(1),buffer.getNumSamples(),p);
    publishMeterBlock(buffer);
}
juce::AudioProcessorEditor* TapeProcessor::createEditor() { return new TapeEditor(*this); }
MonitorProcessor::MonitorProcessor() : ModuleProcessor("MONITOR_EQ","Audio Output / Optional Monitor EQ",{
    {"bass","BASS / 80 Hz",-12.0f,6.0f,0.0f,0.1f,"dB"},
    {"body","BODY / 250 Hz",-12.0f,6.0f,0.0f,0.1f,"dB"},
    {"presence","PRESENCE / 3 kHz",-12.0f,6.0f,0.0f,0.1f,"dB"},
    {"air","AIR / 10 kHz",-12.0f,6.0f,0.0f,0.1f,"dB"},
    {"preamp","MONITOR HEADROOM",-24.0f,0.0f,0.0f,0.1f,"dB"},
    {"enabled","MONITOR EQ ENABLE",0.0f,1.0f,0.0f,1.0f,"",true},
}) {}
void MonitorProcessor::prepareToPlay(double rate,int) { resetMeterTelemetry(); actualRate.store(rate); core.prepare(rate); setLatencySamples(0); publishMeterPreparation(); }
void MonitorProcessor::processBlock(juce::AudioBuffer<float>& buffer,juce::MidiBuffer&) {
    juce::ScopedNoDenormals guard;
    if(buffer.getNumChannels()<2||buffer.getNumSamples()<=0) return;
    MonitorSettings p;
    p.bass=value(0);
    p.body=value(1);
    p.presence=value(2);
    p.air=value(3);
    p.preamp=value(4);
    p.enabled=value(5)>.5f;
    core.process(buffer.getWritePointer(0),buffer.getWritePointer(1),buffer.getNumSamples(),p);
    publishMeterBlock(buffer);
}
juce::AudioProcessorEditor* MonitorProcessor::createEditor() { return new MonitorEditor(*this); }
}
