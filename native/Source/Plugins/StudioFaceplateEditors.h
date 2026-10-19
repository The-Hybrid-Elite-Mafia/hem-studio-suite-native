#pragma once
// Editors for the 2026-10 Studio faceplates. The artwork is the background; every
// knob, button, slider, meter, value box and display is drawn and wired in code over
// the artwork's painted controls. Knobs/sliders/buttons bind to APVTS through JUCE
// attachments; displays read the processor's lock-free ScopeTap on a 30 Hz timer.
#include "StudioFaceplateProcessors.h"
#include "../UI/StudioFaceplateKit.h"

namespace hem {

class StudioFaceplateEditor : public juce::AudioProcessorEditor, private juce::Timer {
public:
    StudioFaceplateEditor(StudioFaceplateProcessor&, const void* artData, size_t artBytes, float designWidth, float designHeight);
    ~StudioFaceplateEditor() override;
    void paint(juce::Graphics&) final;
    void resized() final;
    void setCloseRequest(std::function<void()> f) { closeRequest = std::move(f); }
    // Test/inspection access.
    std::vector<juce::Component*> interactiveControls() const;
    void pumpForTest() { timerCallback(); }
protected:
    using Rect = juce::Rectangle<float>;
    fp::Knob& knob(const char* id, float cx, float cy, float capRadius, Rect valueBox, std::function<juce::String(double)> format);
    fp::FaceSlider& slider(const char* id, Rect bounds, Rect valueBox, std::function<juce::String(double)> format);
    fp::FaceButton& toggle(const char* id, Rect r, const juce::String& text, fp::FaceButton::Style style = fp::FaceButton::Style::orange);
    void choice(const char* id, std::vector<std::pair<Rect, juce::String>> options, fp::FaceButton::Style style = fp::FaceButton::Style::orange);
    fp::FaceButton& action(Rect r, const juce::String& text, std::function<void()> onClick, fp::FaceButton::Style style = fp::FaceButton::Style::orange);
    // Editor-only view choice (stored with the plugin state, not automatable).
    void viewChoice(const juce::Identifier& key, int fallback, std::vector<std::pair<Rect, juce::String>> options);
    juce::Component& hotspot(Rect r, const juce::String& tooltip, std::function<void()> onClick);
    void historyButtons(Rect ab, Rect undo, Rect redo);
    void meters(std::array<Rect, 4> bars, std::array<Rect, 4> boxes, float bottomDb = -60, float topDb = 6);
    void momentary(const char* id, Rect r, const juce::String& text);
    juce::ComboBox& combo(const char* id, Rect r, const juce::StringArray& items); // id = parameter, or nullptr for an editor-only list
    void dynamicRegion(Rect r) { dynamicRegions.push_back(r); }
    void resetAllToDefaults();
    // Host tests and accessibility find a parameter's control by its ID and name.
    void identifyControl(juce::Component& c, const char* id) { c.setComponentID(id); if (auto* p = param(id)) c.setName(p->getName(64)); }
    void finishSetup(); // call at the end of each derived constructor

    virtual void consume(const float* frames, int count) { juce::ignoreUnused(frames, count); }
    virtual void update() {}
    virtual void drawOverlay(juce::Graphics&) {}

