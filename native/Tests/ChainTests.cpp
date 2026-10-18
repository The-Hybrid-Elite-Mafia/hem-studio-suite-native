#include <JuceHeader.h>
#include "../Source/InsertChain.h"
#include "../Source/Plugins/PluginRegistry.h"
#include <iostream>
#include <stdexcept>
static void require(bool b,const char* s){if(!b)throw std::runtime_error(s);}
int main(){juce::ScopedJuceInitialiser_GUI init;try{
 hem::InsertChain chain;constexpr int block=256;chain.prepare(96000,block);std::array<float,block> l{},r{};
 auto render=[&](int blocks){for(int k=0;k<blocks;++k){l.fill(.05f);r.fill(.05f);chain.process(l.data(),r.data(),block);for(float x:l)require(std::isfinite(x),"Non-finite rack output");}};
 render(40);require(std::abs(l.back()-.05f)<1e-6f,"Empty rack is not delayed unity");
 for(int slot=0;slot<8;++slot){auto p=chain.stageNode(hem::createModule(slot),96000,block,chain.slotLatency());require(chain.publish(slot,std::move(p)),"Insertion rejected");render(40);require(!chain.isBusy(slot),"Insertion never completed");require(chain.activeProcessor(slot)!=nullptr,"Processor missing");}
 render(40);require(std::abs(l.back()-.05f)<1e-5f,"Neutral loaded chain not unity");
 for(int i=0;i<8;++i)chain.bypass(i,true);render(40);require(std::abs(l.back()-.05f)<1e-6f,"Bypass altered gain");
 auto* eq=chain.activeProcessor(1);auto* parameter=eq->state.getParameter("mudGain");parameter->setValueNotifyingHost(parameter->convertTo0to1(-7));juce::MemoryBlock state;eq->getStateInformation(state);
 auto copy=hem::createModule(1);copy->setStateInformation(state.getData(),int(state.getSize()));require(std::abs(copy->state.getRawParameterValue("mudGain")->load()+7)<1e-6f,"State roundtrip failed");
 require(chain.publish(0,chain.stageNode(std::move(copy),96000,block,chain.slotLatency())),"Replacement rejected");render(40);auto retired=chain.collectRetired(0);require(retired!=nullptr,"Old node not retired");retired.reset();
 require(chain.publish(0,nullptr),"Remove rejected");render(40);retired=chain.collectRetired(0);require(retired!=nullptr&&!chain.activeProcessor(0),"Removal failed");retired.reset();
 auto* before=chain.activeProcessor(4);auto* other=chain.activeProcessor(5);
 require(chain.swapSlotsStopped(4,5),"Stopped reorder rejected");require(chain.activeProcessor(5)==before&&chain.activeProcessor(4)==other,"Reorder lost processor identity");
 for(const auto& entry:hem::PluginRegistry::entries()){
 auto p=hem::PluginRegistry::create(entry.id);require(p!=nullptr,"Registry factory failed");
 juce::MemoryBlock bytes;p->getStateInformation(bytes);p->setStateInformation(bytes.getData(),int(bytes.getSize()));
 }

 for(const auto& entry:hem::PluginRegistry::entries()){
 if(entry.monitoringOnly)continue;
 require(chain.publish(0,chain.stageNode(entry.create(),96000,block,chain.slotLatency())),"Registry insert rejected");
 chain.bypass(0,false);render(80);auto old=chain.collectRetired(0);old.reset();
 require(chain.activeProcessor(0)!=nullptr,"Finishing processor missing");
 require(std::abs(l.back()-.05f)<1.e-5f,"Finishing processor not latency-compensated unity");
 }
 bool rejected=false;try{chain.stageNode(hem::PluginRegistry::create("monitor-eq"),96000,block,chain.slotLatency());}catch(const std::invalid_argument&){rejected=true;}
 require(rejected,"Monitor EQ entered a track insert");
 chain.release();chain.prepare(48000,128);render(80);require(std::isfinite(l.back()),"Rate restart failed");
 std::cout<<"PASS: eight inserts, unity, latency-aligned bypass, replacement, retirement, state roundtrip, removal, device-rate restart.\n";return 0;
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
