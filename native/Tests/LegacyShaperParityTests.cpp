#include "../Source/DSP/FinishingCore.h"
#include "Fixtures/FinishingCore005a.h"
#include <bit>
#include <cstdint>
#include <iostream>
#include <stdexcept>
int main(){try{std::uint64_t compared=0;for(float drive:{1.f,2.f,8.f})for(float bias:{-.35f,0.f,.4f})for(float amount:{0.f,.25f,1.f}){
 hem::OversampledSaturator current;finishing005a::OversampledSaturator baseline;current.prepare();baseline.prepare();std::uint32_t rng=92317;
 for(int n=0;n<8192;++n){if(n==4096){current.resetHistory();baseline.resetHistory();}for(int ch=0;ch<2;++ch){rng=rng*1664525u+1013904223u;const float input=float(std::bit_cast<std::int32_t>(rng))/1073741824.f;
  const float actual=current.tick(input,ch,drive,bias,amount),expected=baseline.tick(input,ch,drive,bias,amount);
  if(std::bit_cast<std::uint32_t>(actual)!=std::bit_cast<std::uint32_t>(expected))throw std::runtime_error("Default residual=false changed established shaper samples");++compared;
 }} }
 std::cout<<"PASS "<<compared<<" bit-exact old/new legacy shaper samples across drive/bias/mix and history reset; residual option defaults off\n";return 0;
}catch(const std::exception&e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
