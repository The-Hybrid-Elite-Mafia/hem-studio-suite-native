#include "../Source/Systems/LiveVocalManager.h"
#include <iostream>
#include <stdexcept>
static void require(bool b,const char* text){if(!b)throw std::runtime_error(text);}
int main(){try{
 auto m=std::make_unique<hem::LiveVocalManager>();
 juce::AudioBuffer<float> input(16,256),bus(2,256);
 for(int c=0;c<16;++c)for(int i=0;i<256;++i)input.setSample(c,i,float(c+1)/32);
 m->prepare(96000,256,16);
 require(m->setInput(15),"Input 16 unavailable");require(!m->setInput(16),"Invalid index accepted");
 m->setMonitor(hem::LiveVocalManager::Monitor::DrySoftware);
 for(int k=0;k<12;++k){m->capture(input,0,256,0);bus.clear();m->addMonitor(bus,256);}
 require(std::abs(bus.getSample(0,255)-.5f)<1e-6f,"Input 16 mapping failed");
 require(m->setInput(4),"Input 5 unavailable");
 for(int k=0;k<12;++k){m->capture(input,0,256,0);bus.clear();m->addMonitor(bus,256);}
 require(std::abs(bus.getSample(1,255)-5.f/32)<1e-6f,"Channel switch failed");
 m->prepare(48000,256,0);require(!m->setInput(0),"Missing input accepted");m->capture(input,0,256,0);require(m->inputPeak()==0,"Disconnected input leaked");
 require(m->monitoring()==hem::LiveVocalManager::Monitor::Off,"Device reset enabled monitoring");
 m->prepare(48000,256,1);require(m->setInput(0)&&!m->setInput(1),"Mono input bounds failed");
 std::cout<<"PASS: 16-channel capture, channel switching, disconnect silence and mono bounds\n";
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}return 0;}
