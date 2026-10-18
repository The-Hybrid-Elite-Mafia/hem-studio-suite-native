#pragma once
#include <algorithm>
#include <cmath>
namespace hem::bands {
// A conservative rejection bound, not an approximation of the dB law. Peaks
// below half the threshold amplitude are more than 6 dB below threshold, so
// the original clamped request is exactly zero. Near/above threshold keeps the
// original log10 expression. Detector/filter histories still advance normally.
struct DynamicEqRequest {
    double previous=0, rejection=0;
    bool cached=false;
    double get(double peak,double threshold,double range) noexcept {
        if(!(range>0))return 0;
        // The finite interval also ensures the 1e-12 detector floor is below
        // threshold. Outside it, preserve the original general expression.
        if(std::isfinite(threshold)&&threshold>=-120&&threshold<=120){
            // Quantise only the rejection bound downward. The actual request
            // still uses the exact threshold. A six-dB interval avoids pow on
            // every sample of a smoothed threshold automation ramp.
            if(!cached||threshold<previous||threshold>=previous+6){previous=6*std::floor(threshold/6);rejection=.5*std::pow(10.,previous/20.);cached=true;}
            if(peak>=0&&peak<rejection)return 0;
        }
        return std::min(range,std::max(0.,20*std::log10(std::max(1e-12,peak))-threshold));
    }
};
}
