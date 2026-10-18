#pragma once
#include <JuceHeader.h>
#include "AnalysisWorker.h"
namespace hem {
// Lightweight native display. Drawing uses worker snapshots, never live audio
// pointers or FFT calls on the GUI thread.
class AnalysisView final : public juce::Component,private juce::Timer {
public:
    explicit AnalysisView(AnalysisWorker& w):worker(w){startTimerHz(20);setSize(900,520);}
    void paint(juce::Graphics&) override;
private:
    void timerCallback() override {if(worker.copyLatest(frame))repaint();}
    AnalysisWorker& worker;AnalysisWorker::Snapshot frame;
};
}
