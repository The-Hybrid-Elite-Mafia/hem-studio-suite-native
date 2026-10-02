#pragma once
#include "OfflineMixWorker.h"
#include "../HardwareLookAndFeel.h"

namespace hem {
class OfflineMixPanel final : public juce::Component, private juce::Timer {
public:
    using Start = std::function<juce::Result(const juce::File&, const OfflineMixOptions&)>;
    OfflineMixPanel(OfflineMixWorker& worker, Start start) : renderer(worker), startExport(std::move(start)) {
        setLookAndFeel(&look);
        for (auto* c : std::array<juce::Component*,6>{&heading, &description, &tail, &choose, &cancel, &status}) addAndMakeVisible(*c);
        heading.setText("OFFLINE STEREO MIX / 48 kHz / FLOAT32 WAV", juce::dontSendNotification);
        description.setText("Exports from timeline zero through the longest loaded stem, plus the selected tail.\nIncludes track inserts, mute/solo, track gain, input/output gain and the current master limiter.\nListening EQ, headphone correction and live microphone monitoring are excluded.\nLooping and playback position do not change the export. The session is copied at export start.\nTrue-peak certification remains pending for the current limiter.", juce::dontSendNotification);
        tail.setName("Export tail seconds"); tail.setSliderStyle(juce::Slider::LinearHorizontal);
        tail.setRange(0,30,0.1); tail.setValue(2); tail.setTextBoxStyle(juce::Slider::TextBoxRight,false,110,28);
        tail.setTextValueSuffix(" s tail"); tail.setTooltip("Additional audio after the longest stem; increase for long effect tails");
        choose.onClick=[this] { chooseDestination(); };
        cancel.onClick=[this] { renderer.cancel(); status.setText("Cancelling safely...", juce::dontSendNotification); };
        status.setText(renderer.result(),juce::dontSendNotification); startTimerHz(20); setSize(850,430);
    }
    ~OfflineMixPanel() override { stopTimer(); picker.reset(); setLookAndFeel(nullptr); }
    void paint(juce::Graphics& g) override { g.fillAll(juce::Colour(0xff08121a)); }
    void resized() override {
        auto area=getLocalBounds().reduced(24); heading.setBounds(area.removeFromTop(38));
        description.setBounds(area.removeFromTop(155)); tail.setBounds(area.removeFromTop(44));
        auto row=area.removeFromTop(48); choose.setBounds(row.removeFromLeft(row.getWidth()*2/3).reduced(4)); cancel.setBounds(row.reduced(4));
        status.setBounds(area.reduced(4));
    }
private:
    void chooseDestination() {
        if (renderer.isBusy()) return;
        picker=std::make_unique<juce::FileChooser>("Export stereo mix to a NEW WAV file",juce::File(),"*.wav");
        juce::Component::SafePointer<OfflineMixPanel> safe(this);
        picker->launchAsync(juce::FileBrowserComponent::saveMode|juce::FileBrowserComponent::canSelectFiles,
            [safe](const juce::FileChooser& chooser) {
                if (!safe || chooser.getResult()==juce::File()) return;
                const auto destination=chooser.getResult().withFileExtension("wav");
                if(destination.exists()) { safe->status.setText("Choose a new filename; existing audio is never overwritten",juce::dontSendNotification); return; }
                OfflineMixOptions options; options.tailSeconds=safe->tail.getValue();
                const auto result=safe->startExport(destination,options);
                safe->status.setText(result.wasOk()?"Rendering frozen mix snapshot":result.getErrorMessage(),juce::dontSendNotification);
            });
    }
    void timerCallback() override {
        const bool busy=renderer.isBusy(); choose.setEnabled(!busy); tail.setEnabled(!busy); cancel.setEnabled(busy);
        if (busy) status.setText("Export " + juce::String(renderer.progress()*100,1) + "% / live session remains unchanged",juce::dontSendNotification);
        else if (wasBusy) status.setText(renderer.result(),juce::dontSendNotification);
        wasBusy=busy;
    }
    HardwareLookAndFeel look;
    OfflineMixWorker& renderer;
    Start startExport;
    juce::Label heading,description,status;
    juce::Slider tail;
    juce::TextButton choose{"CHOOSE NEW FILE / EXPORT"},cancel{"CANCEL EXPORT"};
    std::unique_ptr<juce::FileChooser> picker;
    bool wasBusy=false;
};
}
