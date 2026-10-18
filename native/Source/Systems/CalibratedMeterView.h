#pragma once
#include <JuceHeader.h>
#include "AudioMeters.h"
#include "../UI/SegmentedMeter.h"

namespace hem {
struct CalibratedHostMeterTestAccess;
// NEW host-view integration, not recovered source. All state here is GUI-only.
// The opaque view replaces baked faceplate LEDs with actual independent stereo
// sample peaks, the detector's 300 ms RMS and its 1 second sample-peak hold.
class CalibratedMeterView : public juce::Component, private juce::Timer {
public:
    explicit CalibratedMeterView(AudioMeters& source,
                                 juce::String title = "OUTPUT / dBFS",
                                 juce::String subtitle = "SAMPLE PEAK + RMS")
        : meters(source), heading(std::move(title)), subheading(std::move(subtitle)) {
        setOpaque(true);
        setName(heading + ": " + subheading);
        setDescription("Independent left and right sample peak and 300 millisecond RMS in dBFS; "
                       "one second peak hold. Click, Return or Space resets clip and invalid sample latches.");
        setWantsKeyboardFocus(true);
        refresh(juce::Time::getMillisecondCounterHiRes());
        startTimerHz(25);
    }
    ~CalibratedMeterView() override { stopTimer(); }

