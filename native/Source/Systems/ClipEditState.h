#pragma once
#include "PitchTimeClipRenderer.h"
#include <juce_data_structures/juce_data_structures.h>
namespace hem::clipEdit {
inline constexpr size_t maxSerializedBytes=32768;
inline constexpr juce::int64 maxFileBytes=64LL*1024*1024*1024;
struct MediaIdentity {juce::String path,sha256;juce::int64 bytes=0,frames=0;double rate=0;int channels=0;};
struct Recipe {pitchtime::Settings settings;double tempoBpm=120;juce::String backend;juce::int64 sourceGeneration=0;};
// Exactly one whole-stem edit and one undo/redo toggle. No arranger ranges/history.
// Replacing this record with another edit deliberately drops the previous undo.
struct State {int trackIndex=-1;bool renderedActive=true;MediaIdentity original,rendered;Recipe recipe;};
juce::Result validate(const State&); // Pure, no files or host mutation.
juce::Result captureMetadata(const pitchtime::ClipRenderMetadata&,int trackIndex,State& out);
juce::Result encode(const State&,juce::ValueTree& out);
juce::Result decode(const juce::ValueTree&,State& out);
// Off-thread checks BOTH identities, including inactive undo media; no relinking,
// mutation or source deletion. Bounded streaming hash, cancellable between reads.
juce::Result verifyFiles(const State&,const pitchtime::Cancel& = {});
bool sameIdentity(const MediaIdentity&,const MediaIdentity&) noexcept;
const MediaIdentity& activeMedia(const State&) noexcept;
// Prepare only. Caller must recheck track generation and atomically publish its
// newly prepared playback+state. Outputs unchanged on failure. Repeated Undo or
// repeated Redo fails; one opposite transition remains available indefinitely.
juce::Result stageToggle(const State&,bool useRendered,const MediaIdentity& current,
                         State& next,MediaIdentity& target,const pitchtime::Cancel& = {});
}
