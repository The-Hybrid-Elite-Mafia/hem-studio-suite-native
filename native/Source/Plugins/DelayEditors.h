#pragma once
#include "DelayProcessors.h"
namespace hem {
class DelayEditor final:public juce::AudioProcessorEditor,private juce::Timer {
public:
    explicit DelayEditor(DelayProcessor&);
    ~DelayEditor() override;
    void paint(juce::Graphics&)override;
    void resized()override;
    void selectTap(int); // message-thread selection of the shared filter editor
    int selectedTap()const noexcept;
    void refreshBindings(){timerCallback();} // deterministic UI/test refresh, message thread
    juce::String statusText()const;
    juce::StringArray layoutIssues()const; // message-thread native geometry/text evidence
private:
    void timerCallback()override;
    struct View;std::unique_ptr<View> view;
};
}
