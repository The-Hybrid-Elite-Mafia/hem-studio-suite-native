#pragma once
// Message-thread UI kit for the 2026-10 Studio faceplates. Every interactive part is
// drawn in code on top of the faceplate artwork, covering the artwork's painted
// knob caps, buttons, value boxes and displays, so nothing on screen is a dead
// painting. Nothing here runs on the audio thread.
#include <JuceHeader.h>
#include <functional>
#include <optional>

namespace hem::fp {

namespace colours {
inline const juce::Colour orange{0xffff7a1a}, orangeDeep{0xffb33f00}, cyan{0xff22d8ff}, blue{0xff1f8fff}, red{0xffff2a2a},
    text{0xffe9eef3}, dimText{0xffa9b3bd}, panel{0xff06090c}, panelEdge{0xff2c3640}, grid{0x2236c8ff}, yellow{0xfff4d03f}, magenta{0xffff4fd8};
}

inline juce::Font font(float height, bool bold = true) { return juce::Font(juce::FontOptions(height, bold ? juce::Font::bold : juce::Font::plain)); }

// ---------------------------------------------------------------- drawing helpers (design coordinates)
inline void fillPanel(juce::Graphics& g, juce::Rectangle<float> r, float corner = 3) {
    g.setColour(colours::panel);
    g.fillRoundedRectangle(r, corner);
}
inline void drawValueBox(juce::Graphics& g, juce::Rectangle<float> r, const juce::String& text, juce::Colour accent = colours::cyan) {
    g.setColour(juce::Colour(0xff03070a));
    g.fillRoundedRectangle(r, 3);
    g.setColour(accent.withAlpha(.25f));
    g.drawRoundedRectangle(r.expanded(1), 4, 2);
    g.setColour(accent.withAlpha(.85f));
    g.drawRoundedRectangle(r, 3, 1.2f);
    g.setColour(colours::text);
    g.setFont(font(r.getHeight() * .72f));
    g.drawFittedText(text, r.toNearestInt(), juce::Justification::centred, 1);
}
inline float freqToX(float hz, juce::Rectangle<float> r, float lo = 20, float hi = 20000) {
    return r.getX() + r.getWidth() * std::log(std::max(hz, lo) / lo) / std::log(hi / lo);
}
inline float xToFreq(float x, juce::Rectangle<float> r, float lo = 20, float hi = 20000) {
    return lo * std::pow(hi / lo, juce::jlimit(0.f, 1.f, (x - r.getX()) / r.getWidth()));
}
inline juce::Colour meterColour(float db) {
    if (db > 0) return colours::red;
    if (db > -6) return juce::Colour(0xffff9a1f);
    if (db > -18) return colours::yellow;
    return juce::Colour(0xff35e05a);
}
// Segmented vertical bar, linear in dB from `bottomDb` to `topDb`.
inline void drawMeterBar(juce::Graphics& g, juce::Rectangle<float> r, float db, float holdDb, float bottomDb = -60, float topDb = 6) {
    g.setColour(juce::Colour(0xff020304));
    g.fillRect(r.expanded(1));
    const int segments = std::max(8, int(r.getHeight() / 5));
    const float segH = r.getHeight() / float(segments);
    for (int i = 0; i < segments; ++i) {
        const float segDb = bottomDb + (topDb - bottomDb) * (float(i) + .5f) / float(segments);
        const auto seg = juce::Rectangle<float>(r.getX(), r.getBottom() - float(i + 1) * segH + 1, r.getWidth(), segH - 1.5f);
        const bool lit = segDb <= db;
        g.setColour(meterColour(segDb).withAlpha(lit ? 1.f : .13f));
        g.fillRect(seg);
    }
    if (holdDb > bottomDb) {
        const float y = r.getBottom() - r.getHeight() * juce::jlimit(0.f, 1.f, (holdDb - bottomDb) / (topDb - bottomDb));
        g.setColour(juce::Colours::white.withAlpha(.9f));
        g.fillRect(r.getX(), y - 1, r.getWidth(), 2.f);
    }
}
inline void drawGrid(juce::Graphics& g, juce::Rectangle<float> r, int columns, int rows) {
    g.setColour(colours::grid);
    for (int i = 1; i < columns; ++i) { const float x = r.getX() + r.getWidth() * float(i) / float(columns); g.drawVerticalLine(int(x), r.getY(), r.getBottom()); }
    for (int i = 1; i < rows; ++i) { const float y = r.getY() + r.getHeight() * float(i) / float(rows); g.drawHorizontalLine(int(y), r.getX(), r.getRight()); }
}
inline void drawLegend(juce::Graphics& g, juce::Point<float> at, std::initializer_list<std::pair<juce::Colour, juce::String>> items, float size = 13) {
    float y = at.y;
    for (const auto& [c, t] : items) {
        g.setColour(c); g.fillRect(at.x, y + 2, size, size - 3);
        g.setColour(colours::text); g.setFont(font(size, false));
        g.drawText(t, juce::Rectangle<float>(at.x + size + 6, y, 120, size + 2), juce::Justification::centredLeft);
        y += size + 8;
    }
}
// Glowing polyline (series of y values across r).
inline void drawTrace(juce::Graphics& g, const juce::Path& p, juce::Colour c, float width = 1.6f) {
    g.setColour(c.withAlpha(.25f)); g.strokePath(p, juce::PathStrokeType(width * 3.5f, juce::PathStrokeType::curved));
    g.setColour(c); g.strokePath(p, juce::PathStrokeType(width, juce::PathStrokeType::curved));
}

// ---------------------------------------------------------------- Knob
class Knob final : public juce::Slider {
public:
    Knob() {
        setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        setRotaryParameters(juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
        setMouseCursor(juce::MouseCursor::PointingHandCursor);
        setVelocityBasedMode(false);
    }
    juce::Colour accent = colours::orange;
    std::function<juce::String(double)> format;
    void paint(juce::Graphics& g) override {
        const auto b = getLocalBounds().toFloat();
        const float r = std::min(b.getWidth(), b.getHeight()) * .5f, cap = r * .82f;
        const auto c = b.getCentre();
        const float start = getRotaryParameters().startAngleRadians, end = getRotaryParameters().endAngleRadians;
        const float proportion = float(valueToProportionOfLength(getValue()));
        const float angle = start + proportion * (end - start);
        // Value arc.
        juce::Path arc; arc.addCentredArc(c.x, c.y, r * .93f, r * .93f, 0, start, angle, true);
        g.setColour(accent.withAlpha(.30f)); g.strokePath(arc, juce::PathStrokeType(r * .16f));
        g.setColour(accent); g.strokePath(arc, juce::PathStrokeType(r * .06f));
        // Cap: shadow, bevel, brushed face.
        g.setColour(juce::Colours::black.withAlpha(.85f)); g.fillEllipse(juce::Rectangle<float>(cap * 2.1f, cap * 2.1f).withCentre(c.translated(0, cap * .06f)));
        juce::ColourGradient bevel(juce::Colour(0xff8b929a), c.x, c.y - cap, juce::Colour(0xff15181c), c.x, c.y + cap, false);
        g.setGradientFill(bevel); g.fillEllipse(juce::Rectangle<float>(cap * 2, cap * 2).withCentre(c));
        juce::ColourGradient face(juce::Colour(0xff5d646c), c.x - cap * .3f, c.y - cap * .4f, juce::Colour(0xff0d0f12), c.x + cap * .6f, c.y + cap * .8f, true);
        g.setGradientFill(face); g.fillEllipse(juce::Rectangle<float>(cap * 1.78f, cap * 1.78f).withCentre(c));
        g.setColour(juce::Colours::white.withAlpha(.05f));
        for (float k = .2f; k < .9f; k += .09f) g.drawEllipse(juce::Rectangle<float>(cap * 1.78f * k, cap * 1.78f * k).withCentre(c), .8f);
        g.setColour(juce::Colours::white.withAlpha(isMouseOverOrDragging() ? .35f : .18f));
        g.drawEllipse(juce::Rectangle<float>(cap * 1.78f, cap * 1.78f).withCentre(c), 1.f);
        // Pointer.
        const auto tip = c.getPointOnCircumference(cap * .80f, angle), base = c.getPointOnCircumference(cap * .25f, angle);
        g.setColour(colours::cyan.withAlpha(.35f)); g.drawLine({base, tip}, cap * .16f);
        g.setColour(juce::Colours::white); g.drawLine({base, tip}, cap * .07f);
    }
};

// ---------------------------------------------------------------- Buttons
// A ToggleButton (so host tests and accessibility see a real toggle) drawn as a lit faceplate key.
class FaceButton final : public juce::ToggleButton {
public:
    enum class Style { orange, blue, red };
    explicit FaceButton(const juce::String& label, Style s = Style::orange) : juce::ToggleButton(label), style(s) {
        setClickingTogglesState(false);
        setMouseCursor(juce::MouseCursor::PointingHandCursor);
    }
    Style style;
    bool chevron = false;
    float textScale = .5f;
    float inset = 0; // backdrop margin that hides the artwork's own painted glow around this key
    void paintButton(juce::Graphics& g, bool over, bool down) override {
        if (inset > 0) { g.setColour(juce::Colour(0xff07090c)); g.fillRoundedRectangle(getLocalBounds().toFloat(), inset * 1.4f); }
        auto b = getLocalBounds().toFloat().reduced(1 + inset);
        const float corner = std::min(6.f, b.getHeight() * .18f);
        const bool on = getToggleState();
        const juce::Colour lit = style == Style::blue ? colours::blue : style == Style::red ? colours::red : colours::orange;
        g.setColour(juce::Colour(0xff050608)); g.fillRoundedRectangle(b.expanded(1), corner + 1);
        if (on) {
            juce::ColourGradient fill(lit.brighter(.15f), b.getX(), b.getY(), lit.darker(1.4f), b.getX(), b.getBottom(), false);
            g.setGradientFill(fill); g.fillRoundedRectangle(b, corner);
            g.setColour(juce::Colour(0xff0b0d10).withAlpha(.55f)); g.fillRoundedRectangle(b.reduced(3), corner);
            g.setColour(lit.withAlpha(.35f)); g.drawRoundedRectangle(b.expanded(1.5f), corner + 1.5f, 3.f);
            g.setColour(lit.brighter(.6f)); g.drawRoundedRectangle(b, corner, 1.6f);
        } else {
            juce::ColourGradient fill(juce::Colour(0xff252a31), b.getX(), b.getY(), juce::Colour(0xff0b0d10), b.getX(), b.getBottom(), false);
            g.setGradientFill(fill); g.fillRoundedRectangle(b, corner);
            g.setColour(juce::Colour(over ? 0xff5a6672 : 0xff39424c)); g.drawRoundedRectangle(b, corner, 1.f);
        }
        if (down) { g.setColour(juce::Colours::white.withAlpha(.08f)); g.fillRoundedRectangle(b, corner); }
        auto t = b;
        if (chevron) {
            auto c = t.removeFromRight(t.getHeight()).reduced(t.getHeight() * .3f);
            juce::Path p; p.addTriangle(c.getX(), c.getY() + c.getHeight() * .25f, c.getRight(), c.getY() + c.getHeight() * .25f, c.getCentreX(), c.getBottom() - c.getHeight() * .15f);
            g.setColour(colours::cyan); g.fillPath(p);
        }
        g.setColour(on ? juce::Colours::white : chevron ? colours::cyan : colours::dimText);
        g.setFont(font(std::min(b.getHeight() * textScale + 2.f, 18.f)));
        g.drawFittedText(getButtonText(), t.reduced(3, 0).toNearestInt(), juce::Justification::centred, 1, .55f);
        if (!isEnabled()) { g.setColour(juce::Colours::black.withAlpha(.5f)); g.fillRoundedRectangle(b, corner); }
    }
};

// ---------------------------------------------------------------- Drop-down look (Mode / View boxes)
class FaceLook final : public juce::LookAndFeel_V4 {
public:
    FaceLook() {
        setColour(juce::ComboBox::textColourId, colours::cyan);
        setColour(juce::PopupMenu::backgroundColourId, juce::Colour(0xff0b0f14));
        setColour(juce::PopupMenu::textColourId, colours::text);
        setColour(juce::PopupMenu::highlightedBackgroundColourId, colours::orangeDeep);
        setColour(juce::PopupMenu::highlightedTextColourId, juce::Colours::white);
    }
    void drawComboBox(juce::Graphics& g, int width, int height, bool, int, int, int, int, juce::ComboBox& box) override {
        const auto b = juce::Rectangle<float>(0, 0, float(width), float(height)).reduced(1);
        g.setColour(juce::Colour(0xff04070a)); g.fillRoundedRectangle(b, 4);
        g.setColour(colours::cyan.withAlpha(box.isMouseOver(true) ? .9f : .6f)); g.drawRoundedRectangle(b, 4, 1.2f);
        const auto c = b.withLeft(b.getRight() - b.getHeight()).reduced(b.getHeight() * .32f);
        juce::Path p; p.addTriangle(c.getX(), c.getY() + c.getHeight() * .2f, c.getRight(), c.getY() + c.getHeight() * .2f, c.getCentreX(), c.getBottom() - c.getHeight() * .1f);
        g.setColour(colours::cyan); g.fillPath(p);
        g.setColour(colours::cyan.withAlpha(.35f)); g.drawVerticalLine(int(b.getRight() - b.getHeight()), b.getY() + 3, b.getBottom() - 3);
    }
    juce::Font getComboBoxFont(juce::ComboBox& box) override { return font(std::min(17.f, float(box.getHeight()) * .58f)); }
    void positionComboBoxText(juce::ComboBox& box, juce::Label& label) override {
        label.setBounds(4, 1, box.getWidth() - box.getHeight() - 8, box.getHeight() - 2);
        label.setFont(getComboBoxFont(box));
        label.setJustificationType(juce::Justification::centred);
    }
};

// ---------------------------------------------------------------- Horizontal slider
class FaceSlider final : public juce::Slider {
public:
    FaceSlider() { setSliderStyle(juce::Slider::LinearHorizontal); setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0); setMouseCursor(juce::MouseCursor::PointingHandCursor); }
    std::function<juce::String(double)> format;
    void paint(juce::Graphics& g) override {
        const auto b = getLocalBounds().toFloat();
        const float thumb = b.getHeight() * .5f;
        const auto track = juce::Rectangle<float>(b.getX() + thumb, b.getCentreY() - 2.5f, b.getWidth() - 2 * thumb, 5);
        const float x = track.getX() + track.getWidth() * float(valueToProportionOfLength(getValue()));
        g.setColour(juce::Colour(0xff020406)); g.fillRoundedRectangle(track.expanded(2), 4);
        g.setColour(juce::Colour(0xff2b3640)); g.drawRoundedRectangle(track.expanded(2), 4, 1);
        const auto filled = track.withRight(x);
        g.setColour(colours::cyan.withAlpha(.3f)); g.fillRoundedRectangle(filled.expanded(0, 3), 4);
        g.setColour(colours::cyan); g.fillRoundedRectangle(filled, 2.5f);
        const auto knob = juce::Rectangle<float>(thumb * 1.8f, thumb * 1.8f).withCentre({x, b.getCentreY()});
        g.setColour(juce::Colours::black.withAlpha(.7f)); g.fillEllipse(knob.translated(0, 1.5f).expanded(1));
        juce::ColourGradient grad(juce::Colour(0xff9aa1a8), knob.getX(), knob.getY(), juce::Colour(0xff15181c), knob.getX(), knob.getBottom(), false);
        g.setGradientFill(grad); g.fillEllipse(knob);
        g.setColour(juce::Colours::white.withAlpha(.5f)); g.drawEllipse(knob.reduced(knob.getWidth() * .3f), 1);
    }
};

// ---------------------------------------------------------------- Undo / Redo / A-B over all parameters
class ParameterHistory {
public:
    void bind(std::vector<juce::RangedAudioParameter*> list) { params = std::move(list); current = snapshot(); slots[0] = current; slots[1].clear(); }
    // Poll from a timer. Records a step once a change has settled and no mouse button is held.
    void poll() {
        auto now = snapshot();
        if (now == current) return;
        if (juce::ModifierKeys::currentModifiers.isAnyMouseButtonDown()) return;
        undo.push_back(current); if (undo.size() > 200) undo.erase(undo.begin());
        redo.clear(); current = std::move(now);
    }
    bool canUndo() const { return !undo.empty(); }
    bool canRedo() const { return !redo.empty(); }
    void undoStep() { poll(); if (undo.empty()) return; redo.push_back(current); apply(undo.back()); undo.pop_back(); }
    void redoStep() { poll(); if (redo.empty()) return; undo.push_back(current); apply(redo.back()); redo.pop_back(); }
    // A/B: store the live settings in the active slot, switch, recall the other slot.
    // B starts as a copy of A the first time it is opened.
    void toggleAB() {
        poll(); slots[size_t(active)] = current; active ^= 1;
        if (slots[size_t(active)].empty()) slots[size_t(active)] = current;
        const auto target = slots[size_t(active)];
        if (target != current) { undo.push_back(current); redo.clear(); apply(target); }
    }
    int activeSlot() const { return active; }
private:
    std::vector<float> snapshot() const { std::vector<float> v; v.reserve(params.size()); for (auto* p : params) v.push_back(p->getValue()); return v; }
    void apply(const std::vector<float>& v) {
        for (size_t i = 0; i < params.size() && i < v.size(); ++i)
            if (params[i]->getValue() != v[i]) { params[i]->beginChangeGesture(); params[i]->setValueNotifyingHost(v[i]); params[i]->endChangeGesture(); }
        current = v;
    }
    std::vector<juce::RangedAudioParameter*> params;
    std::vector<float> current;
    std::vector<std::vector<float>> undo, redo;
    std::array<std::vector<float>, 2> slots;
    int active = 0;
};

// ---------------------------------------------------------------- Spectrum (message thread)
class SpectrumAnalyser {
public:
    static constexpr int order = 12, size = 1 << order, bins = size / 2 + 1;
    SpectrumAnalyser() : fft(order), window(size), ring(size, 0.f), work(size * 2, 0.f), db(bins, -120.f) {
        float sum = 0;
        for (int i = 0; i < size; ++i) { window[size_t(i)] = .5f - .5f * std::cos(juce::MathConstants<float>::twoPi * float(i) / float(size)); sum += window[size_t(i)]; }
        scale = 2.f / sum;
    }
    void push(float x) { ring[size_t(write)] = x; write = (write + 1) % size; ++fresh; }
    // Recomputes when at least a quarter frame of new samples has arrived.
    bool update(float releasePerUpdate = .85f) {
        if (fresh < size / 4) return false;
        fresh = 0;
        for (int i = 0; i < size; ++i) work[size_t(i)] = ring[size_t((write + i) % size)] * window[size_t(i)];
        std::fill(work.begin() + size, work.end(), 0.f);
        fft.performFrequencyOnlyForwardTransform(work.data(), true);
        for (int k = 0; k < bins; ++k) {
            const float v = juce::Decibels::gainToDecibels(work[size_t(k)] * scale, -140.f);
            db[size_t(k)] = v > db[size_t(k)] ? v : db[size_t(k)] * releasePerUpdate + v * (1 - releasePerUpdate);
        }
        return true;
    }
    // Max level across the bins that fall into [hzLo, hzHi].
    float levelAt(float hzLo, float hzHi, double rate) const {
        const int a = juce::jlimit(0, bins - 1, int(hzLo / rate * size)), b = juce::jlimit(a, bins - 1, int(std::ceil(hzHi / rate * size)));
        float m = -140;
        for (int k = a; k <= b; ++k) m = std::max(m, db[size_t(k)]);
        return m;
    }
    void clear() { std::fill(ring.begin(), ring.end(), 0.f); std::fill(db.begin(), db.end(), -120.f); fresh = 0; }
private:
    juce::dsp::FFT fft;
    std::vector<float> window, ring, work, db;
    int write = 0, fresh = 0; float scale = 1;
};

// Spectrum path across r (log frequency, dB from 0 at top to floorDb at bottom of yRange).
inline juce::Path spectrumPath(const SpectrumAnalyser& s, juce::Rectangle<float> r, double rate, float topDb, float bottomDb, juce::Rectangle<float> yRange, bool closed) {
    juce::Path p;
    const int columns = int(r.getWidth() / 2);
    for (int i = 0; i <= columns; ++i) {
        const float x0 = r.getX() + r.getWidth() * float(i) / float(columns), x1 = r.getX() + r.getWidth() * float(i + 1) / float(columns);
        const float v = s.levelAt(xToFreq(x0, r), xToFreq(x1, r), rate);
        const float y = juce::jlimit(r.getY(), r.getBottom(), yRange.getY() + yRange.getHeight() * (topDb - v) / (topDb - bottomDb));
        if (i == 0) { if (closed) { p.startNewSubPath(x0, r.getBottom()); p.lineTo(x0, y); } else p.startNewSubPath(x0, y); }
        else p.lineTo(x0, y);
    }
    if (closed) { p.lineTo(r.getRight(), r.getBottom()); p.closeSubPath(); }
    return p;
}

// Peak meter state with hold and fall (message thread).
struct MeterState {
    float db = -100, hold = -100; int holdTicks = 0;
    void feed(float peak) {
        const float v = juce::Decibels::gainToDecibels(peak, -100.f);
        db = v > db ? v : std::max(v, db - 1.2f);
        if (v >= hold) { hold = v; holdTicks = 45; } else if (--holdTicks < 0) hold = std::max(v, hold - .8f);
    }
};

} // namespace hem::fp