    void paint(juce::Graphics& g) override {
        // Always cover the entire LED aperture, even at unsupported tiny sizes.
        g.fillAll(juce::Colour(0xff061017));
        const auto bounds = getLocalBounds().toFloat();
        if (bounds.getWidth() < 60 || bounds.getHeight() < 95) return;
        const auto layout = geometry();
        const bool live = state == State::live;
        g.setColour(juce::Colour(0xff00a7be));
        g.drawRoundedRectangle(bounds.reduced(1), 4, 1);
        g.setColour(juce::Colour(0xffd5faff));
        g.setFont(juce::Font(juce::FontOptions(layout.font).withStyle("Bold")));
        g.drawFittedText(heading, layout.title, juce::Justification::centred, 1, .75f);
        g.setFont(juce::Font(juce::FontOptions(layout.font * .86f)));
        g.drawFittedText(subheading, layout.subtitle, juce::Justification::centred, 1, .75f);

        const auto yFor = [&](float db) { return layout.left.getBottom()
            - layout.left.getHeight() * ui::meterDbPosition(db); };
        g.setFont(juce::Font(juce::FontOptions(layout.font * .86f)));
        for (float tick : {0.f, -6.f, -12.f, -24.f, -48.f, -60.f}) {
            // Compact lanes have only the endpoint/reference ticks to avoid collisions.
            if (layout.left.getHeight() < 110.f && tick != 0.f && tick != -24.f && tick != -60.f) continue;
            if (layout.left.getHeight() < 38.f && tick == -24.f) continue;
            const float y = yFor(tick);
            g.setColour(juce::Colour(0xff25404b));
            g.drawLine(layout.left.getX(), y, layout.right.getRight(), y, 1.f);
            g.setColour(juce::Colour(0xff8aa6b3));
            g.drawText(juce::String(int(tick)), 2, int(y - layout.row * .5f),
                       int(layout.left.getX()) - 6, layout.row, juce::Justification::centredRight);
        }
        drawChannel(g, data.left, layout.left, live);
        drawChannel(g, data.right, layout.right, live);
        g.setColour(juce::Colour(0xffd5faff));
        g.setFont(juce::Font(juce::FontOptions(layout.font * .9f)));
        drawPair(g, layout, 0, "", "L", "R");
        drawPair(g, layout, 1, "PK", live ? reading(data.left.peakDb) : "--",
                                           live ? reading(data.right.peakDb) : "--");
        g.setColour(juce::Colour(0xff55dccc));
        drawPair(g, layout, 2, "RMS", live ? reading(data.left.rmsDb) : "--",
                                            live ? reading(data.right.rmsDb) : "--");
        if (!layout.compact) {
            g.setColour(juce::Colour(0xff8aa6b3));
            g.setFont(juce::Font(juce::FontOptions(layout.font * .78f)));
            g.drawFittedText("300ms RMS / 1s HOLD", layout.info, juce::Justification::centred, 1, .75f);
        }
        // Clip/invalid are historical latches, so retain their warning even when
        // publication goes stale; bars/digits still require current samples.
        const bool warning = data.left.clipped || data.right.clipped || data.nonFiniteSamples > 0;
        g.setColour(warning ? juce::Colour(0xffff8a55) : juce::Colour(0xff8aa6b3));
        g.setFont(juce::Font(juce::FontOptions(layout.font * .78f)));
        g.drawFittedText(statusText(), layout.status, juce::Justification::centred, 1, .70f);
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
    friend struct CalibratedHostMeterTestAccess;
    enum class State { unavailable, waiting, live, stale };
    struct Layout {
        juce::Rectangle<int> title, subtitle, info, status;
        juce::Rectangle<float> left, right;
        float font = 8;
        int row = 10, readoutY = 0;
        bool compact = false;
    };
    Layout geometry() const {
        Layout r;
        const int w = getWidth(), h = getHeight();
        r.compact = h < 180;
        r.font = r.compact ? 8.f : juce::jlimit(8.f, 14.f, w * 12.f / 149.f);
        r.row = int(std::ceil(r.font * 1.3f));
        const int titleHeight = r.row + 2;
        r.title = {3, 4, w - 6, titleHeight};
        r.subtitle = {3, 4 + titleHeight, w - 6, r.row};
        const float top = float(r.subtitle.getBottom() + 9);
        const int footerRows = r.compact ? 4 : 5;
        const float bottom = float(h - footerRows * r.row - 5);
        const float left = juce::jlimit(20.f, 30.f, w * .18f);
        const float gap = juce::jlimit(5.f, 10.f, w * .06f);
        const float width = (w - left - gap - 9.f) * .5f;
        r.left = {left, top, width, juce::jmax(1.f, bottom - top)};
        r.right = r.left.translated(width + gap, 0.f);
        r.readoutY = int(r.left.getBottom()) + 1;
        r.info = {3, r.readoutY + 3 * r.row, w - 6, r.row};
        r.status = {3, r.readoutY + (r.compact ? 3 : 4) * r.row, w - 6, r.row};
        return r;
    }
    static juce::String reading(float db) {
        return db <= AudioMeters::silenceFloorDb ? juce::String("-inf") : juce::String(db, 1);
    }
    void drawPair(juce::Graphics& g, const Layout& layout, int row,
                  const juce::String& label, const juce::String& left, const juce::String& right) const {
        const int y = layout.readoutY + row * layout.row;
        g.drawFittedText(label, 2, y, int(layout.left.getX()) - 4, layout.row,
                         juce::Justification::centredRight, 1, .75f);
        g.drawFittedText(left, int(layout.left.getX()), y, int(layout.left.getWidth()), layout.row,
                         juce::Justification::centred, 1, .75f);
        g.drawFittedText(right, int(layout.right.getX()), y, int(layout.right.getWidth()), layout.row,
                         juce::Justification::centred, 1, .75f);
    }
    void drawChannel(juce::Graphics& g, const AudioMeters::ChannelSnapshot& channel,
                     juce::Rectangle<float> lane, bool live) const {
        ui::paintSegmentedMeter(g, lane, ui::meterDbPosition(channel.peakDb), live);
        ui::paintMeterMark(g, lane, ui::meterDbPosition(channel.rmsDb), juce::Colour(0xff55dccc),
                          1.5f, ui::MeterMarkRole::rms, live);
        ui::paintMeterMark(g, lane, ui::meterDbPosition(channel.heldPeakDb), juce::Colour(0xffffbf60),
                          1.f, ui::MeterMarkRole::peakHold, live);
        g.setColour(channel.clipped ? juce::Colour(0xffff504a) : juce::Colour(0xff28353d));
        g.fillRoundedRectangle(lane.getX(), lane.getY() - 6, lane.getWidth(), 3, 1);
    }
    juce::String statusText() const {
        const juce::String stateText = state == State::unavailable ? "NO AUDIO"
                                    : state == State::waiting ? "WAITING"
                                    : state == State::stale ? "STALE" : "LIVE";
        if (data.nonFiniteSamples > 0) return stateText + " / INVALID / RESET";
        if (data.left.clipped || data.right.clipped) return stateText + " / CLIP / RESET";
        return stateText;
    }
    void refresh(double nowMs) {
        const auto next = meters.snapshot();
        // A failed bounded read has no coherent publication. Never replace the
        // last coherent baseline with its empty payload and falsely count the
        // following unchanged snapshot as new audio. An unprepared first read
        // also stays dark until a coherent baseline can be established.
        if (next.publication == 0) { state = State::unavailable; return; }
        // Seed an observation baseline without presenting a frozen old snapshot
        // as current audio when this view is first opened or reopened.
        if (!observed) {
            observed = true;
            data = next;
            state = data.ready ? State::waiting : State::unavailable;
            hasFreshSamples = false;
            lastAdvanceMs = nowMs;
            return;
        }
        const bool generationChanged = next.generation != data.generation;
        const bool advanced = next.processedSamples != data.processedSamples;
        if (generationChanged || !next.ready || next.processedSamples == 0) hasFreshSamples = false;
        if (next.ready && next.processedSamples > 0 && (generationChanged || advanced)) {
            lastAdvanceMs = nowMs;
            hasFreshSamples = true;
        }
        data = next;
        state = !data.ready ? State::unavailable : !hasFreshSamples ? State::waiting
              : nowMs - lastAdvanceMs >= 1000.0 ? State::stale : State::live;
    }
    void timerCallback() override { refresh(juce::Time::getMillisecondCounterHiRes()); repaint(); }
    AudioMeters& meters;
    juce::String heading, subheading;
    AudioMeters::Snapshot data;
    State state = State::unavailable;
    double lastAdvanceMs = 0;
    bool observed = false, hasFreshSamples = false;
};
} // namespace hem
