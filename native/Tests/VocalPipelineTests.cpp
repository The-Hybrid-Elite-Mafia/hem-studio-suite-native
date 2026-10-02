#include "../Source/Systems/LiveVocalManager.h"
#include <iostream>
#include <stdexcept>
static void require(bool b,const char* m){if(!b)throw std::runtime_error(m);}
int main(){juce::ScopedJuceInitialiser_GUI runtime;try{
 auto manager=std::make_unique<hem::LiveVocalManager>();manager->prepare(96000,256,2);
 juce::AudioBuffer<float> hardware(2,256),bus(2,256);for(int i=0;i<256;++i){hardware.setSample(0,i,.25f);hardware.setSample(1,i,-.5f);}
 manager->setMonitor(hem::LiveVocalManager::Monitor::DrySoftware);
 for(int block=0;block<10;++block){bus.clear();manager->capture(hardware,0,256,block*256./96000);manager->addMonitor(bus,256);}
 require(std::abs(bus.getSample(0,255)-.25f)<1.e-6,"Dry input mapping");require(manager->rack.latency()==0,"Empty tracking slots added latency");
 manager->setInput(1);for(int block=0;block<10;++block){bus.clear();manager->capture(hardware,0,256,0);manager->addMonitor(bus,256);}require(std::abs(bus.getSample(0,255)+.5f)<1.e-6,"Input channel switch");
 manager->setMonitor(hem::LiveVocalManager::Monitor::InterfaceDirect);for(int block=0;block<10;++block){bus.clear();manager->capture(hardware,0,256,0);manager->addMonitor(bus,256);}require(bus.getMagnitude(0,256)==0,"Hardware direct mode duplicated vocal return");
 auto node=manager->rack.makeNode("sipa",96000,256);manager->rack.exchange(0,std::move(node));require(manager->rack.latency()==0,"Zero-latency SIPA added delay");manager->setMonitor(hem::LiveVocalManager::Monitor::InsertsSoftware);
 for(int block=0;block<10;++block){bus.clear();manager->capture(hardware,0,256,0);manager->addMonitor(bus,256);}require(std::abs(bus.getSample(0,255)+.5f)<1.e-5,"Neutral insert path changed vocal");
 auto limiter=manager->rack.makeNode("true-peak",96000,256);manager->rack.exchange(7,std::move(limiter));require(manager->rack.latency()==480,"Lookahead latency not reported");
 require(hardware.getSample(0,0)==.25f&&hardware.getSample(1,0)==-.5f,"Capture overwrote hardware buffer");
 std::cout<<"PASS: input selection, dry capture, interface-direct software mute, natural rack latency and neutral insert routing\n";
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}return 0;}
