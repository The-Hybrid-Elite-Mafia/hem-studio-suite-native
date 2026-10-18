#pragma once
#include <JuceHeader.h>
#include <array>
#include <atomic>
#include <memory>
#include <limits>
namespace hem {
// One persistent batch at a time. A timed-out batch outlives run(), with fault
// latched until control-thread quiescence. Configure/destruct off callback.
class TrackRenderScheduler final {
public:
 static constexpr int maximumHelpers=12,maximumJobs=13;
 using Render=void(*)(void*,int) noexcept;
 TrackRenderScheduler();
 ~TrackRenderScheduler();
 bool configure(int helpers) noexcept;
 int helperCount()const noexcept{return count;}
 bool run(void* context,Render,const int* jobs,int number,juce::int64 deadline=std::numeric_limits<juce::int64>::max()) noexcept;
 bool faulted()const noexcept{return fault.load(std::memory_order_acquire);}
 bool outstanding()const noexcept{return remaining.load(std::memory_order_acquire)!=0;}
 void quiesce()noexcept; // Control/device lifecycle only, never the audio callback.
 void latchFault()noexcept{fault.store(true,std::memory_order_release);}
 void clearFault()noexcept{jassert(!outstanding());fault.store(false,std::memory_order_release);}
private:
 friend struct TrackRenderSchedulerTestAccess;
 bool(*startWorker)(juce::Thread&)=nullptr; // Test-only failure injection; default uses native startup.
 void(*beforeHelperDrain)()noexcept=nullptr; // Deterministic helper-stall harness; never called by callback.
 struct Worker;
 std::array<std::unique_ptr<Worker>,maximumHelpers> workers;
 std::array<int,maximumJobs> indices{};
 std::atomic<int> next{0},remaining{0};
 std::atomic<bool> fault{false};
 void* context=nullptr;Render render=nullptr;int jobs=0,count=0,rounding=0;
 unsigned simdControl=0;
 void drain() noexcept;
 void stop() noexcept;
};
}
