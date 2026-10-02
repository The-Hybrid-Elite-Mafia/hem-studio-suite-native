#pragma once
#include <JuceHeader.h>
#include <optional>
#include "../SessionState.h"
#include "../Systems/AudioMeters.h"

namespace hem {
// Message-thread, single-window views over the host's existing engine. Navigation
// changes visibility only; no view starts/stops audio or owns a TrackBank.
class NativeWorkspace final : public juce::Component, private juce::Timer {
public:
    enum class Page { plugin, mixer, vault, routing };
    struct RouteTelemetry {
        AudioMeters::Snapshot output;
        bool headphoneCorrection = false;
        bool monitorReady = false;
        juce::String calibrationName, outputDevice;
        double sampleRate = 0;
        int blockSize = 0, hardwareOutputs = 0, limiterLatencySamples = 0;
        std::optional<float> limiterReductionDb; // absent unless the engine really publishes GR
    };
    struct Callbacks {
        std::function<TrackBank&()> bank;
        std::function<int()> getSelectedTrack, getSelectedSlot;
        std::function<void(int)> setSelectedTrack, setSelectedSlot;
        std::function<void(int)> onOpenSlot, onInstallModule;
        std::function<void()> onMonitor;
        std::function<void()> onLoadVst3, onReloadVst3;
        std::function<RouteTelemetry()> routeTelemetry;
    };
    explicit NativeWorkspace(Callbacks);
    ~NativeWorkspace() override;
    void setPage(Page);
    Page page() const noexcept;
    void refresh();
    // The editor borrows its processor. The HOST MUST clear it before removing,
    // replacing or retiring that processor or its TrackBank (even when hidden).
    void setEditor(std::unique_ptr<juce::AudioProcessorEditor>);
    void clearEditor();
    bool hasEditor() const noexcept;
    void setPluginExpanded(bool);
    bool pluginExpanded() const noexcept;
    void setSplitProportion(float); // bounded to .22 .. .60; left-pane fraction
    float splitProportion() const noexcept;
    void setVaultFilter(const juce::String& search, const juce::String& category = "All categories");
    std::vector<int> filteredRegistryIndices() const;
    void paint(juce::Graphics&) override;
    void resized() override;
private:
    struct Impl;
    std::unique_ptr<Impl> impl;
    void timerCallback() override;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NativeWorkspace)
};
} // namespace hem
