#pragma once

#include "InstrumentalCore.h"
#include "../HardwareLookAndFeel.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <functional>
#include <memory>
#include <utility>

namespace hem::instrumental {

// Message-thread presentation state. The host, not this panel, decides when a
// complete render is eligible to be staged into the existing stem importer.
struct ViewState {
    juce::String status;
    bool busy = false;
    bool canRender = true;
    bool canApply = false;
    double progress = 0.0;
};

/** Offline, source-only integration of the saved 2.0.0 sequencer's 13-lane,
    32-step interaction model. The main Studio's look and artwork stay intact.

    This component has no audio callback, live transport, MIDI input, file I/O,
    file chooser, decoding, rendering job, worker wait, or sample-buffer copy.
    Controls edit a private RenderPlan on the message thread. Each callback
    receives a value snapshot whose sample buffers remain shared and const.
    The host owns I/O, cancellation, publication and all background processing.

    setPlan, setSampleLabels, setViewState and clearCallbacks are message-thread
    methods. Hosts must clear callbacks before retiring their own controller.
    Offline controls deliberately do not bind to the live mixer's APVTS. */
class InstrumentalPanel final : public juce::Component {
    struct Columns {
        juce::Rectangle<int> sample, steps, pitch, pan, gain, mute, solo, choke;
    };

    static Columns columnsFor(juce::Rectangle<int> area)
    {
        Columns c;
        area = area.reduced(2, 0);
        c.choke = area.removeFromRight(64);
        area.removeFromRight(4);
        c.solo = area.removeFromRight(30);
        area.removeFromRight(4);
        c.mute = area.removeFromRight(30);
        area.removeFromRight(4);
        c.gain = area.removeFromRight(82);
        area.removeFromRight(6);
        c.pan = area.removeFromRight(82);
        area.removeFromRight(6);
        c.pitch = area.removeFromRight(82);
        area.removeFromRight(10);
        c.sample = area.removeFromLeft(184);
        area.removeFromLeft(8);
        c.steps = area;
        return c;
    }

    class StepButton final : public juce::TextButton {
    public:
        void initialise(int lane, int index)
        {
            step = index;
            setButtonText("Lane " + juce::String(lane + 1) + ", step " + juce::String(index + 1));
            setName(getButtonText());
            setComponentID("instrumental-lane-" + juce::String(lane + 1)
                           + "-step-" + juce::String(index + 1));
            setTooltip(getButtonText() + ": toggle a sixteenth-note trigger. Tab and Space also work.");
            setClickingTogglesState(true);
            setWantsKeyboardFocus(true);
        }

        void paintButton(juce::Graphics& g, bool over, bool down) override
        {
            const auto box = getLocalBounds().toFloat().reduced(1.0f, 3.0f);
            if (box.isEmpty())
                return;
            const bool active = getToggleState();
            const float opacity = isEnabled() ? 1.0f : 0.38f;
            const auto accent = active ? juce::Colour(0xffffbb55) : juce::Colour(0xff58dce8);
            auto fill = active ? juce::Colour(0xff714116)
                               : ((step / 4) % 2 == 0 ? juce::Colour(0xff16323b)
                                                     : juce::Colour(0xff0a1b25));
            if (over || down)
                fill = fill.brighter(0.25f);
            g.setColour(fill.withMultipliedAlpha(opacity));
            g.fillRoundedRectangle(box, 2.5f);
            g.setColour(accent.withAlpha(opacity * (active || over ? 0.95f : 0.3f)));
            g.drawRoundedRectangle(box, 2.5f, active ? 1.5f : 0.7f);
            if (step % 4 == 0)
            {
                g.setColour(accent.withAlpha(opacity * 0.8f));
                g.fillRect(box.getX() + 2.0f, box.getY() + 2.0f,
                           juce::jmax(0.0f, box.getWidth() - 4.0f), 2.0f);
            }
            g.setColour((active ? juce::Colour(0xffffe1af) : juce::Colour(0xffa3c6cd))
                            .withAlpha(opacity));
            g.setFont(juce::Font(juce::FontOptions(10.0f)));
            g.drawFittedText(juce::String(step + 1), getLocalBounds().reduced(1, 5),
                             juce::Justification::centred, 1, 0.7f);
            if (hasKeyboardFocus(true))
            {
                g.setColour(juce::Colours::white.withAlpha(opacity));
                g.drawRoundedRectangle(box.reduced(1.5f), 1.5f, 1.0f);
            }
        }

