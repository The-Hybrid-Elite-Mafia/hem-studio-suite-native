#pragma once
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_dsp/juce_dsp.h>
#include <juce_data_structures/juce_data_structures.h>
#include <array>
#include <atomic>
#include <memory>
#include <vector>
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <functional>
#include <climits>
namespace hem {
inline constexpr double defaultSampleRate = 48000.0; // Primary native target; prepare uses actual device/session rate.
static_assert(std::atomic<void*>::is_always_lock_free);
static_assert(std::atomic<float>::is_always_lock_free);
static_assert(std::atomic<unsigned>::is_always_lock_free);
// Single control/worker producer, one audio consumer. Destruction only on producer.
template<class T> class Publication {
 std::atomic<T*> pending{nullptr}; std::array<T*,64> retired{};
 std::atomic<unsigned> read{0},write{0}; T* current=nullptr;
public:
 ~Publication(){ delete pending.load(); delete current; collect(); }
 void publish(std::unique_ptr<T> value){ collect(); delete pending.exchange(value.release(),std::memory_order_acq_rel); }
 void collect(){auto r=read.load(std::memory_order_relaxed);auto w=write.load(std::memory_order_acquire);while(r!=w){delete retired[r%64];++r;}read.store(r,std::memory_order_release);}
 T* acquire() noexcept {auto w=write.load(std::memory_order_relaxed);if(w-read.load(std::memory_order_acquire)<64){if(auto* p=pending.exchange(nullptr,std::memory_order_acq_rel)){if(current){retired[w%64]=current;write.store(w+1,std::memory_order_release);}current=p;}}return current;}
 T* peek() const noexcept{return current;}
 // For clip crossfades: caller retains old pointer until retire is called.
 T* take() noexcept {return pending.exchange(nullptr,std::memory_order_acq_rel);}
 bool canRetire() const noexcept{return write.load(std::memory_order_relaxed)-read.load(std::memory_order_acquire)<64;}
 void retire(T* p) noexcept{if(!p)return;auto w=write.load(std::memory_order_relaxed);jassert(canRetire());retired[w%64]=p;write.store(w+1,std::memory_order_release);}
};
struct Clip {juce::AudioBuffer<float> audio; double sampleRate=defaultSampleRate;};
struct ClipReport {std::vector<double> mean;float peak=0,rms=0,crestDb=0,trimDb=0;bool suspectedClipping=false;};
class OfflineWorkerUtility {
public:
 static ClipReport condition(Clip&,float headroomDb=-3,float attenuationDb=-4);
 static std::unique_ptr<Clip> loadWav(const juce::File&,double targetRate);
 class Job final: public juce::ThreadPoolJob {
  std::unique_ptr<Clip> clip; Publication<Clip>& destination;
 public: std::atomic<int> result{0}; // 0 pending, 1 published, 2 cancelled, 3 failed.
  Job(std::unique_ptr<Clip> c,Publication<Clip>& d):ThreadPoolJob("Clip conditioning"),clip(std::move(c)),destination(d){}
  JobStatus runJob() override {try{condition(*clip);if(shouldExit())result.store(2);else{destination.publish(std::move(clip));result.store(1);}}catch(...){result.store(3);}return jobHasFinished;}
 };
};
class ClipPlayback {
 Publication<Clip>& publication; Clip* active=nullptr;Clip* previous=nullptr;int fade=0,fadeLength=480; int64_t position=0;
public:
 explicit ClipPlayback(Publication<Clip>& p):publication(p){}
 ~ClipPlayback(){delete active;delete previous;} // Stop/join callback first.
 void prepare(double rate){fadeLength=std::max(1,int(std::lround(.01*rate)));}
 void seek(int64_t p) noexcept{position=std::max<int64_t>(0,p);} // Audio thread command only.
 void render(juce::AudioBuffer<float>& out,int n) noexcept;
};
class Delay {
 juce::AudioBuffer<float> ring;int pos=0,delay=0;
public:
 void prepare(int channels,int samples){delay=samples;ring.setSize(channels,std::max(1,samples));ring.clear();pos=0;}
 void process(juce::AudioBuffer<float>& b,int n) noexcept{if(!delay)return;for(int i=0;i<n;++i){for(int c=0;c<b.getNumChannels();++c){auto* r=ring.getWritePointer(c);auto* x=b.getWritePointer(c);float t=r[pos];r[pos]=x[i];x[i]=t;}if(++pos==delay)pos=0;}}
};
struct Processor {
 virtual ~Processor()=default;
 virtual void prepare(double,int,int)=0;
 virtual void process(juce::AudioBuffer<float>&,int) noexcept=0;
 virtual int latency() const noexcept=0;
 virtual juce::ValueTree state() const=0; // Control thread, before publication.
};
struct CurveTable {std::array<float,1024> gain{};};
class VectorizedDynamics final:public Processor {
 Publication<CurveTable> tables;juce::ValueTree cached{"Dynamics"};double sr=defaultSampleRate;float env=0,attack=0,release=0;bool rms=false;
public:
 static std::unique_ptr<CurveTable> makeCurve(const std::vector<std::pair<float,float>>&);
 void updateCurve(const std::vector<std::pair<float,float>>& points){tables.publish(makeCurve(points));}
 void configure(const std::vector<std::pair<float,float>>&,float attackMs,float releaseMs,bool rmsDetector);
 void prepare(double,int,int) override;
 void process(juce::AudioBuffer<float>&,int) noexcept override;
 int latency() const noexcept override{return 0;}
 juce::ValueTree state() const override{return cached.createCopy();}
 void collect(){tables.collect();}
};
class ModularInsertSlot {
public:
 struct Pair {
  std::array<std::unique_ptr<Processor>,2> processor;
  juce::ValueTree State_A{"A"},State_B{"B"};std::array<Delay,2> align;
  static std::unique_ptr<Pair> fromStates(const juce::ValueTree& a,const juce::ValueTree& b,
   const std::function<std::unique_ptr<Processor>(const juce::ValueTree&)>& factory){
   auto p=std::make_unique<Pair>();p->processor[0]=factory(a);p->processor[1]=factory(b);
   if(!p->processor[0]||!p->processor[1])throw std::invalid_argument("State factory failed");
   p->State_A=a.createCopy();p->State_B=b.createCopy();return p;
  }
  void prepare(double rate,int block,int channels,int budget);
 };
private:
 Publication<Pair> pairs;juce::AudioBuffer<float> dryDelayBuffer,wetA,wetB,oldWet;
 Pair* active=nullptr;Pair* previous=nullptr;int replacement=0,warm=0;
 Delay dryDelay;double sampleRate=defaultSampleRate;int budget=0,block=0,channels=0;float ab=0,bypassMix=0,deltaMix=0,step=.001f;
public:
 ~ModularInsertSlot(){delete active;delete previous;}
 std::atomic<bool> selectB{false},bypass{false},delta{false};
 void prepare(double,int,int,int latencyBudget=512);
 // Fully prepare on the single control thread; publishes at callback boundary.
 void install(std::unique_ptr<Pair>,double rate);
 void remove(){install(std::make_unique<Pair>(),sampleRate);}
 void collect(){pairs.collect();}
 void process(juce::AudioBuffer<float>&,int) noexcept;
 int latency()const noexcept{return budget;}
};
class MasterTruePeakLimiter {
 std::unique_ptr<juce::dsp::Oversampling<float>> oversampler;juce::AudioBuffer<float> ring;
 struct Peak {uint64_t index;float value;};std::vector<Peak> deque;uint64_t counter=0,head=0,tail=0;
 int delay=0,pos=0,maxBlock=0;
 std::vector<float> attackShape; // Prepared cubic table; never resized in process().
 // Double release state avoids float rounding stalling below unity.
 double gain=1,releaseFast=0,releaseSlow=0;float ceiling=.891250938f;
public:
 void prepare(double,int,int=2);
 void process(juce::AudioBuffer<float>&,int) noexcept;
 int latency() const noexcept{return oversampler?delay/4+int(std::lround(oversampler->getLatencyInSamples())):0;}
};
class StemIngestManager {
 struct Biquad {double b0=1,b1=0,b2=0,a1=0,a2=0;float z1=0,z2=0;float tick(float x)noexcept{float y=float(b0*x+z1);z1=float(b1*x-a1*y+z2);z2=float(b2*x-a2*y);return y;}};
 std::array<std::array<Biquad,2>,13> hp;std::array<juce::AudioBuffer<float>,13> scratch;
 std::array<juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>,13> trim;std::array<float,13> mix{},hpMix{};int maxBlock=0;float step=0;MasterTruePeakLimiter limiter;
public:
 std::array<std::array<ModularInsertSlot,8>,13> slots;
 std::array<std::atomic<float>,13> trimDb,gainDb;
 std::array<std::atomic<bool>,13> highPass,mute,solo;
 std::array<int,13> hardwareChannel; // Configure only with callback stopped; -1 means disabled.
 static constexpr const char* inputTrimParameterId = "inputTrimDb";
 // Control-thread setter: sanitizes, clamps and rounds to the 0.1 dB contract.
 // Existing direct trimDb writes remain supported and are normalized on read.
 bool setInputTrimDb(size_t strip, float db) noexcept;
 StemIngestManager();
 void prepare(double,int);
 void processBlock(const juce::AudioBuffer<float>& hardwareInput,juce::AudioBuffer<float>& stereoOutput,int n) noexcept;
 int latency()const noexcept{return slots[0][0].latency()*8+limiter.latency();}
};
}
