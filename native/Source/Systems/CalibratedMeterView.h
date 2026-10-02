#pragma once
#include <JuceHeader.h>
#include "AudioMeters.h"

namespace hem {
// UI-only view: opaque background covers the faceplate's decorative meter area.
// Heights use a fixed -60..0 dBFS scale, never normalization to the current audio.
class CalibratedMeterView : public juce::Component, private juce::Timer {
public:
    explicit CalibratedMeterView(AudioMeters& source) : meters(source) {
        setOpaque(true);
        setName("Final audio output sample peak and RMS meters");
        setWantsKeyboardFocus(true);
        data = meters.snapshot(); // First paint reflects already prepared/processed audio.
        startTimerHz(25);
    }
    ~CalibratedMeterView() override { stopTimer(); }

    void paint(juce::Graphics& g) override {
        g.fillAll(juce::Colour(0xff061017));
        const auto bounds = getLocalBounds().toFloat();
        if (bounds.getWidth() < 60 || bounds.getHeight() < 100) return;
        const float sx = bounds.getWidth() / 190.0f, sy = bounds.getHeight() / 310.0f;
        const float font = juce::jlimit(8.0f, 14.0f, 12.0f * juce::jmin(sx, sy));
        const auto plot = juce::Rectangle<float>(40 * sx, 52 * sy, 136 * sx, 186 * sy);
        const auto yFor = [&](float db) { return plot.getBottom() - plot.getHeight() * juce::jlimit(0.0f, 1.0f, (db + 60.0f) / 60.0f); };
        g.setColour(juce::Colour(0xff00a7be));
        g.drawRoundedRectangle(bounds.reduced(1), 4, 1);
        g.setFont(juce::Font(juce::FontOptions(font).withStyle("Bold")));
        g.setColour(juce::Colour(0xffd5faff));
        g.drawText("OUTPUT / dBFS", 0, int(6 * sy), getWidth(), int(19 * sy), juce::Justification::centred);
        g.setFont(juce::Font(juce::FontOptions(font * 0.87f)));
        g.drawText("SAMPLE PEAK + RMS", 0, int(25 * sy), getWidth(), int(15 * sy), juce::Justification::centred);
        for (const float tick : {0.0f, -6.0f, -12.0f, -24.0f, -48.0f, -60.0f}) {
            const float y = yFor(tick);
            g.setColour(juce::Colour(0xff25404b));
            g.drawLine(plot.getX(), y, plot.getRight(), y, 1.0f);
            g.setColour(juce::Colour(0xff8aa6b3));
            g.drawText(juce::String(int(tick)), int(3 * sx), int(y - 7 * sy), int(31 * sx), int(14 * sy), juce::Justification::centredRight);
        }
        drawChannel(g, data.left, juce::Rectangle<float>(51 * sx, plot.getY(), 43 * sx, plot.getHeight()), "L");
        drawChannel(g, data.right, juce::Rectangle<float>(121 * sx, plot.getY(), 43 * sx, plot.getHeight()), "R");
        g.setColour(juce::Colour(0xffd5faff));
        g.setFont(juce::Font(juce::FontOptions(font * 0.90f)));
        g.drawText("PEAK  " + reading(data.left.peakDb) + " / " + reading(data.right.peakDb),
                   2, int(254 * sy), getWidth() - 4, int(17 * sy), juce::Justification::centred);
        g.setColour(juce::Colour(0xff55dccc));
        g.drawText("RMS   " + reading(data.left.rmsDb) + " / " + reading(data.right.rmsDb),
                   2, int(271 * sy), getWidth() - 4, int(17 * sy), juce::Justification::centred);
        const bool warning = data.left.clipped || data.right.clipped || data.nonFiniteSamples > 0;
        g.setColour(warning ? juce::Colour(0xffff8a55) : juce::Colour(0xff8aa6b3));
        g.drawText(!data.ready ? "NO AUDIO" : data.nonFiniteSamples > 0 ? "INVALID / CLICK RESET" : warning ? "CLIP / CLICK RESET" : "1s HOLD / CLICK RESET",
                   0, int(291 * sy), getWidth(), int(15 * sy), juce::Justification::centred);
        if (hasKeyboardFocus(true)) {
            g.setColour(juce::Colours::white);
            g.drawRoundedRectangle(bounds.reduced(3), 3, 1);
        }
    }
    void mouseDown(const juce::MouseEvent&) override { meters.resetClip(); }
    bool keyPressed(const juce::KeyPress& key) override {
        if (key == juce::KeyPress::returnKey || key == juce::KeyPress::spaceKey) {
            meters.resetClip();
            return true;
        }
        return false;
    }

private:
    static juce::String reading(float db) { return db <= AudioMeters::silenceFloorDb ? juce::String("-inf") : juce::String(db, 1); }
    void drawChannel(juce::Graphics& g, const AudioMeters::ChannelSnapshot& channel,
                     juce::Rectangle<float> lane, const juce::String& name) const {
        const auto yFor = [&](float db) { return lane.getBottom() - lane.getHeight() * juce::jlimit(0.0f, 1.0f, (db + 60.0f) / 60.0f); };
        g.setColour(juce::Colour(0xff02070b));
        g.fillRect(lane);
        g.setColour(juce::Colour(0xff235767));
        g.fillRect(lane.withTop(yFor(channel.peakDb)));
        g.setColour(juce::Colour(0xff35baa9));
        g.fillRect(lane.reduced(lane.getWidth() * 0.20f, 0).withTop(yFor(channel.rmsDb)));
        if (channel.peakDb > -60) {
            g.setColour(juce::Colour(0xff52e7ff));
            g.drawLine(lane.getX(), yFor(channel.peakDb), lane.getRight(), yFor(channel.peakDb), 2);
        }
        if (channel.heldPeakDb > -60) {
            g.setColour(juce::Colour(0xffffbf60));
            g.drawLine(lane.getX(), yFor(channel.heldPeakDb), lane.getRight(), yFor(channel.heldPeakDb), 2);
        }
        g.setColour(channel.clipped ? juce::Colour(0xffff504a) : juce::Colour(0xff28353d));
        g.fillRoundedRectangle(lane.getX(), lane.getY() - 8, lane.getWidth(), 4, 1);
        g.setColour(juce::Colour(0xffd5faff));
        g.drawText(name, int(lane.getX()), int(lane.getBottom() + 3), int(lane.getWidth()), 14, juce::Justification::centred);
    }
    void timerCallback() override { data = meters.snapshot(); repaint(); }
    AudioMeters& meters;
    AudioMeters::Snapshot data;
};
} // namespace hem
