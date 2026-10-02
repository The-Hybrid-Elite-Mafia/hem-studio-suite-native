#include "LiveVocalManager.h"
#include <stdexcept>
namespace hem {
void TrackingInsertChain::prepare(double rate,int block){maximumBlock=std::max(1,block);for(auto& b:dry)b.assign(size_t(maximumBlock),0);
 for(auto& p:slots)if(p){p->processor->setPlayConfigDetails(2,2,rate,block);p->processor->prepareToPlay(rate,block);p->dryDelay.prepare(p->processor->getLatencySamples());p->wet.reset(rate,p->bypassed?0.f:1.f);}}
std::unique_ptr<TrackingInsertChain::Node> TrackingInsertChain::makeNode(const juce::String& id,double rate,int block){
 if(id.isEmpty())return {};auto p=PluginRegistry::create(id);if(!p||p->monitorOnly())throw std::runtime_error("Invalid vocal insert");
 auto node=std::make_unique<Node>();node->processor=std::move(p);node->processor->setPlayConfigDetails(2,2,rate,block);node->processor->prepareToPlay(rate,block);node->dryDelay.prepare(node->processor->getLatencySamples());node->wet.reset(rate,1);return node;
}
std::unique_ptr<TrackingInsertChain::Node> TrackingInsertChain::exchange(int index,std::unique_ptr<Node> p){if(index<0||index>=8)throw std::out_of_range("Vocal insert");slots[size_t(index)].swap(p);return p;}
void TrackingInsertChain::process(float* l,float* r,int n) noexcept {
 for(auto& node:slots)if(node){std::copy_n(l,n,dry[0].data());std::copy_n(r,n,dry[1].data());node->dryDelay.process(dry[0].data(),dry[1].data(),n);
    float* c[]{l,r};view.setDataToReferTo(c,2,n);midi.clear();node->processor->processBlock(view,midi);node->wet.set(node->bypassed?0.f:1.f);
    for(int i=0;i<n;++i){const float w=node->wet.next();l[i]=dry[0][size_t(i)]+w*(l[i]-dry[0][size_t(i)]);r[i]=dry[1][size_t(i)]+w*(r[i]-dry[1][size_t(i)]);}}
}
void LiveVocalManager::prepare(double sr,int block,int inputs){recorder.stop();++generation;rate=sr;channels=std::clamp(inputs,0,128);input=0;inputWeights.resize(size_t(channels));for(int c=0;c<channels;++c)inputWeights[size_t(c)].reset(sr,c==0?1.f:0.f);dry.setSize(2,block);processed.setSize(2,block);rack.prepare(sr,block);dryMix.reset(sr,0);wetMix.reset(sr,0);level.reset(sr,1);monitor=Monitor::Off;}
bool LiveVocalManager::configureTake(TakeRecorder::Setup& setup){if(recorder.busy())return false;if(channels<=0||input.load()<0||input.load()>=channels)return false;setup.rate=rate;setup.channels=printInserts?2:1;setup.printedLatency=printInserts?rack.latency():0;takeGeneration=generation;return true;}
void LiveVocalManager::capture(const juce::AudioBuffer<float>& buffer,int start,int n,double position) noexcept {
 const int index=input.load();const bool valid=index>=0&&index<channels&&index<buffer.getNumChannels();float max=0;
 for(int c=0;c<channels;++c)inputWeights[size_t(c)].set(valid&&c==index?1.f:0.f);
 for(int i=0;i<n;++i){float raw=0;for(int c=0;c<channels;++c){const float weight=inputWeights[size_t(c)].next();if(c<channels&&c<buffer.getNumChannels())raw+=weight*buffer.getSample(c,start+i);}const float sample=std::isfinite(raw)?raw:0.f;dry.setSample(0,i,sample);dry.setSample(1,i,sample);max=std::max(max,std::abs(sample));}
 peak.store(max);for(int c=0;c<2;++c)processed.copyFrom(c,0,dry,c,0,n);rack.process(processed.getWritePointer(0),processed.getWritePointer(1),n);
 if(takeGeneration!=generation)recorder.stop();
 const auto& tap=printInserts?processed:dry;recorder.push(tap.getReadPointer(0),tap.getReadPointer(1),n,position);
}
void LiveVocalManager::addMonitor(juce::AudioBuffer<float>& bus,int n) noexcept {
 const auto m=monitor.load();dryMix.set(m==Monitor::DrySoftware?1.f:0.f);wetMix.set(m==Monitor::InsertsSoftware?1.f:0.f);level.set(gain(monitorDb.load()));
 auto* l=bus.getWritePointer(0);auto* r=bus.getWritePointer(1);
 for(int i=0;i<n;++i){const float d=dryMix.next(),w=wetMix.next(),g=level.next();l[i]+=g*(d*dry.getSample(0,i)+w*processed.getSample(0,i));r[i]+=g*(d*dry.getSample(1,i)+w*processed.getSample(1,i));}
}
}
