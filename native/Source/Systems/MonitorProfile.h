#pragma once
#include <juce_events/juce_events.h>
#include "../DSP/MonitoringRouter.h"
namespace hem {
struct MonitorProfile {
    enum class Filter {Bell,LowShelf,HighShelf};
    struct Band {Filter type=Filter::Bell;float hz=1000,q=.7071f,db=0;};
    juce::String name,device,connection,deviceMode,measurementSource;
    float preampDb=0;
    std::vector<Band> bands;
    static MonitorProfile load(const juce::File&);
    static void save(const MonitorProfile&,const juce::File&);
    MonitoringRouter::Curve prepare(double sampleRate) const;
};
// JSON I/O and coefficient generation stay away from GUI/audio callbacks.
class MonitorProfileLoader final : private juce::Thread,private juce::Timer {
public:
    using Ready=std::function<void(const MonitorProfile&,const MonitoringRouter::Curve&,unsigned)>;
    using Error=std::function<void(const juce::String&)>;
    MonitorProfileLoader(Ready,Error);~MonitorProfileLoader() override;
    bool load(const juce::File&,double sampleRate,unsigned deviceGeneration);
    bool isBusy() const noexcept {return busy.load();}
private:
    void run() override;void timerCallback() override;
    Ready ready;Error errorHandler;
    juce::File file;double rate=96000;unsigned generation=0;
    MonitorProfile profile;MonitoringRouter::Curve curve;juce::String error;
    std::atomic<bool> busy{false},done{false};
};
}
