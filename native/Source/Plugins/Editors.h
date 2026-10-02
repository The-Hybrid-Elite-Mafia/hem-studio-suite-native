#pragma once
#include "Processors.h"
#include "../HardwareLookAndFeel.h"
namespace hem {
class ModuleEditor : public juce::AudioProcessorEditor,private juce::Timer {
public:
 explicit ModuleEditor(ModuleProcessor&);~ModuleEditor() override;
 void paint(juce::Graphics&) override;void resized() override;
private:
 struct Control {
    juce::Label label;juce::Slider knob;juce::ToggleButton toggle;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> sliderAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> buttonAttachment;
    bool isToggle=false;
 };
 void timerCallback() override {repaint();}
 ModuleProcessor& processor;HardwareLookAndFeel look;
 std::vector<std::unique_ptr<Control>> controls;
};
class SipaEditor final:public ModuleEditor {public:explicit SipaEditor(SipaProcessor& p):ModuleEditor(p){}};
class SurgicalEqEditor final:public ModuleEditor {public:explicit SurgicalEqEditor(SurgicalEqProcessor& p):ModuleEditor(p){}};
class VcaEditor final:public ModuleEditor {public:explicit VcaEditor(VcaProcessor& p):ModuleEditor(p){}};
class TruePeakEditor final:public ModuleEditor {public:explicit TruePeakEditor(TruePeakProcessor& p):ModuleEditor(p){}};
}
