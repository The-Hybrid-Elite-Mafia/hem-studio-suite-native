#include "StudioFaceplateEditors.h"
#include <FaceplateArt.h>

namespace hem {
using namespace fp;
using Rect = juce::Rectangle<float>;

namespace {
juce::String pct(double v) { return juce::String(juce::roundToInt(v)) + "%"; }
juce::String plain(double v) { return juce::String(juce::roundToInt(v)); }
juce::String db1(double v) { return juce::String(v, 1) + " dB"; }
juce::String hz(double v) { return v < 1000 ? juce::String(juce::roundToInt(v)) + " Hz" : juce::String(v / 1000.0, 1) + " kHz"; }
juce::String ms(double v) { return v < 10 ? juce::String(v, 1) + " ms" : juce::String(juce::roundToInt(v)) + " ms"; }
juce::String degrees(double v) { return juce::String(juce::roundToInt(v)) + juce::String(juce::CharPointer_UTF8("\xc2\xb0")); }
juce::String signedInt(double v) { const int i = juce::roundToInt(v); return (i > 0 ? "+" : "") + juce::String(i); }

// Transparent click area over an artwork tile that has no on/off state (Back, Presets).
class HotSpot final : public juce::Button {
public:
    HotSpot() : juce::Button({}) { setMouseCursor(juce::MouseCursor::PointingHandCursor); }
    void paintButton(juce::Graphics& g, bool over, bool down) override {
        const auto b = getLocalBounds().toFloat().reduced(2);
        if (over || down) { g.setColour(colours::cyan.withAlpha(down ? .35f : .18f)); g.fillRoundedRectangle(b, 8); g.setColour(colours::cyan.withAlpha(.8f)); g.drawRoundedRectangle(b, 8, 2); }
    }
};

const std::array<juce::Colour, 4> detectionColours{colours::cyan, colours::orange, colours::yellow, colours::magenta};
}

// =====================================================================================
StudioFaceplateEditor::StudioFaceplateEditor(StudioFaceplateProcessor& p, const void* data, size_t bytes, float w, float h)
    : juce::AudioProcessorEditor(p), processorRef(p), designW(w), designH(h),
      pullBuffer(size_t(studio::ScopeTap::capacity) * studio::ScopeTap::channels, 0.f) {
    art = juce::ImageCache::getFromMemory(data, int(bytes));
    p.scopeTap().discard();
    setOpaque(true);
}
StudioFaceplateEditor::~StudioFaceplateEditor() {
    stopTimer();
    for (auto& c : owned) c->setLookAndFeel(nullptr);
}

void StudioFaceplateEditor::finishSetup() {
    std::vector<juce::RangedAudioParameter*> list;
    for (const auto& spec : processorRef.parameterSpecs())
        if (juce::String(spec.id) != "resetLearn") if (auto* p = param(spec.id)) list.push_back(p);
    history.bind(std::move(list));
    setResizable(true, true);
    setResizeLimits(900, int(900 * designH / designW), 3344, int(3344 * designH / designW));
    if (auto* c = getConstrainer()) c->setFixedAspectRatio(double(designW) / double(designH));
    setSize(1280, juce::roundToInt(1280 * designH / designW));
    startTimerHz(30);
}

juce::Rectangle<int> StudioFaceplateEditor::toScreen(Rect r) const {
    const float k = float(getWidth()) / designW;
    return Rect(r.getX() * k, r.getY() * k, r.getWidth() * k, r.getHeight() * k).getSmallestIntegerContainer();
}
void StudioFaceplateEditor::resized() {
    const float k = float(getWidth()) / designW;
    for (auto& p : placed) {
        p.component->setBounds(toScreen(p.bounds.expanded(p.halo)));
        if (auto* b = dynamic_cast<FaceButton*>(p.component)) b->inset = p.halo * k;
    }
}

std::vector<juce::Component*> StudioFaceplateEditor::interactiveControls() const {
    std::vector<juce::Component*> list;
    for (auto& p : placed) list.push_back(p.component);
    return list;
}

void StudioFaceplateEditor::paint(juce::Graphics& g) {
    g.fillAll(juce::Colours::black);
    const float k = float(getWidth()) / designW;
    g.addTransform(juce::AffineTransform::scale(k));
    g.setImageResamplingQuality(juce::Graphics::mediumResamplingQuality);
    if (art.isValid()) g.drawImage(art, Rect(0, 0, designW, designH), juce::RectanglePlacement::stretchToFit);
    for (const auto& r : readouts) drawValueBox(g, r.box, r.format(r.slider->getValue()));
    if (hasMeters) {
        for (size_t i = 0; i < 4; ++i) {
            drawMeterBar(g, meterBars[i], meterStates[i].db, meterStates[i].hold, meterBottom, meterTop);
            const float hold = meterStates[i].hold;
            drawValueBox(g, meterBoxes[i], hold <= -99 ? juce::String("-inf") : juce::String(hold, 1));
        }
    }
    drawOverlay(g);
}

Knob& StudioFaceplateEditor::knob(const char* id, float cx, float cy, float capRadius, Rect box, std::function<juce::String(double)> format) {
    auto k = std::make_unique<Knob>();
    auto& ref = *k;
    ref.format = format;
    sliderAttachments.push_back(std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(processorRef.state, id, ref));
    if (auto* p = param(id)) { ref.setDoubleClickReturnValue(true, p->convertFrom0to1(p->getDefaultValue())); ref.setTooltip(p->getName(64) + " (double-click resets)"); }
    identifyControl(ref, id);
    ref.onValueChange = [this, box] { repaint(toScreen(box).expanded(3)); };
    const float r = (capRadius + 2) / .82f;
    placed.push_back({&ref, Rect(cx - r, cy - r, 2 * r, 2 * r)});
    readouts.push_back({&ref, box, std::move(format)});
    addAndMakeVisible(ref);
    owned.push_back(std::move(k));
    return ref;
}
FaceSlider& StudioFaceplateEditor::slider(const char* id, Rect bounds, Rect box, std::function<juce::String(double)> format) {
    auto s = std::make_unique<FaceSlider>();
    auto& ref = *s;
    ref.format = format;
    sliderAttachments.push_back(std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(processorRef.state, id, ref));
    if (auto* p = param(id)) { ref.setDoubleClickReturnValue(true, p->convertFrom0to1(p->getDefaultValue())); ref.setTooltip(p->getName(64)); }
    identifyControl(ref, id);
    ref.onValueChange = [this, box] { repaint(toScreen(box).expanded(3)); };
    placed.push_back({&ref, bounds});
    readouts.push_back({&ref, box, std::move(format)});
    addAndMakeVisible(ref);
    owned.push_back(std::move(s));
    return ref;
}
FaceButton& StudioFaceplateEditor::toggle(const char* id, Rect r, const juce::String& text, FaceButton::Style style) {
    auto b = std::make_unique<FaceButton>(text, style);
    auto& ref = *b;
    ref.setClickingTogglesState(true);
    if (auto* p = param(id)) ref.setTooltip(p->getName(64));
    identifyControl(ref, id);
    buttonAttachments.push_back(std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(processorRef.state, id, ref));
    placed.push_back({&ref, r, 5});
    addAndMakeVisible(ref);
    owned.push_back(std::move(b));
    return ref;
}
void StudioFaceplateEditor::choice(const char* id, std::vector<std::pair<Rect, juce::String>> options, FaceButton::Style style) {
    auto* p = param(id);
    jassert(p != nullptr);
    if (p == nullptr) return;
    auto binding = std::make_unique<ChoiceBinding>();
    auto* raw = binding.get();
    for (size_t i = 0; i < options.size(); ++i) {
        auto b = std::make_unique<FaceButton>(options[i].second, style);
        auto& ref = *b;
        ref.setTooltip(p->getName(64) + ": " + options[i].second);
        ref.setComponentID(i == 0 ? juce::String(id) : juce::String(id) + "_" + juce::String(int(i)));
        ref.setName(i == 0 ? p->getName(64) : p->getName(64) + " " + options[i].second);
        ref.getProperties().set("hemChoiceParameter", juce::String(id));
        ref.getProperties().set("hemChoiceValue", int(i));
        ref.onClick = [raw, i] { if (raw->attachment) raw->attachment->setValueAsCompleteGesture(float(i)); };
        raw->buttons.push_back(&ref);
        placed.push_back({&ref, options[i].first, 5});
        addAndMakeVisible(ref);
        owned.push_back(std::move(b));
    }
    raw->attachment = std::make_unique<juce::ParameterAttachment>(*p, [raw](float v) {
        const int selected = juce::roundToInt(v);
        for (size_t i = 0; i < raw->buttons.size(); ++i) raw->buttons[i]->setToggleState(int(i) == selected, juce::dontSendNotification);
    });
    raw->attachment->sendInitialUpdate();
    choices.push_back(std::move(binding));
}
void StudioFaceplateEditor::viewChoice(const juce::Identifier& key, int fallback, std::vector<std::pair<Rect, juce::String>> options) {
    auto binding = std::make_unique<ViewBinding>();
    binding->key = key;
    auto* raw = binding.get();
    const int current = view(key, fallback);
    for (size_t i = 0; i < options.size(); ++i) {
        auto b = std::make_unique<FaceButton>(options[i].second);
        auto& ref = *b;
        ref.setToggleState(int(i) == current, juce::dontSendNotification);
        ref.onClick = [this, raw, i] {
            processorRef.setViewSetting(raw->key, int(i));
            for (size_t j = 0; j < raw->buttons.size(); ++j) raw->buttons[j]->setToggleState(j == i, juce::dontSendNotification);
            repaint();
        };
        raw->buttons.push_back(&ref);
        placed.push_back({&ref, options[i].first, 5});
        addAndMakeVisible(ref);
        owned.push_back(std::move(b));
    }
    views.push_back(std::move(binding));
}
FaceButton& StudioFaceplateEditor::action(Rect r, const juce::String& text, std::function<void()> onClick, FaceButton::Style style) {
    auto b = std::make_unique<FaceButton>(text, style);
    auto& ref = *b;
    ref.onClick = std::move(onClick);
    placed.push_back({&ref, r, 5});
    addAndMakeVisible(ref);
    owned.push_back(std::move(b));
    return ref;
}
juce::Component& StudioFaceplateEditor::hotspot(Rect r, const juce::String& tooltip, std::function<void()> onClick) {
    auto b = std::make_unique<HotSpot>();
    auto& ref = *b;
    ref.setTooltip(tooltip);
    ref.onClick = std::move(onClick);
    placed.push_back({&ref, r});
    addAndMakeVisible(ref);
    owned.push_back(std::move(b));
    return ref;
}
void StudioFaceplateEditor::historyButtons(Rect ab, Rect undo, Rect redo) {
    abButton = &action(ab, "A / B", [this] { history.toggleAB(); if (abButton) abButton->setToggleState(history.activeSlot() == 1, juce::dontSendNotification); });
    abButton->setTooltip("Compare two settings: lit = B");
    action(undo, "UNDO", [this] { history.undoStep(); }).setTooltip("Undo the last change");
    action(redo, "REDO", [this] { history.redoStep(); }).setTooltip("Redo");
}
void StudioFaceplateEditor::meters(std::array<Rect, 4> bars, std::array<Rect, 4> boxes, float bottomDb, float topDb) {
    meterBars = bars; meterBoxes = boxes; meterBottom = bottomDb; meterTop = topDb; hasMeters = true;
}
void StudioFaceplateEditor::momentary(const char* id, Rect r, const juce::String& text) {
    // A bound toggle that releases itself: the audio thread sees one rising edge.
    auto& b = toggle(id, r, text);
    const juce::Component::SafePointer<FaceButton> button(&b);
    b.onClick = [button] {
        if (button == nullptr || !button->getToggleState()) return;
        juce::Timer::callAfterDelay(300, [button] { if (button != nullptr) button->setToggleState(false, juce::sendNotificationSync); });
    };
}
juce::ComboBox& StudioFaceplateEditor::combo(const char* id, Rect r, const juce::StringArray& items) {
    auto c = std::make_unique<juce::ComboBox>();
    auto& ref = *c;
    ref.setLookAndFeel(&look);
    ref.addItemList(items, 1);
    ref.setMouseCursor(juce::MouseCursor::PointingHandCursor);
    if (id != nullptr) {
        identifyControl(ref, id);
        if (auto* p = param(id)) ref.setTooltip(p->getName(64));
        comboAttachments.push_back(std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(processorRef.state, id, ref));
    }
    placed.push_back({&ref, r});
    addAndMakeVisible(ref);
    owned.push_back(std::move(c));
    return ref;
}
void StudioFaceplateEditor::resetAllToDefaults() {
    for (const auto& spec : processorRef.parameterSpecs())
        if (auto* p = param(spec.id)) { p->beginChangeGesture(); p->setValueNotifyingHost(p->getDefaultValue()); p->endChangeGesture(); }
}

void StudioFaceplateEditor::timerCallback() {
    const int n = processorRef.scopeTap().pull(pullBuffer.data(), int(studio::ScopeTap::capacity));
    std::array<float, 4> peak{};
    for (int i = 0; i < n; ++i) for (size_t c = 0; c < 4; ++c) peak[c] = std::max(peak[c], std::abs(pullBuffer[size_t(i) * studio::ScopeTap::channels + c]));
    for (size_t c = 0; c < 4; ++c) meterStates[c].feed(peak[c]);
    consume(pullBuffer.data(), n);
    update();
    history.poll();
    if (abButton) abButton->setToggleState(history.activeSlot() == 1, juce::dontSendNotification);
    for (const auto& r : dynamicRegions) repaint(toScreen(r).expanded(2));
    if (hasMeters) for (size_t i = 0; i < 4; ++i) { repaint(toScreen(meterBars[i]).expanded(3)); repaint(toScreen(meterBoxes[i]).expanded(3)); }
}

// =====================================================================================
// Vocal De-Reverb (artwork 1672 x 941)
VocalDeReverbEditor::VocalDeReverbEditor(VocalDeReverbProcessor& p)
    : StudioFaceplateEditor(p, FaceplateArt::vocal_de_reverb_jpg, size_t(FaceplateArt::vocal_de_reverb_jpgSize), 1672, 941), dsp(p),
      tailIn(300, -100.f), tailOut(300, -100.f), field(2048, std::array<float, 4>{}) {
    meters({Rect(74, 228, 24, 196), Rect(162, 228, 24, 196), Rect(1488, 228, 24, 196), Rect(1576, 228, 24, 196)},
           {Rect(62, 431, 52, 26), Rect(140, 431, 58, 26), Rect(1472, 431, 58, 26), Rect(1550, 431, 62, 26)});
    const std::array<const char*, 10> ids{"reduction", "early", "tail", "ambience", "roomSize", "damping", "clarity", "presence", "mix", "output"};
    const std::array<float, 10> xs{120, 280, 438, 597, 756, 914, 1073, 1232, 1391, 1551};
    for (size_t i = 0; i < ids.size(); ++i) knob(ids[i], xs[i], 586, 30, Rect(xs[i] - 47, 645, 94, 26), i == 9 ? db1 : pct);
    choice("mode", {{Rect(62, 716, 152, 26), "NATURAL"}, {Rect(62, 749, 152, 26), "AGGRESSIVE"}, {Rect(62, 781, 152, 26), "TRANSPARENT"}});
    choice("algorithm", {{Rect(282, 716, 158, 26), "SPECTRAL"}, {Rect(282, 749, 158, 26), "ADAPTIVE"}, {Rect(282, 781, 158, 26), "LEARNED"}});
    choice("stereo", {{Rect(1233, 719, 122, 26), "MID"}, {Rect(1233, 751, 122, 26), "SIDE"}, {Rect(1233, 783, 122, 26), "MID / SIDE"}});
    toggle("learn", Rect(1452, 719, 156, 24), "LEARN", FaceButton::Style::blue).setTooltip("Learn the room from the material while it plays (used by LEARNED)");
    momentary("resetLearn", Rect(1452, 751, 156, 24), "RESET");
    toggle("bypass", Rect(1452, 783, 156, 24), "BYPASS", FaceButton::Style::red);
    focusSlider = &slider("focus", Rect(495, 718, 320, 54), Rect(615, 780, 82, 26), hz);
    slider("sensitivity", Rect(883, 718, 252, 54), Rect(970, 780, 84, 26), pct);
    viewChoice("fieldView", 2, {{Rect(1095, 440, 104, 36), "BEFORE"}, {Rect(1205, 440, 100, 36), "AFTER"}, {Rect(1314, 440, 96, 36), "BOTH"}});
    historyButtons(Rect(1105, 852, 80, 38), Rect(1200, 852, 84, 38), Rect(1297, 852, 84, 38));
    dynamicRegion(Rect(296, 214, 348, 236)); dynamicRegion(Rect(676, 214, 386, 230)); dynamicRegion(Rect(1138, 228, 226, 226));
    finishSetup();
}
void VocalDeReverbEditor::consume(const float* f, int n) {
    const int frameLength = std::max(1, int(rate() * .01));
    for (int i = 0; i < n; ++i, f += studio::ScopeTap::channels) {
        before.push(.5f * (f[0] + f[1])); after.push(.5f * (f[2] + f[3]));
        field[fieldWrite] = {f[0], f[1], f[2], f[3]}; fieldWrite = (fieldWrite + 1) % field.size();
        accIn += double(f[0]) * f[0] + double(f[1]) * f[1]; accOut += double(f[2]) * f[2] + double(f[3]) * f[3];
        if (++accCount >= frameLength) {
            tailIn.erase(tailIn.begin()); tailIn.push_back(float(10 * std::log10(accIn / (2 * accCount) + 1.e-12)));
            tailOut.erase(tailOut.begin()); tailOut.push_back(float(10 * std::log10(accOut / (2 * accCount) + 1.e-12)));
            accIn = accOut = 0; accCount = 0;
        }
    }
}
void VocalDeReverbEditor::update() { before.update(); after.update(); }
void VocalDeReverbEditor::drawOverlay(juce::Graphics& g) {
    // Spectrum: 20 Hz - 20 kHz, 0 to -80 dB (axis labels are on the artwork).
    const Rect spec(300, 218, 340, 228), specY(300, 235, 340, 211);
    fillPanel(g, spec);
    g.setColour(colours::grid);
    for (float f : {50.f, 100.f, 200.f, 500.f, 1000.f, 2000.f, 5000.f, 10000.f}) g.drawVerticalLine(int(freqToX(f, spec)), spec.getY(), spec.getBottom());
    for (int d = 1; d < 4; ++d) g.drawHorizontalLine(int(specY.getY() + specY.getHeight() * float(d) / 4), spec.getX(), spec.getRight());
    {
        const auto pb = spectrumPath(before, spec, rate(), 0, -80, specY, true), pa = spectrumPath(after, spec, rate(), 0, -80, specY, true);
        g.setColour(colours::orange.withAlpha(.22f)); g.fillPath(pb);
        drawTrace(g, spectrumPath(before, spec, rate(), 0, -80, specY, false), colours::orange, 1.2f);
        g.setColour(colours::cyan.withAlpha(.22f)); g.fillPath(pa);
        drawTrace(g, spectrumPath(after, spec, rate(), 0, -80, specY, false), colours::cyan, 1.2f);
    }
    drawLegend(g, {546, 226}, {{colours::orange, "Before"}, {colours::cyan, "After"}});
    g.setColour(colours::dimText); g.setFont(font(12, false));
    const float t60 = dsp.engine().t60Seconds.load(), red = dsp.engine().reductionDb.load();
    juce::String status = "T60 " + juce::String(t60, 2) + " s   " + juce::String(red, 1) + " dB";
    if (dsp.engine().learnedReady.load()) status << "   ROOM LEARNED";
    g.drawText(status, Rect(306, 422, 330, 18), juce::Justification::centredLeft);

    // Reverb tail: last 3 s of signal energy (10 ms frames), 0 to -80 dB.
    const Rect tail(680, 218, 378, 222), tailX(683, 222, 364, 214);
    fillPanel(g, tail);
    drawGrid(g, tailX, 6, 4);
    auto curve = [&](const std::vector<float>& v, bool closed) {
        juce::Path p;
        for (size_t i = 0; i < v.size(); ++i) {
            const float x = tailX.getX() + tailX.getWidth() * float(i) / float(v.size() - 1);
            const float y = juce::jlimit(tailX.getY(), tailX.getBottom(), tailX.getY() + tailX.getHeight() * (-v[i]) / 80.f);
            if (i == 0) { if (closed) { p.startNewSubPath(x, tailX.getBottom()); p.lineTo(x, y); } else p.startNewSubPath(x, y); } else p.lineTo(x, y);
        }
        if (closed) { p.lineTo(tailX.getRight(), tailX.getBottom()); p.closeSubPath(); }
        return p;
    };
    g.setGradientFill(juce::ColourGradient(colours::orange.withAlpha(.45f), 0, tailX.getY(), colours::orange.withAlpha(.05f), 0, tailX.getBottom(), false)); g.fillPath(curve(tailIn, true));
    drawTrace(g, curve(tailIn, false), colours::orange, 1.1f);
    g.setGradientFill(juce::ColourGradient(colours::cyan.withAlpha(.45f), 0, tailX.getY(), colours::cyan.withAlpha(.05f), 0, tailX.getBottom(), false)); g.fillPath(curve(tailOut, true));
    drawTrace(g, curve(tailOut, false), colours::cyan, 1.1f);
    drawLegend(g, {966, 226}, {{colours::orange, "Before"}, {colours::cyan, "After"}});
    g.setColour(colours::dimText); g.setFont(font(11, false));
    g.drawText("NOW", Rect(1010, 420, 44, 14), juce::Justification::centredRight);

    // Stereo field (goniometer): M up, S across.
    const juce::Point<float> c(1251, 340); const float radius = 108;
    g.setColour(colours::panel); g.fillEllipse(Rect(radius * 2, radius * 2).withCentre(c));
    g.setColour(colours::cyan.withAlpha(.35f));
    for (float k : {.33f, .66f, 1.f}) g.drawEllipse(Rect(radius * 2 * k, radius * 2 * k).withCentre(c), k == 1.f ? 1.4f : .8f);
    g.drawLine(c.x - radius, c.y, c.x + radius, c.y, .8f); g.drawLine(c.x, c.y - radius, c.x, c.y + radius, .8f);
    const int which = view("fieldView", 2);
    auto dots = [&](size_t offset, juce::Colour colour) {
        g.setColour(colour.withAlpha(.55f));
        for (size_t i = 0; i < field.size(); i += 2) {
            const float l = field[i][offset], r = field[i][offset + 1];
            const float m = .5f * (l + r), s = .5f * (l - r), mag = std::sqrt(m * m + s * s);
            if (mag < 1.e-5f) continue;
            const float scaled = radius * std::min(1.f, std::sqrt(mag * 1.4f)) / mag;
            g.fillRect(c.x - s * scaled - .8f, c.y - m * scaled - .8f, 1.6f, 1.6f);
        }
    };
    if (which != 1) dots(0, colours::orange);
    if (which != 0) dots(2, colours::cyan);
    // Frequency focus scale under the slider (log, matches the slider travel).
    const Rect labels(498, 762, 318, 16);
    g.setColour(juce::Colour(0xff090c10)); g.fillRect(labels);
    g.setColour(colours::text); g.setFont(font(13, false));
    const float k = float(getWidth()) / designW;
    for (auto [f, t] : std::initializer_list<std::pair<float, const char*>>{{20.f, "20"}, {200.f, "200"}, {2000.f, "2K"}, {20000.f, "20K"}}) {
        const float x = (float(focusSlider->getX()) + float(focusSlider->getPositionOfValue(f))) / k;
        g.drawText(t, Rect(x - 20, labels.getY(), 40, 16), juce::Justification::centred);
    }
}

// =====================================================================================
// S.I.P.A. (artwork 1536 x 864)
SipaArchitectEditor::SipaArchitectEditor(SipaArchitectProcessor& p)
    : StudioFaceplateEditor(p, FaceplateArt::sipa_architect_jpg, size_t(FaceplateArt::sipa_architect_jpgSize), 1536, 864), dsp(p),
      recent(1024, std::array<float, 2>{}), correlationHistory(160, 0.f) {
    meters({Rect(55, 210, 20, 160), Rect(128, 210, 20, 160), Rect(1392, 210, 20, 160), Rect(1466, 210, 20, 160)},
           {Rect(46, 381, 52, 24), Rect(104, 381, 52, 24), Rect(1384, 381, 52, 24), Rect(1441, 381, 52, 24)}, -60, 0);
    hotspot(Rect(45, 40, 140, 92), "Back to the rack", [this] { requestClose(); });
    hotspot(Rect(1352, 40, 140, 92), "Presets", [this] { showPresets(); });
    viewChoice("fieldView", 0, {{Rect(198, 409, 86, 28), "VECTOR"}, {Rect(299, 409, 86, 28), "POLAR"}, {Rect(400, 409, 100, 28), "CORRELATION"}});
    viewChoice("scopeView", 1, {{Rect(537, 410, 82, 26), "LEFT"}, {Rect(631, 410, 82, 26), "BOTH"}, {Rect(722, 410, 82, 26), "RIGHT"}});
    const std::array<std::pair<float, const char*>, 4> presets{{{50.f, "NARROW"}, {100.f, "NORMAL"}, {150.f, "WIDE"}, {200.f, "MAX"}}};
    const std::array<Rect, 4> presetRects{Rect(838, 412, 56, 24), Rect(902, 412, 58, 24), Rect(968, 412, 62, 24), Rect(1036, 412, 50, 24)};
    for (size_t i = 0; i < presets.size(); ++i) {
        const float target = presets[i].first;
        auto& b = action(presetRects[i], presets[i].second, [this, target] { setValue("width", target); });
        b.textScale = .42f;
        b.setTooltip("Width " + juce::String(int(target)) + "%");
    }
    toggle("monoCheck", Rect(1121, 381, 214, 38), "MONO CHECK", FaceButton::Style::blue);
    const std::array<const char*, 6> ids{"width", "panLaw", "centerFocus", "depth", "bass", "output"};
    const std::array<float, 6> xs{97, 238, 380, 521, 662, 803};
    const std::array<std::function<juce::String(double)>, 6> formats{pct, db1, plain, plain, [](double v) { return v < 20 ? juce::String("OFF") : hz(v); }, db1};
    for (size_t i = 0; i < ids.size(); ++i) knob(ids[i], xs[i], 512, 31, Rect(xs[i] - 42, 595, 84, 22), formats[i]);
    choice("phaseMode", {{Rect(915, 489, 108, 30), "NORMAL"}, {Rect(915, 527, 108, 26), "INVERT L"}, {Rect(915, 559, 108, 26), "INVERT R"}, {Rect(915, 591, 108, 26), "SWAP L/R"}});
    knob("phaseRotate", 1114, 544, 25, Rect(1072, 597, 84, 22), degrees);
    choice("stereoMode", {{Rect(1236, 489, 104, 30), "STEREO"}, {Rect(1236, 527, 104, 26), "MID / SIDE"}, {Rect(1236, 559, 104, 26), "SIDE ONLY"}, {Rect(1236, 591, 104, 26), "MONO"}});
    knob("msBalance", 1430, 544, 25, Rect(1390, 597, 84, 22), signedInt);
    knob("harmonicWidth", 103, 708, 28, Rect(63, 748, 80, 22), plain);
    knob("air", 249, 708, 28, Rect(209, 748, 80, 22), plain);
    knob("hpfSide", 409, 712, 22, Rect(369, 750, 80, 20), [](double v) { return v <= 20.5 ? juce::String("OFF") : hz(v); });
    knob("lpfSide", 540, 712, 22, Rect(500, 750, 80, 20), [](double v) { return v >= 19999 ? juce::String("OFF") : hz(v); });
    knob("dynamicWidth", 991, 708, 28, Rect(951, 748, 80, 22), plain);
    knob("trim", 1137, 708, 28, Rect(1097, 748, 80, 22), db1);
    toggle("bypass", Rect(40, 798, 150, 46), "BYPASS", FaceButton::Style::red);
    historyButtons(Rect(822, 800, 94, 40), Rect(930, 800, 94, 40), Rect(1040, 800, 94, 40));
    dynamicRegion(Rect(250, 218, 206, 196)); dynamicRegion(Rect(528, 220, 288, 190)); dynamicRegion(Rect(834, 220, 248, 190));
    dynamicRegion(Rect(1114, 252, 226, 98)); dynamicRegion(Rect(1262, 664, 210, 106));
    finishSetup();
}
void SipaArchitectEditor::showPresets() {
    struct Preset { const char* name; std::vector<std::pair<const char*, float>> values; };
    static const std::vector<Preset> presets{
        {"Default (neutral)", {}},
        {"Vocal: centered and clean", {{"width", 90}, {"centerFocus", 40}, {"bass", 120}, {"hpfSide", 150}}},
        {"Mix bus: wide and safe", {{"width", 130}, {"bass", 120}, {"dynamicWidth", 35}, {"air", 25}}},
        {"Master: polish", {{"width", 112}, {"bass", 100}, {"air", 20}, {"harmonicWidth", 10}}},
        {"Pads: huge", {{"width", 175}, {"depth", 45}, {"harmonicWidth", 30}, {"air", 40}, {"bass", 150}}},
        {"Mono-compatible check", {{"monoCheck", 1}}},
    };
    juce::PopupMenu menu;
    for (size_t i = 0; i < presets.size(); ++i) menu.addItem(int(i) + 1, presets[i].name);
    const juce::Component::SafePointer<SipaArchitectEditor> self(this);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this).withMousePosition(), [self](int result) {
        if (self == nullptr || result <= 0) return;
        self->resetAllToDefaults();
        for (const auto& [id, v] : presets[size_t(result - 1)].values) self->setValue(id, v);
    });
}
void SipaArchitectEditor::consume(const float* f, int n) {
    for (int i = 0; i < n; ++i, f += studio::ScopeTap::channels) {
        const float l = f[2], r = f[3];
        recent[recentWrite] = {l, r}; recentWrite = (recentWrite + 1) % recent.size();
        ll += double(l) * l; rr += double(r) * r; lr += double(l) * r;
        const double m = .5 * (l + r), s = .5 * (l - r); mm += m * m; ss += s * s;
        blockPeakL = std::max(blockPeakL, std::abs(l)); blockPeakR = std::max(blockPeakR, std::abs(r));
        ++windowCount;
    }
}
void SipaArchitectEditor::update() {
    const int window = std::max(1, int(rate() * .05));
    if (windowCount >= window) {
        correlation = ll * rr > 1.e-12 ? float(lr / std::sqrt(ll * rr)) : 0.f;
        measuredWidth = mm + ss > 1.e-12 ? float(std::sqrt(ss / (mm + ss))) : 0.f;
        rmsL = float(std::sqrt(ll / windowCount)); rmsR = float(std::sqrt(rr / windowCount));
        correlationHistory[correlationWrite] = correlation; correlationWrite = (correlationWrite + 1) % correlationHistory.size();
        ll = rr = lr = mm = ss = 0; windowCount = 0;
    }
    outputMeters[0].feed(blockPeakL); outputMeters[1].feed(rmsL); outputMeters[2].feed(blockPeakR); outputMeters[3].feed(rmsR);
    blockPeakL = blockPeakR = 0;
}
void SipaArchitectEditor::drawOverlay(juce::Graphics& g) {
    // Stereo field.
    const juce::Point<float> c(352, 316); const float rx = 96, ry = 92;
    g.setColour(colours::panel); g.fillEllipse(Rect(rx * 2, ry * 2).withCentre(c));
    g.setColour(colours::cyan.withAlpha(.3f));
    for (float k : {.33f, .66f, 1.f}) g.drawEllipse(Rect(rx * 2 * k, ry * 2 * k).withCentre(c), k == 1.f ? 1.4f : .8f);
    g.drawLine(c.x - rx, c.y, c.x + rx, c.y, .7f); g.drawLine(c.x, c.y - ry, c.x, c.y + ry, .7f);
    const int fieldView = view("fieldView", 0);
    if (fieldView == 2) {
        // Correlation history across the circle, +1 at top, -1 at bottom.
        juce::Path p;
        for (size_t i = 0; i < correlationHistory.size(); ++i) {
            const float v = correlationHistory[(correlationWrite + i) % correlationHistory.size()];
            const float x = c.x - rx * .9f + 1.8f * rx * float(i) / float(correlationHistory.size() - 1), y = c.y - v * ry * .85f;
            if (i == 0) p.startNewSubPath(x, y); else p.lineTo(x, y);
        }
        drawTrace(g, p, correlation < 0 ? colours::red : colours::cyan, 1.4f);
    } else {
        g.setColour(colours::cyan.withAlpha(.6f));
        for (size_t i = 0; i < recent.size(); ++i) {
            const float l = recent[i][0], r = recent[i][1];
            const float m = .5f * (l + r), s = .5f * (l - r), mag = std::sqrt(l * l + r * r);
            if (mag < 1.e-5f) continue;
            float x, y;
            if (fieldView == 0) { const float k = std::min(1.f, std::sqrt(mag)) / std::max(1.e-6f, std::sqrt(m * m + s * s)); x = c.x - s * k * rx; y = c.y - m * k * ry; }
            else { const float angle = std::atan2(-s, std::abs(m)) * (m < 0 ? -1.f : 1.f), rad = std::min(1.f, std::sqrt(mag)); x = c.x + std::sin(angle) * rad * rx; y = c.y - std::cos(angle) * rad * ry; }
            g.fillRect(x - .9f, y - .9f, 1.8f, 1.8f);
        }
    }
    // Phase scope: last 10 ms.
    const Rect scope(532, 224, 280, 160);
    fillPanel(g, scope); drawGrid(g, scope, 8, 4);
    const int scopeView = view("scopeView", 1);
    const int span = std::min(int(recent.size()), std::max(64, int(rate() * .01)));
    for (int ch = 0; ch < 2; ++ch) {
        if ((scopeView == 0 && ch == 1) || (scopeView == 2 && ch == 0)) continue;
        juce::Path p;
        for (int i = 0; i < span; ++i) {
            const float v = recent[(recentWrite + recent.size() - size_t(span) + size_t(i)) % recent.size()][size_t(ch)];
            const float x = scope.getX() + scope.getWidth() * float(i) / float(span - 1), y = juce::jlimit(scope.getY(), scope.getBottom(), scope.getCentreY() - v * scope.getHeight() * .48f);
            if (i == 0) p.startNewSubPath(x, y); else p.lineTo(x, y);
        }
        drawTrace(g, p, ch == 0 ? colours::orange : colours::blue, 1.3f);
    }
    const float phase = std::acos(juce::jlimit(-1.f, 1.f, correlation)) * 180.f / juce::MathConstants<float>::pi;
    drawValueBox(g, Rect(594, 386, 210, 20), "PHASE: " + juce::String(phase, 1) + juce::String(juce::CharPointer_UTF8("\xc2\xb0")));
    // Stereo width fan (measured spread) with the WIDTH setting.
    const Rect fan(838, 224, 240, 160);
    fillPanel(g, fan);
    const juce::Point<float> apex(958, 380); const float reach = 148, maxAngle = juce::degreesToRadians(52.f);
    g.setColour(colours::blue.withAlpha(.35f));
    for (float k : {.25f, .5f, .75f, 1.f}) { juce::Path a; a.addCentredArc(apex.x, apex.y, reach * k, reach * k, 0, -maxAngle, maxAngle, true); g.strokePath(a, juce::PathStrokeType(.8f)); }
    g.drawLine({apex, apex.getPointOnCircumference(reach, -maxAngle)}, 1); g.drawLine({apex, apex.getPointOnCircumference(reach, maxAngle)}, 1);
    const float spread = maxAngle * juce::jlimit(0.f, 1.f, measuredWidth * 1.4142f);
    juce::Path wedge; wedge.startNewSubPath(apex); wedge.addCentredArc(apex.x, apex.y, reach, reach, 0, -spread, spread); wedge.closeSubPath();
    g.setGradientFill(juce::ColourGradient(colours::cyan.withAlpha(.55f), apex.x, apex.y, colours::orange.withAlpha(.35f), apex.x, apex.y - reach, false)); g.fillPath(wedge);
    g.setColour(juce::Colours::white); g.drawLine(apex.x, apex.y, apex.x, apex.y - reach, 1.6f);
    g.setColour(colours::text); g.setFont(font(13)); g.drawText("L", Rect(848, 230, 16, 16), juce::Justification::centred); g.drawText("R", Rect(1052, 230, 16, 16), juce::Justification::centred);
    drawValueBox(g, Rect(874, 386, 170, 20), "WIDTH: " + pct(valueOf("width")));
    // Correlation meter.
    const Rect bar(1120, 258, 214, 30);
    g.setColour(juce::Colour(0xff020304)); g.fillRect(bar.expanded(2));
    const int segments = 28;
    for (int i = 0; i < segments; ++i) {
        const float v = -1 + 2 * (float(i) + .5f) / float(segments);
        const juce::Colour col = v < 0 ? colours::red.interpolatedWith(colours::yellow, v + 1) : colours::yellow.interpolatedWith(juce::Colour(0xff35e05a), v);
        const float x = bar.getX() + bar.getWidth() * float(i) / float(segments);
        g.setColour(col.withAlpha(v <= correlation ? 1.f : .2f)); g.fillRect(x + 1, bar.getY() + 2, bar.getWidth() / float(segments) - 2, bar.getHeight() - 4);
    }
    const float cx = bar.getX() + bar.getWidth() * (correlation + 1) * .5f;
    g.setColour(juce::Colours::white); g.fillRect(cx - 1.5f, bar.getY() - 2, 3.f, bar.getHeight() + 4);
    drawValueBox(g, Rect(1123, 341, 210, 26), "CORRELATION: " + juce::String(correlation >= 0 ? "+" : "") + juce::String(correlation, 2));
    // Output meter (peak + RMS per side), +6 to -48 dB.
    const std::array<Rect, 4> bars{Rect(1268, 668, 16, 98), Rect(1290, 668, 16, 98), Rect(1418, 668, 16, 98), Rect(1440, 668, 16, 98)};
    for (size_t i = 0; i < 4; ++i) drawMeterBar(g, bars[i], outputMeters[i].db, i % 2 == 0 ? outputMeters[i].hold : -100, -48, 6);
}

