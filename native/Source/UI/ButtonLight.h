#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

namespace hem::ui {
/** Lit-hardware feedback for buttons sitting on the faceplate artwork.
    ON (or held down): the whole button face glows like a lit switch - a warm wash that is brightest
    in the middle, a bright edge and a soft halo. Hover: a faint lift. No keyboard-focus boxes:
    the thin cyan/white outlines read as a "blue box" around the last clicked button.
    Message thread only (paint code). */
inline void drawButtonLight (juce::Graphics& g, juce::Rectangle<float> box, bool on, bool down, bool hover,
                             bool enabled, float corner = 4.0f, juce::Colour lamp = juce::Colour (0xffffb23a))
{
    if (! enabled || box.getWidth() < 4.0f || box.getHeight() < 4.0f)
        return;
    if (on || down)
    {
        const float level = down && ! on ? 0.85f : (down ? 1.15f : 1.0f);
        juce::ColourGradient wash (lamp.withAlpha (juce::jmin (1.0f, 0.42f * level)), box.getCentreX(), box.getCentreY(),
                                   lamp.withAlpha (0.10f * level), box.getX(), box.getY(), true);
        g.setGradientFill (wash);
        g.fillRoundedRectangle (box, corner);
        for (int i = 3; i >= 1; --i)   // soft halo just inside the button
        {
            g.setColour (lamp.withAlpha (0.09f * float (4 - i) * level));
            g.drawRoundedRectangle (box.reduced (float (i) * 0.5f), corner, float (i) * 2.0f);
        }
        g.setColour (lamp.brighter (0.35f).withAlpha (juce::jmin (1.0f, 0.95f * level)));
        g.drawRoundedRectangle (box, corner, 1.6f);
    }
    else if (hover)
    {
        g.setColour (juce::Colours::white.withAlpha (0.07f));
        g.fillRoundedRectangle (box, corner);
    }
}
} // namespace hem::ui
