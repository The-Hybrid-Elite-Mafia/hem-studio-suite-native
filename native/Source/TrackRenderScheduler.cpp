#include "TrackRenderScheduler.h"
#include <cfenv>
#if defined(_M_X64) || defined(_M_IX86) || defined(__SSE__)
#include <xmmintrin.h>
#endif
namespace hem {
struct TrackRenderScheduler::Worker final:juce::Thread {
 TrackRenderScheduler& owner;juce::WaitableEvent wake;
 explicit Worker(TrackRenderScheduler& scheduler):Thread("HEM track render"),owner(scheduler){}
 void run()override{
  while(true){wake.wait(-1);if(threadShouldExit())return;
   (void)owner.remaining.load(std::memory_order_acquire);
   // Copy the callback's floating-point mode, including denormal handling.
   std::fesetround(owner.rounding);
#if defined(_M_X64) || defined(_M_IX86) || defined(__SSE__)
   _mm_setcsr(owner.simdControl);
#endif
   if(owner.beforeHelperDrain)owner.beforeHelperDrain();
   owner.drain();
   // Final access to this batch. The callback cannot reuse it before all acks.
   owner.remaining.fetch_sub(1,std::memory_order_release);
  }
 }
};
TrackRenderScheduler::TrackRenderScheduler()=default;
TrackRenderScheduler::~TrackRenderScheduler(){stop();}
void TrackRenderScheduler::stop()noexcept{
 quiesce();
 for(auto& worker:workers)if(worker){worker->signalThreadShouldExit();worker->wake.signal();}
 for(auto& worker:workers)if(worker){worker->waitForThreadToExit(-1);worker.reset();}
 count=0;clearFault();
}
void TrackRenderScheduler::quiesce()noexcept{while(outstanding())juce::Thread::sleep(1);}
bool TrackRenderScheduler::configure(int helpers)noexcept{
 stop();const int available=std::max(0,juce::SystemStats::getNumCpus()-1);
 helpers=juce::jlimit(0,std::min(maximumHelpers,available),helpers);
 try{for(int i=0;i<helpers;++i){workers[size_t(i)]=std::make_unique<Worker>(*this);
   const bool started=startWorker?startWorker(*workers[size_t(i)]):workers[size_t(i)]->startThread(juce::Thread::Priority::high);
   if(!started){stop();return false;}
  }count=helpers;return true;
 }catch(...){stop();return false;}
}
void TrackRenderScheduler::drain()noexcept{
 for(;;){const int job=next.fetch_add(1,std::memory_order_relaxed);if(job>=jobs)return;render(context,indices[size_t(job)]);}
}
bool TrackRenderScheduler::run(void* target,Render function,const int* list,int number,juce::int64 deadline)noexcept{
 if(faulted())return false;
 jassert(number>=0&&number<=maximumJobs&&remaining.load()==0);
 if(number<=0)return true;
 if(!count||number==1){for(int i=0;i<number;++i)function(target,list[i]);return true;}
 context=target;render=function;jobs=number;std::copy_n(list,number,indices.begin());
 rounding=std::fegetround();
#if defined(_M_X64) || defined(_M_IX86) || defined(__SSE__)
 simdControl=_mm_getcsr();
#endif
 next.store(0,std::memory_order_relaxed);remaining.store(count,std::memory_order_release);
 for(int i=0;i<count;++i)workers[size_t(i)]->wake.signal();
 // The callback checks its budget before claiming another finite DSP job.
 while(juce::Time::getHighResolutionTicks()<deadline){const int job=next.fetch_add(1,std::memory_order_relaxed);if(job>=jobs)break;render(context,indices[size_t(job)]);}
 // Bounded polling only: no OS wait, yield, mutex, allocation or logging here.
 // Late workers retain the persistent batch. Control code must quiesce before
 // reusing any input, graph or descriptor, even after the callback has returned.
 constexpr unsigned maximumPolls=1000000;
 for(unsigned poll=0;poll<maximumPolls;++poll){if(!outstanding())return true;if(juce::Time::getHighResolutionTicks()>=deadline)break;
#if defined(_M_X64) || defined(_M_IX86) || defined(__SSE__)
  _mm_pause();
#endif
 }
 latchFault();return false;
}
}
