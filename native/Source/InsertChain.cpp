#include "InsertChain.h"
#include "Plugins/NativeResourceStateProvider.h"
#include <stdexcept>
namespace hem {
void InsertChain::PreparedNode::stage(double sr,int block,int budget){
 if(!processor->supportsStagedPreparation())return;
 if(stagedCompensation&&stagedRate==sr&&stagedBlock==block&&stagedBudget==budget)return;
 processor->stagePreparation(sr,block);
 try{const int latency=processor->stagedLatencySamples();if(latency<0||latency>budget)throw std::runtime_error("Staged delay exceeds slot latency budget");
  auto pending=std::make_unique<HostDelay>();pending->prepare(budget-latency);stagedCompensation=std::move(pending);stagedRate=sr;stagedBlock=block;stagedBudget=budget;
 }catch(...){processor->cancelStagedPreparation();throw;}
}
void InsertChain::PreparedNode::cancelStage()noexcept{processor->cancelStagedPreparation();stagedCompensation.reset();}
void InsertChain::PreparedNode::prepare(double sr,int block,int budget){
 deltaReferenceConsumer=dynamic_cast<analysis::ReferenceConsumer*>(processor.get());
 if(processor->supportsStagedPreparation()){
  stage(sr,block,budget);processor->setPlayConfigDetails(2,2,sr,block);processor->prepareToPlay(sr,block);
  compensation=std::move(*stagedCompensation);stagedCompensation.reset();return;
 }
 processor->setPlayConfigDetails(2,2,sr,block);processor->prepareToPlay(sr,block);
 const int latency=processor->processingReady()?processor->getLatencySamples():0;
 if(latency<0||latency>budget)throw std::runtime_error("Processor latency exceeds this host's 6 ms per-slot budget");
 compensation.prepare(budget-latency);
}
InsertChain::InsertChain(){for(auto& s:slots)s.owned.reserve(3);}
void InsertChain::stageDelayPreparations(double rate,int block){
 if(!std::isfinite(rate)||rate<8000||rate>384000)throw std::invalid_argument("Insert preparation rate must be 8000..384000 Hz");
 try{for(auto& s:slots)for(auto& p:s.owned)p->stage(rate,std::max(1,block),int(std::ceil(rate*.006)));}
 catch(...){cancelDelayPreparations();throw;}
}
void InsertChain::cancelDelayPreparations()noexcept{for(auto& s:slots)for(auto& p:s.owned)p->cancelStage();}
void InsertChain::prepare(double rate,int block){
 if(!std::isfinite(rate)||rate<8000||rate>384000)throw std::invalid_argument("Insert preparation rate must be 8000..384000 Hz");
 const int maximum=std::max(1,block),budget=int(std::ceil(rate*.006));
 std::array<std::vector<float>,2> nextDry,nextOld,nextNew,nextReference;std::array<HostDelay,slotCount> nextDelays,nextReferenceDelays;
 try{
  stageDelayPreparations(rate,block);
  for(auto* pair:{&nextDry,&nextOld,&nextNew,&nextReference})for(auto& ch:*pair)ch.assign(size_t(maximum),0);
  for(auto& delay:nextDelays)delay.prepare(budget);
  for(auto& delay:nextReferenceDelays)delay.prepare(budget);
  // Any legacy preparation failure occurs before committing staged delay cores.
  for(auto& s:slots)for(auto& p:s.owned)if(!p->processor->supportsStagedPreparation())p->prepare(rate,maximum,budget);
 }catch(...){cancelDelayPreparations();throw;}
 for(auto& s:slots)for(auto& p:s.owned)if(p->processor->supportsStagedPreparation())p->prepare(rate,maximum,budget);
 sampleRate=rate;maximumBlock=maximum;latencyBudget=budget;dry.swap(nextDry);oldWet.swap(nextOld);newWet.swap(nextNew);referenceTap.swap(nextReference);referenceTimeline=0;
 for(size_t i=0;i<slots.size();++i){auto& s=slots[i];s.dryDelay=std::move(nextDelays[i]);s.referenceDelay=std::move(nextReferenceDelays[i]);s.wet.reset(rate,s.bypassed.load()?0.f:1.f);s.replace.reset(rate,0);s.replace.length=std::max(1,int(rate*.02));s.transitioning=false;s.warmup=0;}
}
void InsertChain::release(){for(auto& s:slots)for(auto& p:s.owned)p->processor->releaseResources();}
std::unique_ptr<InsertChain::PreparedNode> InsertChain::stageNode(std::unique_ptr<ModuleProcessor> p,double rate,int block,int budget){
 if(!p)return {};if(p->monitorOnly())throw std::invalid_argument("Monitoring-only processors cannot enter a track insert rack");if(!p->realtimeInsertAllowed())throw std::invalid_argument("Offline source operations cannot enter a realtime insert rack");auto node=std::make_unique<PreparedNode>();node->processor=std::move(p);node->prepare(rate,block,budget);return node;
}
void InsertChain::installInitial(int i,std::unique_ptr<PreparedNode> node,bool bypassed){
 if(i<0||i>=slotCount)throw std::out_of_range("Insert slot");
 auto& s=slots[size_t(i)];
 if(!s.owned.empty()||s.request.load()||s.busy.load())throw std::logic_error("Initial install requires an empty unpublished slot");
 if(node)if(auto* provider=dynamic_cast<NativeResourceStateProvider*>(node->processor.get()))provider->sealNativeStateForPublication();
 auto* pointer=node.get();if(node)s.owned.push_back(std::move(node));
 s.active.store(pointer);s.bypassed.store(bypassed);s.wet.reset(sampleRate,bypassed?0.f:1.f);
}
bool InsertChain::isBusy(int i) const noexcept {if(i<0||i>=slotCount)return true;const auto& s=slots[size_t(i)];return s.busy.load(std::memory_order_acquire)||s.retired.load(std::memory_order_acquire)!=nullptr;}
bool InsertChain::publish(int i,std::unique_ptr<PreparedNode> node){if(isBusy(i))return false;auto& s=slots[size_t(i)];auto* ptr=node.get();
 if(node)if(auto* provider=dynamic_cast<NativeResourceStateProvider*>(node->processor.get()))provider->sealNativeStateForPublication();
 if(node)s.owned.push_back(std::move(node));s.busy.store(true,std::memory_order_release);s.incoming.store(ptr,std::memory_order_release);s.request.store(true,std::memory_order_release);return true;
}
bool InsertChain::publishStopped(int i,std::unique_ptr<PreparedNode> node){
 if(!publish(i,std::move(node)))return false;completePendingStopped();return true;
}
void InsertChain::completePendingStopped(){
 for(auto& s:slots){
  if(!s.request.load(std::memory_order_acquire))continue;
  auto* old=s.active.load(std::memory_order_relaxed);
  s.active.store(s.incoming.load(std::memory_order_acquire),std::memory_order_release);
  s.incoming.store(nullptr,std::memory_order_release);s.retired.store(old,std::memory_order_release);
  s.request.store(false,std::memory_order_release);s.busy.store(false,std::memory_order_release);
  s.transitioning=false;s.warmup=0;s.replace.reset(sampleRate,1);s.wet.reset(sampleRate,s.bypassed.load()?0.f:1.f);
 }
}
std::unique_ptr<InsertChain::PreparedNode> InsertChain::collectRetired(int i){auto& s=slots[size_t(i)];auto* ptr=s.retired.exchange(nullptr,std::memory_order_acq_rel);if(!ptr)return {};
 auto it=std::find_if(s.owned.begin(),s.owned.end(),[ptr](const auto& p){return p.get()==ptr;});if(it==s.owned.end())return {};auto node=std::move(*it);s.owned.erase(it);return node;
}
ModuleProcessor* InsertChain::activeProcessor(int i) const noexcept {auto* p=slots[size_t(i)].active.load(std::memory_order_acquire);return p?p->processor.get():nullptr;}
void InsertChain::bypass(int i,bool b) noexcept {if(i>=0&&i<slotCount)slots[size_t(i)].bypassed.store(b,std::memory_order_release);}
bool InsertChain::isBypassed(int i) const noexcept {return slots[size_t(i)].bypassed.load(std::memory_order_acquire);}
bool InsertChain::swapSlotsStopped(int a,int b){
 if(a<0||b<0||a>=slotCount||b>=slotCount||isBusy(a)||isBusy(b))return false;
 // Message thread, transport stopped, device callback excluded by the host.
 auto& x=slots[size_t(a)];auto& y=slots[size_t(b)];
 auto* xp=x.active.load();auto* yp=y.active.load();
 x.owned.swap(y.owned);x.active.store(yp);y.active.store(xp);
 const bool xb=x.bypassed.load(),yb=y.bypassed.load();x.bypassed.store(yb);y.bypassed.store(xb);
 x.wet.reset(sampleRate,yb?0.f:1.f);y.wet.reset(sampleRate,xb?0.f:1.f);
 return true;
}
bool InsertChain::run(PreparedNode* node,float* l,float* r,int n,double blockBpm) noexcept {if(!node||!node->processor->processingReady())return false;
 if(node->deltaReferenceConsumer)node->deltaReferenceConsumer->setReferenceBlock({referenceTap[0].data(),referenceTap[1].data(),n,sampleRate,referenceTimeline,true});
 float* channels[]{l,r};view.setDataToReferTo(channels,2,n);midi.clear();node->processor->setBlockTempo(blockBpm);node->processor->processBlock(view,midi);
 // A plug-in can change latency/layout while processing this block. Never emit
 // its now-uncompensated output; use the slot's already aligned dry path.
 if(!node->processor->processingReady())return false;node->compensation.process(l,r,n);return true;
}
void InsertChain::process(float* left,float* right,int samples,double blockBpm) noexcept {
 for(int offset=0;offset<samples;offset+=maximumBlock){const int n=std::min(maximumBlock,samples-offset);float* l=left+offset;float* r=right+offset;
  std::copy_n(l,n,referenceTap[0].data());std::copy_n(r,n,referenceTap[1].data());
  for(auto& s:slots){auto* old=s.active.load(std::memory_order_relaxed);
    if(!s.transitioning&&s.request.load(std::memory_order_acquire)) {s.transitioning=true;s.warmup=latencyBudget;s.replace.reset(sampleRate,0);s.replace.length=std::max(1,int(sampleRate*.02));}
    auto* next=s.transitioning?s.incoming.load(std::memory_order_acquire):nullptr;
    if(!old&&!s.transitioning){
      // Empty slots still own an immutable delay and bypass-ramp timeline.
      // Delay in place to avoid the dry/oldWet scratch copies. Retain the exact
      // legacy arithmetic (including signed-zero/nonfinite behavior) rather
      // than silently turning this path into an unqualified numerical bypass.
      s.dryDelay.process(l,r,n);
      s.wet.set(s.bypassed.load(std::memory_order_acquire)?0.f:1.f);
      for(int i=0;i<n;++i){const float wet=s.wet.next();
        l[i]=l[i]+wet*(l[i]+0.f*(newWet[0][size_t(i)]-l[i])-l[i]);
        r[i]=r[i]+wet*(r[i]+0.f*(newWet[1][size_t(i)]-r[i])-r[i]);}
      s.referenceDelay.process(referenceTap[0].data(),referenceTap[1].data(),n);
      continue;
    }
    std::copy_n(l,n,dry[0].data());std::copy_n(r,n,dry[1].data());s.dryDelay.process(dry[0].data(),dry[1].data(),n);
    if(old){std::copy_n(l,n,oldWet[0].data());std::copy_n(r,n,oldWet[1].data());if(!run(old,oldWet[0].data(),oldWet[1].data(),n,blockBpm)){std::copy_n(dry[0].data(),n,oldWet[0].data());std::copy_n(dry[1].data(),n,oldWet[1].data());}}
    else {std::copy_n(dry[0].data(),n,oldWet[0].data());std::copy_n(dry[1].data(),n,oldWet[1].data());}
    if(s.transitioning){if(next){std::copy_n(l,n,newWet[0].data());std::copy_n(r,n,newWet[1].data());if(!run(next,newWet[0].data(),newWet[1].data(),n,blockBpm)){std::copy_n(dry[0].data(),n,newWet[0].data());std::copy_n(dry[1].data(),n,newWet[1].data());}}
        else {std::copy_n(dry[0].data(),n,newWet[0].data());std::copy_n(dry[1].data(),n,newWet[1].data());}}
    s.wet.set(s.bypassed.load(std::memory_order_acquire)?0.f:1.f);
    for(int i=0;i<n;++i){if(s.transitioning&&s.warmup>0){if(--s.warmup==0)s.replace.set(1);}
        const float blend=s.transitioning?s.replace.next():0,wet=s.wet.next();
        l[i]=dry[0][size_t(i)]+wet*(oldWet[0][size_t(i)]+blend*(newWet[0][size_t(i)]-oldWet[0][size_t(i)])-dry[0][size_t(i)]);
        r[i]=dry[1][size_t(i)]+wet*(oldWet[1][size_t(i)]+blend*(newWet[1][size_t(i)]-oldWet[1][size_t(i)])-dry[1][size_t(i)]);}
    if(s.transitioning&&s.warmup==0&&s.replace.remaining==0&&s.replace.value==1){
        s.active.store(next,std::memory_order_release);s.incoming.store(nullptr,std::memory_order_release);s.retired.store(old,std::memory_order_release);
        s.request.store(false,std::memory_order_release);s.transitioning=false;s.busy.store(false,std::memory_order_release);}
    // Independent chain-start reference advances by the SAME immutable slot
    // budget as wet/bypass paths, never by substituting the current slot input.
    s.referenceDelay.process(referenceTap[0].data(),referenceTap[1].data(),n);
  }
  referenceTimeline+=std::uint64_t(n);
 }
}
}



