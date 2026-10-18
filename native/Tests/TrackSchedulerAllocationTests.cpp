#include "../Source/TrackRenderScheduler.h"
#include <cstdlib>
#include <new>
#include <iostream>
#include <malloc.h>
namespace allocationProbe {
std::atomic<bool> enabled{false};std::atomic<unsigned> allocations{0},frees{0};
std::array<std::atomic<void*>,13> threads{};
bool tracked()noexcept{if(!enabled.load(std::memory_order_relaxed))return false;auto id=juce::Thread::getCurrentThreadId();for(auto& t:threads)if(t.load(std::memory_order_relaxed)==id)return true;return false;}
void allocated()noexcept{if(tracked())++allocations;}void freed(void* p)noexcept{if(p&&tracked())++frees;}
}
void* operator new(size_t n){allocationProbe::allocated();if(auto* p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void* operator new[](size_t n){return ::operator new(n);}
void operator delete(void* p)noexcept{allocationProbe::freed(p);std::free(p);}void operator delete[](void* p)noexcept{::operator delete(p);}
void operator delete(void* p,size_t)noexcept{::operator delete(p);}void operator delete[](void* p,size_t)noexcept{::operator delete(p);}
void* operator new(size_t n,std::align_val_t a){allocationProbe::allocated();if(auto* p=_aligned_malloc(n?n:1,size_t(a)))return p;throw std::bad_alloc();}
void* operator new[](size_t n,std::align_val_t a){return ::operator new(n,a);}
void operator delete(void* p,std::align_val_t)noexcept{allocationProbe::freed(p);_aligned_free(p);}void operator delete[](void* p,std::align_val_t a)noexcept{::operator delete(p,a);}
void operator delete(void* p,size_t,std::align_val_t a)noexcept{::operator delete(p,a);}void operator delete[](void* p,size_t,std::align_val_t a)noexcept{::operator delete(p,a);}
namespace hem {struct TrackRenderSchedulerTestAccess {
 inline static int index=0;
 static bool start(juce::Thread& worker){const bool ok=worker.startThread(juce::Thread::Priority::high);if(ok)allocationProbe::threads[size_t(++index)]=worker.getThreadId();return ok;}
 static void prepare(TrackRenderScheduler& s){index=0;s.startWorker=start;}
};}
void job(void* context,int i)noexcept{auto* values=static_cast<std::atomic<unsigned>*>(context);values[i].fetch_add(1,std::memory_order_relaxed);}
int main(){allocationProbe::threads[0]=juce::Thread::getCurrentThreadId();hem::TrackRenderScheduler scheduler;hem::TrackRenderSchedulerTestAccess::prepare(scheduler);
 if(!scheduler.configure(12)){std::cerr<<"FAIL worker startup\n";return 1;}std::array<int,13> ids{};std::array<std::atomic<unsigned>,13> values{};for(int i=0;i<13;++i)ids[size_t(i)]=i;
 for(int i=0;i<100;++i)scheduler.run(values.data(),job,ids.data(),13);
 allocationProbe::enabled=true;for(int i=0;i<1000;++i)scheduler.run(values.data(),job,ids.data(),13);allocationProbe::enabled=false;
 scheduler.quiesce();const bool fault=scheduler.faulted();
 const auto a=allocationProbe::allocations.load(),f=allocationProbe::frees.load();std::cout<<"Scheduler callback + twelve registered helper threads: new="<<a<<", delete="<<f<<" / 1000 warmed batches (not whole GUI/global allocator claim)\n";
 if(a||f||fault)return 1;for(auto& value:values)if(value!=1100)return 1;std::cout<<"PASS allocation/deallocation probe and all jobs complete\n";return 0;
}