    private:
        int step = 0;
    };

    static void configureValue(juce::Slider& slider, const juce::String& name,
                               double low, double high, double interval,
                               double reset, const juce::String& suffix)
    {
        slider.setName(name);
        slider.setSliderStyle(juce::Slider::LinearBar);
        slider.setRange(low, high, interval);
        slider.setTextBoxStyle(juce::Slider::TextBoxLeft, false, 82, 28);
        slider.setTextValueSuffix(suffix);
        slider.setDoubleClickReturnValue(true, reset);
        slider.setScrollWheelEnabled(false);
        slider.setTooltip(name + ": drag to adjust or click the value to type. Double-click resets.");
        slider.setValue(reset, juce::dontSendNotification);
    }

    class LaneRow final : public juce::Component {
    public:
        std::function<void()> onChanged, onLoad;

        explicit LaneRow(int index) : laneIndex(index)
        {
            for (auto* c : std::array<juce::Component*, 7>{
                     &sample, &pitch, &pan, &gain, &mute, &solo, &choke})
                addAndMakeVisible(*c);

            const auto prefix = "Lane " + juce::String(index + 1) + " ";
            sample.setName(prefix + "load sample");
            sample.setComponentID("instrumental-load-lane-" + juce::String(index + 1));
            sample.onClick = [this] {
                const auto callback = onLoad;
                if (callback)
                    callback();
            };

            configureValue(pitch, prefix + "pitch", -24.0, 24.0, 0.1, 0.0, " st");
            configureValue(pan, prefix + "pan", -1.0, 1.0, 0.01, 0.0, "");
            configureValue(gain, prefix + "gain", -60.0, 12.0, 0.1, 0.0, " dB");
            pan.setTooltip(prefix + "pan: -1 is left, 0 is centre, +1 is right.");
            pitch.setTooltip(prefix + "pitch in semitones; resampling also changes sample duration.");
            pitch.onValueChange = [this] { changed(); };
            pan.onValueChange = [this] { changed(); };
            gain.onValueChange = [this] { changed(); };

            mute.setButtonText("M");
            solo.setButtonText("S");
            mute.setName(prefix + "mute");
            solo.setName(prefix + "solo");
            mute.setTooltip(prefix + "mute in the next offline render.");
            solo.setTooltip(prefix + "solo in the next offline render. Mute still takes precedence.");
            mute.setClickingTogglesState(true);
            solo.setClickingTogglesState(true);
            mute.onClick = [this] { changed(); };
            solo.onClick = [this] { changed(); };

            choke.setName(prefix + "choke group");
            choke.addItem("OFF", 1);
            for (int group = 1; group <= 4; ++group)
                choke.addItem(juce::String(group), group + 1);
            choke.setTooltip("Choke group: a trigger stops earlier voices in the same group. OFF disables grouping.");
            choke.onChange = [this] { changed(); };

            for (int step = 0; step < kStepCount; ++step)
            {
                auto& button = steps[static_cast<std::size_t>(step)];
                button.initialise(index, step);
                button.onClick = [this] { changed(); };
                addAndMakeVisible(button);
            }
        }

        ~LaneRow() override { clearCallbacks(); }

        void clearCallbacks()
        {
            onChanged = {};
            onLoad = {};
            sample.onClick = {};
            pitch.onValueChange = {};
            pan.onValueChange = {};
            gain.onValueChange = {};
            mute.onClick = {};
            solo.onClick = {};
            choke.onChange = {};
            for (auto& button : steps)
                button.onClick = {};
        }

        void setLane(const Lane& lane, const juce::String& source)
        {
            pitch.setValue(lane.pitchSemitones, juce::dontSendNotification);
            pan.setValue(lane.pan, juce::dontSendNotification);
            gain.setValue(lane.gainDb, juce::dontSendNotification);
            mute.setToggleState(lane.mute, juce::dontSendNotification);
            solo.setToggleState(lane.solo, juce::dontSendNotification);
            choke.setSelectedId(lane.chokeGroup + 1, juce::dontSendNotification);
            for (int step = 0; step < kStepCount; ++step)
                steps[static_cast<std::size_t>(step)].setToggleState(
                    (lane.steps & (std::uint32_t{1} << step)) != 0,
                    juce::dontSendNotification);

            const auto laneNumber = juce::String(laneIndex + 1).paddedLeft('0', 2);
            const auto sourceName = source.replaceCharacter('\\', '/').fromLastOccurrenceOf("/", false, false);
            const auto caption = source.isEmpty() ? (lane.sample ? "SAMPLE READY" : "LOAD SAMPLE")
                                                  : (sourceName.isEmpty() ? source : sourceName);
            sample.setButtonText(laneNumber + "  " + caption);
            sample.setTooltip("Lane " + juce::String(laneIndex + 1) + ": click to load or replace a sample."
                              + (source.isNotEmpty() ? "\n" + source : juce::String()));
        }