// =====================================================================================
// Transient Shaper (artwork 1672 x 941)
TransientShaperEditor::TransientShaperEditor(TransientShaperProcessor& p)
    : StudioFaceplateEditor(p, FaceplateArt::transient_shaper_jpg, size_t(FaceplateArt::transient_shaper_jpgSize), 1672, 941), dsp(p),
      lanes(253, std::array<float, 2>{}), wave(199, std::array<float, 3>{}) {
    meters({Rect(68, 226, 24, 198), Rect(150, 226, 24, 198), Rect(1497, 226, 24, 198), Rect(1578, 226, 24, 198)},
           {Rect(56, 431, 52, 26), Rect(132, 431, 54, 26), Rect(1484, 431, 56, 26), Rect(1562, 431, 56, 26)});
    choice("detection", {{Rect(1246, 216, 172, 28), "TRANSIENT"}, {Rect(1246, 252, 172, 28), "SUSTAIN"}, {Rect(1246, 289, 172, 28), "ADAPTIVE"}});
    choice("focusRange", {{Rect(1246, 365, 172, 28), "LOW"}, {Rect(1246, 401, 172, 28), "MID"}, {Rect(1246, 437, 172, 28), "HIGH"}});
    const std::array<const char*, 9> ids{"attackAmount", "sustainAmount", "sensitivity", "length", "lowFocus", "highFocus", "clipGuard", "mix", "output"};
    const std::array<float, 9> xs{120, 293, 468, 650, 834, 1013, 1190, 1369, 1548};
    for (size_t i = 0; i < ids.size(); ++i) {
        std::function<juce::String(double)> format = i == 3 ? [](double v) { return v < .5 ? juce::String("AUTO") : juce::String(juce::roundToInt(v)) + " ms"; }
                                                   : i == 8 ? std::function<juce::String(double)>(db1) : i >= 6 ? std::function<juce::String(double)>(plain) : std::function<juce::String(double)>(signedInt);
        knob(ids[i], xs[i], 590, 30, i == 3 ? Rect(582, 660, 136, 26) : Rect(xs[i] - 48, 660, 96, 26), format);
    }
    choice("processMode", {{Rect(66, 754, 92, 40), "CLEAN"}, {Rect(170, 754, 90, 40), "PUNCH"}, {Rect(272, 754, 90, 40), "SMOOTH"}, {Rect(376, 754, 92, 40), "AGGRESSIVE"}});
    toggle("solo", Rect(1090, 755, 114, 38), "SOLO").setTooltip("Listen to the detector band (Focus Range)");
    toggle("delta", Rect(1222, 755, 114, 38), "DELTA").setTooltip("Listen to only what the shaper adds or removes");
    toggle("bypass", Rect(1355, 755, 114, 38), "BYPASS", FaceButton::Style::red);
    action(Rect(1489, 755, 116, 38), "RESET", [this] { resetAllToDefaults(); }).setTooltip("Reset every control to its default");
    dynamicRegion(Rect(234, 212, 534, 242)); dynamicRegion(Rect(796, 212, 406, 242)); dynamicRegion(Rect(640, 734, 388, 70));
    finishSetup();
}
void TransientShaperEditor::consume(const float* f, int n) {
    const int laneLength = std::max(1, int(rate() * 2.5 / double(lanes.size())));
    const int waveLength = std::max(1, int(rate() * .6 / double(wave.size())));
    for (int i = 0; i < n; ++i, f += studio::ScopeTap::channels) {
        laneOnset = std::max(laneOnset, f[4]); laneDecay = std::max(laneDecay, f[5]);
        if (++laneCount >= laneLength) { lanes[laneWrite] = {laneOnset, laneDecay}; laneWrite = (laneWrite + 1) % lanes.size(); laneOnset = laneDecay = 0; laneCount = 0; }
        const float o = .5f * (f[2] + f[3]);
        waveMin = std::min(waveMin, o); waveMax = std::max(waveMax, o); waveOnset = std::max(waveOnset, f[4]);
        if (++waveCount >= waveLength) { wave[waveWrite] = {waveMin, waveMax, waveOnset}; waveWrite = (waveWrite + 1) % wave.size(); waveMin = waveMax = waveOnset = 0; waveCount = 0; }
    }
}
void TransientShaperEditor::update() {
    const std::array<float, 3> now{dsp.engine().transientLevel.load(), dsp.engine().sustainLevel.load(), dsp.engine().outputLevel.load()};
    for (size_t i = 0; i < 3; ++i) monitor[i] = now[i] > monitor[i] ? now[i] : std::max(now[i], monitor[i] - 1.5f);
}
void TransientShaperEditor::drawOverlay(juce::Graphics& g) {
    // Transient analysis lanes (2.5 s): onset spikes (cyan), sustain/decay (orange).
    const Rect cover(236, 214, 530, 238), lanesArea(258, 222, 500, 222);
    fillPanel(g, cover); drawGrid(g, lanesArea, 10, 4);
    juce::Path onset, decay;
    for (size_t i = 0; i < lanes.size(); ++i) {
        const auto& v = lanes[(laneWrite + i) % lanes.size()];
        const float x = lanesArea.getX() + lanesArea.getWidth() * float(i) / float(lanes.size() - 1);
        const float yo = lanesArea.getBottom() - lanesArea.getHeight() * .95f * v[0], yd = lanesArea.getBottom() - lanesArea.getHeight() * (.25f + .3f * v[1]);
        if (i == 0) { onset.startNewSubPath(x, yo); decay.startNewSubPath(x, yd); } else { onset.lineTo(x, yo); decay.lineTo(x, yd); }
    }
    drawTrace(g, decay, colours::orange, 1.3f);
    drawTrace(g, onset, colours::cyan, 1.3f);
    drawLegend(g, {626, 232}, {{colours::cyan, "TRANSIENTS"}, {colours::orange, "SUSTAIN"}}, 12);
    // Waveform detail (0.6 s): onset portions cyan, the rest orange.
    const Rect waveArea(800, 215, 398, 235);
    fillPanel(g, waveArea);
    g.setColour(colours::grid); g.drawHorizontalLine(int(waveArea.getCentreY()), waveArea.getX(), waveArea.getRight());
    const float columnWidth = waveArea.getWidth() / float(wave.size());
    for (size_t i = 0; i < wave.size(); ++i) {
        const auto& v = wave[(waveWrite + i) % wave.size()];
        const float x = waveArea.getX() + columnWidth * float(i);
        const float top = waveArea.getCentreY() - juce::jlimit(-1.f, 1.f, v[1]) * waveArea.getHeight() * .46f, bottom = waveArea.getCentreY() - juce::jlimit(-1.f, 1.f, v[0]) * waveArea.getHeight() * .46f;
        g.setColour(v[2] > .2f ? colours::cyan : colours::orange);
        g.fillRect(x, top, std::max(1.f, columnWidth - .6f), std::max(1.f, bottom - top));
    }
    // Real-time monitor: -60 .. 0 dBFS.
    const std::array<Rect, 3> bars{Rect(645, 738, 378, 16), Rect(645, 762, 378, 16), Rect(645, 785, 378, 16)};
    const std::array<juce::Colour, 3> barColours{colours::cyan, colours::orange, juce::Colour(0xff35e05a)};
    for (size_t i = 0; i < 3; ++i) {
        g.setColour(juce::Colour(0xff030507)); g.fillRect(bars[i]);
        g.setColour(colours::panelEdge); g.drawRect(bars[i], 1);
        const float x = 672 + (juce::jlimit(-60.f, 0.f, monitor[i]) + 60) / 60 * (1013 - 672);
        if (monitor[i] > -60) {
            const Rect fill(bars[i].getX() + 2, bars[i].getY() + 3, x - bars[i].getX() - 2, bars[i].getHeight() - 6);
            g.setGradientFill(juce::ColourGradient(barColours[i].withAlpha(.35f), fill.getX(), 0, barColours[i], fill.getRight(), 0, false));
            g.fillRect(fill);
        }
    }
}

