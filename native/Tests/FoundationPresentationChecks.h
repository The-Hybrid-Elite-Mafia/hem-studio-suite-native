#pragma once
#include "../Source/UI/NativeWorkspace.h"
#include "../Source/Systems/VocalTrackingPanel.h"

namespace presentationChecks {
inline void require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
inline juce::Component* find(juce::Component& c,const char* id){if(c.getComponentID()==id)return &c;for(auto* child:c.getChildren())if(auto* result=find(*child,id))return result;return nullptr;}
inline juce::Slider& slider(juce::Component& c,const char* id){auto* result=dynamic_cast<juce::Slider*>(find(c,id));require(result,"Mapped slider absent");return *result;}
inline juce::Button& button(juce::Component& c,const char* id){auto* result=dynamic_cast<juce::Button*>(find(c,id));require(result,"Mapped button absent");return *result;}
inline void pointerClick(juce::Button& control){
    require(control.isEnabled()&&control.getWidth()>0&&control.getHeight()>0,"Pointer target unavailable");
    const auto now=juce::Time::getCurrentTime();const auto point=control.getLocalBounds().getCentre().toFloat();
    auto event=[&](int modifiers){return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(),point,juce::ModifierKeys(modifiers),1,0,0,0,0,&control,&control,now,point,now,1,false);};
    auto& component=static_cast<juce::Component&>(control);component.mouseDown(event(juce::ModifierKeys::leftButtonModifier));component.mouseUp(event(0));
}
inline void set(hem::ModuleProcessor& p,const char* id,float value){auto* parameter=p.state.getParameter(id);parameter->setValueNotifyingHost(parameter->convertTo0to1(value));}
struct Gestures : juce::AudioProcessorParameter::Listener {
    int begins=0,ends=0,values=0;
    void parameterValueChanged(int,float) override {++values;}
    void parameterGestureChanged(int,bool begin) override {begin?++begins:++ends;}
};
inline void mappings(){
    using namespace hem::ui::foundationMapping;
    for(const char* id:{"vocal-rider","plosive-control"}){
        auto p=hem::PluginRegistry::create(id);std::unique_ptr<juce::AudioProcessorEditor> editor(p->createEditor());
        const bool rider=juce::String(id)=="vocal-rider";const char* raw=rider?"floor":"threshold";
        auto& mapped=slider(*editor,rider?"riderSensitivity":"plosiveSensitivity");auto* parameter=p->state.getParameter(raw);Gestures events;parameter->addListener(&events);
        for(double value:{0.,25.,100.}){mapped.setValue(value,juce::sendNotificationSync);const float expected=parameter->getNormalisableRange().snapToLegalValue(float(rider?riderFloor(value):plosiveThreshold(value)));
            require(std::abs(p->state.getRawParameterValue(raw)->load()-expected)<1e-4,"Mapped sensitivity does not reach original parameter");}
        require(events.begins==events.ends&&events.begins>=2&&events.values>=2,"One-shot mapped edits lack balanced gestures");
        const int before=events.begins;mapped.onDragStart();mapped.setValue(40,juce::sendNotificationSync);mapped.setValue(45,juce::sendNotificationSync);mapped.onDragEnd();
        require(events.begins==before+1&&events.ends==events.begins,"Continuous sensitivity gesture split or unbalanced");
        set(*p,raw,rider?-60.f:-24.f);require(std::abs(mapped.getValue()-(rider?riderSensitivity(-60):plosiveSensitivity(-24)))<1e-3,"Raw automation fails to refresh sensitivity");
        mapped.setValue(50,juce::sendNotificationSync);
        const double beforeKey=mapped.getValue();require(mapped.keyPressed(juce::KeyPress(juce::KeyPress::rightKey))&&mapped.getValue()>beforeKey,"Mapped keyboard edit failed");
        const auto now=juce::Time::getCurrentTime();const auto origin=mapped.getLocalBounds().getCentre().toFloat();
        auto event=[&](juce::Point<float> point,int modifiers,bool dragged){return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(),point,juce::ModifierKeys(modifiers),1,0,0,0,0,&mapped,&mapped,now,origin,now,1,dragged);};
        const double beforeDrag=mapped.getValue();mapped.mouseDown(event(origin,juce::ModifierKeys::leftButtonModifier,false));mapped.mouseDrag(event(origin+juce::Point<float>(0,-35),juce::ModifierKeys::leftButtonModifier,true));mapped.mouseUp(event(origin,0,true));require(mapped.getValue()>beforeDrag,"Mapped pointer edit failed");
        mapped.setValue(50,juce::sendNotificationSync);juce::MouseWheelDetails wheel;wheel.deltaY=.2f;wheel.deltaX=0;wheel.isReversed=false;wheel.isSmooth=false;wheel.isInertial=false;
        mapped.mouseWheelMove(event(origin,0,false),wheel);require(mapped.getValue()>50,"Mapped wheel edit failed");
        mapped.mouseDoubleClick(event(origin,0,false));require(std::abs(p->state.getRawParameterValue(raw)->load()-(rider?-60.f:-24.f))<1e-4,"Mapped double-click reset changed physical default");
        mapped.setValue(mapped.getValueFromText("25 %"),juce::sendNotificationSync);require(std::abs(p->state.getRawParameterValue(raw)->load()-(rider?-35.f:-19.5f))<.051,"Mapped text-entry conversion");
        require(events.begins==events.ends,"Input route left an open parameter gesture");
        parameter->removeListener(&events);
        if(rider){auto* status=dynamic_cast<juce::Label*>(find(*editor,"rider-live-readonly"));require(status&&status->getText().contains("read-only")&&status->getName().contains("read-only"),"Read-only ride indication is not visible/accessibly labeled");
            auto& slow=button(*editor,"rider-speed-slow");auto& fast=button(*editor,"rider-speed-fast");
            require(!slow.getToggleState()&&!fast.getToggleState(),"Default 400/150 should be Custom");
            Gestures riseEvents,fallEvents;auto* rise=p->state.getParameter("rise");auto* fall=p->state.getParameter("fall");rise->addListener(&riseEvents);fall->addListener(&fallEvents);
            slow.onClick();require(p->state.getRawParameterValue("rise")->load()==800&&p->state.getRawParameterValue("fall")->load()==300&&slow.getToggleState()&&!fast.getToggleState(),"Slow preset mapping");
            fast.onClick();require(p->state.getRawParameterValue("rise")->load()==200&&p->state.getRawParameterValue("fall")->load()==75&&fast.getToggleState()&&!slow.getToggleState(),"Fast preset mapping");
            require(riseEvents.begins==2&&riseEvents.ends==2&&fallEvents.begins==2&&fallEvents.ends==2,"Speed preset gestures");
            set(*p,"rise",450);dynamic_cast<hem::ModuleEditor&>(*editor).refreshParameterActivity();require(!slow.getToggleState()&&!fast.getToggleState(),"Custom automation overwritten/classified as preset");
            juce::MemoryBlock state;p->getStateInformation(state);auto restored=hem::PluginRegistry::create(id);restored->setStateInformation(state.getData(),int(state.getSize()));
            require(restored->state.getRawParameterValue("rise")->load()==450&&restored->state.getRawParameterValue("fall")->load()==75,"Speed saved-state compatibility");
            rise->removeListener(&riseEvents);fall->removeListener(&fallEvents);
        }
    }
    auto p=hem::PluginRegistry::create("hum-buzz");p->prepareToPlay(48000,256);std::unique_ptr<juce::AudioProcessorEditor> editor(p->createEditor());auto& width=slider(*editor,"notchWidthHz");
    const float initialQ=p->state.getRawParameterValue("q")->load();Gestures frequencyRefresh;auto* watchedQ=p->state.getParameter("q");watchedQ->addListener(&frequencyRefresh);
    require(std::abs(width.getValue()-notchWidth(50,48000,initialQ))<1e-8,"Initial exact notch width");
    set(*p,"fundamental",60);dynamic_cast<hem::ModuleEditor&>(*editor).refreshParameterActivity();require(p->state.getRawParameterValue("q")->load()==initialQ&&std::abs(width.getValue()-notchWidth(60,48000,initialQ))<1e-8,"Frequency refresh modifies Q or leaves stale width");
    p->prepareToPlay(96000,256);dynamic_cast<hem::ModuleEditor&>(*editor).refreshParameterActivity();require(p->state.getRawParameterValue("q")->load()==initialQ&&std::abs(width.getValue()-notchWidth(60,96000,initialQ))<1e-8,"Rate refresh modifies Q or leaves stale width");
    require(frequencyRefresh.begins==0&&frequencyRefresh.ends==0&&frequencyRefresh.values==0,"Frequency/rate display emitted Q automation");watchedQ->removeListener(&frequencyRefresh);
    Gestures events;auto* q=p->state.getParameter("q");q->addListener(&events);width.setValue(notchWidth(60,96000,20),juce::sendNotificationSync);
    require(std::abs(p->state.getRawParameterValue("q")->load()-20)<1e-4&&events.begins==1&&events.ends==1,"Notch width inverse/gesture");q->removeListener(&events);
    for(double requested:{10.,17.345,30.045,59.999,60.}){width.setValue(notchWidth(60,96000,requested),juce::sendNotificationSync);const double stored=p->state.getRawParameterValue("q")->load();require(std::abs(stored-requested)<=.05001,"Width edit exceeds half-Q-step snapping error");require(std::abs(width.getValue()-notchWidth(60,96000,stored))<1e-7,"Width readout not synchronized to snapped Q");}
    width.setValue(notchWidth(60,96000,20),juce::sendNotificationSync);
    juce::MemoryBlock state;p->getStateInformation(state);auto restored=hem::PluginRegistry::create("hum-buzz");restored->setStateInformation(state.getData(),int(state.getSize()));require(restored->state.getRawParameterValue("q")->load()==20&&restored->state.getRawParameterValue("fundamental")->load()==60,"Width saves derived parameter instead of Q");
}
inline void bypassLifetime(){
    auto bank=std::make_unique<hem::TrackBank>();int selectedTrack=0,selectedSlot=2;
    auto& original=bank->tracks[0].chain;auto& other=bank->tracks[1].chain;original.prepare(48000,256);other.prepare(48000,256);
    original.installInitial(2,original.stageNode(hem::PluginRegistry::create("smart-gate"),48000,256,original.slotLatency()),true);
    other.installInitial(3,other.stageNode(hem::PluginRegistry::create("plosive-control"),48000,256,other.slotLatency()),false);
    auto* processor=original.activeProcessor(2);hem::NativeWorkspace::Callbacks callbacks;
    callbacks.bank=[&]() -> hem::TrackBank& {return *bank;};callbacks.getSelectedTrack=[&]{return selectedTrack;};callbacks.getSelectedSlot=[&]{return selectedSlot;};
      auto workspace=std::make_unique<hem::NativeWorkspace>(std::move(callbacks));
      workspace->setSize(1280,758); // Exercise editable host geometry, not the deliberately disabled zero-size preview.
    auto editor=std::unique_ptr<juce::AudioProcessorEditor>(processor->createEditor());auto* module=dynamic_cast<hem::ModuleEditor*>(editor.get());
    require(!module->hostBypassState().has_value(),"Standalone editor fabricates host bypass state");
    workspace->setEditor(std::move(editor));auto binding=module->hostBypassBinding();require(binding.read().value_or(false),"Original host bypass not reflected");
    auto& control=button(*module,"foundation-host-bypass");require(control.isEnabled()&&control.getToggleState(),"Actual faceplate bypass not bound");
    selectedTrack=1;selectedSlot=3;pointerClick(control);
    require(!original.isBypassed(2)&&!other.isBypassed(3),"Selection redirects bypass to wrong slot");
    original.bypass(2,true);module->refreshParameterActivity();require(control.getToggleState(),"External host bypass not reflected in faceplate");
    workspace->clearEditor();require(!binding.read().has_value(),"Retired editor callback remains live");binding.write(false);require(original.isBypassed(2),"Retired callback mutates chain");
    selectedTrack=0;selectedSlot=2;workspace->setEditor(std::unique_ptr<juce::AudioProcessorEditor>(processor->createEditor()));
    auto* fresh=dynamic_cast<hem::ModuleEditor*>(find(*workspace,"foundation-host-bypass")->findParentComponentOfClass<hem::ModuleEditor>());require(fresh,"Host editor lookup");auto current=fresh->hostBypassBinding();
    auto replacement=original.stageNode(hem::PluginRegistry::create("dc-subsonic"),48000,256,original.slotLatency());require(original.publish(2,std::move(replacement)),"Stage transition failed");
    require(!current.read().has_value(),"Busy slot allows stale bypass");current.write(false);require(original.isBypassed(2),"Busy callback mutates bypass");
    workspace->clearEditor();original.completePendingStopped();require(!current.read().has_value(),"Replaced processor callback remains live");
    workspace->setEditor(std::unique_ptr<juce::AudioProcessorEditor>(original.activeProcessor(2)->createEditor()));auto* finalEditor=find(*workspace,"foundation-host-bypass")->findParentComponentOfClass<hem::ModuleEditor>();auto afterDestruction=finalEditor->hostBypassBinding();workspace.reset();
    require(!afterDestruction.read().has_value(),"Destroyed workspace callback remains live");afterDestruction.write(false);
    auto standalone=std::unique_ptr<juce::AudioProcessorEditor>(other.activeProcessor(3)->createEditor());require(!button(*standalone,"foundation-host-bypass").isEnabled(),"Unbound standalone bypass is falsely actionable");
}
inline void trackingBypassLifetime(){
    struct SilentSource final:juce::AudioSource {void prepareToPlay(int,double)override{} void releaseResources()override{} void getNextAudioBlock(const juce::AudioSourceChannelInfo& info)override{info.clearActiveBufferRegion();}} source;
    hem::NativeAudioDeviceHost device(source,false);auto ownedManager=std::make_unique<hem::LiveVocalManager>();auto& manager=*ownedManager;
    manager.prepare(48000,256,2);
    manager.rack.exchange(1,manager.rack.makeNode("vocal-rider",48000,256));
    auto panel=std::make_unique<hem::VocalTrackingPanel>(manager,device,[]{},[]{return 0;},[]{return 0;},[]{return juce::String();},[](const std::function<void()>& fn){fn();});
    auto editor=std::unique_ptr<juce::AudioProcessorEditor>(manager.rack.processor(1)->createEditor());auto& module=dynamic_cast<hem::ModuleEditor&>(*editor);
    panel->attachHostBypass(module,1);auto binding=module.hostBypassBinding();require(binding.read().has_value()&&!binding.read().value(),"Tracking host binding missing");
    auto& control=button(module,"foundation-host-bypass");pointerClick(control);
    {require(manager.rack.bypassed(1),"Tracking faceplate bypass did not reach rack");manager.rack.bypass(1,false);}
    module.refreshParameterActivity();require(!control.getToggleState(),"Tracking rack bypass did not reach faceplate");
    {manager.prepare(96000,256,2);}
    require(!binding.read().has_value(),"Tracking preparation generation leaves stale binding active");binding.write(true);require(!manager.rack.bypassed(1),"Stale generation mutates tracking rack");
    panel->attachHostBypass(module,1);auto fresh=module.hostBypassBinding();auto next=manager.rack.makeNode("hum-buzz",96000,256);
    std::unique_ptr<hem::TrackingInsertChain::Node> retired;
    {retired=manager.rack.exchange(1,std::move(next));}
    require(!fresh.read().has_value(),"Retired tracking processor still bypasses replacement");fresh.write(true);require(!manager.rack.bypassed(1),"Stale callback bypasses replacement");
    editor.reset();retired.reset();require(!fresh.read().has_value(),"Destroyed tracking editor leaves binding active");
    editor.reset(manager.rack.processor(1)->createEditor());auto& current=dynamic_cast<hem::ModuleEditor&>(*editor);panel->attachHostBypass(current,1);auto afterPanel=current.hostBypassBinding();panel.reset();
    require(!afterPanel.read().has_value(),"Destroyed tracking panel leaves binding active");afterPanel.write(true);require(!manager.rack.bypassed(1),"Destroyed panel callback mutates rack");
    require(device.getCurrentAudioDevice()==nullptr,"UI-only bypass test opened a hardware device");
}

}
