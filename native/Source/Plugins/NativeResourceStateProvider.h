#pragma once
#include <JuceHeader.h>
#include "../Systems/NativeResourceState.h"
#include <cstdint>
#include <functional>
#include <optional>

namespace hem {
struct NativeProcessorSnapshot {
    juce::ValueTree parameters{"PARAMETERS"};
    std::optional<nativeResource::State> native;
    // Explicit Reference Match auxiliary state; bounded/decoded by its provider.
    // Never place this tree inside numeric PARAMETERS. deepCopy clones it.
    std::optional<juce::ValueTree> matchProfile;
    // Coherent committed generation, useful for dirty checks; never serialized.
    uint64_t committedRevision=0;
};
struct NativeRestoreContext {
    double sampleRate=0;
    int blockSize=0;
    double tempoBpm=120;
    bool nonRealtime=false;
    std::function<bool()> cancelled;
};

// Optional interface, deliberately separate from ModuleProcessor. None of the
// existing 35 engines implement it. Convolution's legacy persistence is intact.
// All methods run off the audio callback. Capture must atomically collect scalar
// parameters, resource identities and revision from ONE committed generation.
// Any pending decode/capture/refit/publication/transition must fail immediately:
// no waiting and no fallback to an older generation. A successful output must
// remain stable after return, with no mutable aliases to provider-owned trees.
// The provider must enforce
// this inside tryCapture as well as exposing the advisory pending-work query.
class NativeResourceStateProvider {
public:
    virtual ~NativeResourceStateProvider()=default;
    // Lifetime-stable, immutable layout supplied by code, never candidate data.
    virtual const nativeResource::ExpectedLayout& expectedNativeResourceLayout() const noexcept=0;
    virtual bool hasPendingNativeStateWork() const noexcept=0;
    // Host calls immediately before publishing a prepared node. This only seals
    // private-candidate mutation; no allocation, I/O, locking or resource work.
    // Legacy/synthetic providers with no mutable loading API need no action.
    virtual void sealNativeStateForPublication() noexcept {}
    virtual juce::Result tryCaptureNativeState(NativeProcessorSnapshot& out)=0;
    // Pure validation: no parameter changes, resource I/O or publication.
    virtual juce::Result validateNativeState(const NativeProcessorSnapshot&) const=0;
    // ONLY a freshly constructed, unpublished processor may receive this call.
    // The provider owns scalar+resource restoration, decoding, hashing and all
    // preparation. Failure may alter the candidate; caller must discard it.
    // Success must be fully settled before the caller stages the private node.
    // The host will then call normal prepareToPlay; repeated preparation must
    // preserve this committed state and return synchronously ready/settled.
    // Session/export routes recheck readiness after that host preparation.
    virtual juce::Result restoreNativeStateOffThread(const NativeProcessorSnapshot&,
                                                    const NativeRestoreContext&)=0;
};
} // namespace hem
