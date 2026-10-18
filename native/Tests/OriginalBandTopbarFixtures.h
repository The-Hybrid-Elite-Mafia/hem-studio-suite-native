#pragma once
#include "../Source/UI/NativeWorkspace.h"
static void originalBandTopbarInteractions(const juce::File& evidence){
    for(int module=0;module<4;++module){
        auto processor=hem::PluginRegistry::create(ids[size_t(module)]);
        std::unique_ptr<juce::AudioProcessorEditor> editor(processor->createEditor());
        auto* owner=dynamic_cast<hem::OriginalBandTopbarOwner*>(editor.get());require(owner!=nullptr,"band topbar interface");
        require(editor->getWidth()==1280&&editor->getHeight()==720,"band host-sized default");
        auto& topbar=owner->bandTopbar();auto* close=find(*editor,"uiClose");require(close&&!close->isEnabled(),"unbound close is honestly unavailable");
        for(auto size:{juce::Point<int>(1280,720),juce::Point<int>(1152,648),juce::Point<int>(1000,563)}){
            editor->setSize(size.x,size.y);for(const char* id:{"uiPresets","uiSavePreset","uiClose","uiBack"})if(auto* c=find(*editor,id))if(c->isVisible())require(editor->getLocalBounds().contains(editor->getLocalArea(c,c->getLocalBounds())),"topbar button contained");
        }
        const auto oldRequest=topbar.beginPresetRequest(),newRequest=topbar.beginPresetRequest();require(!topbar.isCurrentPresetRequest(oldRequest)&&topbar.isCurrentPresetRequest(newRequest),"new chooser request supersedes older callback");auto staleFile=evidence.getChildFile("stale-"+juce::String(module)+".hempreset");require(!topbar.completePresetRequest(oldRequest,staleFile,true)&&!staleFile.existsAsFile(),"older chooser cannot write after newer request");topbar.cancelPresetRequest();require(!topbar.isCurrentPresetRequest(newRequest)&&!topbar.completePresetRequest(newRequest,staleFile,true)&&!staleFile.existsAsFile(),"cancel invalidates chooser callback and write");
        auto file=evidence.getChildFile("module-"+juce::String(module)+".hempreset");set(*processor,"output",-9);require(topbar.saveTo(file),"save uses existing host state");set(*processor,"output",3);require(topbar.loadFrom(file),"load existing host state");require(std::abs(processor->state.getRawParameterValue("output")->load()+9)<.01f,"preset physical value roundtrip");
        auto invalid=evidence.getChildFile("invalid.hempreset");invalid.replaceWithText("invalid state");juce::MemoryBlock before,after;processor->getStateInformation(before);require(!topbar.loadFrom(invalid),"reject invalid preset");processor->getStateInformation(after);require(before==after,"failed load leaves state unchanged");
        auto other=hem::PluginRegistry::create(ids[size_t((module+1)%4)]);juce::MemoryBlock foreign;other->getStateInformation(foreign);invalid.replaceWithData(foreign.getData(),foreign.getSize());require(!topbar.loadFrom(invalid),"reject other module state");
        int closed=0;topbar.setCloseAction([&]{++closed;});topbar.requestClose();require(closed==0,"navigation queued, not inline");pumpBandMessages(10);require(closed==1,"navigation delivered");processor->getStateInformation(after);require(before==after,"navigation does not mutate audio state");
        topbar.requestClose();editor.reset();pumpBandMessages(10);require(closed==1,"destroyed editor cannot deliver queued navigation");
        editor.reset(processor->createEditor());owner=dynamic_cast<hem::OriginalBandTopbarOwner*>(editor.get());owner->bandTopbar().setCloseAction([&]{++closed;});pumpBandMessages(10);require(closed==1,"replacement cannot receive old navigation");
    }
    {
        auto bank=std::make_unique<hem::TrackBank>();int closed=0;
        hem::NativeWorkspace::Callbacks callbacks;callbacks.bank=[&]()->hem::TrackBank&{return *bank;};callbacks.getSelectedTrack=[] {return 0;};callbacks.getSelectedSlot=[] {return 0;};callbacks.onEditorBack=[&]{++closed;};
        hem::NativeWorkspace workspace(std::move(callbacks));workspace.setSize(1280,720);auto processor=hem::PluginRegistry::create(ids[0]);
        auto install=[&]{auto editor=std::unique_ptr<juce::AudioProcessorEditor>(processor->createEditor());auto* owner=dynamic_cast<hem::OriginalBandTopbarOwner*>(editor.get());workspace.setEditor(std::move(editor));return owner;};
        auto* first=install();first->bandTopbar().requestClose();first->bandTopbar().requestClose();pumpBandMessages(10);require(closed==1&&!workspace.hasEditor(),"repeated close removes original editor once");
        first=install();first->bandTopbar().requestClose();install();pumpBandMessages(10);require(closed==1&&workspace.hasEditor(),"queued close cannot close newer host editor");
        first=install();first->bandTopbar().requestClose();workspace.clearEditor();pumpBandMessages(10);require(closed==1&&!workspace.hasEditor(),"interrupted host close is discarded");
    }
    const auto art=juce::ImageCache::getFromMemory(ApprovedArt::approved_03_png,ApprovedArt::approved_03_pngSize);
    juce::Image montage(juce::Image::ARGB,768,768,true);juce::Graphics g(montage);
    const juce::Rectangle<int> crops[]{{24,521,64,64},{87,521,64,64},{375,765,90,84},{1475,738,88,82}};
    for(int i=0;i<4;++i){auto raw=art.getClippedImage(crops[i]);auto clean=hem::ui::stationaryReferenceDial(raw);
        int changed=0;for(int y=0;y<raw.getHeight();++y)for(int x=0;x<raw.getWidth();++x)if(raw.getPixelAt(x,y)!=clean.getPixelAt(x,y)){++changed;const float radius=std::hypot(x-raw.getWidth()*.5f,y-raw.getHeight()*.5f);require(radius<std::min(raw.getWidth(),raw.getHeight())*.46f,"pointer repair preserves original outer ring");}
        require(changed>0,"actual approved baked pointer found");
        g.drawImage(raw,juce::Rectangle<float>(0,float(i*192),192,192),juce::RectanglePlacement::stretchToFit);g.drawImage(clean,juce::Rectangle<float>(192,float(i*192),192,192),juce::RectanglePlacement::stretchToFit);
    }
    juce::FileOutputStream stream(evidence.getChildFile("pointer-before-after.png"));require(juce::PNGImageFormat().writeImageToStream(montage,stream),"pointer evidence");
    std::cout<<"PASS Band4 topbar: default size, bounds, native-state preset roundtrip/rejection, deferred close, interrupted close and replacement safety\n";
}

