#pragma once
#include <array>
#include <atomic>
#include <cmath>
#include <algorithm>
namespace hem {
// Single audio writer. GUI snapshots are bounded and discard concurrent frames.
struct NonlinearTelemetry {
    static constexpr size_t size=512;
    struct Snapshot {std::array<float,size> samples{};float input=0,output=0;bool ready=false;};
    std::array<std::atomic<float>,size> samples{};std::atomic<unsigned> sequence{0},count{0};
    std::atomic<float> input{0},output{0};unsigned position=0,total=0;
    void reset() noexcept {sequence.fetch_add(1);for(auto& s:samples)s.store(0);position=total=0;count.store(0);input.store(0);output.store(0);sequence.fetch_add(1);}
    void begin(const float* l,const float* r,int n) noexcept {sequence.fetch_add(1);float p=0;for(int i=0;i<n;++i)p=std::max({p,std::abs(l[i]),std::abs(r[i])});input.store(p);}
    void end(const float* l,const float* r,int n) noexcept {float p=0;for(int i=0;i<n;++i){samples[position].store(l[i]);position=(position+1)%size;p=std::max({p,std::abs(l[i]),std::abs(r[i])});}total=std::min(unsigned(size),total+unsigned(n));output.store(p);count.store((total<<16)|position);sequence.fetch_add(1);}
    Snapshot snapshot() const noexcept {Snapshot s;for(int retry=0;retry<3;++retry){const auto a=sequence.load();if(a&1)continue;const auto c=count.load();s.input=input.load();s.output=output.load();for(size_t i=0;i<size;++i)s.samples[i]=samples[(size_t(c&65535)+i)%size].load();if(a==sequence.load()){s.ready=(c>>16)==size;return s;}}return {};}
};
}
