#pragma once
// SPDX-License-Identifier: AGPL-3.0-or-later
// Projected mesh adapted from HEM Stem Workbench. See ATTRIBUTION.md.
#include "SpectralCore.h"
#include <functional>
#include <initializer_list>
#include <juce_gui_extra/juce_gui_extra.h>

namespace hem::spectral {
enum class Audition { stopped, original, processed, delta };
struct ViewState {
    std::shared_ptr<const Grid> grid;
    juce::String sourceLabel, status;
    std::uint64_t sourceRevision = 0;
    std::size_t frames = 0;
    double sampleRate = 0.0;
    bool sourceReady = false, busy = false, canApply = false, canAudition = false, canUndo = false;
    bool undoRestoresOriginal = true;
    Audition audition = Audition::stopped;
};
// Message-thread presentation only. The host owns all worker jobs, PCM,
// cancellation, file publication, identity checks, and audition transports.
// No destructor wait, FFT, file decoding, or large PCM copy occurs here.
class SpectralPanel final : public juce::Component {
    class Plot final : public juce::Component
    {
      public:
        std::shared_ptr<const Grid> data;
        const Grid& grid() const noexcept {
            static const Grid empty;
            return data ? *data : empty;
        }
        std::function<void(double, double, double, double)> onSelection;
        bool projected = false, side = false, selected = false;
        double t0 = 0, t1 = 0, f0 = 20, f1 = 20000;
        float yaw = 0.62f, pitch = 0.65f, zoom = 1;
        juce::Point<float> anchor;
        std::vector<std::size_t> markers;
        juce::Rectangle<float> area() const { return getLocalBounds().toFloat().reduced(62, 38); }
        static juce::Colour colour(float db)
        {
            return juce::Colour::fromHSV(
                juce::jmap(juce::jlimit(0.0f, 1.0f, (db + 100.0f) / 100.0f), 0.66f, 0.06f), 0.86f,
                0.18f + 0.82f * juce::jlimit(0.0f, 1.0f, (db + 100.0f) / 100.0f), 1);
        }
        double hz(double y) const
        {
            if (grid().frequenciesHz.empty())
                return 20;
            const double low = grid().frequencyEdgesHz.front(), high = grid().frequencyEdgesHz.back();
            return low * std::pow(high / low, juce::jlimit(0.0, 1.0, y));
        }
        float frequencyY(double f) const
        {
            if (grid().frequenciesHz.empty())
                return 0;
            return static_cast<float>(
                std::log(std::clamp(f, static_cast<double>(grid().frequencyEdgesHz.front()),
                                    static_cast<double>(grid().frequencyEdgesHz.back())) /
                         grid().frequencyEdgesHz.front()) /
                std::log(grid().frequencyEdgesHz.back() / grid().frequencyEdgesHz.front()));
        }
        void paint(juce::Graphics &g) override
        {
            g.fillAll(juce::Colour(0xff091018));
            const auto r = area();
            g.setColour(juce::Colours::white);
            if (grid().timeBins == 0 || grid().freqBins == 0)
            {
                g.drawFittedText("Analysis will appear here. FFT runs off the audio thread.",
                                 getLocalBounds(), juce::Justification::centred, 2);
                return;
            }
            const auto &values = side ? grid().sideDb : grid().magnitudeDb;
            if (!projected)
            {
                const float cw = r.getWidth() / static_cast<float>(grid().timeBins);
                for (std::size_t t = 0; t < grid().timeBins; ++t)
                    for (std::size_t f = 0; f < grid().freqBins; ++f)
                    {
                        const float bottom = frequencyY(grid().frequencyEdgesHz[f]);
                        const float top = frequencyY(grid().frequencyEdgesHz[f + 1]);
                        g.setColour(colour(values[t * grid().freqBins + f]));
                        g.fillRect(r.getX() + static_cast<float>(t) * cw,
                                   r.getBottom() - top * r.getHeight(), cw + 0.5f,
                                   (top - bottom) * r.getHeight() + 0.5f);
                    }
                if (selected)
                {
                    const float x0 = r.getX() +
                                     static_cast<float>(t0 / grid().durationSeconds) * r.getWidth(),
                                x1 = r.getX() +
                                     static_cast<float>(t1 / grid().durationSeconds) * r.getWidth();
                    const float y0 = r.getBottom() - frequencyY(f1) * r.getHeight(),
                                y1 = r.getBottom() - frequencyY(f0) * r.getHeight();
                    g.setColour(juce::Colours::white);
                    g.drawRect(juce::Rectangle<float>::leftTopRightBottom(x0, y0, x1, y1), 2);
                }
                g.setColour(juce::Colour(0xffffc16a));
                for (auto frame : markers)
                {
                    const float x =
                        r.getX() + static_cast<float>(static_cast<double>(frame) / grid().sampleRate /
                                                      grid().durationSeconds) *
                                       r.getWidth();
                    if (x >= r.getX() && x <= r.getRight())
                        g.drawVerticalLine(static_cast<int>(x), r.getY(), r.getBottom());
                }
                g.setColour(juce::Colours::lightgrey);
                g.drawRect(r);
                for (int i = 0; i <= 4; ++i)
                {
                    const float x = r.getX() + r.getWidth() * static_cast<float>(i) / 4;
                    g.drawText(juce::String(grid().durationSeconds * i / 4, 2) + " s",
                               static_cast<int>(x) - 28, static_cast<int>(r.getBottom()) + 6, 65,
                               18, juce::Justification::centred);
                    const double frequency = hz(static_cast<double>(i) / 4);
                    g.drawText(juce::String(static_cast<int>(std::round(frequency))), 0,
                               static_cast<int>(r.getBottom() -
                                                r.getHeight() * static_cast<float>(i) / 4) -
                                   8,
                               57, 18, juce::Justification::centredRight);
                }
                g.drawText("Hz (log)", 4, 4, 100, 20, juce::Justification::left);
            }
            else
            {
                const auto project = [&](float x, float y, float z)
                {
                    x -= 0.5f;
                    y -= 0.5f;
                    const float xx = x * std::cos(yaw) - y * std::sin(yaw),
                                yy = x * std::sin(yaw) + y * std::cos(yaw);
                    return juce::Point<float>{r.getCentreX() + xx * r.getWidth() * 0.65f * zoom,
                                              r.getY() + r.getHeight() * 0.68f +
                                                  yy * r.getHeight() * 0.35f * std::sin(pitch) *
                                                      zoom -
                                                  z * r.getHeight() * 0.40f * zoom};
                };
                for (std::size_t t = 0; t + 1 < grid().timeBins; ++t)
                    for (std::size_t f = 0; f + 1 < grid().freqBins; ++f)
                    {
                        const float db = values[t * grid().freqBins + f];
                        const auto a =
                            project(static_cast<float>(t) / static_cast<float>(grid().timeBins - 1),
                                    frequencyY(grid().frequenciesHz[f]),
                                    juce::jlimit(0.0f, 1.0f, (db + 100) / 100));
                        const auto b = project(
                            static_cast<float>(t + 1) / static_cast<float>(grid().timeBins - 1),
                            frequencyY(grid().frequenciesHz[f]),
                            juce::jlimit(0.0f, 1.0f,
                                         (values[(t + 1) * grid().freqBins + f] + 100) / 100));
                        const auto c =
                            project(static_cast<float>(t) / static_cast<float>(grid().timeBins - 1),
                                    frequencyY(grid().frequenciesHz[f + 1]),
                                    juce::jlimit(0.0f, 1.0f,
                                                 (values[t * grid().freqBins + f + 1] + 100) / 100));
                        g.setColour(colour(db));
                        g.drawLine({a, b}, 0.7f);
                        g.drawLine({a, c}, 0.7f);
                    }
                g.setColour(juce::Colours::white);
                g.drawText("X = 0-" + juce::String(grid().durationSeconds, 2) +
                               " s | Y = " + juce::String(grid().frequencyEdgesHz.front(), 0) + "-" +
                               juce::String(grid().frequencyEdgesHz.back(), 0) +
                               " Hz (log) | Z = -100-0 dBFS",
                           getLocalBounds().removeFromBottom(26), juce::Justification::centred);
                g.drawText("Drag to rotate | wheel to zoom | select regions in 2D",
                           getLocalBounds().removeFromTop(25), juce::Justification::centred);
            }
            g.setColour(juce::Colours::white);
            g.drawText(side ? "Side (L-R)/2: -100 .. 0 dBFS" : "Magnitude: -100 .. 0 dBFS",
                       getWidth() - 310, 5, 300, 20, juce::Justification::centredRight);
        }
        void mouseDown(const juce::MouseEvent &e) override { anchor = e.position; }
        void mouseDrag(const juce::MouseEvent &e) override
        {
            if (projected)
            {
                yaw += e.getDistanceFromDragStartX() * 0.0003f;
                pitch = juce::jlimit(0.15f, 1.3f, pitch + e.getDistanceFromDragStartY() * 0.0002f);
                repaint();
            }
        }
        void mouseUp(const juce::MouseEvent &e) override
        {
            if (projected || grid().timeBins == 0 || !onSelection)
                return;
            const auto r = area();
            const auto a = r.getConstrainedPoint(anchor), b = r.getConstrainedPoint(e.position);
            if (std::abs(a.x - b.x) < 3 || std::abs(a.y - b.y) < 3)
                return;
            onSelection(static_cast<double>(std::min(a.x, b.x) - r.getX()),
                        static_cast<double>(std::max(a.x, b.x) - r.getX()),
                        hz((r.getBottom() - std::max(a.y, b.y)) / r.getHeight()),
                        hz((r.getBottom() - std::min(a.y, b.y)) / r.getHeight()));
        }
        void mouseWheelMove(const juce::MouseEvent &, const juce::MouseWheelDetails &w) override
        {
            if (projected)
            {
                zoom = juce::jlimit(0.5f, 2.0f, zoom + w.deltaY);
                repaint();
            }
        }
    };
  public:
    std::function<void(Selection)> onPreview;
    std::function<void()> onApply, onCancel, onUndo, onSelectionChanged;
    std::function<void(Audition)> onAudition;

