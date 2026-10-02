#include "../Source/UI/NativeWorkspace.h"
#include <iostream>
#include <stdexcept>
#include <limits>

namespace {
void require(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
juce::Component* find(juce::Component& root,const juce::String& id){if(root.getComponentID()==id)return &root;for(auto* child:root.getChildren())if(auto* result=find(*child,id))return result;return nullptr;}
template<class T>T& control(juce::Component& root,const juce::String& id){auto* c=dynamic_cast<T*>(find(root,id));require(c!=nullptr,("Missing production control: "+id).toRawUTF8());return *c;}
void click(juce::Component& root,const juce::String& id){auto& b=control<juce::TextButton>(root,id);require(b.isEnabled(),"Production button was unexpectedly disabled");require(bool(b.onClick),"Button has no production callback");b.onClick();}
int differences(const juce::Image& a,const juce::Image& b){int count=0;for(int y=0;y<a.getHeight();++y)for(int x=0;x<a.getWidth();++x)if(a.getPixelAt(x,y)!=b.getPixelAt(x,y))++count;return count;}
int readablePixels(const juce::Image& image){int n=0;for(int y=0;y<image.getHeight();++y)for(int x=0;x<image.getWidth();++x){auto c=image.getPixelAt(x,y);if((c.getGreen()>130&&c.getBlue()>140)||(c.getRed()>180&&c.getGreen()>100))++n;}return n;}
}
int main(int argc,char** argv){juce::ScopedJuceInitialiser_GUI gui;try{
    const juce::File snapshots=argc>1?juce::File(argv[1]):juce::File{};
    auto saveRaster=[&](const juce::Image& image,const juce::String& name){if(snapshots==juce::File{})return;require(snapshots.createDirectory().wasOk(),"Cannot create workspace snapshot directory");juce::MemoryOutputStream png;require(juce::PNGImageFormat().writeImageToStream(image,png),"Cannot encode workspace raster");require(snapshots.getChildFile(name+".png").replaceWithData(png.getData(),png.getDataSize()),"Cannot save workspace raster");};
    // No AudioDeviceManager or hardware. Real engine, registry and UI components.
    auto bank=std::make_unique<hem::TrackBank>();
    juce::AudioBuffer<float> sourceAudio(2,96000);sourceAudio.clear();
    for(int ch=0;ch<2;++ch)for(int i=0;i<sourceAudio.getNumSamples();++i)sourceAudio.setSample(ch,i,.0316228f);
    juce::MemoryAudioSource source(sourceAudio,true,true);
    for(int i=0;i<13;++i){auto& track=bank->tracks[size_t(i)];track.name="STEM "+juce::String(i+1);track.prepare(96000,128);}
    auto& first=bank->tracks[0];first.transport().setSource(&source,0,nullptr,96000,2);first.loaded=true;first.transport().start();
    int selectedTrack=0,selectedSlot=0,installed=-1,opened=-1,monitorCalls=0;
    hem::MonitorProcessor monitor;monitor.prepareToPlay(96000,128);
    hem::NativeWorkspace* target=nullptr;
    hem::NativeWorkspace::Callbacks callbacks;
    callbacks.bank=[&]() -> hem::TrackBank& {return *bank;};
    callbacks.getSelectedTrack=[&]{return selectedTrack;};callbacks.getSelectedSlot=[&]{return selectedSlot;};
    callbacks.setSelectedTrack=[&](int i){if(target)target->clearEditor();selectedTrack=i;};
    callbacks.setSelectedSlot=[&](int i){if(target)target->clearEditor();selectedSlot=i;};
    callbacks.onOpenSlot=[&](int slot){opened=slot;if(auto* p=bank->tracks[size_t(selectedTrack)].chain.activeProcessor(slot))target->setEditor(std::unique_ptr<juce::AudioProcessorEditor>(p->createEditor()));};
    callbacks.onInstallModule=[&](int index){installed=index;auto& chain=bank->tracks[size_t(selectedTrack)].chain;require(chain.activeProcessor(selectedSlot)==nullptr,"Test attempted to overwrite an occupied slot");auto node=chain.stageNode(hem::PluginRegistry::entries()[size_t(index)].create(),96000,128,chain.slotLatency());chain.installInitial(selectedSlot,std::move(node),false);};
    callbacks.onMonitor=[&]{++monitorCalls;target->setEditor(std::unique_ptr<juce::AudioProcessorEditor>(monitor.createEditor()));};
    callbacks.routeTelemetry=[] {hem::NativeWorkspace::RouteTelemetry t;t.sampleRate=96000;t.blockSize=128;t.hardwareOutputs=2;t.limiterLatencySamples=384;return t;};
    hem::NativeWorkspace workspace(std::move(callbacks));target=&workspace;workspace.setSize(1255,489);
    require(workspace.page()==hem::NativeWorkspace::Page::plugin,"Initial workspace page is incorrect");
    for(int i=0;i<8;++i)require(find(workspace,"plugin-slot-"+juce::String(i))!=nullptr,"One of eight serial slots is absent");
    require(workspace.filteredRegistryIndices().size()==12,"Vault did not use all 12 real registry entries");
    workspace.setVaultFilter("compressor");require(workspace.filteredRegistryIndices()==std::vector<int>{2},"Text search did not find real VCA registry entry");
    workspace.setVaultFilter("","Dynamics");require(workspace.filteredRegistryIndices()==std::vector<int>({2,4,5}),"Category filter does not match registry categories");
    workspace.setVaultFilter("tape","Dynamics");require(workspace.filteredRegistryIndices().empty(),"Search/category conjunction is incorrect");
    workspace.setVaultFilter("TaPe");require(workspace.filteredRegistryIndices()==std::vector<int>{10},"Search should ignore case");
    workspace.setVaultFilter("","All categories");
    control<juce::TextEditor>(workspace,"vault-search").setText("surgical",false);control<juce::TextEditor>(workspace,"vault-search").onTextChange();
    require(workspace.filteredRegistryIndices()==std::vector<int>{1},"Production text editor was not connected to filter");workspace.setVaultFilter("");
    workspace.setPage(hem::NativeWorkspace::Page::vault);click(workspace,"vault-install-0");
    require(installed==0&&bank->tracks[0].chain.activeProcessor(0)!=nullptr,"Vault install did not reach the actual registry/track chain");
    workspace.setPage(hem::NativeWorkspace::Page::plugin);click(workspace,"plugin-slot-0");
    require(opened==0&&workspace.hasEditor(),"Real slot did not open its native editor");
    auto& viewport=control<juce::Viewport>(workspace,"plugin-editor-viewport");
    auto* canvas=viewport.getViewedComponent();require(canvas!=nullptr,"Editor viewport has no canvas");
    juce::AudioProcessorEditor* embedded=nullptr;for(auto* c:canvas->getChildren())if(auto* e=dynamic_cast<juce::AudioProcessorEditor*>(c))embedded=e;
    require(embedded!=nullptr&&embedded->getParentComponent()==canvas,"Native editor was not embedded in this window");
    const int nativeWidth=embedded->getWidth(),nativeHeight=embedded->getHeight();
    saveRaster(workspace.createComponentSnapshot(workspace.getLocalBounds()),"plugin-native-editor");
    juce::Component::SafePointer<juce::AudioProcessorEditor> editorObserver(embedded);
    require(canvas->getWidth()>=nativeWidth&&canvas->getHeight()>=nativeHeight,"Original editor dimensions were cropped without scrolling");
    workspace.setPluginExpanded(true);require(workspace.pluginExpanded()&&!control<juce::Viewport>(workspace,"plugin-stem-viewport").isVisible(),"Plugin expansion did not hide stem pane");
    require(embedded->getWidth()==nativeWidth&&embedded->getHeight()==nativeHeight,"Expanding changed original native editor dimensions");
    saveRaster(workspace.createComponentSnapshot(workspace.getLocalBounds()),"plugin-expanded");
    click(workspace,"plugin-expand");require(!workspace.pluginExpanded(),"Production restore-split control was not wired");
    workspace.setSplitProportion(.52f);const int largeLeft=control<juce::Viewport>(workspace,"plugin-stem-viewport").getWidth();
    workspace.setSplitProportion(.24f);require(control<juce::Viewport>(workspace,"plugin-stem-viewport").getWidth()<largeLeft,"Split proportion did not adjust real geometry");
    workspace.setSplitProportion(2);require(workspace.splitProportion()==.60f,"Split upper bound not enforced");workspace.setSplitProportion(std::numeric_limits<float>::quiet_NaN());require(workspace.splitProportion()==.60f,"Non-finite split corrupted geometry");workspace.setSplitProportion(.32f);
    click(workspace,"plugin-bypass-0");require(bank->tracks[0].chain.isBypassed(0)&&control<juce::TextButton>(workspace,"plugin-bypass-0").getToggleState(),"Bypass did not update real insert and illumination");
    click(workspace,"plugin-bypass-0");require(!bank->tracks[0].chain.isBypassed(0),"Repeated bypass failed");
    const float clippedSample=1.1f;first.signalMeters.push(&clippedSample,&clippedSample,1);workspace.refresh();require(first.signalMeters.snapshot().left.clipped,"Actual meter failed to latch a clipped test sample");
    require(control<juce::Component>(workspace,"plugin-signal-meter").keyPressed(juce::KeyPress(juce::KeyPress::returnKey)),"Plugin meter has no keyboard reset");require(first.signalMeters.snapshot().left.clipped,"Clip reset was not deferred to audio publication");first.signalMeters.push(nullptr,nullptr,0);require(!first.signalMeters.snapshot().left.clipped,"Plugin meter reset did not reach actual clip detector");
    workspace.setPage(hem::NativeWorkspace::Page::mixer);
    first.signalMeters.push(&clippedSample,&clippedSample,1);workspace.refresh();require(control<juce::Component>(workspace,"mixer-signal-meter-0").keyPressed(juce::KeyPress(juce::KeyPress::spaceKey)),"Mixer meter has no keyboard reset");first.signalMeters.push(nullptr,nullptr,0);require(!first.signalMeters.snapshot().left.clipped&&!first.signalMeters.snapshot().right.clipped,"Mixer meter reset did not clear both actual clip latches");
    for(int i=0;i<13;++i){auto& gain=control<juce::Slider>(workspace,"mixer-gain-"+juce::String(i));require(gain.getMinimum()==-60&&gain.getMaximum()==6,"Mixer gain range is not -60..+6 dB");require(!control<juce::TextButton>(workspace,"mixer-mute-"+juce::String(i)).getToggleState(),"Mute lit without engine state");require(!control<juce::TextButton>(workspace,"mixer-solo-"+juce::String(i)).getToggleState(),"Solo lit without engine state");}
    auto& gain=control<juce::Slider>(workspace,"mixer-gain-3");gain.setValue(-11.2,juce::sendNotificationSync);require(std::abs(bank->tracks[3].trimDb.load()+11.2f)<.001f,"Mixer fader did not change real atomic gain");
    click(workspace,"mixer-mute-3");click(workspace,"mixer-solo-3");require(bank->tracks[3].muted.load()&&bank->tracks[3].solo.load(),"Mute/solo not connected to real track state");
    click(workspace,"mixer-mute-3");click(workspace,"mixer-solo-3");
    click(workspace,"mixer-select-12");require(selectedTrack==12&&workspace.page()==hem::NativeWorkspace::Page::plugin,"Mixer edit did not select thirteenth track");require(editorObserver==nullptr&&!workspace.hasEditor(),"Host selection did not destroy borrowed editor");
    // Navigation alone must preserve running transports, insert and gain state.
    auto render=[&]{juce::AudioBuffer<float> sum(2,128);sum.clear();std::array<float,128> gains;gains.fill(1);for(auto& track:bank->tracks)track.render(sum,128,false,gains.data());};
    for(int i=0;i<60;++i)render();workspace.setPage(hem::NativeWorkspace::Page::mixer);workspace.refresh();
    const double position=first.transport().getCurrentPosition();const bool playing=first.transport().isPlaying();
    for(auto page:{hem::NativeWorkspace::Page::routing,hem::NativeWorkspace::Page::vault,hem::NativeWorkspace::Page::plugin,hem::NativeWorkspace::Page::mixer})workspace.setPage(page);
    require(first.transport().isPlaying()==playing&&playing,"Workspace navigation interrupted playback");require(first.transport().getCurrentPosition()==position,"Workspace navigation performed a seek");require(bank->tracks[0].chain.activeProcessor(0)!=nullptr&&bank->tracks[3].trimDb.load()==float(gain.getValue()),"Navigation changed audio state");
    render();require(first.transport().getCurrentPosition()>position,"Transport no longer advanced after navigation");
    workspace.refresh();auto withSignal=workspace.createComponentSnapshot(workspace.getLocalBounds());
    first.signalMeters.prepare(0);workspace.refresh();auto noMeter=workspace.createComponentSnapshot(workspace.getLocalBounds());require(differences(withSignal,noMeter)>25,"Mixer did not paint actual atomic signal telemetry");
    // Real production raster at multiple window sizes: every strip reachable,
    // readable geometry retained; narrow layouts use viewport scrolling.
    for(auto size:{juce::Point<int>(1255,489),juce::Point<int>(1046,395),juce::Point<int>(960,420),juce::Point<int>(1882,733)}){
        workspace.setSize(size.x,size.y);workspace.setPage(hem::NativeWorkspace::Page::mixer);
        for(int i=0;i<13;++i){auto& strip=control<juce::Component>(workspace,"mixer-strip-"+juce::String(i));require(strip.getWidth()>=86&&strip.getHeight()>=270,"Mixer compressed a strip below readable minimum");require(strip.getBounds().getRight()<=strip.getParentComponent()->getWidth(),"Thirteenth strip fell outside scrollable content");}
        // The default 1280x720 shell exposes only 1046x395 of workspace.
        // Test the visible viewport (including horizontal scrollbar loss), not
        // just content bounds, so mute/solo/edit can never hide below it.
        auto& mixerViewport=control<juce::Viewport>(workspace,"mixer-viewport");
        mixerViewport.setViewPosition(0,0);const auto visibleArea=mixerViewport.getViewArea();
        require(!mixerViewport.getVerticalScrollBar().isVisible(),"Mixer requires vertical scrolling at a supported workspace size");
        for(int i=0;i<13;++i){
            for(const auto* prefix:{"mixer-gain-","mixer-readout-","mixer-mute-","mixer-solo-","mixer-select-"}){
                auto& item=control<juce::Component>(workspace,juce::String(prefix)+juce::String(i));
                const auto visibleBounds=mixerViewport.getViewedComponent()->getLocalArea(&item,item.getLocalBounds());
                require(visibleBounds.getY()>=visibleArea.getY()&&visibleBounds.getBottom()<=visibleArea.getBottom(),"Mixer control is vertically clipped by the actual viewport");
            }
        }
        if(size.x==1046&&size.y==395){
            require(mixerViewport.getHorizontalScrollBar().isVisible(),"Default shell should preserve strip width with horizontal scrolling");
            mixerViewport.setViewPosition(mixerViewport.getViewedComponent()->getWidth(),0);
            const auto finalArea=mixerViewport.getViewArea();auto& finalButton=control<juce::Component>(workspace,"mixer-select-12");
            require(finalArea.contains(mixerViewport.getViewedComponent()->getLocalArea(&finalButton,finalButton.getLocalBounds())),"The thirteenth mixer edit button is not fully reachable by horizontal scrolling");
            saveRaster(workspace.createComponentSnapshot(workspace.getLocalBounds()),"mixer-last-strip-1046x395");
            mixerViewport.setViewPosition(0,0);
        }
        for(auto page:{hem::NativeWorkspace::Page::plugin,hem::NativeWorkspace::Page::mixer,hem::NativeWorkspace::Page::vault,hem::NativeWorkspace::Page::routing}){workspace.setPage(page);auto image=workspace.createComponentSnapshot(workspace.getLocalBounds());require(image.isValid()&&image.getWidth()==size.x&&image.getHeight()==size.y,"Workspace raster geometry is invalid");require(readablePixels(image)>1000,"Workspace page lacks readable live UI pixels");saveRaster(image,"workspace-page-"+juce::String(int(page))+"-"+juce::String(size.x)+"x"+juce::String(size.y));}
    }
    workspace.setSize(1255,489);workspace.setPage(hem::NativeWorkspace::Page::vault);click(workspace,"vault-install-11");require(monitorCalls==1&&workspace.hasEditor()&&installed==0,"Monitoring-only registry entry incorrectly installed a stem insert");workspace.clearEditor();require(!workspace.hasEditor(),"Editor cleanup failed");
    // A replacement bank is resolved through the getter, never a stale reference.
    first.transport().setSource(nullptr);for(auto& track:bank->tracks)track.release();
    auto replacement=std::make_unique<hem::TrackBank>();replacement->tracks[12].name="RESTORED STEM";bank.swap(replacement);workspace.refresh();
    workspace.setPage(hem::NativeWorkspace::Page::plugin);require(control<juce::ComboBox>(workspace,"plugin-track-select").getText().contains("RESTORED STEM"),"Workspace retained a retired bank reference");
    std::cout<<"PASS production 13-track mixer controls, real metering, 12-entry search/categories, registry install/monitor routing, split/expand and native editor lifetime, multi-size rasters, bank replacement and uninterrupted navigation\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
