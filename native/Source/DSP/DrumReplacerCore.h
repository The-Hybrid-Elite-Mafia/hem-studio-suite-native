#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>
#include <atomic>
#include <limits>

namespace hem::drum {
// First DSP increment. Resource owners must retain these immutable, already
// decoded/resampled buffers until setBankStopped/reset under callback exclusion.
// No sample ownership is acquired or released in process(). No live bank swaps.
struct SampleView {
    const float* left=nullptr;
    const float* right=nullptr; // nullptr means mono, duplicated to stereo
    int frames=0;
    double sampleRate=0;
};
using Bank=std::array<SampleView,4>;
struct Parameters {
    float sensitivity=.5f; // threshold: -6 dBFS at 0, -60 dBFS at 1
    float retriggerMs=40, velocityMatch=1, blend=1, outputDb=0;
    std::array<float,4> levelDb{{0,0,0,0}};
    std::array<bool,4> enabled{{true,true,true,true}};
    std::array<bool,4> solo{{false,false,false,false}};
};
struct Telemetry {
    uint64_t acceptedHits=0, startedLayers=0, stolenVoices=0, droppedLayers=0;
    float detectorPeak=0, thresholdDb=-33;
    int soundingVoices=0, fadingVoices=0;
};

// Five-millisecond input bins. Positions are detector-input samples, before
// fixed lookahead. Signed extrema combine L/R; peak is the linked detector.
struct DetectorHistoryBin {
    float minimum=0,maximum=0,peak=0,thresholdDb=-33;
    uint32_t hits=0;
    uint64_t firstSample=0,endSample=0,firstOnsetSample=std::numeric_limits<uint64_t>::max();
};
struct DetectorHistorySnapshot {
    static constexpr size_t capacity=256;
    std::array<DetectorHistoryBin,capacity> bins{}; // oldest first
    int count=0;
    double sampleRate=0;
    uint32_t samplesPerBin=0;
    uint64_t totalSamples=0,acceptedHits=0,epoch=0;
};
// Single audio/preparation writer, any GUI readers. All shared payload is atomic
// (ordinary-data seqlocks would be undefined C++). A busy snapshot fails boundedly.
class DetectorHistory {
public:
    void reset(double rate) noexcept {
        sequence.fetch_add(1);head.store(0);count.store(0);sampleRate.store(rate);
        width.store(uint32_t(std::ceil(rate*.005)));total.store(0);hits.store(0);epoch.fetch_add(1);sequence.fetch_add(1);
    }
    void append(const DetectorHistoryBin& bin,uint64_t accepted) noexcept {
        sequence.fetch_add(1);const auto index=head.load();auto& destination=bins[index];
        destination.minimum.store(bin.minimum);destination.maximum.store(bin.maximum);destination.peak.store(bin.peak);
        destination.threshold.store(bin.thresholdDb);destination.hits.store(bin.hits);destination.first.store(bin.firstSample);
        destination.end.store(bin.endSample);destination.onset.store(bin.firstOnsetSample);
        head.store((index+1)%DetectorHistorySnapshot::capacity);
        count.store(std::min(uint32_t(DetectorHistorySnapshot::capacity),count.load()+1));
        total.store(bin.endSample);hits.store(accepted);sequence.fetch_add(1);
    }
    bool snapshot(DetectorHistorySnapshot& out) const noexcept {
        for(int attempt=0;attempt<3;++attempt){const auto before=sequence.load();if(before&1)continue;
            DetectorHistorySnapshot result;const auto n=count.load(),end=head.load();
            result.count=int(n);result.sampleRate=sampleRate.load();result.samplesPerBin=width.load();
            result.totalSamples=total.load();result.acceptedHits=hits.load();result.epoch=epoch.load();
            for(uint32_t k=0;k<n;++k){const auto index=(end+DetectorHistorySnapshot::capacity-n+k)%DetectorHistorySnapshot::capacity;
                const auto& source=bins[index];auto& bin=result.bins[k];
                bin.minimum=source.minimum.load();bin.maximum=source.maximum.load();bin.peak=source.peak.load();bin.thresholdDb=source.threshold.load();
                bin.hits=source.hits.load();bin.firstSample=source.first.load();bin.endSample=source.end.load();bin.firstOnsetSample=source.onset.load();}
            if(sequence.load()==before){out=result;return true;}}
        return false;
    }
private:
    struct AtomicBin {std::atomic<float> minimum{0},maximum{0},peak{0},threshold{-33};
        std::atomic<uint32_t> hits{0};std::atomic<uint64_t> first{0},end{0},onset{0};};
    std::array<AtomicBin,DetectorHistorySnapshot::capacity> bins{};
    std::atomic<uint64_t> sequence{0},total{0},hits{0},epoch{0};
    std::atomic<uint32_t> head{0},count{0},width{0};std::atomic<double> sampleRate{0};
    static_assert(std::atomic<float>::is_always_lock_free&&std::atomic<uint64_t>::is_always_lock_free&&
                  std::atomic<uint32_t>::is_always_lock_free&&std::atomic<double>::is_always_lock_free);
};
class Core {
public:
    static constexpr int voiceCount=32;
    // Off callback; validates input before storage or sample scanning. The
    // provider additionally enforces file size, hashes and aggregate ownership.
    static bool validateSample(const SampleView& s,double rate) noexcept {
        if (!s.left) return !s.right && s.frames==0 && s.sampleRate==0;
        if (!std::isfinite(rate) || rate<8000 || rate>192000 ||
            s.sampleRate!=rate || s.frames<=0 || s.frames>int(rate*10)) return false;
        for (int i=0;i<s.frames;++i)
            if (!std::isfinite(s.left[i]) || (s.right && !std::isfinite(s.right[i]))) return false;
        return true;
    }
    // Allocations and buffer reset are preparation work, never callback work.
    bool prepare(double rate,const Bank& samples) {
        if (!std::isfinite(rate) || rate<8000 || rate>192000) return false;
        for (const auto& s:samples) if (!validateSample(s,rate)) return false;
        const int delay=int(std::ceil(rate*.003));
        std::vector<Stereo> dry(size_t(delay),Stereo{});
        std::vector<Event> events(size_t(delay),Event{});
        dry_.swap(dry);events_.swap(events);rate_=rate;bank_=samples;
        slew_=float(1.0-std::exp(-1.0/(rate*.005)));
        fadeSamples_=std::max(1,int(std::ceil(rate*.001)));
        prepared_=true;reset();return true;
    }
    bool setBankStopped(const Bank& samples) noexcept {
        if (!prepared_) return false;
        for (const auto& s:samples) if (!validateSample(s,rate_)) return false;
        reset();bank_=samples;return true;
    }
    void reset() noexcept {
        voices_={};tails_={};std::fill(dry_.begin(),dry_.end(),Stereo{});
        std::fill(events_.begin(),events_.end(),Event{});
        cursor_=0;refractory_=0;armed_=true;serial_=0;parametersReady_=false;
        meters_={};mix_=0;output_=1;levels_.fill(0);
        history_.reset(rate_);historyWidth_=uint32_t(std::ceil(rate_*.005));historyBin_={};historyBinSamples_=0;inputSamples_=0;
    }
    bool detectorHistory(DetectorHistorySnapshot& out) const noexcept {return history_.snapshot(out);}
    int latencySamples() const noexcept {return int(dry_.size());}
    const Telemetry& telemetryOnAudioThread() const noexcept {return meters_;}
    static float thresholdDb(float sensitivity) noexcept {return -6-54*bound(sensitivity,0,1,.5f);}