    SpectralPanel() {
        setSize(1120, 720);
        for (auto* c : std::initializer_list<juce::Component*>{
                 &plot, &source, &status, &mode, &metric, &start, &end, &low, &high,
                 &amount, &preview, &apply, &cancel, &undo, &original, &processed,
                 &delta, &stop, &legend, &selectionLabel})
            addAndMakeVisible(c);
        source.setFont(juce::Font(juce::FontOptions(15.0f, juce::Font::bold)));
        mode.setButtonText("3D amplitude mesh");
        mode.setClickingTogglesState(true);
        mode.onClick = [this] { plot.projected = mode.getToggleState(); plot.repaint(); };
        metric.setButtonText("Show stereo side");
        metric.setClickingTogglesState(true);
        metric.onClick = [this] { plot.side = metric.getToggleState(); plot.repaint(); };
        configure(start, 0.0, 1.0, "Start s", 0.0);
        configure(end, 0.0, 1.0, "End s", 1.0);
        configure(low, 20.0, 48000.0, "Low Hz", 20.0);
        configure(high, 20.0, 48000.0, "High Hz", 48000.0);
        configure(amount, -24.0, 0.0, "Attenuation dB", -6.0);
        for (auto* slider : {&start, &end, &low, &high})
            slider->onValueChange = [this] { select(); };
        amount.onValueChange = [this] { invalidatePreview(); };
        preview.setButtonText("Compute selected preview");
        apply.setButtonText("Apply to selected track");
        cancel.setButtonText("Cancel work");
        undo.setButtonText("Restore original");
        original.setButtonText("Hear original");
        processed.setButtonText("Hear processed");
        delta.setButtonText("Hear removed (delta)");
        stop.setButtonText("Stop audition");
        preview.onClick = [this] {
            if (canPreview() && onPreview) onPreview(selection());
        };
        apply.onClick = [this] { if (!state.busy && state.canApply && onApply) onApply(); };
        cancel.onClick = [this] { if (state.busy && onCancel) onCancel(); };
        undo.onClick = [this] { if (!state.busy && state.canUndo && onUndo) onUndo(); };
        original.onClick = [this] { requestAudition(Audition::original); };
        processed.onClick = [this] { requestAudition(Audition::processed); };
        delta.onClick = [this] { requestAudition(Audition::delta); };
        stop.onClick = [this] { if (onAudition) onAudition(Audition::stopped); };
        plot.onSelection = [this](double x0, double x1, double f0, double f1) {
            if (state.busy || !state.sourceReady || plot.area().getWidth() <= 0) return;
            const double duration = double(state.frames) / state.sampleRate;
            start.setValue(x0 / plot.area().getWidth() * duration, juce::dontSendNotification);
            end.setValue(x1 / plot.area().getWidth() * duration, juce::dontSendNotification);
            low.setValue(f0, juce::dontSendNotification);
            high.setValue(f1, juce::dontSendNotification);
            select();
        };
        legend.setText("Offline FFT | projected height is amplitude, not physical space | select in 2D | no automatic artifact recognition", juce::dontSendNotification);
        selectionLabel.setText("Drag a time/frequency rectangle in 2D or adjust the range controls.", juce::dontSendNotification);
        status.setText("Select a loaded track to inspect its immutable source.", juce::dontSendNotification);
        refreshControls();
    }