        void writeControlsTo(Lane& lane) const
        {
            std::uint32_t mask = 0;
            for (int step = 0; step < kStepCount; ++step)
                if (steps[static_cast<std::size_t>(step)].getToggleState())
                    mask |= (std::uint32_t{1} << step);
            lane.steps = mask;
            lane.pitchSemitones = static_cast<float>(pitch.getValue());
            lane.pan = static_cast<float>(pan.getValue());
            lane.gainDb = static_cast<float>(gain.getValue());
            lane.mute = mute.getToggleState();
            lane.solo = solo.getToggleState();
            lane.chokeGroup = juce::jlimit(0, 4, choke.getSelectedId() - 1);
        }

        void setEditable(bool editable)
        {
            // setEnabled propagates disabled state to all controls, including
            // open text entry fields and the step grid's keyboard actions.
            setEnabled(editable);
        }

        void paint(juce::Graphics& g) override
        {
            g.setColour(laneIndex % 2 == 0 ? juce::Colour(0xff0c1b24)
                                         : juce::Colour(0xff08131c));
            g.fillRoundedRectangle(getLocalBounds().toFloat(), 3.0f);
        }

        void resized() override
        {
            const auto c = columnsFor(getLocalBounds().reduced(0, 2));
            sample.setBounds(c.sample);
            pitch.setBounds(c.pitch.reduced(0, 3));
            pan.setBounds(c.pan.reduced(0, 3));
            gain.setBounds(c.gain.reduced(0, 3));
            mute.setBounds(c.mute);
            solo.setBounds(c.solo);
            choke.setBounds(c.choke.reduced(0, 3));
            for (int step = 0; step < kStepCount; ++step)
            {
                const int left = c.steps.getX() + c.steps.getWidth() * step / kStepCount;
                const int right = c.steps.getX() + c.steps.getWidth() * (step + 1) / kStepCount;
                steps[static_cast<std::size_t>(step)].setBounds(
                    left, c.steps.getY(), right - left, c.steps.getHeight());
            }
        }

    private:
        void changed()
        {
            const auto callback = onChanged;
            if (isEnabled() && callback)
                callback();
        }

        const int laneIndex;
        juce::TextButton sample, mute, solo;
        juce::Slider pitch, pan, gain;
        juce::ComboBox choke;
        std::array<StepButton, kStepCount> steps;
    };

public:
    std::function<void(RenderPlan)> onPlanChanged, onRender, onSavePattern;
    std::function<void(int)> onLoadSample;
    std::function<void()> onCancel, onApply, onLoadPattern;

