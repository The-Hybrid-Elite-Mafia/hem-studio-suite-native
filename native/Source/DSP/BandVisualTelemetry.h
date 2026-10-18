#pragma once
#include <array>
#include <atomic>
#include <cstdint>
namespace hem::bands {
// GUI subscription controls telemetry work only, never processing or sound.
// Numeric filter coefficients remain double: float conversion corrupts narrow
// low-frequency/high-Q response curves even when audio coefficients are sound.
struct VisualFrame {
    uint32_t revision=0;uint64_t samples=0;double rate=0;bool processed=false,transition=false;
    int topology=1;unsigned scopeCount=0;
    double inputPeak=0,outputPeak=0;
    std::array<double,3> crossovers{};
    std::array<double,8> signedDynamics{};
    std::array<double,4> bandCuts{},correlations{};
    std::array<bool,4> correlationValid{};
    std::array<std::array<double,11>,10> coefficients{};
    std::array<std::array<std::array<float,2>,32>,4> scopes{};
    float width=100,midParticipation=1,sideParticipation=1;
};
class VisualTelemetry {
public:
    std::atomic<bool> subscribed{false};
    void publish(const VisualFrame& f) noexcept {
        sequence.fetch_add(1);samples.store(f.samples);rate.store(f.rate);scopeCount.store(f.scopeCount);flags.store((f.processed?1u:0u)|(f.transition?2u:0u)|(f.topology?4u:0u));
        inputPeak.store(f.inputPeak);outputPeak.store(f.outputPeak);width.store(f.width);mid.store(f.midParticipation);side.store(f.sideParticipation);
        for(size_t i=0;i<3;++i)crossovers[i].store(f.crossovers[i]);
        for(size_t i=0;i<8;++i)signedDynamics[i].store(f.signedDynamics[i]);
        for(size_t b=0;b<4;++b){cuts[b].store(f.bandCuts[b]);correlations[b].store(f.correlations[b]);valid[b].store(f.correlationValid[b]);
            for(size_t i=0;i<32;++i)for(size_t c=0;c<2;++c)scopes[b][i][c].store(f.scopes[b][i][c]);}
        for(size_t b=0;b<10;++b)for(size_t i=0;i<11;++i)coefficients[b][i].store(f.coefficients[b][i]);
        sequence.fetch_add(1);
    }
    bool read(VisualFrame& f) const noexcept {
        for(int attempt=0;attempt<3;++attempt){const auto before=sequence.load();if(before&1u)continue;
            f.samples=samples.load();f.rate=rate.load();f.scopeCount=scopeCount.load();const auto state=flags.load();f.processed=state&1u;f.transition=state&2u;f.topology=state&4u?1:0;
            f.inputPeak=inputPeak.load();f.outputPeak=outputPeak.load();f.width=width.load();f.midParticipation=mid.load();f.sideParticipation=side.load();
            for(size_t i=0;i<3;++i)f.crossovers[i]=crossovers[i].load();for(size_t i=0;i<8;++i)f.signedDynamics[i]=signedDynamics[i].load();
            for(size_t b=0;b<4;++b){f.bandCuts[b]=cuts[b].load();f.correlations[b]=correlations[b].load();f.correlationValid[b]=valid[b].load();
                for(size_t i=0;i<32;++i)for(size_t c=0;c<2;++c)f.scopes[b][i][c]=scopes[b][i][c].load();}
            for(size_t b=0;b<10;++b)for(size_t i=0;i<11;++i)f.coefficients[b][i]=coefficients[b][i].load();
            if(before==sequence.load()){f.revision=before;return true;}
        }return false;
    }
private:
    std::atomic<uint32_t> sequence{0},flags{0};std::atomic<unsigned> scopeCount{0};std::atomic<uint64_t> samples{0};
    std::atomic<double> rate{0},inputPeak{0},outputPeak{0};std::atomic<float> width{100},mid{1},side{1};
    std::array<std::atomic<double>,3> crossovers{};std::array<std::atomic<double>,8> signedDynamics{};
    std::array<std::atomic<double>,4> cuts{},correlations{};std::array<std::atomic<bool>,4> valid{};
    std::array<std::array<std::atomic<double>,11>,10> coefficients{};
    std::array<std::array<std::array<std::atomic<float>,2>,32>,4> scopes{};
    static_assert(std::atomic<double>::is_always_lock_free&&std::atomic<uint64_t>::is_always_lock_free);
};
}
