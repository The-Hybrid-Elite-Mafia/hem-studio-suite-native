#include "StemFileImporter.h"
namespace hem {
StemFileImporter::StemFileImporter(juce::TimeSliceThread& d,ContextProvider c,Commit handler)
 :Thread("HEM WAV import worker"),disk(d),contextProvider(std::move(c)),commit(std::move(handler)){
    setLookAndFeel(&look);for(auto* child:std::array<juce::Component*,4>{&browseButton,&applyButton,&cancelButton,&status})addAndMakeVisible(*child);
    status.setText("DROP 1-13 WAV FILES / sequential tracks 1-13",juce::dontSendNotification);
    status.setInterceptsMouseClicks(false,false);status.setJustificationType(juce::Justification::centred);
    browseButton.onClick=[this]{browse();};applyButton.onClick=[this]{apply();};cancelButton.onClick=[this]{cancel();};
    applyButton.setEnabled(false);setSize(900,110);startTimerHz(20);
}
StemFileImporter::~StemFileImporter(){shutdown();setLookAndFeel(nullptr);}
void StemFileImporter::shutdown(){
    if(closed)return;closed=true;stopTimer();picker.reset();signalThreadShouldExit();waitForThreadToExit(-1);
    // Shutdown occurs before the shared disk read-ahead thread is stopped.
    pending.reset();finished.reset();garbage.reset();
}
bool StemFileImporter::isInterestedInFileDrag(const juce::StringArray& files){
    if(closed||busy.load()||pending||files.isEmpty()||files.size()>capacity)return false;
    for(const auto& name:files)if(!name.endsWithIgnoreCase(".wav"))return false;
    return true;
}
void StemFileImporter::fileDragEnter(const juce::StringArray&,int,int){hovered=true;repaint();}
void StemFileImporter::fileDragExit(const juce::StringArray&){hovered=false;repaint();}
void StemFileImporter::filesDropped(const juce::StringArray& files,int,int){hovered=false;submitFiles(files);repaint();}
// Background releases, drags and right-clicks are not import commands.
// BROWSE WAVS and explicit file drops remain the import entry points.
void StemFileImporter::browse(){
    if(closed||busy.load()||pending)return;
    picker=std::make_unique<juce::FileChooser>("Import up to 13 mono/stereo WAV stems",juce::File(),"*.wav");
    juce::Component::SafePointer<StemFileImporter> safe(this);
    picker->launchAsync(juce::FileBrowserComponent::openMode|juce::FileBrowserComponent::canSelectFiles|juce::FileBrowserComponent::canSelectMultipleItems,
        [safe](const juce::FileChooser& chooser){if(!safe)return;juce::StringArray paths;for(const auto& f:chooser.getResults())paths.add(f.getFullPathName());if(!paths.isEmpty())safe->submitFiles(paths);});
}
void StemFileImporter::submitFiles(const juce::StringArray& files,int firstTrack,bool requireEmptyTargets,double projectTempo){
    jassert(juce::MessageManager::getInstance()->isThisTheMessageThread());
    if(closed||busy.load()||pending){status.setText("Finish or clear the current import first",juce::dontSendNotification);return;}
    if(firstTrack<0||files.isEmpty()||firstTrack+files.size()>capacity){status.setText("Import must fit within 13 tracks; no files were changed",juce::dontSendNotification);return;}
    if(projectTempo!=0&&(!std::isfinite(projectTempo)||projectTempo<40||projectTempo>240)){status.setText("Invalid instrumental tempo; import rejected",juce::dontSendNotification);return;}
    for(const auto& path:files)if(!path.endsWithIgnoreCase(".wav")){status.setText("WAV only; the whole batch was rejected",juce::dontSendNotification);return;}
    jobContext=contextProvider();if(jobContext.sampleRate<=0||jobContext.blockSize<=0){status.setText("Session processing is not prepared; try again",juce::dontSendNotification);return;}
    jobFiles=files;jobFirstTrack=firstTrack;jobRequireEmptyTargets=requireEmptyTargets;jobProjectTempo=projectTempo;error.clear();progress.store(0);ready.store(false);cleanupJob=false;busy.store(true);
    status.setText("Preparing WAV readers and sample-rate conversion...",juce::dontSendNotification);browseButton.setEnabled(false);applyButton.setEnabled(false);
    if(!startThread()){busy.store(false);browseButton.setEnabled(true);status.setText("Could not start the import worker",juce::dontSendNotification);}
}
void StemFileImporter::run(){
    if(cleanupJob){garbage.reset();ready.store(true,std::memory_order_release);return;}
    try{
        juce::AudioFormatManager formats;formats.registerFormat(new juce::WavAudioFormat(),true);
        auto batch=std::make_unique<Batch>();batch->context=jobContext;batch->firstTrack=jobFirstTrack;batch->count=jobFiles.size();batch->requireEmptyTargets=jobRequireEmptyTargets;batch->projectTempo=jobProjectTempo;
        for(int i=0;i<batch->count;++i){
            if(threadShouldExit()){error="Import cancelled; original tracks unchanged";ready.store(true,std::memory_order_release);return;}
            auto stem=std::make_unique<StemPlayback>();stem->prepareFile(juce::File(jobFiles[i]),formats,disk,jobContext.sampleRate,jobContext.blockSize,jobContext.looping);
            batch->stems[size_t(i)]=std::move(stem);progress.store(i+1,std::memory_order_relaxed);
        }
        if(threadShouldExit())error="Import cancelled; original tracks unchanged";else finished=std::move(batch);
    }catch(const std::exception& e){error=juce::String(e.what());}
    ready.store(true,std::memory_order_release);
}
void StemFileImporter::timerCallback(){
    if(busy.load()&&!cleanupJob&&!ready.load())status.setText("PREPARING "+juce::String(progress.load())+" / "+juce::String(jobFiles.size())+" WAV files  -  current audio unchanged",juce::dontSendNotification);
    if(!ready.load(std::memory_order_acquire)||isThreadRunning())return;
    ready.store(false);busy.store(false);
    if(cleanupJob){cleanupJob=false;browseButton.setEnabled(true);return;}
    if(error.isNotEmpty()){status.setText(error,juce::dontSendNotification);browseButton.setEnabled(true);return;}
    pending=std::move(finished);if(!pending)return;
    juce::String mapping;
    for(int i=0;i<pending->count;++i)mapping+="TRACK "+juce::String(pending->firstTrack+i+1)+": "+pending->stems[size_t(i)]->file.getFileName()+"\n";
    status.setTooltip(mapping);status.setText("READY: "+juce::String(pending->count)+" WAVs -> TRACKS "+juce::String(pending->firstTrack+1)+"-"+juce::String(pending->firstTrack+pending->count)+" / APPLY, or press PLAY while stopped",juce::dontSendNotification);
    applyButton.setEnabled(true);
}
void StemFileImporter::apply(){
    applyPreparedBatch();
}
juce::Result StemFileImporter::applyPreparedBatch(){
    if(!pending||busy.load())return juce::Result::fail("Import is not ready yet");
    const auto result=commit(*pending);
    if(result.failed()){status.setText(result.getErrorMessage(),juce::dontSendNotification);return result;}
    status.setText("IMPORT APPLIED / original insert chains retained / press PLAY",juce::dontSendNotification);
    disposePending();
    return juce::Result::ok();
}
void StemFileImporter::disposePending(){
    // The host swaps replaced sources INTO the batch. Reclaim their readers and
    // buffering clients on this worker, never in the callback or commit lock.
    garbage=std::move(pending);cleanupJob=true;busy.store(true);ready.store(false);applyButton.setEnabled(false);
    if(!startThread()){busy.store(false);cleanupJob=false;status.setText("Cleanup worker unavailable; close importer to release retained sources",juce::dontSendNotification);}
}
void StemFileImporter::cancel(){
    if(busy.load()){if(!cleanupJob)signalThreadShouldExit();return;}
    if(pending){status.setText("Prepared batch cleared; original tracks unchanged",juce::dontSendNotification);disposePending();}
}
void StemFileImporter::paint(juce::Graphics& g){
    juce::ColourGradient glass(juce::Colour(0xff263039),0,0,juce::Colour(0xff070a10),0,float(getHeight()),false);
    g.setGradientFill(glass);g.fillRoundedRectangle(getLocalBounds().toFloat().reduced(2),10);
    g.setColour(hovered?juce::Colour(0xffff8800):juce::Colour(0xff00cdda));g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(2),10,2);
}
void StemFileImporter::resized(){auto b=getLocalBounds().reduced(8);auto row=b.removeFromBottom(32);browseButton.setBounds(row.removeFromLeft(row.getWidth()/3).reduced(2));applyButton.setBounds(row.removeFromLeft(row.getWidth()/2).reduced(2));cancelButton.setBounds(row.reduced(2));status.setBounds(b);}
}
