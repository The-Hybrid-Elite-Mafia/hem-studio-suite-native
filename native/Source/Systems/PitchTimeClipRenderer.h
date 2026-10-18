#pragma once
#include "../DSP/PitchTimeBackend.h"
#include <juce_audio_formats/juce_audio_formats.h>

namespace hem::pitchtime {
struct ClipRenderRequest {
    juce::File source, destination;
    juce::String expectedSourceSha256;
    juce::int64 sourceGeneration = 0, startFrame = 0, frameCount = 0;
    Settings settings;
    double capturedTempoBpm = 120;
};
struct ClipRenderMetadata {
    juce::String sourceSha256, renderedSha256, recipeJson, recipeSha256;
    juce::File source, rendered;
    juce::int64 sourceBytes = 0, renderedBytes = 0, sourceFrames = 0, renderedFrames = 0;
    juce::int64 sourceGeneration = 0, startFrame = 0, frameCount = 0;
    double sampleRate = 0;
    int channels = 0;
};
struct ClipRenderResult {
    enum class Status { failed, cancelled, complete };
    Status status = Status::failed;
    juce::String error;
    ClipRenderMetadata metadata;
    Result backend;
};
// Synchronous OFFLINE WORKER service. No host mutation, playback, audition or
// source deletion. Caller commits prepared playback only if sourceGeneration
// still matches, and stores original/rendered identities for Undo. Successful
// output is already published; failed/cancelled work never owns destination.
class PitchTimeClipRenderer {
public:
    static juce::File uniqueDestination(const juce::File& existingDirectory);
    static ClipRenderResult render(const ClipRenderRequest&, const Cancel& = {}, const CorrectionObserver& = {});
};
}
