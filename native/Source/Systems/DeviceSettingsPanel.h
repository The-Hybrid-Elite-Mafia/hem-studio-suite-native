#pragma once
#include <JuceHeader.h>
namespace hem {
// The platform backend supplies names and channel masks. No model assumptions.
class DeviceSettingsPanel final : public juce::Component {
public:
 explicit DeviceSettingsPanel(juce::AudioDeviceManager& m):manager(m) {
  addAndMakeVisible(refresh);addAndMakeVisible(note);addAndMakeVisible(view);
  note.setText("Choose driver, input/output device and active channels. USB interfaces appear under their installed driver. Select one mono output or two stereo outputs. Changes restart the audio device.",juce::dontSendNotification);
  refresh.onClick=[this]{scan();};scan();
 }
 void setDeviceChangesAllowed(bool allowed){
  if(changesAllowed==allowed)return;changesAllowed=allowed;refresh.setEnabled(allowed);if(selector)selector->setEnabled(allowed);
  note.setText(allowed?"Choose installed driver, devices and active channels. Actual hardware names/rates are shown. Changes restart the audio device.":"Hardware settings are read-only while playback, recording or export is active. Navigation and the shared transport remain available; pause/finish before changing devices.",juce::dontSendNotification);
 }
 void resized() override {
  auto b=getLocalBounds().reduced(10);refresh.setBounds(b.removeFromTop(34));
  note.setBounds(b.removeFromTop(64));view.setBounds(b);
  if(selector)selector->setSize(std::max(200,view.getWidth()-20),std::max(600,view.getHeight()));
 }
private:
 void scan(){
  for(auto* type:manager.getAvailableDeviceTypes())type->scanForDevices();
  // AudioSourcePlayer supports at most 128 compacted channels. Individual
  // hardware channel identities remain selectable, not fixed to input 1/2.
  selector=std::make_unique<juce::AudioDeviceSelectorComponent>(manager,0,128,1,2,false,false,false,false);
  selector->setEnabled(changesAllowed);view.setViewedComponent(selector.get(),false);resized();
 }
 bool changesAllowed=true;
 juce::AudioDeviceManager& manager;
 juce::TextButton refresh{"RESCAN CONNECTED AUDIO DEVICES"};juce::Label note;
 std::unique_ptr<juce::AudioDeviceSelectorComponent> selector;
 juce::Viewport view;
};
}
