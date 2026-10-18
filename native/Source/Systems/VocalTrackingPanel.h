#pragma once
#include "LiveVocalManager.h"
#include "NativeAudioDeviceHost.h"
#include "../StudioWindow.h"
#include "../Plugins/Editors.h"
namespace hem {
class VocalTrackingPanel final:public juce::Component,private juce::Timer {
public:
    VocalTrackingPanel(LiveVocalManager&,NativeAudioDeviceHost&,std::function<void()> setup,std::function<int()> backingLatency,std::function<int()> masterLatency,std::function<juce::String()> mayRecord,std::function<void(const std::function<void()>&)> withGraphControl);
    ~VocalTrackingPanel() override {stopTimer();++bypassBindingEpoch;editor.reset();setLookAndFeel(nullptr);}
    void resized() override;
    void attachHostBypass(ModuleEditor&,int slot); // message thread; no window/device launch
private:
    friend struct VocalTrackingPanelTestAccess;
    juce::String inputProblem() const;
    bool recordingSourceMatches(unsigned,juce::AudioIODevice*,int) const;
    void completeTakeChoice(const juce::File&,unsigned request,unsigned expectedGeneration,juce::AudioIODevice* expectedDevice,int expectedInput);
    void updateMonitoringNotice();
    void timerCallback() override;void chooseTake();void insert(int,bool remove=false);void open(int);
    LiveVocalManager& manager;NativeAudioDeviceHost& device;
    std::function<void()> setupDevice;std::function<int()> backingDelay,masterDelay;std::function<juce::String()> recordCheck;
    std::function<void(const std::function<void()>&)> withGraphControl;
    HardwareLookAndFeel look;
    juce::ComboBox input,monitor,plugin;
    juce::TextButton hardware{"INPUT / OUTPUT DEVICE"},record{"RECORD NEW TAKE"},stop{"STOP / FINALIZE TAKE"};
    juce::ToggleButton print{"PRINT INSERTS TO STEREO TAKE"};juce::Slider level,offset;
    juce::Label status,help,monitoringNotice;
    struct Row{juce::TextButton name,load{"INSERT"},remove{"REMOVE"},bypass{"BYP"};};
    std::array<Row,8> rows;
    std::unique_ptr<juce::FileChooser> chooser;std::unique_ptr<StudioWindow> editor;
    juce::String lastInputs,notice;int edited=-1;uint64_t bypassBindingEpoch=0;
    unsigned takeChooserRequest=0,displayedInputGeneration=0;
    juce::AudioIODevice* displayedInputDevice=nullptr;
};
}
