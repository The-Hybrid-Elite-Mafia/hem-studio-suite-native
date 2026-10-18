#pragma once
#include "Processors.h"
#include <optional>
#include "../UI/PluginTelemetryView.h"
#include "../UI/PhotorealisticLookAndFeel.h"
#include "../UI/FoundationMaterialLook.h"
#include "../UI/PhysicalValueSlider.h"
namespace hem {
class ModuleEditor : public juce::AudioProcessorEditor,private juce::Timer {
public:
 explicit ModuleEditor(ModuleProcessor&,bool useGeneric=true);~ModuleEditor() override;
 void paint(juce::Graphics&) override;void resized() override;
 struct HostBypassBinding {
    // Message-thread-only callbacks; nullopt means unavailable/stale binding.
    std::function<std::optional<bool>()> read;
    std::function<void(bool)> write;
 };
 void setHostBypassBinding(HostBypassBinding binding){hostBypass=std::move(binding);refreshParameterActivity();}
 HostBypassBinding hostBypassBinding() const {return hostBypass;}
 std::optional<bool> hostBypassState() const {return hostBypass.read?hostBypass.read():std::nullopt;}
 void requestHostBypass(bool state){if(hostBypass.write&&hostBypassState().has_value())hostBypass.write(state);refreshParameterActivity();}
 virtual void refreshParameterActivity(); // message thread, also used by native interaction tests
private:
 struct Control {
    juce::Label label;ui::PhysicalValueSlider knob;juce::ToggleButton toggle;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> sliderAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> buttonAttachment;
    bool isToggle=false;
 };
 void timerCallback() override;
 ui::PluginTelemetryView telemetryView;
 HostBypassBinding hostBypass;
 ModuleProcessor& processor;SuiteMaterialLook look;
 struct Section {juce::String title;juce::Rectangle<float> bounds;juce::StringArray ids;};
 std::vector<Section> sections;
 void configureSections();
 juce::Image faceplate,brandReference;
 std::vector<std::unique_ptr<Control>> controls;
};

class SurgicalEqEditor final:public ModuleEditor {public:explicit SurgicalEqEditor(SurgicalEqProcessor& p):ModuleEditor(p){}};
class VcaEditor final:public ModuleEditor {public:explicit VcaEditor(VcaProcessor& p):ModuleEditor(p){}};
class TruePeakEditor final:public ModuleEditor {public:explicit TruePeakEditor(TruePeakProcessor& p):ModuleEditor(p){}};
}

#include "NativeOriginalEditors.h"
