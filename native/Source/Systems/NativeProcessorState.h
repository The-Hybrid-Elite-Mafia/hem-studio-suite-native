#pragma once
#include "../Plugins/Processors.h"
#include "../Plugins/NativeResourceStateProvider.h"

namespace hem {
// Non-audio-thread whole-state helpers. Fallible outputs are unchanged on error.
// Binary helpers are a bounded future-provider envelope, not overrides of any
// existing processor's get/setStateInformation methods. The new binary envelope
// is exact UTF-8 XML bytes with no NUL terminator or JUCE binary framing.
class NativeProcessorState final {
public:
    static constexpr size_t maxBinaryBytes=1024*1024;
    static juce::Result capture(ModuleProcessor&,NativeProcessorSnapshot& out);
    static juce::Result validate(ModuleProcessor&,const NativeProcessorSnapshot&);
    // Check again AFTER the host's normal stageNode/prepareToPlay and before
    // installing the private node. Legacy processors keep their existing rules.
    static juce::Result validatePreparedForPublication(ModuleProcessor&);
    static NativeProcessorSnapshot deepCopy(const NativeProcessorSnapshot&);
    // Takes ownership of a FRESH, UNPUBLISHED candidate. It is destroyed on any
    // failure; output remains unchanged. Never pass a live processor here.
    static juce::Result restoreUnpublished(std::unique_ptr<ModuleProcessor> candidate,
        const NativeProcessorSnapshot&,const NativeRestoreContext&,
        std::unique_ptr<ModuleProcessor>& out);
    static juce::Result encodeBinary(ModuleProcessor&,const NativeProcessorSnapshot&,juce::MemoryBlock& out);
    static juce::Result decodeBinary(ModuleProcessor&,const void*,size_t,NativeProcessorSnapshot& out);
};
} // namespace hem
