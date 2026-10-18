#pragma once
#include <JuceHeader.h>
#include "StemPlayback.h"
#include "HardwareLookAndFeel.h"
#include <array>
#include <functional>
#include <stdexcept>

namespace hem {
class StemFileImporter final : public juce::Component,
                               public juce::FileDragAndDropTarget,
                               private juce::Thread,
                               private juce::Timer {
public:
    static constexpr int capacity=13;
    struct Context {double sampleRate=0;int blockSize=0;unsigned generation=0;bool looping=false;};
    struct Batch {
        Context context;int firstTrack=0,count=0;bool requireEmptyTargets=false;double projectTempo=0;
        std::array<std::unique_ptr<StemPlayback>,capacity> stems;
    };
    using ContextProvider=std::function<Context()>;
    // Called only on the message thread. The host checks its device generation
    // and swaps source pointers while the device callback is briefly excluded.
    using Commit=std::function<juce::Result(Batch&)>;
    StemFileImporter(juce::TimeSliceThread&,ContextProvider,Commit);
    ~StemFileImporter() override;
    bool isInterestedInFileDrag(const juce::StringArray&) override;
    void fileDragEnter(const juce::StringArray&,int,int) override;
    void fileDragExit(const juce::StringArray&) override;
    void filesDropped(const juce::StringArray&,int,int) override;
    void paint(juce::Graphics&) override;
    void resized() override;
    // Same safe path for an existing one-file picker; firstTrack is zero-based.
    void submitFiles(const juce::StringArray&,int firstTrack=0,bool requireEmptyTargets=false,double projectTempo=0);
    bool isBusy() const noexcept {return busy.load();}
    bool hasPendingWork() const noexcept {return busy.load()||ready.load()||pending!=nullptr;} // message thread
    bool hasPreparedBatch() const noexcept {return pending!=nullptr;}
    // Only an explicit Apply/Play command may publish a prepared batch.
    juce::Result applyPreparedBatch();
    void shutdown();
    void cancelPendingWork(){cancel();}
private:
    friend struct StemFileImporterTestAccess;
    void run() override;
    void timerCallback() override;
    void browse();
    void apply();
    void cancel();
    void disposePending();
    juce::TimeSliceThread& disk;
    ContextProvider contextProvider;Commit commit;
    HardwareLookAndFeel look;
    juce::TextButton browseButton{"BROWSE WAVS"},applyButton{"APPLY WHEN STOPPED"},cancelButton{"CANCEL / CLEAR"};
    juce::Label status;
    juce::TooltipWindow tooltips{nullptr,500};
    juce::StringArray jobFiles;int jobFirstTrack=0;bool jobRequireEmptyTargets=false;double jobProjectTempo=0;Context jobContext;
    std::unique_ptr<Batch> pending,finished,garbage;
    juce::String error;std::atomic<bool> busy{false},ready{false};
    std::atomic<int> progress{0};bool hovered=false,cleanupJob=false,closed=false;
    std::unique_ptr<juce::FileChooser> picker;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StemFileImporter)
};
}
