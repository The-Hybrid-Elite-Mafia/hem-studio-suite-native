#pragma once
#include <JuceHeader.h>
namespace hem::ui {
inline constexpr int moduleEditorWidth=1280,moduleEditorHeight=720;
inline juce::Rectangle<float> moduleMeterPanel() noexcept {return {230,128,910,98};}
inline juce::Rectangle<float> moduleResponsePanel() noexcept {auto a=moduleMeterPanel();return a.withWidth(a.getWidth()*.52f);}
inline juce::Rectangle<float> moduleMeterReadoutBounds(bool eq) noexcept {auto a=moduleMeterPanel();return eq?a.withTrimmedLeft(moduleResponsePanel().getWidth()+8):a;}
}
