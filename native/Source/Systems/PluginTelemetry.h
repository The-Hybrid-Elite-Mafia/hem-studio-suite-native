#pragma once
#include "PluginAnalysis.h"
#include "ReductionHistory.h"
namespace hem {
class PluginTelemetry {
public:
    explicit PluginTelemetry(bool spectrum=true,bool analysisWorker=true):analysis(spectrum,analysisWorker){}
    struct Levels { std::array<float,4> peak{},rms{},gains{}; unsigned epoch=0,blocks=0,samples=0;std::uint64_t historySerial=0;double rate=0;bool valid=false; };
    using MeterPoint=ReductionHistory::Point;
    bool readMeterPoint(std::uint64_t serial,MeterPoint& result) const noexcept {
        return history.read(serial,analysis.epoch(),result);
    }
    void prepare(double sr) noexcept {
        rate=std::isfinite(sr)&&sr>=8000&&sr<=384000?sr:0;
        memory=rate>0?std::exp(-1./(.3*rate)):0;energy={};inputPeak={};inputValid=true;blocks=0;analysis.prepare(rate);history.prepare(rate,analysis.epoch());
        sequence.fetch_add(1);ready.store(false);sequence.fetch_add(1);
    }
    void suspend() noexcept {analysis.suspend();sequence.fetch_add(1);ready.store(false);sequence.fetch_add(1);}
    // Called before DSP and after DSP by the same serialized audio producer.
    void input(const float* l,const float* r,int n) noexcept {
        inputPeak={};inputValid=true;
        scan(l,r,n,0,inputPeak,inputValid);
    }
    void output(const float* l,const float* r,int n,const std::array<float,4>& gain={},float measured=0,float auxiliary=0) noexcept {
        std::array<float,2> p{};bool valid=inputValid;scan(l,r,n,2,p,valid);
        sequence.fetch_add(1);
        for(int i=0;i<4;++i){peaks[i].store(i<2?inputPeak[i]:p[i-2]);rms[i].store(float(std::sqrt(energy[i])));gains[i].store(gain[i]);}
        ++blocks;historySerial.store(history.push(unsigned(n),measured,auxiliary));
        sampleCount.store(unsigned(n));blockCount.store(blocks);sampleRate.store(rate);epoch.store(analysis.epoch());ready.store(valid&&rate>0);
        sequence.fetch_add(1);analysis.push(l,r,n);
    }
    bool read(Levels& out) const noexcept {
        for(int attempt=0;attempt<3;++attempt){const auto before=sequence.load();if(before&1)continue;
            Levels v;for(int i=0;i<4;++i){v.peak[i]=peaks[i].load();v.rms[i]=rms[i].load();v.gains[i]=gains[i].load();}
            v.epoch=epoch.load();v.blocks=blockCount.load();v.historySerial=historySerial.load();v.samples=sampleCount.load();v.rate=sampleRate.load();v.valid=ready.load();
            if(before==sequence.load()&&v.epoch==analysis.epoch()){out=v;return true;}
        }return false;
    }
    PluginAnalysis analysis;
private:
    ReductionHistory history;
    std::atomic<std::uint64_t> historySerial{0};
    void scan(const float* l,const float* r,int n,int offset,std::array<float,2>& peak,bool& valid) noexcept {
        for(int i=0;i<n;++i)for(int ch=0;ch<2;++ch){const float raw=(ch?r:l)[i];const float v=std::isfinite(raw)?raw:0;
            if(!std::isfinite(raw))valid=false;peak[ch]=std::max(peak[ch],std::abs(v));
            energy[offset+ch]=memory*energy[offset+ch]+(1-memory)*double(v)*v;
        }
    }
    double rate=0,memory=0;std::array<double,4> energy{};std::array<float,2> inputPeak{};bool inputValid=true;unsigned blocks=0;
    std::atomic<unsigned> sequence{0},sampleCount{0},blockCount{0},epoch{0};
    std::array<std::atomic<float>,4> peaks{},rms{},gains{};std::atomic<double> sampleRate{0};std::atomic<bool> ready{false};
    static_assert(std::atomic<float>::is_always_lock_free&&std::atomic<unsigned>::is_always_lock_free&&std::atomic<double>::is_always_lock_free&&std::atomic<bool>::is_always_lock_free);
};
}
