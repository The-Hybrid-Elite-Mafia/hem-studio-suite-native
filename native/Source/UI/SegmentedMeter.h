#pragma once
// NEW implementation. Message-thread painting only; no processor/engine state.
#include <juce_gui_basics/juce_gui_basics.h>
#include "SegmentedMeterModel.h"

namespace hem::ui {
inline void assertMeterPaintThread() {
    // GUI hosts already own the message manager; do not create one here.
    if (auto* manager = juce::MessageManager::getInstanceWithoutCreating())
        jassert(manager->isThisTheMessageThread());
}
inline MeterRect meterRect(juce::Rectangle<float> r) noexcept {
    return {r.getX(), r.getY(), r.getWidth(), r.getHeight()};
}
inline juce::Rectangle<float> juceMeterRect(MeterRect r) noexcept {
    return {r.x, r.y, r.width, r.height};
}
inline void paintSegmentedMeter(juce::Graphics& g, juce::Rectangle<float> lane,
                                float normalized, bool valid, bool reduction = false,
                                int segmentCount = defaultMeterSegments) {
    assertMeterPaintThread();
    const auto model = meterSegments(meterRect(lane), normalized, valid, reduction, segmentCount);
    if (model.count == 0) return;
    const juce::Graphics::ScopedSaveState saved(g);
    g.setColour(juce::Colour(0xff020609));
    g.fillRect(lane);
    for (int i = 0; i < model.count; ++i) {
        auto colour = juce::Colour(0xff10232b);
        if (model.lit(i)) {
            const float db = meterFloorDb + 60.0f * static_cast<float>(i+1) / model.count;
            colour = reduction ? juce::Colour(0xffffbd59)
                     : db >= -3.0f ? juce::Colour(0xffff655b)
                     : db >= -12.0f ? juce::Colour(0xffffbd59)
                                    : juce::Colour(0xff41d7c5);
        }
        g.setColour(colour);
        g.fillRect(juceMeterRect(model.segment(i)));
    }
}
inline void paintMeterMark(juce::Graphics& g, juce::Rectangle<float> lane,
                          float normalized, juce::Colour colour, float thickness,
                          MeterMarkRole role, bool valid = true) {
    assertMeterPaintThread();
    const auto marks = meterMarks(meterRect(lane), normalized, valid, role, thickness);
    if (!marks.visible) return;
    const juce::Graphics::ScopedSaveState saved(g);
    g.setColour(colour);
    g.fillRect(juceMeterRect(marks.first));
    if (usableMeterRect(marks.second)) g.fillRect(juceMeterRect(marks.second));
    if (usableMeterRect(marks.highlight)) {
        g.setColour(juce::Colours::white);
        g.fillRect(juceMeterRect(marks.highlight));
    }
}
// Compatibility with recovered workspace patch: RMS used 1.5f, hold used 1.f.
// New callers should use the explicit role overload above.
inline void paintMeterMark(juce::Graphics& g, juce::Rectangle<float> lane,
                          float normalized, juce::Colour colour, float thickness = 1.5f) {
    paintMeterMark(g, lane, normalized, colour, thickness,
                  thickness > 1.0f ? MeterMarkRole::rms : MeterMarkRole::peakHold);
}
} // namespace hem::ui