    juce::RangedAudioParameter* param(const char* id) const { return processorRef.state.getParameter(id); }
    float valueOf(const char* id) const { if (auto* p = param(id)) return p->convertFrom0to1(p->getValue()); return 0; }
    void setValue(const char* id, float v) { if (auto* p = param(id)) { p->beginChangeGesture(); p->setValueNotifyingHost(p->convertTo0to1(v)); p->endChangeGesture(); } }
    int view(const juce::Identifier& key, int fallback) const { return processorRef.viewSetting(key, fallback); }
    double rate() const { const double r = processorRef.actualRate.load(); return r > 0 ? r : 48000; }
    StudioFaceplateProcessor& processorRef;
    float designW, designH;

private:
    void timerCallback() override;
    juce::Rectangle<int> toScreen(Rect r) const;
    struct Placed { juce::Component* component; Rect bounds; float halo = 0; };
    struct ValueReadout { juce::Slider* slider; Rect box; std::function<juce::String(double)> format; };
    struct ChoiceBinding { std::vector<fp::FaceButton*> buttons; std::unique_ptr<juce::ParameterAttachment> attachment; };
    struct ViewBinding { juce::Identifier key; std::vector<fp::FaceButton*> buttons; };
    juce::Image art;
    fp::FaceLook look; // outlives every owned control
    std::vector<std::unique_ptr<juce::Component>> owned;
    std::vector<Placed> placed;
    std::vector<ValueReadout> readouts;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>> sliderAttachments;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>> buttonAttachments;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment>> comboAttachments;
    std::vector<std::unique_ptr<ChoiceBinding>> choices;
    std::vector<std::unique_ptr<ViewBinding>> views;
    std::vector<Rect> dynamicRegions;
    std::array<Rect, 4> meterBars{}, meterBoxes{};
    bool hasMeters = false; float meterBottom = -60, meterTop = 6;
    std::array<fp::MeterState, 4> meterStates;
    std::vector<float> pullBuffer;
    fp::ParameterHistory history;
    fp::FaceButton* abButton = nullptr;
    std::function<void()> closeRequest;
protected:
    void requestClose() { if (closeRequest) closeRequest(); }
};

class VocalDeReverbEditor final : public StudioFaceplateEditor {
public:
    explicit VocalDeReverbEditor(VocalDeReverbProcessor&);
private:
    void consume(const float*, int) override;
    void update() override;
    void drawOverlay(juce::Graphics&) override;
    VocalDeReverbProcessor& dsp;
    fp::SpectrumAnalyser before, after;
    std::vector<float> tailIn, tailOut;   // 3 s of 10 ms energy frames (dB)
    double accIn = 0, accOut = 0; int accCount = 0;
    std::vector<std::array<float, 4>> field; size_t fieldWrite = 0;
    fp::FaceSlider* focusSlider = nullptr;
};

class SipaArchitectEditor final : public StudioFaceplateEditor {
public:
    explicit SipaArchitectEditor(SipaArchitectProcessor&);
private:
    void consume(const float*, int) override;
    void update() override;
    void drawOverlay(juce::Graphics&) override;
    void showPresets();
    SipaArchitectProcessor& dsp;
    std::vector<std::array<float, 2>> recent; size_t recentWrite = 0;  // output L/R
    std::vector<float> correlationHistory; size_t correlationWrite = 0;
    double ll = 0, rr = 0, lr = 0, mm = 0, ss = 0; int windowCount = 0;
    float correlation = 0, measuredWidth = 0, rmsL = 0, rmsR = 0;
    std::array<fp::MeterState, 4> outputMeters; // peak L, rms L, peak R, rms R
    float blockPeakL = 0, blockPeakR = 0;
};

class TransientShaperEditor final : public StudioFaceplateEditor {
public:
    explicit TransientShaperEditor(TransientShaperProcessor&);
private:
    void consume(const float*, int) override;
    void update() override;
    void drawOverlay(juce::Graphics&) override;
    TransientShaperProcessor& dsp;
    std::vector<std::array<float, 2>> lanes; size_t laneWrite = 0;      // onset, decay per column
    std::vector<std::array<float, 3>> wave; size_t waveWrite = 0;       // min, max, onset per column
    float laneOnset = 0, laneDecay = 0; int laneCount = 0;
    float waveMin = 0, waveMax = 0, waveOnset = 0; int waveCount = 0;
    std::array<float, 3> monitor{-100, -100, -100};
};

class BreathMouthStudioEditor final : public StudioFaceplateEditor {
public:
    explicit BreathMouthStudioEditor(BreathMouthStudioProcessor&);
private:
    void consume(const float*, int) override;
    void update() override;
    void drawOverlay(juce::Graphics&) override;
    void setView(int v);
    BreathMouthStudioProcessor& dsp;
    fp::SpectrumAnalyser before, after;
    juce::ComboBox* viewBox = nullptr;
    std::array<fp::FaceButton*, 5> tabs{};
    std::vector<std::array<float, 4>> history; size_t historyWrite = 0;  // in min/max, out min/max per column
    std::vector<std::array<float, 2>> reduction; size_t reductionWrite = 0; // gain dB, score per column
    std::vector<std::vector<float>> spectrogram; size_t spectrogramWrite = 0;
    float colInMin = 0, colInMax = 0, colOutMin = 0, colOutMax = 0, colGain = 0, colScore = 0; int colCount = 0;
};

} // namespace hem
