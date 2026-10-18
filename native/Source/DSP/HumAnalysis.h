#pragma once
#include "FoundationCore.h"
#include <complex>
namespace hem {
struct HumAnalysisFrame {
    static constexpr int bins=128;
    std::array<float,bins> spectrum{};
    float strongestDb=-120,thresholdDb=-60,fundamental=50,seconds=0;
    uint64_t endSample=0,epoch=0;int harmonics=8;bool detected=false;
};
// Three ownership slots: 0 free, 1 audio-owned, 2 ready, 3 worker-owned.
// Audio never waits; a saturated queue drops an entire measurement window.
// The worker never accesses mutable DSP/parameters, only captured metadata.
class HumAnalysis {
public:
    static constexpr int size=8192;
    void prepare(double fs) noexcept { // worker stopped; audio excluded
        rate=fs;stride=std::max(1,int(std::floor(fs/4000.)));decimatedRate=fs/stride;
        count=decimation=0;writing=-1;sample=0;keyFrequency=-1;keyHarmonics=0;keySensitivity=-1;
        filters={};filters[0].configure(1,fs,1400,.541196100146197);filters[1].configure(1,fs,1400,1.306562964876377);
        for(auto& slot:slots)slot.state.store(0);epoch.fetch_add(1);publishedEpoch.store(0);
    }
    void settings(float requestedFrequency,int requestedHarmonics,float sensitivity) noexcept {
        if(requestedFrequency==keyFrequency&&requestedHarmonics==keyHarmonics&&sensitivity==keySensitivity)return;
        keyFrequency=requestedFrequency;keyHarmonics=requestedHarmonics;keySensitivity=sensitivity;
        epoch.fetch_add(1);if(writing>=0)slots[size_t(writing)].state.store(0,std::memory_order_release);
        writing=-1;count=0;decimation=0;
    }
    void observe(const std::array<double,2>& input) noexcept {
        ++sample;std::array<double,2> filtered{};
        for(int c=0;c<2;++c)filtered[size_t(c)]=filters[1].tick(filters[0].tick(input[size_t(c)],c),c);
        if(++decimation<stride)return;decimation=0;
        if(count==0){writing=-1;for(int i=0;i<3;++i){int free=0;if(slots[size_t(i)].state.compare_exchange_strong(free,1,std::memory_order_acquire)){writing=i;break;}}}
        if(writing>=0){auto& slot=slots[size_t(writing)];slot.left[size_t(count)]=filtered[0];slot.right[size_t(count)]=filtered[1];}
        if(++count==size){count=0;if(writing>=0){auto& slot=slots[size_t(writing)];slot.frequency=keyFrequency;slot.harmonics=keyHarmonics;slot.sensitivity=keySensitivity;slot.rate=decimatedRate;slot.end=sample;slot.epoch=epoch.load();slot.state.store(2,std::memory_order_release);writing=-1;}}
    }
    // Only the single worker calls consume; deterministic tests can call it
    // synchronously. All FFT storage is prepared member storage.
    bool consume() noexcept {
        int selected=-1;uint64_t oldest=UINT64_MAX;
        for(int i=0;i<3;++i)if(slots[size_t(i)].state.load(std::memory_order_acquire)==2&&slots[size_t(i)].end<oldest){selected=i;oldest=slots[size_t(i)].end;}
        if(selected<0)return false;auto& slot=slots[size_t(selected)];slot.state.store(3,std::memory_order_release);
        HumAnalysisFrame frame;frame.fundamental=slot.frequency;frame.harmonics=slot.harmonics;frame.thresholdDb=-30.f-.6f*slot.sensitivity;frame.seconds=float(size/slot.rate);frame.endSample=slot.end;frame.epoch=slot.epoch;
        powers.fill(0);
        for(int channel=0;channel<2;++channel){const auto& input=channel?slot.right:slot.left;double windowPower=0,normalization=1;
            for(double value:input)normalization=std::max(normalization,std::abs(value));
            for(int i=0;i<size;++i){const double window=.5-.5*std::cos(2*pi*i/size);fft[size_t(i)]={float(input[size_t(i)]/normalization*window),0};windowPower+=window*window;}
            for(unsigned i=1,j=0;i<unsigned(size);++i){unsigned bit=size/2;for(;j&bit;bit>>=1)j^=bit;j^=bit;if(i<j)std::swap(fft[i],fft[j]);}
            for(int length=2;length<=size;length*=2){const std::complex<float> root(float(std::cos(-2*pi/length)),float(std::sin(-2*pi/length)));
                for(int i=0;i<size;i+=length){std::complex<float> w(1,0);for(int j=0;j<length/2;++j){const auto a=fft[size_t(i+j)],b=w*fft[size_t(i+j+length/2)];fft[size_t(i+j)]=a+b;fft[size_t(i+j+length/2)]=a-b;w*=root;}}}
            const double scale=2.*normalization*normalization/(size*windowPower);
            for(int i=1;i<size/2;++i)channelPower[size_t(i)]=std::norm(fft[size_t(i)])*scale;
            for(int i=1;i<size/2;++i)powers[size_t(i)]=std::max(powers[size_t(i)],channelPower[size_t(i)]);
            for(int h=1;h<=slot.harmonics;++h){const int center=int(std::round(h*slot.frequency*size/slot.rate));if(center<9||center+9>=size/2)continue;
                double signal=0,noise=0;for(int d=-2;d<=2;++d)signal+=channelPower[size_t(center+d)];
                for(int d=5;d<=9;++d)noise+=channelPower[size_t(center-d)]+channelPower[size_t(center+d)];noise*=.5; // five-bin equivalent
                const float db=float(foundation::powerDb(signal));frame.strongestDb=std::max(frame.strongestDb,db);
                if(db>=frame.thresholdDb&&signal>=std::max(1.e-24,noise)*std::pow(10.,1.2))frame.detected=true;
            }
        }
        for(int i=0;i<HumAnalysisFrame::bins;++i){const double low=30*std::pow(50.,double(i)/HumAnalysisFrame::bins),high=30*std::pow(50.,double(i+1)/HumAnalysisFrame::bins);
            const int a=std::max(1,int(std::floor(low*size/slot.rate))),b=std::min(size/2-1,int(std::ceil(high*size/slot.rate)));double power=0;for(int j=a;j<=b;++j)power+=powers[size_t(j)];frame.spectrum[size_t(i)]=float(foundation::powerDb(power));}
        if(frame.epoch==epoch.load()){sequence.fetch_add(1);for(int i=0;i<HumAnalysisFrame::bins;++i)published[size_t(i)].store(frame.spectrum[size_t(i)]);
            strongest.store(frame.strongestDb);threshold.store(frame.thresholdDb);frequency.store(frame.fundamental);seconds.store(frame.seconds);detected.store(frame.detected);harmonics.store(frame.harmonics);endSample.store(frame.endSample);publishedEpoch.store(frame.epoch);sequence.fetch_add(1);}
        slot.state.store(0,std::memory_order_release);return true;
    }
    bool read(HumAnalysisFrame& frame) const noexcept {
        for(int attempt=0;attempt<3;++attempt){const auto before=sequence.load();if(before&1)continue;
            frame.epoch=publishedEpoch.load();for(int i=0;i<HumAnalysisFrame::bins;++i)frame.spectrum[size_t(i)]=published[size_t(i)].load();frame.strongestDb=strongest.load();frame.thresholdDb=threshold.load();frame.fundamental=frequency.load();frame.seconds=seconds.load();frame.detected=detected.load();frame.harmonics=harmonics.load();frame.endSample=endSample.load();
            if(sequence.load()==before&&frame.epoch!=0&&frame.epoch==epoch.load())return true;}
        return false;
    }
private:
    static_assert(std::atomic<float>::is_always_lock_free&&std::atomic<bool>::is_always_lock_free&&std::atomic<int>::is_always_lock_free&&std::atomic<uint64_t>::is_always_lock_free);
    struct Slot {std::atomic<int> state{0};std::array<double,size> left{},right{};float frequency=50,sensitivity=50;int harmonics=8;double rate=4000;uint64_t end=0,epoch=0;};
    std::array<Slot,3> slots;
    std::array<foundation::Section,2> filters{};
    double rate=48000,decimatedRate=4000;int stride=12,count=0,decimation=0,writing=-1,keyHarmonics=0;
    float keyFrequency=-1,keySensitivity=-1;uint64_t sample=0;
    std::array<std::complex<float>,size> fft{};std::array<double,size/2> powers{},channelPower{};
    std::atomic<uint64_t> epoch{0},publishedEpoch{0},endSample{0};std::atomic<unsigned> sequence{0};
    std::array<std::atomic<float>,HumAnalysisFrame::bins> published{};
    std::atomic<float> strongest{-120},threshold{-60},frequency{50},seconds{0};std::atomic<bool> detected{false};std::atomic<int> harmonics{8};
};
class HumAnalysisSource {public:virtual ~HumAnalysisSource()=default;virtual bool readHumAnalysis(HumAnalysisFrame&) const noexcept {return false;}};
}
