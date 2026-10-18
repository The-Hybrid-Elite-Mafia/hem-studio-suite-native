#pragma once
#include "FoundationCore.h"
namespace hem {
// Fixed storage for the entire supported 8..384 kHz domain. Audio owner only.
// No control signal is derived from delay-line startup silence.
template<FoundationKind Kind> class FoundationModes {
public:
    void prepare(double fs,float lookahead=0,bool listen=false) noexcept {
        delay=Kind==FoundationKind::gate?int(std::ceil(fs*.006)):0;
        rate=fs;position=0;history={};gains.fill(1.);lastReduction=0;
        tap=targetTap=clampTap(lookahead);tapStep=0;tapRemaining=0;
        audition=listen?1.:0.;targetAudition=audition;auditionStep=0;auditionRemaining=0;
        transition=std::max(1,int(std::ceil(fs*.010)));
    }
    int latency() const noexcept {return delay;}
    float reduction() const noexcept {return lastReduction;}
    void targets(float lookahead,bool listen) noexcept {
        if constexpr(Kind==FoundationKind::gate){const int next=clampTap(lookahead);
            if(next!=targetTap){targetTap=next;tapRemaining=transition;tapStep=(next-tap)/transition;}}
        const double next=listen?1.:0.;
        if(next!=targetAudition){targetAudition=next;auditionRemaining=transition;auditionStep=(next-audition)/transition;}
    }
    std::array<double,2> process(const std::array<double,2>& dry,const std::array<double,2>& normal,const FoundationSettings& p,double envelope) noexcept {
        if(tapRemaining&&!--tapRemaining)tap=targetTap;else if(tapRemaining)tap+=tapStep;
        if(auditionRemaining&&!--auditionRemaining)audition=targetAudition;else if(auditionRemaining)audition+=auditionStep;
        if constexpr(Kind==FoundationKind::gate){
            auto& now=history[position];now={dry,normal,p[8]*.01,foundation::dbGain(p[9])};gains[position]=foundation::dbGain(-envelope);
            const auto at=[this](int ago){return (position+capacity-size_t(ago))%capacity;};
            const auto delayed=history[at(delay)];
            const double ago=delay-tap;const int first=int(std::floor(ago));const double fraction=ago-first;
            const double g=gains[at(first)]*(1-fraction)+gains[at(std::min(first+1,delay))]*fraction;
            lastReduction=float(-20*std::log10(std::max(1.e-12,1-delayed.mix+delayed.mix*g)));
            std::array<double,2> result{};
            for(size_t c=0;c<2;++c){
                // Exact legacy arithmetic at L=0, including parameter events.
                const double y=tap==0?delayed.normal[c]:delayed.output*(delayed.dry[c]+delayed.mix*(delayed.dry[c]*g-delayed.dry[c]));
                result[c]=audition==0?y:audition==1?delayed.dry[c]:y+audition*(delayed.dry[c]-y);
            }
            position=(position+1)%capacity;return result;
        }else if constexpr(Kind==FoundationKind::plosive||Kind==FoundationKind::hum){
            std::array<double,2> result{};
            for(size_t c=0;c<2;++c){const double removed=dry[c]-normal[c];result[c]=audition==0?normal[c]:audition==1?removed:normal[c]+audition*(removed-normal[c]);}
            return result;
        }else return normal;
    }
private:
    int clampTap(float ms) const noexcept {return int(std::clamp(std::round(rate*(std::isfinite(ms)?std::clamp(double(ms),0.,6.):0.)*.001),0.,double(delay)));}
    static constexpr size_t capacity=2306; // ceil(384000*.006)+2
    struct Frame {std::array<double,2> dry{},normal{};double mix=1,output=1;};
    // Non-gate instances have no delay storage.
    std::array<Frame,Kind==FoundationKind::gate?capacity:1> history{};
    std::array<double,Kind==FoundationKind::gate?capacity:1> gains{};
    size_t position=0;double rate=0,tap=0,tapStep=0,audition=0,targetAudition=0,auditionStep=0;
    int delay=0,targetTap=0,transition=1,tapRemaining=0,auditionRemaining=0;float lastReduction=0;
};
}