    InstrumentalPanel()
    {
        setLookAndFeel(&look);
        setName("Offline Instrumental Builder");
        for (auto* c : std::array<juce::Component*, 21>{
                 &heading, &description, &bpm, &swing, &repeats, &tail,
                 &bpmLabel, &swingLabel, &repeatsLabel, &tailLabel,
                 &sampleHeader, &stepsHeader, &pitchHeader, &panHeader, &gainHeader,
                 &muteHeader, &soloHeader, &chokeHeader, &status, &footer, &duration})
            addAndMakeVisible(*c);
        for (auto* button : std::array<juce::TextButton*, 5>{
                 &render, &cancel, &apply, &savePattern, &loadPattern})
            addAndMakeVisible(*button);

        label(heading, "INSTRUMENTAL BUILDER / OFFLINE / 13 LANES", 18.0f);
        label(description,
              "Load samples, program 32 sixteenth-note steps, then render aligned 96 kHz / 32-bit float WAV stems.\n"
              "Two bars per repeat. Swing 50% is straight. Use Studio playback after staging and applying the stems.",
              13.0f);
        label(bpmLabel, "BPM", 12.0f);
        label(swingLabel, "SWING", 12.0f);
        label(repeatsLabel, "REPEATS", 12.0f);
        label(tailLabel, "TAIL", 12.0f);
        configureValue(bpm, "Pattern tempo", 40.0, 240.0, 0.1, 120.0, " BPM");
        configureValue(swing, "Swing percentage", 50.0, 75.0, 0.1, 50.0, " %");
        configureValue(repeats, "Pattern repeats", 1.0, 128.0, 1.0, 1.0, " x");
        configureValue(tail, "Rendered tail", 0.0, 30.0, 0.1, 2.0, " s");
        swing.setTooltip("50% is straight; up to 75% delays the second sixteenth-note in each pair.");
        tail.setTooltip("Extra seconds after the final pattern repeat. All 13 files receive the same final length.");
        bpm.onValueChange = [this] { timingChanged(); };
        swing.onValueChange = [this] { timingChanged(); };
        repeats.onValueChange = [this] { timingChanged(); };
        tail.onValueChange = [this] { timingChanged(); };

        label(sampleHeader, "SAMPLE / CLICK TO LOAD", 11.0f);
        label(stepsHeader, "32 STEPS / QUARTER-NOTE GROUPS", 11.0f);
        label(pitchHeader, "PITCH / st", 11.0f);
        label(panHeader, "PAN / L ... R", 11.0f);
        label(gainHeader, "GAIN / dB", 11.0f);
        label(muteHeader, "M", 11.0f);
        label(soloHeader, "S", 11.0f);
        label(chokeHeader, "CHOKE", 11.0f);
        for (auto* header : std::array<juce::Label*, 7>{
                 &stepsHeader, &pitchHeader, &panHeader, &gainHeader,
                 &muteHeader, &soloHeader, &chokeHeader})
            header->setJustificationType(juce::Justification::centred);

        for (int lane = 0; lane < kLaneCount; ++lane)
        {
            auto& row = rows[static_cast<std::size_t>(lane)];
            row = std::make_unique<LaneRow>(lane);
            row->onChanged = [this, lane] {
                if (view.busy)
                    return;
                rows[static_cast<std::size_t>(lane)]->writeControlsTo(
                    plan.lanes[static_cast<std::size_t>(lane)]);
                edited();
            };
            row->onLoad = [this, lane] {
                const auto callback = onLoadSample;
                if (!view.busy && callback)
                    callback(lane);
            };
            addAndMakeVisible(*row);
        }

        render.setTooltip("Render a frozen copy of this pattern in the background. Existing files are never overwritten.");
        apply.setTooltip("Only empty mixer tracks; then use importer APPLY or PLAY to commit");
        savePattern.setTooltip("Save a separate pattern document to keep builder edits after closing Studio. Session save does not include the builder pattern. Sample audio is referenced, not embedded.");
        loadPattern.setTooltip("Load a previously saved builder pattern. The host validates and decodes its samples in the background.");
        cancel.setTooltip("Request cancellation of the active builder job. No new work starts here.");
        render.onClick = [this] {
            const auto callback = onRender;
            if (!view.busy && view.canRender && callback)
            {
                // Read current control values as well as the cached plan: JUCE
                // slider change notifications may still be queued this turn.
                plan = snapshotControls();
                callback(plan);
            }
        };
        savePattern.onClick = [this] {
            const auto callback = onSavePattern;
            if (!view.busy && callback)
            {
                plan = snapshotControls();
                callback(plan);
            }
        };
        loadPattern.onClick = [this] {
            const auto callback = onLoadPattern;
            if (!view.busy && callback)
                callback();
        };
        apply.onClick = [this] {
            const auto callback = onApply;
            if (!view.busy && view.canApply && callback)
                callback();
        };
        cancel.onClick = [this] {
            const auto callback = onCancel;
            if (view.busy && callback)
                callback();
        };

        label(status, "Load a sample to begin. Rendering and sample loading run in the background.", 13.0f);
        status.setJustificationType(juce::Justification::topLeft);
        label(footer,
              "STAGE 13 STEMS targets empty mixer tracks only. Then use the stem importer's APPLY or PLAY to commit.\n"
              "Applying the staged stems also sets the project tempo to the rendered pattern's BPM.\n"
              "Draft stays available while Studio is open. Save pattern to keep it after closing Studio; session save does not include the builder pattern.",
              12.0f);
        footer.setColour(juce::Label::textColourId, juce::Colour(0xffffd18a));
        label(duration, "", 12.0f);
        duration.setJustificationType(juce::Justification::centredRight);
        setPlan(plan);
        updateAvailability();
        setSize(1240, 830);
    }

