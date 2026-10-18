#include "../Source/HardwareLookAndFeel.h"
#include "../Source/Systems/CalibratedMeterView.h"
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
void require(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
int differences(const juce::Image& first, const juce::Image& second) {
    int count = 0;
    for (int y = 0; y < first.getHeight(); ++y)
        for (int x = 0; x < first.getWidth(); ++x)
            if (first.getPixelAt(x, y) != second.getPixelAt(x, y)) ++count;
    return count;
}
juce::Image dial(bool artwork, float position, bool enabled = true) {
    HardwareLookAndFeel look(artwork);
    juce::Slider slider;
    slider.setBounds(0, 0, 124, 136);
    slider.setEnabled(enabled);
    juce::Image image(juce::Image::ARGB, 124, 136, true);
    juce::Graphics g(image);
    look.drawRotarySlider(g, 13, 17, 94, 98, position,
                          juce::MathConstants<float>::pi * 1.2f,
                          juce::MathConstants<float>::pi * 2.8f, slider);
    return image;
}
bool hasAmber(const juce::Image& image, float x, float y) {
    for (int iy = int(y) - 2; iy <= int(y) + 2; ++iy)
        for (int ix = int(x) - 2; ix <= int(x) + 2; ++ix) {
            const auto colour = image.getPixelAt(ix, iy);
            if (colour.getFloatRed() > 0.75f && colour.getFloatGreen() > 0.35f
                && colour.getFloatBlue() < 0.5f && colour.getFloatAlpha() > 0.8f) return true;
        }
    return false;
}
juce::Image button(bool artwork, bool active, bool down, bool enabled = true) {
    HardwareLookAndFeel look(artwork);
    juce::TextButton control("PLAY");
    control.setBounds(0, 0, 102, 55);
    control.setLookAndFeel(&look);
    control.setEnabled(enabled);
    control.setToggleState(active, juce::dontSendNotification);
    juce::Image image(juce::Image::ARGB, 102, 55, true);
    juce::Graphics g(image);
    look.drawButtonBackground(g, control, control.findColour(juce::TextButton::buttonColourId), false, down);
    look.drawButtonText(g, control, false, down);
    control.setLookAndFeel(nullptr);
    return image;
}
juce::Image fader(bool artwork, bool horizontal, float position) {
    HardwareLookAndFeel look(artwork);
    juce::Slider slider;
    slider.setBounds(0, 0, horizontal ? 200 : 50, horizontal ? 50 : 200);
    slider.setSliderStyle(horizontal ? juce::Slider::LinearHorizontal : juce::Slider::LinearVertical);
    juce::Image image(juce::Image::ARGB, slider.getWidth(), slider.getHeight(), true);
    juce::Graphics g(image);
    look.drawLinearSlider(g, horizontal ? 10 : 5, horizontal ? 5 : 10,
                          horizontal ? 180 : 40, horizontal ? 40 : 180,
                          position, 10, 190, slider.getSliderStyle(), slider);
    return image;
}

juce::Image meterImage(hem::AudioMeters& meters, int width = 190, int height = 310) {
    // Use the production Component and actual post-push atomic meter state.
    // No test-only state injection, painted substitute or timer approximation.
    hem::CalibratedMeterView view(meters);
    view.setBounds(0, 0, width, height);
    juce::Image image(juce::Image::ARGB, width, height, true);
    juce::Graphics g(image);
    view.paint(g);
    return image;
}
int firstPeakFill(const juce::Image& image, int x, int top, int bottom) {
    const juce::Colour peakFill(0xff235767);
    for (int y = top; y < bottom; ++y)
        if (image.getPixelAt(x, y) == peakFill) return y;
    return -1;
}
void requireOpaque(const juce::Image& image) {
    for (int y = 0; y < image.getHeight(); ++y)
        for (int x = 0; x < image.getWidth(); ++x)
            require(image.getPixelAt(x, y).getAlpha() == 255,
                    "Meter lets decorative faceplate pixels show through");
}
void testMeterRaster() {
    hem::AudioMeters source;
    require(source.prepare(1000), "Meter test source failed to prepare");
    std::vector<float> left(3000, 0.5f), right(3000, 0.25f);
    source.push(left.data(), right.data(), int(left.size()));
    const auto actual = source.snapshot();
    require(std::abs(actual.left.peakDb + 6.0206f) < 0.001f
            && std::abs(actual.right.peakDb + 12.0412f) < 0.001f,
            "Meter raster input is not a real calibrated detector snapshot");
    for (const int scale : {1, 2}) {
        const auto image = meterImage(source, 190 * scale, 310 * scale);
        requireOpaque(image);
        const auto yFor = [scale](float db) {
            return float(scale) * (238.0f - 186.0f * (db + 60.0f) / 60.0f);
        };
        const int leftFill = firstPeakFill(image, 55 * scale, 52 * scale, 238 * scale);
        const int rightFill = firstPeakFill(image, 125 * scale, 52 * scale, 238 * scale);
        require(leftFill >= 0 && rightFill >= 0, "Prepared audio has no visible peak bars on first paint");
        require(std::abs(float(leftFill) - yFor(actual.left.peakDb)) <= 3.0f
                && std::abs(float(rightFill) - yFor(actual.right.peakDb)) <= 3.0f,
                "Meter bar height is not calibrated to the fixed -60..0 dBFS scale");
        require(rightFill > leftFill + 12 * scale, "Independent L/R levels are visually merged");
        require(image.getPixelAt(55 * scale, 80 * scale) == juce::Colour(0xff235767)
                && image.getPixelAt(125 * scale, 80 * scale) == juce::Colour(0xff02070b),
                "A -6 dBFS left and -12 dBFS right signal do not render separately");
        require(image.getPixelAt(70 * scale, 110 * scale) == juce::Colour(0xff35baa9),
                "Real RMS level is missing from the inside of the channel bar");
        require(image.getPixelAt(55 * scale, 60 * scale) == juce::Colour(0xff02070b),
                "Meter normalizes a quiet signal to the top of its scale");
        // Between the independent channel lanes, every documented dBFS tick must
        // remain at its absolute calibration coordinate. Tolerate antialiasing.
        const auto background = juce::Colour(0xff061017);
        for (const float tick : {0.0f, -6.0f, -12.0f, -24.0f, -48.0f, -60.0f}) {
            const int y = int(std::round(yFor(tick)));
            bool gridFound = false;
            for (int offset = -1; offset <= 1; ++offset)
                gridFound = gridFound || image.getPixelAt(104 * scale, y + offset) != background;
            require(gridFound, "A fixed dBFS calibration grid line is missing or misplaced");
        }
    }

    // Swapping real channel samples must swap the bars, without changing scale.
    source.prepare(1000);
    source.push(right.data(), left.data(), int(left.size()));
    const auto swapped = meterImage(source);
    require(swapped.getPixelAt(55, 80) == juce::Colour(0xff02070b)
            && swapped.getPixelAt(125, 80) == juce::Colour(0xff235767),
            "Meter channel swap does not follow actual source samples");

    source.prepare(48000);
    source.push(nullptr, nullptr, 256);
    const auto silent = meterImage(source);
    requireOpaque(silent);
    require(firstPeakFill(silent, 55, 52, 238) < 0 && firstPeakFill(silent, 125, 52, 238) < 0,
            "Silent audio produces a fictitious peak bar");
    require(silent.getPixelAt(70, 150) == juce::Colour(0xff02070b)
            && silent.getPixelAt(140, 150) == juce::Colour(0xff02070b),
            "Silent audio produces a fictitious RMS bar");

    const float fullScale = 1.0f, halfScale = 0.5f;
    source.push(&fullScale, &halfScale, 1);
    const auto clipped = meterImage(source);
    require(clipped.getPixelAt(60, 46) == juce::Colour(0xffff504a)
            && clipped.getPixelAt(130, 46) == juce::Colour(0xff28353d),
            "Clip lamps do not reflect independent actual channel latches");
    source.resetClip();
    source.push(nullptr, nullptr, 0);
    const auto reset = meterImage(source);
    require(reset.getPixelAt(60, 46) == juce::Colour(0xff28353d),
            "Actual audio-thread clip reset is not reflected on first paint");
}
}
int main() {
    juce::ScopedJuceInitialiser_GUI gui;
    try {
        for (const bool artwork : {false, true}) {
            const auto minimum = dial(artwork, 0.0f), middle = dial(artwork, 0.5f), maximum = dial(artwork, 1.0f);
            require(differences(minimum, middle) > 30 && differences(middle, maximum) > 30,
                    "Dial pixels do not respond to normalized parameter position");
            require(differences(middle, dial(artwork, 0.5f, false)) > 30,
                    "Disabled dial has no visible disabled state");
            require(middle.getPixelAt(0, 0).getAlpha() == 0 && middle.getPixelAt(123, 135).getAlpha() == 0,
                    "Dial paint escapes its local bounds");
            const auto expected = hem::ui::dialGeometry(13, 17, 94, 98, 0.5f,
                juce::MathConstants<float>::pi * 1.2f, juce::MathConstants<float>::pi * 2.8f);
            require(hasAmber(middle, expected.pointerEndX, expected.pointerEndY),
                    "Actual raster dial pointer is not at its computed position");
            const auto off = button(artwork, false, false), on = button(artwork, true, false);
            require(differences(off, on) > 100, "Actual toggle state does not illuminate the button");
            require(differences(off, button(artwork, false, true)) > 100,
                    "Momentary press does not illuminate the button");
            require(differences(off, button(artwork, false, false)) == 0,
                    "Momentary release does not return to stable off appearance");
            require(differences(on, button(artwork, true, false, false)) > 30,
                    "Disabled button has no visible disabled state");
            require(differences(fader(artwork, true, 10), fader(artwork, true, 190)) > 50,
                    "Horizontal fader pixels do not follow the thumb position");
            require(differences(fader(artwork, false, 10), fader(artwork, false, 190)) > 50,
                    "Vertical fader pixels do not follow the thumb position");
        }
        testMeterRaster();
        std::cout << "PASS: native JUCE dial/slider raster changes, bounds, pointer position, "
                     "button active/press/release and disabled states in standard/artwork modes; "
                     "actual meter first-paint snapshots, fixed dBFS scale/bars at 1x/2x, stereo "
                     "separation/swap, RMS, opaque silence and clip/reset lamps\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
