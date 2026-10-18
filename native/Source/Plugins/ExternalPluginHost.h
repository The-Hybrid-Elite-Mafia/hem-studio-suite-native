#pragma once
#include "Processors.h"
#include "ExternalPluginState.h"

namespace hem {
class ExternalInstanceOwner;

// Explicit, in-process, selected-file VST3 hosting. No directory scanning,
// downloads, background instantiation, or crash-isolation claim is made.
// All public operations and destruction MUST run on the JUCE message thread.
// Keep this service alive until workers have joined, editors have closed, and
// all chain adapters are destroyed; then collectGarbage() on that thread.
class ExternalPluginHost final {
public:
    ExternalPluginHost();
    ~ExternalPluginHost();

    juce::Array<juce::PluginDescription> discoverSelected(const juce::File&, juce::String& error);
    std::unique_ptr<ModuleProcessor> stage(const ExternalPluginState&, double sampleRate,
                                         int maximumBlock, int latencyBudget, bool nonRealtime,
                                         juce::String& error);
    // The caller must first gate the adapter and establish audio/worker
    // quiescence. A gate alone does not wait for an already executing block.
    static bool capture(ModuleProcessor&, ExternalPluginState&, juce::String& error);
    void collectGarbage();
    size_t retainedInstanceCount() const noexcept {return owners.size();}

private:
    juce::AudioPluginFormatManager formats;
    std::vector<std::shared_ptr<ExternalInstanceOwner>> owners;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ExternalPluginHost)
};
} // namespace hem
