#pragma once
#include <array>
#include <atomic>
#include <algorithm>
#include <cmath>
#include <cstdint>
namespace hem::restoration {
struct Display {
    static constexpr size_t points=512,bands=32,columns=128;
    std::array<std::array<float,points>,3> history{};
    std::array<float,bands> frequencies{},inputDb{},outputDb{},deltaDb{},cutDb{};
    std::array<std::array<float,bands>,columns> spectrogram{};
    std::array<float,2> detection{},reduction{};
    size_t count=0,cursor=0,spectralCount=0,spectralCursor=0;
    uint64_t frames=0,events=0,repaired=0,skipped=0;
    double sampleRate=0;float seconds=0,maximumCut=0;bool valid=false;
};
// All published payloads are atomic, so an unsuccessful bounded snapshot has no
// data race. Single audio writer. Reads may return unavailable under contention.
class Telemetry {
public:
    void prepare(double rate,float seconds)noexcept {sr.store(rate);duration.store(seconds);interval=std::max(1,int(std::lround(rate*seconds/Display::points)));spectralInterval=std::max(1,int(std::lround(rate*.02)));reset();}
    void reset()noexcept {sequence.fetch_add(1);count.store(0);cursor.store(0);spectralCount.store(0);spectralCursor.store(0);frames.store(0);events.store(0);repaired.store(0);skipped.store(0);valid.store(false);maximumCut.store(0);for(auto&x:detection)x.store(0);for(auto&x:reduction)x.store(0);sequence.fetch_add(1);clock=until=spectralUntil=0;pending={};}
    void sample(float a,float b,float c,bool peakHold=false)noexcept {
        ++clock;++spectralUntil;
        if(peakHold){pending[0]=std::max(pending[0],a);pending[1]=std::max(pending[1],b);pending[2]=std::max(pending[2],c);}else pending={a,b,c};
        if(++until<interval)return;until=0;sequence.fetch_add(1);const auto i=cursor.load();for(size_t j=0;j<3;++j)history[j][i].store(pending[j]);cursor.store((i+1)%Display::points);count.store(std::min(Display::points,count.load()+1));frames.store(clock);valid.store(true);sequence.fetch_add(1);pending={};
    }
    void spectrum(const std::array<float,Display::bands>&hz,const std::array<float,Display::bands>&pre,const std::array<float,Display::bands>&post,const std::array<float,Display::bands>&delta,const std::array<float,Display::bands>&cuts)noexcept{
        if(spectralUntil<spectralInterval)return;spectralUntil=0;sequence.fetch_add(1);const auto column=spectralCursor.load();float sum=0;for(size_t i=0;i<Display::bands;++i){frequencies[i].store(hz[i]);inputDb[i].store(pre[i]);outputDb[i].store(post[i]);deltaDb[i].store(delta[i]);cutDb[i].store(cuts[i]);spectrogram[column][i].store(pre[i]);sum+=cuts[i];}maximumCut.store(sum);spectralCursor.store((column+1)%Display::columns);spectralCount.store(std::min(Display::columns,spectralCount.load()+1));frames.store(clock);valid.store(true);sequence.fetch_add(1);
    }
    void status(float d0,float d1,float r0,float r1,uint64_t e=0,uint64_t r=0,uint64_t s=0)noexcept{sequence.fetch_add(1);detection[0].store(d0);detection[1].store(d1);reduction[0].store(r0);reduction[1].store(r1);events.store(e);repaired.store(r);skipped.store(s);frames.store(clock);valid.store(clock>0);sequence.fetch_add(1);}
    Display snapshot()const noexcept{
        Display d;for(int attempt=0;attempt<3;++attempt){const auto before=sequence.load();if(before&1)continue;d.count=count.load();d.cursor=cursor.load();d.spectralCount=spectralCount.load();d.spectralCursor=spectralCursor.load();d.frames=frames.load();d.events=events.load();d.repaired=repaired.load();d.skipped=skipped.load();d.sampleRate=sr.load();d.seconds=duration.load();d.maximumCut=maximumCut.load();d.valid=valid.load();for(size_t j=0;j<3;++j)for(size_t i=0;i<Display::points;++i)d.history[j][i]=history[j][i].load();for(size_t i=0;i<Display::bands;++i){d.frequencies[i]=frequencies[i].load();d.inputDb[i]=inputDb[i].load();d.outputDb[i]=outputDb[i].load();d.deltaDb[i]=deltaDb[i].load();d.cutDb[i]=cutDb[i].load();}for(size_t j=0;j<Display::columns;++j)for(size_t i=0;i<Display::bands;++i)d.spectrogram[j][i]=spectrogram[j][i].load();for(size_t i=0;i<2;++i){d.detection[i]=detection[i].load();d.reduction[i]=reduction[i].load();}if(before==sequence.load())return d;}return {};
    }
private:
    std::array<std::array<std::atomic<float>,Display::points>,3>history{};
    std::array<std::atomic<float>,Display::bands>frequencies{},inputDb{},outputDb{},deltaDb{},cutDb{};
    std::array<std::array<std::atomic<float>,Display::bands>,Display::columns>spectrogram{};
    std::array<std::atomic<float>,2>detection{},reduction{};
    std::atomic<size_t>count{0},cursor{0},spectralCount{0},spectralCursor{0};std::atomic<uint64_t>frames{0},events{0},repaired{0},skipped{0};std::atomic<unsigned>sequence{0};std::atomic<double>sr{0};std::atomic<float>duration{0},maximumCut{0};std::atomic<bool>valid{false};
    uint64_t clock=0;int interval=1,until=0,spectralInterval=1,spectralUntil=0;std::array<float,3>pending{};
    static_assert(std::atomic<float>::is_always_lock_free&&std::atomic<double>::is_always_lock_free&&std::atomic<size_t>::is_always_lock_free&&std::atomic<uint64_t>::is_always_lock_free);
};
}
