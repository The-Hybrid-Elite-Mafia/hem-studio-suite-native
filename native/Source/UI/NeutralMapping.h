#pragma once
#include <algorithm>
#include <cmath>
namespace hem::ui {
inline bool isBipolar(double lo,double hi) noexcept {return std::isfinite(lo)&&std::isfinite(hi)&&lo<0&&hi>0;}
inline double bipolarToPosition(double v,double lo,double hi) noexcept {
 if(!isBipolar(lo,hi))return 0;
 v=std::isfinite(v)?std::clamp(v,lo,hi):0;
 return v<0?.5*(v-lo)/-lo:.5+.5*v/hi;
}
inline double positionToBipolar(double p,double lo,double hi) noexcept {
 if(!isBipolar(lo,hi))return 0;
 p=std::isfinite(p)?std::clamp(p,0.,1.):.5;
 return p<.5?lo*(1-2*p):hi*(2*p-1);
}
}
