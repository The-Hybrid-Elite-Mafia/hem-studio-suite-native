#pragma once
#include "../SessionState.h"

namespace hem {
// Disk operations run on SessionWorker (or another non-audio worker).
// XML and JSON represent the SAME validated 13 x 8 session schema.
class PresetManager final {
public:
    static void save(const SessionSnapshot&,const juce::File&);
    static SessionSnapshot load(const juce::File&);
    static juce::Array<juce::File> list(const juce::File& directory);
    static juce::Result rename(const juce::File&,const juce::String& newBaseName);
    static juce::Result remove(const juce::File&);
    static juce::var toJson(const juce::ValueTree&);
    static juce::ValueTree fromJson(const juce::var&,int depth=0);
private:
    static bool supported(const juce::File&);
};
}