    ~InstrumentalPanel() override
    {
        clearCallbacks();
        clearControlCallbacks();
        setLookAndFeel(nullptr);
    }

    // Detach only the host. The local controls remain wired so a retained panel
    // can be reopened and rebound to a replacement host without reconstruction.
    void clearCallbacks()
    {
        onPlanChanged = {};
        onLoadSample = {};
        onRender = {};
        onCancel = {};
        onApply = {};
        onSavePattern = {};
        onLoadPattern = {};
    }

    void detachHostCallbacks() { clearCallbacks(); }

    // Silent synchronization from the host; never calls onPlanChanged. Large
    // sample arrays are not duplicated because RenderPlan holds const pointers.
    void setPlan(const RenderPlan& next)
    {
        plan = next;
        bpm.setValue(plan.bpm, juce::dontSendNotification);
        swing.setValue(plan.swingPercent, juce::dontSendNotification);
        repeats.setValue(plan.repeats, juce::dontSendNotification);
        tail.setValue(plan.tailSeconds, juce::dontSendNotification);
        refreshRows();
        updateDuration();
    }

    const RenderPlan& getPlan() const noexcept { return plan; }

    // Pass full source paths for useful tooltips. Only a compact filename is
    // drawn on each sample button; this method performs no filesystem access.
    void setSampleLabels(const std::array<juce::String, kLaneCount>& next)
    {
        sampleLabels = next;
        refreshRows();
    }

    void setViewState(const ViewState& next)
    {
        view = next;
        updateAvailability();
        juce::String text = view.status;
        if (view.busy)
        {
            const auto bounded = std::isfinite(view.progress)
                                     ? juce::jlimit(0.0, 1.0, view.progress) : 0.0;
            if (text.isEmpty())
                text = "Working on a frozen pattern snapshot";
            text += "\nProgress: " + juce::String(bounded * 100.0, 1)
                    + "% / editing is paused / Cancel remains available";
        }
        else if (text.isEmpty())
            text = "Ready. Render the current pattern, then stage the completed stems.";
        status.setText(text, juce::dontSendNotification);
        status.setTooltip(text);
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colour(0xff08121a));
        g.setColour(juce::Colour(0xff24505c));
        g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(3.0f), 6.0f, 1.0f);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced(16);
        heading.setBounds(area.removeFromTop(28));
        description.setBounds(area.removeFromTop(38));
        auto timing = area.removeFromTop(64);
        const auto place = [&](juce::Label& caption, juce::Slider& value) {
            auto item = timing.removeFromLeft(150).reduced(4, 1);
            caption.setBounds(item.removeFromTop(19));
            value.setBounds(item.removeFromTop(32));
        };
        place(bpmLabel, bpm);
        place(swingLabel, swing);
        place(repeatsLabel, repeats);
        place(tailLabel, tail);
        auto patternFiles = timing.removeFromLeft(320);
        savePattern.setBounds(patternFiles.removeFromLeft(160).reduced(4, 10));
        loadPattern.setBounds(patternFiles.reduced(4, 10));
        duration.setBounds(timing.reduced(3, 7));

        const auto headers = columnsFor(area.removeFromTop(25));
        sampleHeader.setBounds(headers.sample);
        stepsHeader.setBounds(headers.steps);
        pitchHeader.setBounds(headers.pitch);
        panHeader.setBounds(headers.pan);
        gainHeader.setBounds(headers.gain);
        muteHeader.setBounds(headers.mute);
        soloHeader.setBounds(headers.solo);
        chokeHeader.setBounds(headers.choke);
        for (auto& row : rows)
            row->setBounds(area.removeFromTop(37));

        auto actions = area.removeFromTop(46);
        render.setBounds(actions.removeFromLeft(240).reduced(3, 5));
        cancel.setBounds(actions.removeFromLeft(145).reduced(3, 5));
        apply.setBounds(actions.removeFromLeft(240).reduced(3, 5));
        status.setBounds(area.removeFromTop(62).reduced(2));
        footer.setBounds(area.reduced(1, 0));
    }

