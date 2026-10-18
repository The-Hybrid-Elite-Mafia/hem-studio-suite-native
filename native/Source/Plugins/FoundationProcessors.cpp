#include "FoundationProcessors.h"
#include "FoundationEditors.h"
namespace hem {
DcSubsonicProcessor::DcSubsonicProcessor():FoundationProcessor("DC_SUBSONIC","DC / Subsonic Cleaner",{
    {"dcOn","DC BLOCK",0,1,0,1,"",true},
    {"subOn","SUBSONIC FILTER",0,1,0,1,"",true},
    {"cutoff","SUBSONIC CUTOFF",10,80,25,.1f,"Hz"},
    {"steep","24 dB/OCT",0,1,0,1,"",true},
    {"mix","MIX",0,100,100,.1f,"%"},
    {"output","OUTPUT",-24,6,0,.1f,"dB"}},PluginMeterKind::samplePeak) {}
SmartGateProcessor::SmartGateProcessor():FoundationProcessor("SMART_GATE","Smart Gate / Expander",{
    {"threshold","OPEN THRESHOLD",-80,0,-40,.1f,"dBFS peak"},
    {"hysteresis","CLOSE HYSTERESIS",0,24,6,.1f,"dB"},
    {"ratio","EXPANSION RATIO",1,10,2,.01f,":1"},
    {"range","MAX ATTENUATION",0,24,0,.1f,"dB"},
    {"open","OPEN TIME",.1f,50,5,.1f,"ms"},
    {"hold","CLOSE HOLD",0,500,50,1,"ms"},
    {"close","CLOSE TIME",5,2000,100,1,"ms"},
    {"gate","HARD GATE",0,1,0,1,"",true},
    {"mix","MIX",0,100,100,.1f,"%"},
    {"output","OUTPUT",-24,6,0,.1f,"dB"},
    {"lookaheadMs","LOOKAHEAD",0,6,0,.1f,"ms"},
    {"keyListen","KEY LISTEN",0,1,0,1,"",true}},PluginMeterKind::gainReduction) {}
VocalRiderProcessor::VocalRiderProcessor():FoundationProcessor("VOCAL_RIDER","Vocal Rider",{
    {"target","TARGET RMS",-36,-6,-18,.1f,"dBFS RMS"},
    {"maxUp","MAX BOOST",0,12,0,.1f,"dB"},
    {"maxDown","MAX CUT",0,12,0,.1f,"dB"},
    {"rise","BOOST TIME",50,2000,400,1,"ms"},
    {"fall","CUT TIME",10,2000,150,1,"ms"},
    {"floor","ACTIVITY FLOOR",-80,-20,-60,.1f,"dBFS RMS"},
    {"mix","MIX",0,100,100,.1f,"%"},
    {"output","OUTPUT",-24,6,0,.1f,"dB"}},PluginMeterKind::envelopeGain) {}
PlosiveProcessor::PlosiveProcessor():FoundationProcessor("PLOSIVE_CONTROL","Plosive Control",{
    {"cutoff","LOW-BAND CUTOFF",40,180,100,1,"Hz"},
    {"threshold","LOW-BAND THRESHOLD",-60,-6,-24,.1f,"dBFS RMS"},
    {"contrast","BURST CONTRAST",0,18,6,.1f,"dB"},
    {"range","MAX LOW-BAND CUT",0,24,0,.1f,"dB"},
    {"attack","CUT ATTACK",.1f,20,2,.1f,"ms"},
    {"release","RECOVERY",20,500,120,1,"ms"},
    {"mix","MIX",0,100,100,.1f,"%"},
    {"output","OUTPUT",-24,6,0,.1f,"dB"},
    {"listenRemoved","LISTEN REMOVED",0,1,0,1,"",true}},PluginMeterKind::gainReduction) {}
HumBuzzProcessor::HumBuzzProcessor():FoundationProcessor("HUM_BUZZ","Hum & Buzz Remover",{
    {"fundamental","FUNDAMENTAL",40,70,50,.01f,"Hz"},
    {"q","NOTCH Q",10,60,30,.1f,"Q"},
    {"harmonics","HARMONICS",1,16,8,1,"count"},
    {"depth","NOTCH DEPTH",0,36,0,.1f,"dB"},
    {"mix","MIX",0,100,100,.1f,"%"},
    {"output","OUTPUT",-24,6,0,.1f,"dB"},
    {"listenRemoved","LISTEN REMOVED",0,1,0,1,"",true},
    {"sensitivity","DETECTION SENSITIVITY",0,100,50,.1f,"%"}},PluginMeterKind::samplePeak) {}
juce::AudioProcessorEditor* DcSubsonicProcessor::createEditor(){return new DcSubsonicEditor(*this);}
juce::AudioProcessorEditor* SmartGateProcessor::createEditor(){return new SmartGateEditor(*this);}
juce::AudioProcessorEditor* VocalRiderProcessor::createEditor(){return new VocalRiderEditor(*this);}
juce::AudioProcessorEditor* PlosiveProcessor::createEditor(){return new PlosiveEditor(*this);}
juce::AudioProcessorEditor* HumBuzzProcessor::createEditor(){return new HumBuzzEditor(*this);}
}
