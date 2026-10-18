#pragma once
#include <algorithm>
#include <cmath>
#include <limits>

namespace hem::ui::foundationMapping {
inline constexpr double pi=3.1415926535897932384626433832795;
inline double riderFloor(double sensitivity) noexcept {return -20-.6*std::clamp(sensitivity,0.,100.);}
inline double riderSensitivity(double floor) noexcept {return std::clamp((-20-floor)/.6,0.,100.);}
inline double plosiveThreshold(double sensitivity) noexcept {return -6-.54*std::clamp(sensitivity,0.,100.);}
inline double plosiveSensitivity(double threshold) noexcept {return std::clamp((-6-threshold)/.54,0.,100.);}
// Exact isolated RBJ notch section half-power bandwidth, not the composite bank
// or depth/mix response. The filter's underlying Q and parameter state are fixed.
inline double notchWidth(double fundamental,double rate,double q) noexcept {
    if(!std::isfinite(fundamental)||!std::isfinite(rate)||!std::isfinite(q)||rate<=0||fundamental<=0||fundamental>=rate*.5||q<=0)return std::numeric_limits<double>::quiet_NaN();
    return rate/pi*std::atan(std::sin(2*pi*fundamental/rate)/(2*q));
}
inline double notchQ(double fundamental,double rate,double width) noexcept {
    if(!std::isfinite(width)||!std::isfinite(fundamental)||!std::isfinite(rate)||rate<=0||fundamental<=0||fundamental>=rate*.5)return std::numeric_limits<double>::quiet_NaN();
    width=std::clamp(width,notchWidth(fundamental,rate,60),notchWidth(fundamental,rate,10));
    return std::clamp(std::sin(2*pi*fundamental/rate)/(2*std::tan(pi*width/rate)),10.,60.);
}
enum class RiderSpeed { custom,slow,fast };
inline RiderSpeed riderSpeed(double rise,double fall) noexcept {
    if(std::abs(rise-800)<.5&&std::abs(fall-300)<.5)return RiderSpeed::slow;
    if(std::abs(rise-200)<.5&&std::abs(fall-75)<.5)return RiderSpeed::fast;
    return RiderSpeed::custom;
}
}
