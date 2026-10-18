#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
// White-box revision injection is confined to this standalone test executable.
// It deterministically models a writer paused during publication; no app hooks.
#define private public
#include "../Source/Systems/AudioMeters.h"
#include "Fixtures/AudioMeters007.h"
#undef private
#include "FoundationAllocationProbe.h"
#include <array>
#include <bit>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <thread>
namespace {
std::uint64_t checks=0;
void check(bool ok,const char* message){++checks;if(!ok)throw std::runtime_error(message);}
template<class A,class B>void same(const A& a,const B& b){
 check(std::bit_cast<std::uint32_t>(a.peakDb)==std::bit_cast<std::uint32_t>(b.peakDb),"007 peak math changed");
 check(std::bit_cast<std::uint32_t>(a.rmsDb)==std::bit_cast<std::uint32_t>(b.rmsDb),"007 RMS math changed");
 check(std::bit_cast<std::uint32_t>(a.heldPeakDb)==std::bit_cast<std::uint32_t>(b.heldPeakDb),"007 hold math changed");check(a.clipped==b.clipped,"007 clip math changed");
}
void parity(){
 std::array<float,257> l{},rr{};hem::AudioMeters meter;hem007::AudioMeters old;
 for(double rate:{8000.,44100.,48000.,96000.,192000.,384000.}){
  auto before=meter.snapshot();check(meter.prepare(rate)==old.prepare(rate),"prepare validity changed");auto start=meter.snapshot();check(start.ready&&start.generation==before.generation+1&&start.processedSamples==0,"prepare generation/count invalid");
  std::uint64_t samples=0;
  for(int block=0;block<257;++block){const int n=std::array<int,6>{0,1,7,31,128,257}[size_t(block%6)];
   for(int i=0;i<n;++i){l[size_t(i)]=.8f*std::sin(float(block*257+i)*.071f);rr[size_t(i)]=.31f*std::cos(float(block*257+i)*.031f);}
   if(block==33&&n){l[0]=std::numeric_limits<float>::infinity();rr[0]=std::numeric_limits<float>::quiet_NaN();}
   if(block==100){meter.resetClip();old.resetClip();}
   allocationProbe::testThread=true;allocationProbe::enabled=true;meter.push(l.data(),rr.data(),n);allocationProbe::enabled=false;allocationProbe::testThread=false;old.push(l.data(),rr.data(),n);samples+=std::uint64_t(n);
   const auto a=meter.snapshot();const auto b=old.snapshot();same(a.left,b.left);same(a.right,b.right);check(a.ready==b.ready&&a.nonFiniteSamples==b.nonFiniteSamples,"007 readiness/nonfinite changed");check(a.generation==start.generation&&a.processedSamples==samples&&(a.publication&1u)==0,"audio metadata not coherent");
  }
  const auto beforeZero=meter.snapshot();meter.resetClip();meter.push(nullptr,nullptr,0);auto afterZero=meter.snapshot();check(afterZero.processedSamples==beforeZero.processedSamples&&afterZero.publication>beforeZero.publication,"zero-length push fabricated audio or failed to publish reset");
  meter.push(nullptr,nullptr,-1);check(meter.snapshot().publication==afterZero.publication,"negative frame push published");
  meter.prepare(0);const auto invalid=meter.snapshot();check(!invalid.ready&&invalid.processedSamples==0&&invalid.generation==start.generation+1,"invalid prepare did not invalidate metadata");meter.push(l.data(),rr.data(),257);check(meter.snapshot().publication==invalid.publication,"unprepared push published");
 }
 std::cout<<"PASS 007 bit-exact stereo peak/RMS/hold/clip detector math, six rates and irregular/zero blocks; generation/count/reset semantics\n";
}
void contention(){
 hem::AudioMeters meter;hem007::AudioMeters old;meter.prepare(48000);old.prepare(48000);float value=.5f;meter.push(&value,&value,1);old.push(&value,&value,1);
 meter.revision.fetch_add(1);old.revision.fetch_add(1);
 const auto unavailable=meter.snapshot();check(!unavailable.ready&&unavailable.generation==0&&unavailable.processedSamples==0&&unavailable.publication==0,"bounded snapshot exposed partial live publication");
 check(old.snapshot().ready,"old snapshot negative control did not reproduce mixed/live fallback");
 meter.revision.fetch_add(1);old.revision.fetch_add(1);check(meter.snapshot().ready,"bounded snapshot did not recover");
 std::cout<<"PASS forced writer-overlap returns unavailable after bounded retries; frozen007 negative control detects old live fallback\n";
}
void concurrent(){
 hem::AudioMeters meter;std::atomic<bool> done{false},start{false};std::atomic<std::uint64_t> observed{0};
 std::thread writer([&]{while(!start.load())std::this_thread::yield();allocationProbe::testThread=true;allocationProbe::enabled=true;
  std::array<float,17> l{},r{};
  for(int i=0;i<20000;++i){meter.prepare(48000);const auto generation=meter.preparationGeneration;const float amplitude=(generation&1u)?.25f:.5f;l.fill(amplitude);r.fill(amplitude*.5f);for(int j=0;j<3;++j)meter.push(l.data(),r.data(),17);if((i%31)==0)std::this_thread::yield();}
  allocationProbe::enabled=false;allocationProbe::testThread=false;done=true;
 });
 start=true;std::uint64_t reads=0;
 auto verify=[&]{const auto s=meter.snapshot();if(!s.ready)return;++reads;check(s.generation>0&&(s.publication&1u)==0&&s.processedSamples%17==0&&s.processedSamples<=51,"torn generation/sample metadata");if(s.processedSamples){const double amplitude=(s.generation&1u)?.25:.5;check(s.left.peakDb==float(20*std::log10(amplitude))&&s.right.peakDb==float(20*std::log10(amplitude*.5)),"payload crossed preparation generation");}else check(s.left.peakDb==hem::AudioMeters::silenceFloorDb&&s.right.peakDb==hem::AudioMeters::silenceFloorDb,"reset metadata paired with old payload");observed.store(s.generation);};
 try{while(!done.load())verify();writer.join();verify();}catch(...){writer.join();throw;}
 check(reads>0,"no coherent concurrent snapshots observed");std::cout<<"PASS 20000 concurrent preparation generations, "<<reads<<" coherent snapshots, no cross-generation payload\n";
}
}
int main(){try{
 allocationProbe::testThread=true;allocationProbe::enabled=true;auto* a=::operator new(31);::operator delete(a);auto* b=::operator new(127,std::align_val_t(64));::operator delete(b,std::align_val_t(64));allocationProbe::enabled=false;allocationProbe::testThread=false;check(allocationProbe::count==2,"allocation probe positive controls failed");allocationProbe::count=0;
 parity();contention();concurrent();check(allocationProbe::count==0,"new meter publication allocated");std::cout<<"PASS "<<checks<<" checks and zero measured writer/callback allocations; publication metadata only\n";return 0;
}catch(const std::exception& e){allocationProbe::enabled=false;allocationProbe::testThread=false;std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
