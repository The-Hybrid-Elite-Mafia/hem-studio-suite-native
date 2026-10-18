#pragma once
#include "../DSP/HumAnalysis.h"
namespace hem {
class HumAnalysisWorker final:private juce::Thread {
public:
    HumAnalysisWorker():juce::Thread("HEM measured hum analysis"){}
    ~HumAnalysisWorker() override {stop();}
    void stop(){signalThreadShouldExit();waitForThreadToExit(-1);}
    void prepare(double rate){stop();analysis.prepare(rate);startThread();}
    HumAnalysis analysis;
private:void run() override {while(!threadShouldExit()){if(!analysis.consume())wait(20);}}
};
}
