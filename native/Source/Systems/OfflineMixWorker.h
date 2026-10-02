#pragma once
#include "OfflineMixRenderer.h"
#include "../Plugins/ExternalPluginHost.h"

namespace hem {
// Owns a frozen snapshot. External lifecycle staging happens on the message
// timer, one instance per tick. Render/file work and adapter retirement stay on
// the worker; actual VST3 owners remain in ExternalPluginHost until main collects.
class OfflineMixWorker final : private juce::Thread, private juce::Timer {
public:
    using Report = std::function<void(const juce::String&)>;
    explicit OfflineMixWorker(Report report,ExternalPluginHost* host=nullptr)
        : juce::Thread("HEM offline mix"), reportResult(std::move(report)), externalHost(host) { startTimerHz(20); }
    ~OfflineMixWorker() override { stopTimer(); cancel(); waitForThreadToExit(-1); }
    bool start(SessionSnapshot state, juce::File destination, OfflineMixOptions settings) {
        if (isBusy()) return false;
        // External preparation precedes the renderer's validation. Reject bad
        // values here before converting the rate-derived latency to an integer.
        if(!std::isfinite(settings.sampleRate)||settings.sampleRate<8000||settings.sampleRate>384000
           ||std::floor(settings.sampleRate)!=settings.sampleRate||settings.blockSize<1||settings.blockSize>8192
           ||!std::isfinite(settings.tailSeconds)||settings.tailSeconds<0||settings.tailSeconds>3600){
            outcome="Invalid export rate, block size or tail; no plug-in was prepared";return false;
        }
        bool hasExternal=false;
        for (auto& track : state.tracks)
            for (auto& slot : track.slots) {slot.parameters=slot.parameters.createCopy();hasExternal|=slot.pluginId=="external-vst3";}
        if(hasExternal&&!externalHost){outcome="External VST3 export requires a message-thread host";return false;}
        state.monitorParameters = state.monitorParameters.createCopy();
        snapshot = std::move(state); file = std::move(destination); options = settings;
        preparedExternal=std::make_unique<OfflinePreparedPlugins>();nextExternal=0;preparingExternal=hasExternal;
        cancelRequested=false;progressValue.store(0); finished.store(false); busy.store(true);
        outcome = hasExternal?"Preparing private VST3 export instances / cancel available":"Rendering frozen mix snapshot";
        if(!preparingExternal&&!startThread()){busy=false;outcome="Cannot start export worker";return false;}
        return true;
    }
    void cancel() noexcept { cancelRequested=true;signalThreadShouldExit(); }
    bool isBusy() const noexcept { return busy.load(std::memory_order_acquire); }
    double progress() const noexcept { return progressValue.load(std::memory_order_relaxed); }
    const juce::String& result() const noexcept { return outcome; }
private:
    void run() override {
        rendered = OfflineMixRenderer::render(snapshot, file, options,
            [this] { return cancelRequested.load()||threadShouldExit(); },
            [this](double value) { progressValue.store(value, std::memory_order_relaxed); },preparedExternal.get());
        finished.store(true, std::memory_order_release);
    }
    void finishStaging(const juce::String& error){
        preparingExternal=false;preparedExternal.reset();busy=false;outcome="Export unchanged: "+error;
        if(externalHost)externalHost->collectGarbage();if(reportResult)reportResult(outcome);
    }
    void timerCallback() override {
        if(preparingExternal){
            if(cancelRequested.load()){finishStaging("Export cancelled before processing");return;}
            while(nextExternal<13*8){const int index=nextExternal++;const auto& slot=snapshot.tracks[size_t(index/8)].slots[size_t(index%8)];
                if(slot.pluginId!="external-vst3")continue;
                juce::String error;
                auto processor=externalHost->stage(slot.external,options.sampleRate,options.blockSize,int(std::ceil(options.sampleRate*.006)),true,error);
                if(!processor){finishStaging(error);return;}
                preparedExternal->slots[size_t(index/8)][size_t(index%8)]=std::move(processor);
                return; // Permit UI cancellation between potentially slow vendor instances.
            }
            preparingExternal=false;
            if(cancelRequested.load()){finishStaging("Export cancelled before processing");return;}
            outcome="Rendering frozen mix snapshot";
            if(!startThread()){finishStaging("Cannot start export worker");return;}
        }
        if (!finished.load(std::memory_order_acquire) || isThreadRunning()) return;
        finished.store(false); busy.store(false);preparedExternal.reset();
        if(externalHost)externalHost->collectGarbage();
        outcome = rendered.status.wasOk()
            ? "Saved " + file.getFileName() + " / " + juce::String(rendered.frames) + " frames / " + juce::String(rendered.latencyRemoved) + " latency samples removed"
            : "Export unchanged: " + rendered.status.getErrorMessage();
        if (reportResult) reportResult(outcome);
    }
    Report reportResult;
    ExternalPluginHost* externalHost=nullptr;
    std::unique_ptr<OfflinePreparedPlugins> preparedExternal;
    SessionSnapshot snapshot;
    juce::File file;
    OfflineMixOptions options;
    OfflineMixResult rendered{juce::Result::ok()};
    juce::String outcome{"Ready to export"};
    bool preparingExternal=false;int nextExternal=0;
    std::atomic<bool> busy{false}, finished{false},cancelRequested{false};
    std::atomic<double> progressValue{0};
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(OfflineMixWorker)
};
} // namespace hem