    // Host calls only on the message thread. All snapshots refer to one source
    // revision. A stale worker result must be rejected by the host before here.
    void setSnapshot(ViewState snapshot) {
        const bool changed = snapshot.sourceRevision != state.sourceRevision ||
                             snapshot.frames != state.frames || snapshot.sampleRate != state.sampleRate;
        const bool newGrid = snapshot.grid != state.grid;
        state = std::move(snapshot);
        if (changed) {
            plot.selected = false;
            const double duration = state.frames > 0 && state.sampleRate > 0
                                        ? double(state.frames) / state.sampleRate : 1.0;
            const double nyquist = state.sampleRate >= 8000.0 ? state.sampleRate * 0.5 : 48000.0;
            start.setRange(0.0, duration);
            end.setRange(0.0, duration);
            low.setRange(20.0, nyquist);
            high.setRange(20.0, nyquist);
            start.setValue(0.0, juce::dontSendNotification);
            end.setValue(duration, juce::dontSendNotification);
            low.setValue(20.0, juce::dontSendNotification);
            high.setValue(nyquist, juce::dontSendNotification);
            amount.setValue(-6.0, juce::dontSendNotification);
            selectionLabel.setText("Drag a time/frequency rectangle in 2D or adjust the range controls.", juce::dontSendNotification);
        }
        plot.data = state.grid;
        if (!state.grid || !state.grid->stereo) {
            metric.setToggleState(false, juce::dontSendNotification);
            plot.side = false;
        }
        source.setText(state.sourceLabel, juce::dontSendNotification);
        status.setText(state.status, juce::dontSendNotification);
        if (changed || newGrid) plot.repaint();
        refreshControls();
    }
    void paint(juce::Graphics& g) override { g.fillAll(juce::Colour(0xff111820)); }
    void resized() override {
        source.setBounds(16, 4, getWidth() - 32, 24);
        mode.setBounds(16, 30, 180, 28);
        metric.setBounds(210, 30, 180, 28);
        plot.setBounds(8, 64, getWidth() - 16, 424);
        legend.setBounds(16, 490, getWidth() - 32, 25);
        start.setBounds(16, 521, 255, 38);
        end.setBounds(285, 521, 255, 38);
        low.setBounds(555, 521, 255, 38);
        high.setBounds(825, 521, 275, 38);
        amount.setBounds(16, 568, 292, 38);
        preview.setBounds(320, 570, 225, 32);
        apply.setBounds(558, 570, 210, 32);
        cancel.setBounds(780, 570, 140, 32);
        undo.setBounds(932, 570, 168, 32);
        original.setBounds(16, 611, 145, 30);
        processed.setBounds(173, 611, 155, 30);
        delta.setBounds(340, 611, 190, 30);
        stop.setBounds(542, 611, 150, 30);
        selectionLabel.setBounds(16, 646, getWidth() - 32, 25);
        status.setBounds(16, 674, getWidth() - 32, 38);
    }

