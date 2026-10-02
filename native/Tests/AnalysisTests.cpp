#include "../Source/Systems/AnalysisWorker.h"
#include <iostream>
#include <stdexcept>
#include <chrono>
#include <cstdlib>
#include <new>
static thread_local bool checkingPush=false;
static std::atomic<int> callbackAllocations{0};
void* operator new(std::size_t n){if(checkingPush)++callbackAllocations;if(auto* p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void* operator new[](std::size_t n){return ::operator new(n);}
void operator delete(void* p) noexcept{std::free(p);}
void operator delete[](void* p) noexcept{std::free(p);}
void operator delete(void* p,std::size_t) noexcept{std::free(p);}
void operator delete[](void* p,std::size_t) noexcept{std::free(p);}
static void require(bool b,const char* t){if(!b)throw std::runtime_error(t);}
int main(){try{
 auto worker=std::make_unique<hem::AnalysisWorker>();worker->prepare(96000);
 const int size=hem::AnalysisWorker::fftSize*2;std::vector<float> l(size),r(size);
 for(int i=0;i<size;++i){l[size_t(i)]=.5f*float(std::sin(juce::MathConstants<double>::twoPi*86*i/hem::AnalysisWorker::fftSize));r[size_t(i)]=.25f*float(std::sin(juce::MathConstants<double>::twoPi*172*i/hem::AnalysisWorker::fftSize));}
 checkingPush=true;worker->push(l.data(),r.data(),size);checkingPush=false;
 auto frame=std::make_unique<hem::AnalysisWorker::Snapshot>();
 auto wait=[&](double rate,std::uint64_t end){for(int i=0;i<500;++i){if(worker->copyLatest(*frame)&&frame->sampleRate==rate&&frame->frameEnd>=end)return true;juce::Thread::sleep(5);}return false;};
 require(wait(96000,size),"Analysis worker did not publish");
 require(std::abs(frame->leftDb[86]+6.0206f)<.02f,"Left FFT amplitude incorrect");require(std::abs(frame->rightDb[172]+12.0412f)<.02f,"Right FFT amplitude incorrect");
 require(frame->validColumns==size/64,"Peak bucket count incorrect");
 float peak=0;for(int i=0;i<frame->validColumns;++i)peak=std::max(peak,frame->waveform[size_t(i)].highL);require(std::abs(peak-.5f)<.002,"Waveform extrema inaccurate");
 for(int i=0;i<100;++i)worker->push(l.data(),r.data(),size);
 juce::Thread::sleep(100);worker->prepare(48000);
 // Wait until the bounded backlog drains; no audio thread ever waits.
 juce::Thread::sleep(100);worker->push(l.data(),r.data(),size);require(wait(48000,size),"Device-rate epoch did not reset analysis");require(frame->droppedFrames>0,"Overflow did not report dropped display data");
 require(callbackAllocations.load()==0,"Audio analysis push allocated memory");
 std::cout<<"PASS: allocation-free audio push; real JUCE FFT amplitudes, stereo bins, waveform extrema, bounded overflow and rate epochs\n";
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}return 0;}
