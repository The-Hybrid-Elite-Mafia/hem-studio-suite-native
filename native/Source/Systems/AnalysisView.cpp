#include "AnalysisView.h"
namespace hem {
void AnalysisView::paint(juce::Graphics& g) {
    g.fillAll(juce::Colour(0xff080e16));
    auto area = getLocalBounds().toFloat().reduced(18);
    g.setColour(juce::Colours::cyan);
    g.drawText(worker.isOperational() ? "PRE-LISTENING MASTER / before vocal return, correction and final limiter"
                                      : "ANALYSIS WORKER UNAVAILABLE",
               area.removeFromTop(26), juce::Justification::centred);
    g.setColour(juce::Colours::lightgrey);
    g.drawText("L cyan / R amber | FFT: sine-peak dBFS | waveform: linear full scale",
               area.removeFromTop(22), juce::Justification::centred);
    auto spectrumFrame = area.removeFromTop(area.getHeight() * .54f);
    auto spectrum = spectrumFrame.reduced(43, 22);
    const auto yForDb = [&](float db) {
        return spectrum.getBottom() - juce::jlimit(0.f, 1.f, (db + 100.f) / 100.f) * spectrum.getHeight();
    };
    const double maximumHz = frame.sampleRate > 40 ? std::min(20000., frame.sampleRate * .5) : 20000.;
    const auto xForHz = [&](double hz) {
        return spectrum.getX() + float(std::log(hz / 20.) / std::log(maximumHz / 20.)) * spectrum.getWidth();
    };
    for (float tick : {0.f, -20.f, -40.f, -60.f, -80.f, -100.f}) {
        g.setColour(juce::Colour(0xff20414b));
        g.drawHorizontalLine(int(yForDb(tick)), spectrum.getX(), spectrum.getRight());
        g.setColour(juce::Colours::lightgrey);
        g.drawText(juce::String(int(tick)), spectrum.getX() - 40, yForDb(tick) - 8, 34, 16,
                   juce::Justification::centredRight);
    }
    for (double hz : {20., 100., 1000., 10000., 20000.}) {
        if (hz > maximumHz) continue;
        const float x = xForHz(hz);
        g.setColour(juce::Colour(0xff18323e));
        g.drawVerticalLine(int(x), spectrum.getY(), spectrum.getBottom());
        g.setColour(juce::Colours::lightgrey);
        g.drawText(hz >= 1000 ? juce::String(hz / 1000., 0) + "k" : juce::String(hz, 0),
                   x - 24, spectrum.getBottom() + 3, 48, 16, juce::Justification::centred);
    }
    if (frame.sampleRate > 0 && spectrum.getWidth() > 0) {
        for (int channel = 0; channel < 2; ++channel) {
            juce::Path path;
            const auto& values = channel == 0 ? frame.leftDb : frame.rightDb;
            for (int x = 0; x < int(spectrum.getWidth()); ++x) {
                const double hz = 20 * std::pow(maximumHz / 20., double(x) / std::max(1.f, spectrum.getWidth() - 1));
                const int bin = juce::jlimit(0, AnalysisWorker::bins - 1, int(hz * AnalysisWorker::fftSize / frame.sampleRate));
                if (x == 0) path.startNewSubPath(spectrum.getX(), yForDb(values[size_t(bin)]));
                else path.lineTo(spectrum.getX() + x, yForDb(values[size_t(bin)]));
            }
            g.setColour(channel == 0 ? juce::Colours::cyan : juce::Colours::orange);
            g.strokePath(path, juce::PathStrokeType(1.3f));
        }
    } else {
        g.setColour(juce::Colours::lightgrey);
        g.drawText("Waiting for a complete audio FFT window", spectrum, juce::Justification::centred);
    }
    area.removeFromBottom(38);
    auto waveform = area.reduced(43, 10);
    for (int channel = 0; channel < 2; ++channel) {
        auto lane = waveform.withY(waveform.getY() + channel * waveform.getHeight() * .5f)
                            .withHeight(waveform.getHeight() * .5f).reduced(0, 6);
        const auto yForAmplitude = [&](float sample) {
            return lane.getCentreY() - juce::jlimit(-1.f, 1.f, sample) * lane.getHeight() * .45f;
        };
        for (float tick : {-1.f, 0.f, 1.f}) {
            g.setColour(juce::Colour(0xff20414b));
            g.drawHorizontalLine(int(yForAmplitude(tick)), lane.getX(), lane.getRight());
            g.setColour(juce::Colours::lightgrey);
            g.drawText(juce::String(tick, 0), lane.getX() - 31, yForAmplitude(tick) - 7, 26, 14,
                       juce::Justification::centredRight);
        }
        g.setColour(channel == 0 ? juce::Colours::cyan : juce::Colours::orange);
        g.drawText(channel == 0 ? "L" : "R", lane.getRight() + 4, lane.getCentreY() - 8, 20, 16,
                   juce::Justification::centredLeft);
        for (int i = 0; i < frame.validColumns; ++i) {
            const auto& bucket = frame.waveform[size_t(i)];
            const float low = channel == 0 ? bucket.lowL : bucket.lowR;
            const float high = channel == 0 ? bucket.highL : bucket.highR;
            const float x = lane.getX() + float(i) * lane.getWidth() / std::max(1, frame.validColumns);
            g.drawVerticalLine(int(x), yForAmplitude(high), yForAmplitude(low));
            if (high > 1 || low < -1) {
                g.setColour(juce::Colours::red);
                if (high > 1) g.fillRect(x, lane.getY(), 2.f, 3.f);
                if (low < -1) g.fillRect(x, lane.getBottom() - 3, 2.f, 3.f);
                g.setColour(channel == 0 ? juce::Colours::cyan : juce::Colours::orange);
            }
        }
    }
    const double seconds = frame.sampleRate > 0 ? frame.validColumns * double(AnalysisWorker::bucketSize) / frame.sampleRate : 0;
    g.setColour(juce::Colours::lightgrey);
    g.drawText("Waveform history " + juce::String(seconds, 3) + " s ending at latest snapshot | 8192 FFT / 64-frame min/max",
               getLocalBounds().removeFromBottom(38).removeFromTop(18), juce::Justification::centred);
    g.setColour(frame.droppedFrames > 0 || frame.discontinuity ? juce::Colours::orange : juce::Colours::lightgrey);
    g.drawText("Dropped display frames: " + juce::String(frame.droppedFrames)
               + (frame.discontinuity ? " / history reset after a gap" : " / display does not affect audio"),
               getLocalBounds().removeFromBottom(19), juce::Justification::centred);
}
}
