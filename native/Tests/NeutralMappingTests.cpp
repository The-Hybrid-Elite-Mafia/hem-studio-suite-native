#include "../Source/UI/NeutralMapping.h"
#include <iostream>
#include <stdexcept>
#include <utility>
namespace { void require(bool condition,const char* message){if(!condition)throw std::runtime_error(message);} }
int main(){try{for(const auto& range:{std::pair{-60.,6.},std::pair{-12.,3.},std::pair{-6.,12.},std::pair{-1.,1.}}){auto [lo,hi]=range;require(hem::ui::bipolarToPosition(0,lo,hi)==.5,"Bipolar zero is not centered");double last=-1;for(int i=0;i<=10000;++i){double p=double(i)/10000;double q=hem::ui::bipolarToPosition(hem::ui::positionToBipolar(p,lo,hi),lo,hi);require(std::abs(p-q)<1e-12&&q>last,"Mapping is not reversible and monotonic");last=q;}}require(!hem::ui::isBipolar(0,100)&&!hem::ui::isBipolar(20,20000)&&!hem::ui::isBipolar(-60,0),"Unipolar range classified as bipolar");std::cout<<"PASS 40004 reversible mapping checks; zero at noon; unipolar domains unchanged\n";return 0;}catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}}