  private:
    ViewState state;
    Plot plot;
    juce::Label source, status, legend, selectionLabel;
    juce::TextButton mode, metric, preview, apply, cancel, undo, original, processed, delta, stop;
    juce::Slider start, end, low, high, amount;
    static void configure(juce::Slider& slider, double minimum, double maximum,
                          const juce::String& label, double value) {
        slider.setRange(minimum, maximum);
        slider.setValue(value, juce::dontSendNotification);
        slider.setSliderStyle(juce::Slider::LinearHorizontal);
        slider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 100, 25);
        slider.setTextValueSuffix(" " + label);
    }
    Selection selection() const {
        const auto duration = double(state.frames) / state.sampleRate;
        const auto firstSeconds = std::clamp(plot.t0, 0.0, duration);
        const auto lastSeconds = std::clamp(plot.t1, 0.0, duration);
        const auto first = std::min(state.frames, static_cast<std::size_t>(std::floor(firstSeconds * state.sampleRate)));
        const auto last = lastSeconds >= duration ? state.frames :
                          std::min(state.frames, static_cast<std::size_t>(std::ceil(lastSeconds * state.sampleRate)));
        return {first, last, float(plot.f0), float(plot.f1),
                juce::Decibels::decibelsToGain(float(amount.getValue()))};
    }
    bool canPreview() const {
        return state.sourceReady && state.frames > 0 && state.sampleRate > 0 &&
               !state.busy && state.grid && state.grid->timeBins > 0 &&
               plot.selected && plot.t1 > plot.t0 && plot.f1 > plot.f0;
    }
    void refreshControls() {
        for (auto* slider : {&start, &end, &low, &high, &amount})
            slider->setEnabled(state.sourceReady && !state.busy);
        preview.setEnabled(canPreview());
        apply.setEnabled(!state.busy && state.canApply);
        cancel.setEnabled(state.busy);
        undo.setEnabled(!state.busy && state.canUndo);
        undo.setButtonText(state.undoRestoresOriginal ? "Restore original" : "Redo spectral edit");
        metric.setEnabled(state.grid && state.grid->stereo);
        for (auto* button : {&original, &processed, &delta})
            button->setEnabled(!state.busy && state.canAudition);
        original.setToggleState(state.audition == Audition::original, juce::dontSendNotification);
        processed.setToggleState(state.audition == Audition::processed, juce::dontSendNotification);
        delta.setToggleState(state.audition == Audition::delta, juce::dontSendNotification);
        stop.setEnabled(state.audition != Audition::stopped);
    }
    void invalidatePreview() {
        if (state.busy) return;
        state.canApply = state.canAudition = false;
        state.audition = Audition::stopped;
        refreshControls();
        if (onSelectionChanged) onSelectionChanged();
    }
    void select() {
        if (state.busy || !state.sourceReady) return;
        plot.t0 = std::min(start.getValue(), end.getValue());
        plot.t1 = std::max(start.getValue(), end.getValue());
        plot.f0 = std::min(low.getValue(), high.getValue());
        plot.f1 = std::max(low.getValue(), high.getValue());
        plot.selected = true;
        invalidatePreview();
        const auto chosen = selection();
        selectionLabel.setText("Selected frames [" + juce::String(static_cast<juce::int64>(chosen.startFrame)) +
                               ", " + juce::String(static_cast<juce::int64>(chosen.endFrame)) + ") / " +
                               juce::String(plot.f0, 0) + "-" + juce::String(plot.f1, 0) +
                               " Hz. Preview leaves the production source unchanged.", juce::dontSendNotification);
        plot.repaint();
    }
    void requestAudition(Audition modeToHear) {
        if (!state.busy && state.canAudition && onAudition) onAudition(modeToHear);
    }
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SpectralPanel)
};
} // namespace hem::spectral
