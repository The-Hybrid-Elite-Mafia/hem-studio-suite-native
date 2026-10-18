#pragma once
#include "Editors.h"
#include "PitchTimeProcessors.h"
namespace hem {
struct PitchTimeViewSnapshot {
 struct Point {double seconds=0,inputCents=0,targetCents=0;bool inputValid=false,targetValid=false;};
 juce::String selection,status,inputNote,targetNote;
 double sourceSeconds=0,renderedSeconds=0,projectBpm=120;
 bool sourceValid=false,renderedValid=false,busy=false,canUndo=false;
 // Worker measurements, copied to GUI by the host. Envelopes are signed peak
 // bins in each file's own time domain. Maximum 1024 values/points each.
 std::vector<float> sourceEnvelope,renderedEnvelope;
 std::vector<Point> pitch;std::array<float,4> peaks{};bool peaksValid=false;
};
class PitchTimeEditor final : public ModuleEditor {
public:
 enum class Command {preview,apply,undo,selectSource};
 explicit PitchTimeEditor(PitchTimeProcessor&);
 ~PitchTimeEditor() override;
 void setCommandRequest(std::function<void(Command)>);
 void setAuditionRequest(std::function<void(bool)> f){auditionRequest=std::move(f);}
 void setRecipeChanged(std::function<void()> f){recipeChanged=std::move(f);}
 void setCloseRequest(std::function<void()> f){closeRequest=std::move(f);}
 // Message thread only, after worker->host transfer. No filesystem or DSP here.
 void setSnapshot(PitchTimeViewSnapshot);
 void requestCommand(Command);
 bool keyPressed(const juce::KeyPress&) override;
 void paint(juce::Graphics&) override;void resized() override;
 void refreshParameterActivity() override;
private:
 struct Control {juce::String id;juce::Rectangle<float> body,read;int kind=0,choice=-1;ui::PhysicalValueSlider slider;juce::ToggleButton button;juce::ComboBox combo;
  std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> sa;std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> ba;std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> ca;};
 void dial(const char*,float,float,float,juce::Rectangle<float>);
 void fader(const char*,juce::Rectangle<float>,juce::Rectangle<float>);
 void numeric(const char*,juce::Rectangle<float>);
 void toggle(const char*,const char*,juce::Rectangle<float>);
 void choice(const char*,const char*,int,juce::Rectangle<float>);
 void combo(const char*,juce::StringArray,juce::Rectangle<float>);
 void command(const char*,const char*,Command,juce::Rectangle<float>);
 void setParameter(const char*,double);
 class Look final : public juce::LookAndFeel_V4 {public:void drawLinearSlider(juce::Graphics&,int,int,int,int,float,float,float,juce::Slider::SliderStyle,juce::Slider&) override;};
 PitchTimeProcessor& processor;juce::Image art;FoundationLook look;Look faderLook;std::vector<std::unique_ptr<Control>> controls;
 PitchTimeViewSnapshot snapshot;std::function<void(Command)> commandRequest;std::function<void()> closeRequest;
 bool updatingTempo=false;
 bool lastTempoSync=false;
 std::function<void(bool)> auditionRequest;std::function<void()> recipeChanged;std::vector<float> lastValues;
};
}

