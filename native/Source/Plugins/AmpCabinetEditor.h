#pragma once
#include "Editors.h"
#include "AmpCabinetProcessor.h"
namespace hem {
class AmpCabinetEditor final: public ModuleEditor {
public:
 explicit AmpCabinetEditor(AmpCabinetProcessor&);
 ~AmpCabinetEditor()override;
 void setLoadRequest(std::function<void()> f){loadRequest=std::move(f);refreshParameterActivity();}
 void setCloseRequest(std::function<void()> f){closeRequest=std::move(f);}
 void resized()override;void paint(juce::Graphics&)override;void refreshParameterActivity()override;
 bool keyPressed(const juce::KeyPress&)override;
private:
 struct Control{juce::String id;juce::Rectangle<float> body,read;int type=0;
 ui::PhysicalValueSlider slider;juce::ToggleButton button;juce::ComboBox combo;
 std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> sa;
 std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> ba;
 std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> ca;};
 void dial(const char*,juce::Rectangle<float>,juce::Rectangle<float>);
 void button(const char*,const char*,juce::Rectangle<float>,bool);
 void choice(const char*,juce::Rectangle<float>,int,const char*(*)(int)noexcept);
 AmpCabinetProcessor& processor;juce::Image art;FoundationLook look;
 std::vector<std::unique_ptr<Control>> controls;std::function<void()> loadRequest,closeRequest;
 juce::Label identity;PluginTelemetry::Levels levels{};uint64_t blocks=0,epoch=0;double lastAt=-1;bool fresh=false;
};
}
