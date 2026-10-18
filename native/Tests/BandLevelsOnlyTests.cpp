#include "../Source/Plugins/PluginRegistry.h"
#include "DelayAllocationProbe.h"
#include <iostream>
#include <stdexcept>
static void check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
int main(){juce::ScopedJuceInitialiser_GUI gui;try{
 check(allocationProbe::calibrate(),"allocation detector positive controls");
 hem::PluginTelemetry levelsOnly(false,false);check(!levelsOnly.analysis.operational(),"levels-only created an analysis thread");levelsOnly.prepare(48000);
 std::array<float,257> inL,inR,outL,outR;inL.fill(.5f);inR.fill(.125f);outL.fill(.25f);outR.fill(.0625f);
 for(int i=0;i<40;++i){allocationProbe::enabled=true;levelsOnly.input(inL.data(),inR.data(),257);levelsOnly.output(outL.data(),outR.data(),257);allocationProbe::enabled=false;}
 hem::PluginTelemetry::Levels levels;check(levelsOnly.read(levels)&&levels.valid&&levels.blocks==40,"levels-only publication invalid");
 check(levels.peak==std::array<float,4>{.5f,.125f,.25f,.0625f},"independent stereo levels changed");hem::PluginAnalysis::Snapshot spectrum;
 check(!levelsOnly.analysis.copyLatest(spectrum)&&levelsOnly.analysis.lastConsumedAt()==0,"levels-only fabricated analysis");
 levelsOnly.suspend();check(!levelsOnly.read(levels)||!levels.valid,"suspend retained valid levels");
 for(const char* id:{"dynamic-eq-8","mid-side-eq","multiband-dynamics","multiband-imager"}){
  auto p=hem::PluginRegistry::create(id);check(p&&!p->processingReady(),"new band falsely ready");p->prepareToPlay(48000,257);
  auto* telemetry=p->pluginTelemetry();check(telemetry&&!telemetry->analysis.operational(),"Band owns unused analysis worker");
  juce::AudioBuffer<float> b(2,257);juce::MidiBuffer midi;for(int c=0;c<2;++c)for(int i=0;i<257;++i)b.setSample(c,i,c?.125f:.5f);
  allocationProbe::enabled=true;p->processBlock(b,midi);allocationProbe::enabled=false;
  check(telemetry->read(levels)&&levels.valid&&levels.peak[0]==.5f&&levels.peak[1]==.125f,"Band stereo input publication invalid");
  check(levels.peak[2]==b.getMagnitude(0,0,257)&&levels.peak[3]==b.getMagnitude(1,0,257),"Band output peaks differ from actual DSP");
  p->releaseResources();check(!p->processingReady()&&!p->meterSnapshot().isPrepared&&!p->meterSnapshot().hasProcessedAudio&&p->actualRate.load()==0,"released Band falsely ready");
  check(!telemetry->read(levels)||!levels.valid,"release retained valid stereo data");allocationProbe::enabled=true;p->processBlock(b,midi);allocationProbe::enabled=false;check(b.getMagnitude(0,0,257)==0&&!p->meterSnapshot().hasProcessedAudio,"released processor ran stale DSP");
  p->prepareToPlay(96000,257);check(p->processingReady()&&p->meterSnapshot().sampleRate==96000&&!p->meterSnapshot().hasProcessedAudio,"reprepare metadata invalid");
 }
 hem::SipaProcessor existing;check(existing.pluginTelemetry()&&existing.pluginTelemetry()->analysis.operational(),"existing legacy worker default changed");
 check(allocationProbe::count==0,"levels callback allocated");std::cout<<"PASS levels-only stereo publication; no Band analysis threads; prepared/released/reprepared readiness; unchanged legacy worker default; zero intercepted C++ callback new\n";return 0;
}catch(const std::exception&e){allocationProbe::enabled=false;std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
