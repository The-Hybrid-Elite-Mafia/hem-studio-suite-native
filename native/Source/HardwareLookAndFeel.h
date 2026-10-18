#pragma once
#include <JuceHeader.h>
#include "UI/ControlGeometry.h"

// Both modes draw real controls. Artwork mode keeps its geometry inside the
// mapped host hitboxes; it never suppresses state or repaints the whole faceplate.
// Names/value labels remain separate, upright components; only dial pointers turn.
class HardwareLookAndFeel : public juce::LookAndFeel_V4 {
public:
    explicit HardwareLookAndFeel(bool imageOverlay = false) : artworkOverlay(imageOverlay) {
        setColour(juce::TextButton::buttonColourId, juce::Colour(0xff0b1117));
        setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff65300c));
        setColour(juce::TextButton::textColourOffId, juce::Colour(0xff70f3ff));
        setColour(juce::TextButton::textColourOnId, juce::Colour(0xffffd18a));
        setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xff0b1117));
        setColour(juce::ComboBox::outlineColourId, juce::Colour(0xff007f90));
        setColour(juce::ComboBox::textColourId, juce::Colour(0xff70f3ff));
        setColour(juce::PopupMenu::backgroundColourId, juce::Colour(0xff080c12));
        setColour(juce::PopupMenu::textColourId, juce::Colour(0xff70f3ff));
        setColour(juce::Label::textColourId, juce::Colour(0xff70f3ff));
        setColour(juce::Slider::textBoxTextColourId, juce::Colour(0xffffbb55));
        setColour(juce::Slider::textBoxBackgroundColourId, juce::Colour(0xff090d12));
        setColour(juce::Slider::textBoxOutlineColourId, juce::Colour(0xff24505c));
        setColour(juce::Slider::thumbColourId, amber());
        setColour(juce::Slider::trackColourId, cyan());
        setColour(juce::Slider::backgroundColourId, juce::Colour(0xff142c34));
        setColour(juce::ToggleButton::textColourId, juce::Colour(0xff70f3ff));
        setColour(juce::ToggleButton::tickColourId, amber());
        setColour(juce::ToggleButton::tickDisabledColourId, juce::Colour(0xff52616a));
    }

    void drawRotarySlider(juce::Graphics& g, int x, int y, int w, int h, float position,
                          float start, float end, juce::Slider& slider) override {
        const auto d = hem::ui::dialGeometry(float(x), float(y), float(w), float(h), position, start, end);
        if (!d.drawable) return;
        const juce::Graphics::ScopedSaveState state(g);
        g.reduceClipRegion(x, y, w, h);
        const float scale = juce::jlimit(0.55f, 1.8f, float(juce::jmin(w, h)) / 96.0f);
        const float alpha = slider.isEnabled() ? 1.0f : 0.35f;
        const bool focused = slider.hasKeyboardFocus(true);
        const bool touched = slider.isMouseOverOrDragging();
        const auto accent = (touched ? amber() : cyan()).withAlpha(alpha);
        const auto circle = juce::Rectangle<float>(d.centreX - d.radius, d.centreY - d.radius,
                                                    d.radius * 2.0f, d.radius * 2.0f);

        // An opaque inner body covers the artwork's static centre. Its original
        // outer metal trim remains visible around the host's live replacement.
        g.setColour(juce::Colour(0xff03080d));
        g.fillEllipse(circle.expanded(2.0f * scale));
        juce::Path track;
        track.addCentredArc(d.centreX, d.centreY, d.ringRadius, d.ringRadius, 0, start, end, true);
        g.setColour(juce::Colour(0xff183c46).withAlpha(alpha));
        g.strokePath(track, juce::PathStrokeType(3.0f * scale));
        juce::Path progress;
        progress.addCentredArc(d.centreX, d.centreY, d.ringRadius, d.ringRadius, 0, start, d.angle, true);
        g.setColour(accent);
        g.strokePath(progress, juce::PathStrokeType(2.5f * scale));
        juce::ColourGradient metal(juce::Colour(0xff56616b).withMultipliedAlpha(alpha),
                                   circle.getX(), circle.getY(), juce::Colour(0xff070b12),
                                   circle.getRight(), circle.getBottom(), false);
        g.setGradientFill(metal);
        g.fillEllipse(circle);
        g.setColour(accent.withMultipliedAlpha(touched || focused ? 1.0f : 0.65f));
        g.drawEllipse(circle, (focused ? 2.0f : 1.2f) * scale);
        g.setColour(juce::Colours::black.withAlpha(0.55f));
        g.drawEllipse(circle.reduced(3.0f * scale), 1.0f * scale);
        // The current JUCE normalized position drives only this pointer and arc.
        g.setColour(amber().withAlpha(alpha));
        g.drawLine(d.pointerStartX, d.pointerStartY, d.pointerEndX, d.pointerEndY, 3.0f * scale);
        const float cap = 3.0f * scale;
        g.fillEllipse(d.pointerEndX - cap * 0.5f, d.pointerEndY - cap * 0.5f, cap, cap);
    }

    void drawLinearSlider(juce::Graphics& g, int x, int y, int w, int h, float position,
                          float minimum, float maximum, juce::Slider::SliderStyle style,
                          juce::Slider& slider) override {
        // Retain JUCE's own multi-thumb/bar semantics instead of silently drawing
        // a one-thumb control for a different slider type.
        if (style != juce::Slider::LinearHorizontal && style != juce::Slider::LinearVertical) {
            juce::LookAndFeel_V4::drawLinearSlider(g, x, y, w, h, position, minimum, maximum, style, slider);
            return;
        }
        if (w <= 0 || h <= 0) return;
        const juce::Graphics::ScopedSaveState state(g);
        g.reduceClipRegion(slider.getLocalBounds());
        const bool horizontal = style == juce::Slider::LinearHorizontal;
        const float alpha = slider.isEnabled() ? 1.0f : 0.35f;
        const float scale = juce::jlimit(0.65f, 1.5f, float(juce::jmin(w, h)) / 32.0f);
        const float centreX = float(x) + float(w) * 0.5f;
        const float centreY = float(y) + float(h) * 0.5f;
        const float pos = hem::ui::boundedSliderPosition(position, float(horizontal ? x : y),
                                                        float(horizontal ? w : h));
        const auto track = horizontal ? juce::Line<float>(float(x), centreY, float(x + w), centreY)
                                      : juce::Line<float>(centreX, float(y + h), centreX, float(y));
        const auto active = horizontal ? juce::Line<float>(float(x), centreY, pos, centreY)
                                       : juce::Line<float>(centreX, float(y + h), centreX, pos);
        g.setColour(juce::Colour(0xff020609));
        g.drawLine(track, 8.0f * scale);
        g.setColour(juce::Colour(0xff22434d).withAlpha(alpha));
        g.drawLine(track, 4.0f * scale);
        g.setColour(cyan().withAlpha(alpha));
        g.drawLine(active, 3.0f * scale);
        const float across = juce::jmin(float(horizontal ? h : w) * 0.75f, 26.0f * scale);
        const float along = 11.0f * scale;
        const auto thumb = horizontal ? juce::Rectangle<float>(pos - along * 0.5f, centreY - across * 0.5f, along, across)
                                       : juce::Rectangle<float>(centreX - across * 0.5f, pos - along * 0.5f, across, along);
        g.setGradientFill(juce::ColourGradient(juce::Colour(0xff5f6c76), thumb.getX(), thumb.getY(),
                                              juce::Colour(0xff0b1117), thumb.getRight(), thumb.getBottom(), false));
        g.fillRoundedRectangle(thumb, 2.0f * scale);
        g.setColour(amber().withAlpha(alpha));
        g.drawRoundedRectangle(thumb, 2.0f * scale, 1.3f * scale);
        if (horizontal) g.drawVerticalLine(int(std::round(pos)), thumb.getY() + 3 * scale, thumb.getBottom() - 3 * scale);
        else g.drawHorizontalLine(int(std::round(pos)), thumb.getX() + 3 * scale, thumb.getRight() - 3 * scale);
        if (slider.hasKeyboardFocus(true)) {
            g.setColour(cyan().withAlpha(alpha));
            g.drawRoundedRectangle(thumb.expanded(2.0f * scale), 3.0f * scale, 1.0f);
        }
    }

    void drawButtonBackground(juce::Graphics& g, juce::Button& button, const juce::Colour& background,
                              bool over, bool down) override {
        if (button.getWidth() < 3 || button.getHeight() < 3) return;
        const bool active = button.getToggleState() || down;
        const float alpha = button.isEnabled() ? 1.0f : 0.35f;
        const float scale = juce::jlimit(0.65f, 1.3f, float(button.getHeight()) / 44.0f);
        const auto bounds = button.getLocalBounds().toFloat().reduced(2.0f * scale);
        const auto accent = (active ? amber() : cyan()).withAlpha(alpha);
        const auto base = active ? button.findColour(juce::TextButton::buttonOnColourId) : background;
        // Opaque glass conceals static faceplate icons under the live text. It is
        // inset, leaving the artwork's original outer bevel undisturbed.
        g.setGradientFill(juce::ColourGradient(base.brighter(over ? 0.30f : 0.13f), bounds.getX(), bounds.getY(),
                                              base.darker(0.4f), bounds.getX(), bounds.getBottom(), false));
        g.fillRoundedRectangle(bounds, 4.0f * scale);
        if (active) {
            g.setColour(accent.withMultipliedAlpha(0.22f));
            g.drawRoundedRectangle(bounds.reduced(1.0f * scale), 4.0f * scale, 4.0f * scale);
        }
        g.setColour(accent.withMultipliedAlpha(active || over ? 1.0f : 0.50f));
        g.drawRoundedRectangle(bounds, 4.0f * scale, (active ? 1.8f : 1.0f) * scale);
        if (active) {
            g.setColour(accent);
            g.fillRoundedRectangle(bounds.getCentreX() - bounds.getWidth() * 0.17f,
                                   bounds.getBottom() - 4.0f * scale, bounds.getWidth() * 0.34f,
                                   2.0f * scale, 1.0f * scale);
        }
    }

    void drawButtonText(juce::Graphics& g, juce::TextButton& button, bool, bool down) override {
        auto colour = button.findColour((button.getToggleState() || down) ? juce::TextButton::textColourOnId
                                                                         : juce::TextButton::textColourOffId);
        if (!button.isEnabled()) colour = colour.withMultipliedAlpha(0.40f);
        const float fontHeight = juce::jlimit(10.0f, artworkOverlay ? 16.0f : 15.0f,
                                            float(button.getHeight()) * 0.36f);
        g.setFont(juce::Font(juce::FontOptions(fontHeight).withStyle("Bold")));
        const auto textBounds=button.getLocalBounds().reduced(6,3);
        if(button.isEnabled()) {
            g.setColour(colour.withAlpha(0.18f));
            for(const auto offset:std::array<juce::Point<int>,4>{{{-1,0},{1,0},{0,-1},{0,1}}})
                g.drawFittedText(button.getButtonText(),textBounds.translated(offset.x,offset.y),juce::Justification::centred,1,0.65f);
        }
        g.setColour(colour);g.drawFittedText(button.getButtonText(),textBounds,juce::Justification::centred,1,0.65f);
    }

    void drawLabel(juce::Graphics& g,juce::Label& label) override {
        g.fillAll(label.findColour(juce::Label::backgroundColourId));
        if(!label.isBeingEdited()) {
            const auto font=getLabelFont(label);g.setFont(font);
            const auto bounds=label.getBorderSize().subtractedFrom(label.getLocalBounds());
            const int lines=juce::jmax(1,int(float(bounds.getHeight())/font.getHeight()));
            const auto colour=label.findColour(juce::Label::textColourId).withMultipliedAlpha(label.isEnabled()?1.f:.45f);
            if(label.isEnabled()) {
                g.setColour(colour.withMultipliedAlpha(.16f));
                for(const auto offset:std::array<juce::Point<int>,4>{{{-1,0},{1,0},{0,-1},{0,1}}})
                    g.drawFittedText(label.getText(),bounds.translated(offset.x,offset.y),label.getJustificationType(),lines,label.getMinimumHorizontalScale());
            }
            g.setColour(colour);g.drawFittedText(label.getText(),bounds,label.getJustificationType(),lines,label.getMinimumHorizontalScale());
        }
        g.setColour(label.findColour(juce::Label::outlineColourId));g.drawRect(label.getLocalBounds());
    }

    void drawToggleButton(juce::Graphics& g, juce::ToggleButton& button, bool over, bool down) override {
        const float alpha = button.isEnabled() ? 1.0f : 0.35f;
        const bool active = button.getToggleState();
        const auto accent = (active || down ? amber() : cyan()).withAlpha(alpha);
        const float size = juce::jmin(22.0f, float(button.getHeight()) * 0.65f);
        const auto lamp = juce::Rectangle<float>(6.0f, (float(button.getHeight()) - size) * 0.5f, size, size);
        g.setColour(active ? juce::Colour(0xff65300c) : juce::Colour(0xff09131b));
        g.fillRoundedRectangle(lamp, 4.0f);
        g.setColour(accent.withMultipliedAlpha(active || over || down ? 1.0f : 0.55f));
        g.drawRoundedRectangle(lamp, 4.0f, active || down ? 2.0f : 1.0f);
        if (active) {
            juce::Path tick;
            tick.startNewSubPath(lamp.getX() + size * 0.22f, lamp.getY() + size * 0.51f);
            tick.lineTo(lamp.getX() + size * 0.43f, lamp.getY() + size * 0.72f);
            tick.lineTo(lamp.getX() + size * 0.79f, lamp.getY() + size * 0.28f);
            g.strokePath(tick, juce::PathStrokeType(2.3f));
        }
        g.setColour(button.findColour(juce::ToggleButton::textColourId).withMultipliedAlpha(alpha));
        g.setFont(juce::Font(juce::FontOptions(juce::jlimit(11.0f, 15.0f, float(button.getHeight()) * 0.36f))));
        g.drawFittedText(button.getButtonText(), button.getLocalBounds().withTrimmedLeft(int(size) + 16).reduced(2),
                         juce::Justification::centredLeft, 2);
    }

private:
    static juce::Colour cyan() { return juce::Colour(0xff00e5ff); }
    static juce::Colour amber() { return juce::Colour(0xffffaa44); }
    const bool artworkOverlay;
};