    // In-place stereo; bounded O(frames * 64) work, no allocation/free, file
    // access, locks or reference-counted ownership. Parameters are a block
    // snapshot supplied by the future APVTS adapter. Telemetry publication is
    // deliberately left to that adapter: GUI must not read this mutable struct.
    void process(float* left,float* right,int frames,const Parameters& p) noexcept {
        if (!left || !right || left==right || frames<=0 || !prepared_) return;
        const float db=thresholdDb(p.sensitivity),threshold=gain(db);
        const auto retrigger=std::max(1,int(std::ceil(rate_*bound(p.retriggerMs,1,500,40)*.001)));
        const float velocity=bound(p.velocityMatch,0,1,1);
        const float targetOutput=gain(bound(p.outputDb,-24,12,0));
        bool any=false,anySolo=false;
        std::array<bool,4> usable{};
        for (int k=0;k<4;++k) {usable[k]=p.enabled[k] && bank_[k].left;any|=usable[k];anySolo|=usable[k]&&p.solo[k];}
        std::array<float,4> targetLevels{};
        for (int k=0;k<4;++k) targetLevels[k]=(usable[k]&&(!anySolo||p.solo[k]))?gain(bound(p.levelDb[k],-60,12,0)):0;
        const float targetMix=any?bound(p.blend,0,1,1):0;
        if (!parametersReady_) {mix_=targetMix;output_=targetOutput;levels_=targetLevels;parametersReady_=true;}
        meters_.detectorPeak=0;meters_.thresholdDb=db;
        for (int n=0;n<frames;++n) {
            const Stereo input{finite(left[n]),finite(right[n])};
            const Stereo dry=dry_[cursor_];dry_[cursor_]=input;
            const Event due=events_[cursor_];events_[cursor_]={};
            const float peak=std::max(std::abs(input.l),std::abs(input.r));
            bool accepted=false;
            meters_.detectorPeak=std::max(meters_.detectorPeak,peak);
            if (refractory_>0) --refractory_;
            if (peak<threshold*.5f) armed_=true;
            if (armed_ && refractory_==0 && peak>=threshold) {
                // The event and original audio cross exactly the same fixed
                // 3 ms ring. Strength is onset peak, not sample normalization.
                events_[cursor_]={true,1-velocity+velocity*std::min(1.0f,peak)};
                armed_=false;refractory_=retrigger;++meters_.acceptedHits;accepted=true;
            }
            recordDetection(input,peak,db,accepted);
            if (due.hit) for (int k=0;k<4;++k)
                if (usable[k]&&(!anySolo||p.solo[k])) start(k,due.velocity);
            for (int k=0;k<4;++k) levels_[k]+=(targetLevels[k]-levels_[k])*slew_;
            mix_+=(targetMix-mix_)*slew_;output_+=(targetOutput-output_)*slew_;
            Stereo wet{};
            for (int v=0;v<voiceCount;++v) {
                addVoice(wet,voices_[v],1);
                auto& tail=tails_[v];
                if (tail.voice.sample) {
                    addVoice(wet,tail.voice,float(tail.remaining)/float(fadeSamples_));
                    if (--tail.remaining<=0) tail.voice={};
                }
            }
            // With no valid enabled layer, preserve dry immediately, including
            // Blend=100. Wet tails cannot silently replace unavailable content.
            const float blend=any?mix_:0;
            left[n]=finite((dry.l*(1-blend)+wet.l*blend)*output_);
            right[n]=finite((dry.r*(1-blend)+wet.r*blend)*output_);
            if (++cursor_==dry_.size()) cursor_=0;
        }
        meters_.soundingVoices=0;meters_.fadingVoices=0;
        for (int i=0;i<voiceCount;++i) {meters_.soundingVoices+=voices_[i].sample!=nullptr;meters_.fadingVoices+=tails_[i].voice.sample!=nullptr;}
    }
private:
    struct Stereo {float l=0,r=0;};
    struct Event {bool hit=false;float velocity=0;};
    struct Voice {const SampleView* sample=nullptr;int position=0,layer=0;float velocity=0;uint64_t serial=0;};
    struct Tail {Voice voice{};int remaining=0;};
    static float bound(float x,float lo,float hi,float fallback) noexcept {return std::isfinite(x)?std::clamp(x,lo,hi):fallback;}
    static float finite(float x) noexcept {return std::isfinite(x)?std::clamp(x,-64.0f,64.0f):0;}
    static float gain(float db) noexcept {return std::pow(10.0f,db*.05f);}
    void start(int layer,float velocity) noexcept {
        int selected=-1;
        for (int i=0;i<voiceCount;++i) if (!voices_[i].sample) {selected=i;break;}
        if (selected<0) {
            for (int i=0;i<voiceCount;++i) if (!tails_[i].voice.sample && (selected<0||voices_[i].serial<voices_[selected].serial)) selected=i;
            // Never truncate an already fading tail to make room. A bounded
            // overload drops a layer and reports it rather than allocating.
            if (selected<0) {++meters_.droppedLayers;return;}
            tails_[selected]={voices_[selected],fadeSamples_};++meters_.stolenVoices;
        }
        voices_[selected]={&bank_[layer],0,layer,velocity,++serial_};++meters_.startedLayers;
    }
    void addVoice(Stereo& sum,Voice& v,float fade) noexcept {
        if (!v.sample) return;
        const auto& s=*v.sample;
        const float g=v.velocity*levels_[v.layer]*fade;
        sum.l+=finite(s.left[v.position])*g;
        sum.r+=finite((s.right?s.right:s.left)[v.position])*g;
        if (++v.position==s.frames) v={};
    }
        void recordDetection(const Stereo& input,float peak,float threshold,bool accepted) noexcept {
        if(historyBinSamples_>0&&historyBin_.thresholdDb!=threshold){history_.append(historyBin_,meters_.acceptedHits-(accepted?1:0));historyBinSamples_=0;}
        if(historyBinSamples_==0){historyBin_={};historyBin_.firstSample=inputSamples_;
            historyBin_.minimum=std::min(input.l,input.r);historyBin_.maximum=std::max(input.l,input.r);}
        historyBin_.minimum=std::min(historyBin_.minimum,std::min(input.l,input.r));
        historyBin_.maximum=std::max(historyBin_.maximum,std::max(input.l,input.r));
        historyBin_.peak=std::max(historyBin_.peak,peak);historyBin_.thresholdDb=threshold;
        if(accepted){if(historyBin_.hits==0)historyBin_.firstOnsetSample=inputSamples_;++historyBin_.hits;}
        ++inputSamples_;historyBin_.endSample=inputSamples_;
        if(++historyBinSamples_>=historyWidth_){history_.append(historyBin_,meters_.acceptedHits);historyBinSamples_=0;}
    }
    DetectorHistory history_;
    DetectorHistoryBin historyBin_{};
    uint64_t inputSamples_=0;uint32_t historyBinSamples_=0,historyWidth_=1;
    Bank bank_{};
    std::array<Voice,voiceCount> voices_{};
    std::array<Tail,voiceCount> tails_{};
    std::array<float,4> levels_{};
    std::vector<Stereo> dry_;
    std::vector<Event> events_;
    size_t cursor_=0;
    double rate_=0;
    int refractory_=0,fadeSamples_=1;
    float slew_=0,mix_=0,output_=1;
    bool armed_=true,prepared_=false,parametersReady_=false;
    uint64_t serial_=0;
    Telemetry meters_{};
};
} // namespace hem::drum


