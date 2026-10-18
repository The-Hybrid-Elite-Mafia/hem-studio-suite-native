#pragma once
#include "BandProcessors.h"
namespace hem {
// An editor owns this component; no callback or asynchronous chooser retains a
// raw editor/processor pointer beyond that component's lifetime.
class OriginalBandTopbar final : public juce::Component {
public:
    enum class Layout { dynamicEq, dynamics, midSide, imager };
    OriginalBandTopbar(BandModuleProcessor& p, Layout l):processor(p),layout(l){
        setInterceptsMouseClicks(false,true);
        for(auto* b:{&presets,&save,&back,&close})b->original=layout==Layout::dynamicEq||layout==Layout::dynamics;
        setup(presets,"uiPresets","PRESETS");setup(save,"uiSavePreset","SAVE");
        setup(back,"uiBack","BACK");setup(close,"uiClose","CLOSE");
        presets.onClick=[this]{showMenu();};save.onClick=[this]{chooseFile(true);};
        back.onClick=close.onClick=[this]{requestClose();};setCloseAction({});
    }
    std::function<void()> reset,advanced,changed;
    void setCloseAction(std::function<void()> action){closeAction=std::move(action);back.setEnabled(bool(closeAction));close.setEnabled(bool(closeAction));}
    void requestClose(){juce::Component::SafePointer<OriginalBandTopbar> safe(this);
        juce::MessageManager::callAsync([safe]{if(safe&&safe->closeAction){auto action=safe->closeAction;action();}});}
    void resized() override {
        auto place=[this](juce::Component& c,juce::Rectangle<float> r){c.setBounds(r.transformedBy(juce::AffineTransform::scale(getWidth()/1672.f,getHeight()/941.f)).toNearestInt());};
        back.setVisible(layout==Layout::dynamics);save.setVisible(layout!=Layout::dynamics);
        if(layout==Layout::dynamicEq){place(presets,{70,50,205,50});place(save,{1331,50,130,50});place(close,{1480,50,120,50});}
        else if(layout==Layout::dynamics){place(presets,{84,65,165,50});place(back,{1326,65,137,50});place(close,{1480,65,119,50});}
        else {const float y=layout==Layout::midSide?51.f:69.f;place(presets,{226,y,164,42});place(save,{1290,y,98,42});place(close,{1391,y,98,42});}
    }
    // The file payload is the processor's existing host-state binary, including
    // its existing schema/version/migration. It is not a parallel preset schema.
    bool saveTo(const juce::File& file){juce::MemoryBlock data,verified;processor.getStateInformation(data);juce::TemporaryFile temporary(file);
        if(!temporary.getFile().replaceWithData(data.getData(),data.getSize())||!temporary.getFile().loadFileAsData(verified)||verified!=data)return false;
        return temporary.overwriteTargetFileWithTemporary();}
    uint64_t beginPresetRequest(){return ++requestGeneration;}
    bool isCurrentPresetRequest(uint64_t request)const{return request==requestGeneration;}
    void cancelPresetRequest(){++requestGeneration;chooser.reset();}
    bool completePresetRequest(uint64_t request,const juce::File& file,bool writing){if(!isCurrentPresetRequest(request))return false;++requestGeneration;if(file==juce::File{})return true;return writing?saveTo(file):loadFrom(file);}
    bool loadFrom(const juce::File& file){if(file.getSize()<=0||file.getSize()>1024*1024)return false;juce::MemoryBlock data;if(!file.loadFileAsData(data))return false;
        auto xml=juce::AudioProcessor::getXmlFromBinary(data.getData(),int(data.getSize()));if(!xml)return false;
        auto tree=juce::ValueTree::fromXml(*xml);if(!tree.hasType(processor.state.state.getType()))return false;
        juce::ValueTree parameters("PARAMETERS");for(int i=0;i<tree.getNumProperties();++i){auto key=tree.getPropertyName(i);parameters.setProperty(key,tree[key],nullptr);}for(auto child:tree)parameters.addChild(child.createCopy(),-1,nullptr);
        juce::ValueTree migrated;juce::String error;if(!processor.migrateParameters(parameters,migrated,error))return false;
        processor.setStateInformation(data.getData(),int(data.getSize()));if(changed)changed();return true;}
private:
    class FaceplateButton final:public juce::TextButton {
    public: bool original=false;
        void paintButton(juce::Graphics& g,bool over,bool down) override {
            if(!original){juce::TextButton::paintButton(g,over,down);return;}
            if(!isEnabled()){g.setColour(juce::Colour(0x77010609));g.fillRoundedRectangle(getLocalBounds().toFloat(),3);}
            if(over||down||hasKeyboardFocus(true)){g.setColour(juce::Colour(down?0xffffa642:0xff44dce9));g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(1),3,1.5f);}
        }
    };
    void setup(juce::TextButton& button,const char* id,const char* caption){button.setComponentID(id);button.setName(caption);button.setButtonText(caption);button.setWantsKeyboardFocus(true);addAndMakeVisible(button);}
    void showMenu(){juce::PopupMenu menu;menu.addItem(1,"Load preset...");menu.addItem(2,"Save preset...");menu.addItem(3,"Reset all parameters",bool(reset));if(advanced)menu.addItem(4,"Advanced / main view");
        juce::Component::SafePointer<OriginalBandTopbar> safe(this);menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&presets),[safe](int result){if(!safe)return;if(result==1||result==2)safe->chooseFile(result==2);else if(result==3&&safe->reset)safe->reset();else if(result==4&&safe->advanced)safe->advanced();});}
    void chooseFile(bool writing){const auto request=beginPresetRequest();chooser=std::make_unique<juce::FileChooser>(writing?"Save module preset":"Load module preset",juce::File{},"*.hempreset");juce::Component::SafePointer<OriginalBandTopbar> safe(this);
        chooser->launchAsync((writing?juce::FileBrowserComponent::saveMode|juce::FileBrowserComponent::warnAboutOverwriting:juce::FileBrowserComponent::openMode)|juce::FileBrowserComponent::canSelectFiles,[safe,writing,request](const juce::FileChooser& fc){if(!safe||!safe->isCurrentPresetRequest(request))return;auto file=fc.getResult();const bool ok=safe->completePresetRequest(request,file,writing);if(!ok)juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,"Preset",writing?"The preset could not be saved.":"The preset could not be loaded.");});}
    BandModuleProcessor& processor;Layout layout;FaceplateButton presets,save,back,close;std::function<void()> closeAction;std::unique_ptr<juce::FileChooser> chooser;uint64_t requestGeneration=0;
};
class OriginalBandTopbarOwner {
public: virtual ~OriginalBandTopbarOwner()=default;virtual OriginalBandTopbar& bandTopbar()=0;
};
}
