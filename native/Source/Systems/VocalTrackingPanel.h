#pragma once
#include "LiveVocalManager.h"
#include "../StudioWindow.h"
namespace hem {
class VocalTrackingPanel final:public juce::Component,private juce::Timer {
public:
    VocalTrackingPanel(LiveVocalManager&,juce::AudioDeviceManager&,std::function<void()> setup,std::function<int()> backingLatency,std::function<int()> masterLatency,std::function<juce::String()> mayRecord);
    ~VocalTrackingPanel() override {stopTimer();editor.reset();setLookAndFeel(nullptr);}
    void resized() override;
private:
    void updateMonitoringNotice();
    void timerCallback() override;void chooseTake();void insert(int,bool remove=false);void open(int);
    LiveVocalManager& manager;juce::AudioDeviceManager& device;
    std::function<void()> setupDevice;std::function<int()> backingDelay,masterDelay;std::function<juce::String()> recordCheck;
    HardwareLookAndFeel look;
    juce::ComboBox input,monitor,plugin;
    juce::TextButton hardware{"INPUT / OUTPUT DEVICE"},record{"RECORD NEW TAKE"},stop{"STOP / FINALIZE TAKE"};
    juce::ToggleButton print{"PRINT INSERTS TO STEREO TAKE"};juce::Slider level,offset;
    juce::Label status,help,monitoringNotice;
    struct Row{juce::TextButton name,load{"INSERT"},remove{"REMOVE"},bypass{"BYP"};};
    std::array<Row,8> rows;
    std::unique_ptr<juce::FileChooser> chooser;std::unique_ptr<StudioWindow> editor;
    juce::String lastInputs,notice;int edited=-1;
};
}