private:
    void clearControlCallbacks()
    {
        bpm.onValueChange = {};
        swing.onValueChange = {};
        repeats.onValueChange = {};
        tail.onValueChange = {};
        render.onClick = {};
        cancel.onClick = {};
        apply.onClick = {};
        savePattern.onClick = {};
        loadPattern.onClick = {};
        for (auto& row : rows)
            if (row)
                row->clearCallbacks();
    }

    RenderPlan snapshotControls() const
    {
        auto snapshot = plan;
        snapshot.bpm = bpm.getValue();
        snapshot.swingPercent = swing.getValue();
        snapshot.repeats = static_cast<int>(std::lround(repeats.getValue()));
        snapshot.tailSeconds = tail.getValue();
        for (int lane = 0; lane < kLaneCount; ++lane)
            rows[static_cast<std::size_t>(lane)]->writeControlsTo(
                snapshot.lanes[static_cast<std::size_t>(lane)]);
        return snapshot;
    }

    static void label(juce::Label& control, const juce::String& text, float size)
    {
        control.setText(text, juce::dontSendNotification);
        control.setFont(juce::Font(juce::FontOptions(size)));
        control.setJustificationType(juce::Justification::centredLeft);
        control.setBorderSize(juce::BorderSize<int>(1, 3, 1, 3));
        control.setMinimumHorizontalScale(0.9f);
    }

    void refreshRows()
    {
        for (int lane = 0; lane < kLaneCount; ++lane)
            if (auto& row = rows[static_cast<std::size_t>(lane)])
                row->setLane(plan.lanes[static_cast<std::size_t>(lane)],
                             sampleLabels[static_cast<std::size_t>(lane)]);
    }

    void updateDuration()
    {
        if (!std::isfinite(plan.bpm) || plan.bpm <= 0.0 || !std::isfinite(plan.tailSeconds))
        {
            duration.setText("Invalid timing", juce::dontSendNotification);
            return;
        }
        const double seconds = 480.0 / plan.bpm * static_cast<double>(plan.repeats)
                               + plan.tailSeconds;
        duration.setText("Pattern + tail\n" + juce::String(seconds, 2) + " seconds",
                         juce::dontSendNotification);
    }

    void timingChanged()
    {
        if (view.busy)
            return;
        plan.bpm = bpm.getValue();
        plan.swingPercent = swing.getValue();
        plan.repeats = static_cast<int>(std::lround(repeats.getValue()));
        plan.tailSeconds = tail.getValue();
        updateDuration();
        edited();
    }

    void edited()
    {
        // A staged render must match the edited pattern. The host invalidates
        // its own completed render on this callback as well.
        view.canApply = false;
        updateAvailability();
        status.setText("Pattern changed. Draft stays available while Studio is open; render again to stage these changes.",
                       juce::dontSendNotification);
        status.setTooltip(status.getText());
        const auto callback = onPlanChanged;
        if (callback)
            callback(plan);
    }

    void updateAvailability()
    {
        const bool editable = !view.busy;
        bpm.setEnabled(editable);
        swing.setEnabled(editable);
        repeats.setEnabled(editable);
        tail.setEnabled(editable);
        for (auto& row : rows)
            if (row)
                row->setEditable(editable);
        savePattern.setEnabled(editable);
        loadPattern.setEnabled(editable);
        render.setEnabled(editable && view.canRender);
        apply.setEnabled(editable && view.canApply);
        cancel.setEnabled(view.busy);
    }

    // Declared first, destroyed last. The destructor detaches the local look
    // before child controls are destroyed, matching Studio's utility panels.
    HardwareLookAndFeel look;
    RenderPlan plan;
    ViewState view;
    std::array<juce::String, kLaneCount> sampleLabels;
    std::array<std::unique_ptr<LaneRow>, kLaneCount> rows;
    juce::Label heading, description, bpmLabel, swingLabel, repeatsLabel, tailLabel;
    juce::Label sampleHeader, stepsHeader, pitchHeader, panHeader, gainHeader;
    juce::Label muteHeader, soloHeader, chokeHeader, status, footer, duration;
    juce::Slider bpm, swing, repeats, tail;
    juce::TextButton render{"RENDER 13 STEMS"}, cancel{"CANCEL"}, apply{"STAGE 13 STEMS"};
    juce::TextButton savePattern{"SAVE PATTERN"}, loadPattern{"LOAD PATTERN"};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(InstrumentalPanel)
};

} // namespace hem::instrumental
