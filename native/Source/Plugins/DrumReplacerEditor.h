#pragma once
#include "Editors.h"
#include "DrumReplacerProcessor.h"
namespace hem {
// The host owns file selection and prepared-node replacement. All callbacks here
// run on the message thread; this editor never mutates a published sample bank.
class DrumReplacerEditor final : public ModuleEditor {
public:
 explicit DrumReplacerEditor(DrumReplacerProcessor&);
 ~DrumReplacerEditor() override;
 void setLoadRequest(std::function<void(int)>);
 void setCloseRequest(std::function<void()>);
 void resized() override;
 void paint(juce::Graphics&) override;
 void refreshParameterActivity() override;
 bool keyPressed(const juce::KeyPress&) override;
private:
 struct Control {
  juce::String id;juce::Rectangle<float> body,read;bool dial=false;int layer=-1;
  ui::PhysicalValueSlider slider;juce::ToggleButton button;
  std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> sa;
  std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> ba;
 };
 void addDial(const juce::String&,float,float,float,juce::Rectangle<float>);
 void addButton(const juce::String&,const juce::String&,juce::Rectangle<float>,bool,int=-1);
 DrumReplacerProcessor& processor;juce::Image art;FoundationLook look;
 std::vector<std::unique_ptr<Control>> controls;
 std::array<juce::Label,4> identities;
 std::function<void(int)> loadRequest;std::function<void()> closeRequest;
 PluginTelemetry::Levels levels{};uint64_t lastBlocks=0,lastEpoch=0,lastHits=0;
 drum::DetectorHistorySnapshot history{};bool historyValid=false;
 double lastAt=-1,hitAt=-1;bool fresh=false,trigger=false;
};
}
