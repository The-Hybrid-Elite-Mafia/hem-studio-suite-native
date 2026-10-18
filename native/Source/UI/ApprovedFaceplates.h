#pragma once
#include <JuceHeader.h>
#include <ApprovedArt.h>

namespace hem::ui {
// Immutable approved chassis. Material reference images remain separate: only
// live apertures may differ. Creating controls does not change APVTS defaults.
inline juce::Image approvedFaceplate(int number) {
    switch (number) {
    case 0: return juce::ImageCache::getFromMemory(ApprovedArt::approved_00_png, ApprovedArt::approved_00_pngSize);
    case 1: return juce::ImageCache::getFromMemory(ApprovedArt::approved_01_png, ApprovedArt::approved_01_pngSize);
    case 5: return juce::ImageCache::getFromMemory(ApprovedArt::approved_05_png, ApprovedArt::approved_05_pngSize);
    case 7: return juce::ImageCache::getFromMemory(ApprovedArt::approved_07_png, ApprovedArt::approved_07_pngSize);
    case 8: return juce::ImageCache::getFromMemory(ApprovedArt::approved_08_png, ApprovedArt::approved_08_pngSize);
    case 10: return juce::ImageCache::getFromMemory(ApprovedArt::approved_10_png, ApprovedArt::approved_10_pngSize);
    case 11: return juce::ImageCache::getFromMemory(ApprovedArt::approved_11_png, ApprovedArt::approved_11_pngSize);
    case 12: return juce::ImageCache::getFromMemory(ApprovedArt::approved_12_png, ApprovedArt::approved_12_pngSize);
    case 28: return juce::ImageCache::getFromMemory(ApprovedArt::approved_28_png, ApprovedArt::approved_28_pngSize);
    case 29: return juce::ImageCache::getFromMemory(ApprovedArt::approved_29_png, ApprovedArt::approved_29_pngSize);
    case 30: return juce::ImageCache::getFromMemory(ApprovedArt::approved_30_png, ApprovedArt::approved_30_pngSize);
    case 31: return juce::ImageCache::getFromMemory(ApprovedArt::approved_31_png, ApprovedArt::approved_31_pngSize);
    case 19: return juce::ImageCache::getFromMemory(ApprovedArt::approved_19_png, ApprovedArt::approved_19_pngSize);
    case 20: return juce::ImageCache::getFromMemory(ApprovedArt::approved_20_png, ApprovedArt::approved_20_pngSize);
    case 23: return juce::ImageCache::getFromMemory(ApprovedArt::approved_23_png, ApprovedArt::approved_23_pngSize);
    case 24: return juce::ImageCache::getFromMemory(ApprovedArt::approved_24_png, ApprovedArt::approved_24_pngSize);
    case 2: return juce::ImageCache::getFromMemory(ApprovedArt::approved_02_png, ApprovedArt::approved_02_pngSize);
    case 6: return juce::ImageCache::getFromMemory(ApprovedArt::approved_06_png, ApprovedArt::approved_06_pngSize);
    case 9: return juce::ImageCache::getFromMemory(ApprovedArt::approved_09_png, ApprovedArt::approved_09_pngSize);
    default: return {};
    }
}
} // namespace hem::ui