// =====================================================================================
// Breath / Mouth Control (artwork 1672 x 941)
BreathMouthStudioEditor::BreathMouthStudioEditor(BreathMouthStudioProcessor& p)
    : StudioFaceplateEditor(p, FaceplateArt::breath_mouth_control_jpg, size_t(FaceplateArt::breath_mouth_control_jpgSize), 1672, 941), dsp(p),
      history(408, std::array<float, 4>{}), reduction(408, std::array<float, 2>{}), spectrogram(408, std::vector<float>(64, -120.f)) {
    meters({Rect(68, 232, 24, 204), Rect(148, 232, 24, 204), Rect(1499, 232, 24, 204), Rect(1579, 232, 24, 204)},
           {Rect(54, 443, 50, 26), Rect(130, 443, 56, 26), Rect(1484, 443, 56, 26), Rect(1560, 443, 58, 26)});
    const std::array<const char*, 5> tabNames{"DETECT", "PROCESS", "LISTEN", "VISUALIZE", "ADVANCED"};
    const std::array<float, 5> tabX{252, 401, 546, 689, 834};
    static constexpr std::array<int, 5> tabView{0, 3, 1, 2, 4};
    for (size_t i = 0; i < 5; ++i) { const int v = tabView[i]; tabs[i] = &action(Rect(tabX[i], 186, 134, 34), tabNames[i], [this, v] { setView(v); }); }
    combo("mode", Rect(1050, 203, 176, 28), {"VOCAL", "SPEECH", "RAP"}).setTooltip("Detector speed: Vocal (sung), Speech, Rap (fastest)");
    viewBox = &combo(nullptr, Rect(1248, 203, 178, 28), {"SPECTRUM", "WAVEFORM", "SPECTROGRAM", "REDUCTION", "INFO"});
    viewBox->setTooltip("Display");
    viewBox->onChange = [this] { if (viewBox->getSelectedItemIndex() >= 0 && viewBox->getSelectedItemIndex() != view("view", 0)) setView(viewBox->getSelectedItemIndex()); };
    choice("detection", {{Rect(1181, 284, 236, 32), "BREATH"}, {Rect(1181, 326, 236, 32), "MOUTH NOISE"}, {Rect(1181, 363, 236, 32), "SIBILANCE"}, {Rect(1181, 400, 236, 32), "ALL"}});
    choice("focusRange", {{Rect(1180, 485, 68, 30), "LOW"}, {Rect(1260, 485, 78, 30), "MID"}, {Rect(1348, 485, 68, 30), "HIGH"}});
    const std::array<const char*, 9> ids{"sensitivity", "threshold", "attack", "release", "reduction", "frequency", "q", "mix", "output"};
    const std::array<float, 9> xs{118, 290, 462, 638, 815, 991, 1166, 1344, 1540};
    const std::array<std::function<juce::String(double)>, 9> formats{plain, db1, ms, ms, db1, hz, [](double v) { return juce::String(v, v < 10 ? 2 : 1); }, pct, db1};
    for (size_t i = 0; i < ids.size(); ++i) knob(ids[i], xs[i], 630, 30, Rect(xs[i] - 47, 697, 94, 26), formats[i]);
    toggle("bypass", Rect(57, 767, 76, 36), "BYPASS", FaceButton::Style::red);
    toggle("solo", Rect(149, 767, 76, 36), "SOLO").setTooltip("Listen to the filter band only");
    historyButtons(Rect(240, 767, 76, 36), Rect(335, 767, 78, 36), Rect(428, 767, 78, 36));
    toggle("stageDetect", Rect(560, 780, 106, 28), "DETECT").setTooltip("Off: no detection, the cut is applied all the time");
    toggle("stageFilter", Rect(696, 780, 106, 28), "FILTER").setTooltip("Off: the cut is broadband instead of the Frequency/Q band");
    toggle("stageDynamic", Rect(835, 780, 110, 28), "DYNAMIC").setTooltip("Off: full Reduction whenever detected, ignoring Threshold");
    toggle("stageOutput", Rect(977, 780, 108, 28), "OUTPUT").setTooltip("Off: Mix and Output are ignored");
    auto& internal = action(Rect(1136, 780, 106, 28), "INTERNAL", {});
    internal.setToggleState(true, juce::dontSendNotification);
    internal.setTooltip("Detection listens to this track");
    auto& external = action(Rect(1252, 780, 98, 28), "EXTERNAL", {});
    external.setEnabled(false);
    external.setTooltip("Not available: Studio Suite has no sidechain routing between tracks yet");
    choice("listenReduction", {{Rect(1386, 780, 104, 28), "NORMAL"}, {Rect(1500, 780, 108, 28), "REDUCTION"}});
    dynamicRegion(Rect(286, 246, 842, 270));
    finishSetup();
    setView(view("view", 0));
}
void BreathMouthStudioEditor::setView(int v) {
    static constexpr std::array<int, 5> tabView{0, 3, 1, 2, 4};
    if (view("view", 0) != v) processorRef.setViewSetting("view", v); // opening the editor must not change plugin state
    for (size_t i = 0; i < 5; ++i) tabs[i]->setToggleState(tabView[i] == v, juce::dontSendNotification);
    viewBox->setSelectedItemIndex(juce::jlimit(0, 4, v), juce::dontSendNotification);
    repaint();
}
void BreathMouthStudioEditor::consume(const float* f, int n) {
    const int column = std::max(1, int(rate() * 4.0 / double(history.size())));
    for (int i = 0; i < n; ++i, f += studio::ScopeTap::channels) {
        const float in = .5f * (f[0] + f[1]), out = .5f * (f[2] + f[3]);
        before.push(in); after.push(out);
        if (colCount == 0) { colInMin = colInMax = in; colOutMin = colOutMax = out; colGain = 0; colScore = 0; }
        colInMin = std::min(colInMin, in); colInMax = std::max(colInMax, in); colOutMin = std::min(colOutMin, out); colOutMax = std::max(colOutMax, out);
        colGain = std::min(colGain, f[4]); colScore = std::max(colScore, f[5]);
        if (++colCount >= column) {
            history[historyWrite] = {colInMin, colInMax, colOutMin, colOutMax}; historyWrite = (historyWrite + 1) % history.size();
            reduction[reductionWrite] = {colGain, colScore}; reductionWrite = (reductionWrite + 1) % reduction.size();
            colCount = 0;
        }
    }
}
void BreathMouthStudioEditor::update() {
    if (after.update()) {
        auto& columnData = spectrogram[spectrogramWrite];
        for (size_t b = 0; b < columnData.size(); ++b) {
            const float lo = 20 * std::pow(1000.f, float(b) / float(columnData.size())), hi = 20 * std::pow(1000.f, float(b + 1) / float(columnData.size()));
            columnData[b] = after.levelAt(lo, hi, rate());
        }
        spectrogramWrite = (spectrogramWrite + 1) % spectrogram.size();
    }
    before.update();
}
void BreathMouthStudioEditor::drawOverlay(juce::Graphics& g) {
    const Rect cover(306, 248, 820, 240), freqArea(313, 250, 787, 236), yRange(313, 265, 787, 225);
    fillPanel(g, cover);
    const int v = view("view", 0);
    const int detection = juce::jlimit(0, 3, juce::roundToInt(valueOf("detection")));
    const juce::Colour accent = detectionColours[size_t(detection)];
    if (v == 0) {
        g.setColour(colours::grid);
        for (float f : {50.f, 100.f, 200.f, 500.f, 1000.f, 2000.f, 5000.f, 10000.f}) g.drawVerticalLine(int(freqToX(f, freqArea)), cover.getY(), cover.getBottom());
        for (int d = 1; d < 5; ++d) g.drawHorizontalLine(int(yRange.getY() + yRange.getHeight() * float(d) / 5), cover.getX(), cover.getRight());
        // Processed band (-3 dB edges of the Frequency / Q bell) and the detector focus region.
        const float f0 = valueOf("frequency"), q = std::max(.1f, valueOf("q"));
        const float spread = std::sqrt(1 + 1 / (4 * q * q)), half = 1 / (2 * q);
        const float lo = f0 * (spread - half), hi = f0 * (spread + half);
        const Rect band(freqToX(lo, freqArea), cover.getY() + 16, std::max(3.f, freqToX(hi, freqArea) - freqToX(lo, freqArea)), cover.getHeight() - 18);
        g.setColour(accent.withAlpha(.18f)); g.fillRect(band);
        g.setColour(accent.withAlpha(.9f)); g.drawRect(band, 1.2f);
        g.setFont(font(13)); g.drawText(std::array<const char*, 4>{"BREATH", "MOUTH NOISE", "SIBILANCE", "ALL"}[size_t(detection)], Rect(band.getCentreX() - 60, cover.getY() + 1, 120, 15), juce::Justification::centred);
        const auto [flo, fhi] = studio::BreathMouthStudioCore::focusRegion(juce::roundToInt(valueOf("focusRange")));
        const float x0 = freqToX(flo, freqArea), x1 = freqToX(std::min(fhi, float(rate() * .45)), freqArea);
        g.setColour(colours::text.withAlpha(.55f)); g.drawLine(x0, cover.getBottom() - 6, x1, cover.getBottom() - 6, 2);
        g.setFont(font(10, false)); g.drawText("DETECTOR FOCUS", Rect(x0, cover.getBottom() - 20, x1 - x0, 12), juce::Justification::centred);
        const auto pb = spectrumPath(before, freqArea, rate(), 0, -100, yRange, true), pa = spectrumPath(after, freqArea, rate(), 0, -100, yRange, true);
        g.setColour(juce::Colour(0x559aa3ab)); g.fillPath(pb);
        drawTrace(g, spectrumPath(before, freqArea, rate(), 0, -100, yRange, false), juce::Colour(0xffb9c2ca), 1.f);
        g.setColour(colours::cyan.withAlpha(.25f)); g.fillPath(pa);
        drawTrace(g, spectrumPath(after, freqArea, rate(), 0, -100, yRange, false), colours::cyan, 1.2f);
        drawLegend(g, {330, 266}, {{juce::Colour(0xffb9c2ca), "BEFORE"}, {colours::cyan, "AFTER"}});
    } else if (v == 1) {
        drawGrid(g, cover, 8, 4);
        const float w = cover.getWidth() / float(history.size());
        for (size_t i = 0; i < history.size(); ++i) {
            const auto& c = history[(historyWrite + i) % history.size()];
            const float x = cover.getX() + w * float(i), mid = cover.getCentreY(), h = cover.getHeight() * .46f;
            g.setColour(juce::Colour(0x889aa3ab)); g.fillRect(x, mid - juce::jlimit(-1.f, 1.f, c[1]) * h, std::max(1.f, w - .4f), std::max(1.f, (c[1] - c[0]) * h));
            g.setColour(colours::cyan); g.fillRect(x, mid - juce::jlimit(-1.f, 1.f, c[3]) * h, std::max(1.f, w - .4f), std::max(1.f, (c[3] - c[2]) * h));
        }
        drawLegend(g, {330, 266}, {{juce::Colour(0xffb9c2ca), "BEFORE"}, {colours::cyan, "AFTER"}});
    } else if (v == 2) {
        const float w = cover.getWidth() / float(spectrogram.size());
        const size_t rows = spectrogram[0].size();
        const float h = cover.getHeight() / float(rows);
        for (size_t i = 0; i < spectrogram.size(); ++i) {
            const auto& column = spectrogram[(spectrogramWrite + i) % spectrogram.size()];
            for (size_t b = 0; b < rows; ++b) {
                const float level = juce::jlimit(0.f, 1.f, (column[b] + 100) / 90);
                if (level <= 0) continue;
                g.setColour(juce::Colour(0xff001a33).interpolatedWith(level < .6f ? colours::cyan : colours::orange, level));
                g.fillRect(cover.getX() + w * float(i), cover.getBottom() - h * float(b + 1), w + .5f, h + .5f);
            }
        }
        g.setColour(colours::text.withAlpha(.6f)); g.setFont(font(11, false));
        g.drawText("SPECTROGRAM (AFTER)  20 Hz - 20 kHz", Rect(cover.getX() + 8, cover.getY() + 4, 300, 14), juce::Justification::centredLeft);
    } else if (v == 3) {
        drawGrid(g, cover, 8, 6);
        juce::Path gain, score;
        for (size_t i = 0; i < reduction.size(); ++i) {
            const auto& c = reduction[(reductionWrite + i) % reduction.size()];
            const float x = cover.getX() + cover.getWidth() * float(i) / float(reduction.size() - 1);
            const float yg = cover.getY() + cover.getHeight() * juce::jlimit(0.f, 1.f, -c[0] / 30.f), ys = cover.getBottom() - cover.getHeight() * .3f * c[1];
            if (i == 0) { gain.startNewSubPath(x, yg); score.startNewSubPath(x, ys); } else { gain.lineTo(x, yg); score.lineTo(x, ys); }
        }
        drawTrace(g, score, accent, 1.2f);
        drawTrace(g, gain, colours::orange, 1.6f);
        drawLegend(g, {330, 258}, {{colours::orange, "GAIN REDUCTION (0 to -30 dB)"}, {accent, "DETECTION"}}, 12);
    } else {
        g.setColour(colours::text); g.setFont(font(15, false));
        const auto& e = dsp.engine();
        const auto [flo, fhi] = studio::BreathMouthStudioCore::focusRegion(juce::roundToInt(valueOf("focusRange")));
        juce::StringArray lines;
        lines.add("Detection now: " + juce::String(juce::roundToInt(e.detection.load() * 100)) + "%   Reduction now: " + juce::String(e.appliedDb.load(), 1) + " dB");
        lines.add("Events detected: " + juce::String(int(e.events.load())));
        lines.add("Detector focus: " + hz(flo) + " - " + hz(fhi) + "    Cut band: " + hz(valueOf("frequency")) + ", Q " + juce::String(valueOf("q"), 2));
        lines.add("Latency: " + juce::String(e.latency()) + " samples (" + juce::String(1000.0 * e.latency() / rate(), 2) + " ms) at " + juce::String(rate() / 1000.0, 1) + " kHz");
        lines.add("External sidechain: not available (Studio Suite has no track-to-track sidechain routing yet)");
        lines.add("Detectors are heuristic, not a trained model. Check with LISTEN > REDUCTION.");
        for (int i = 0; i < lines.size(); ++i) g.drawText(lines[i], Rect(cover.getX() + 18, cover.getY() + 18 + 34 * float(i), cover.getWidth() - 36, 24), juce::Justification::centredLeft);
    }
}

} // namespace hem
