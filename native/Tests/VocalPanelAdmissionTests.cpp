#include "../Source/Systems/VocalTrackingPanel.h"
#include <iostream>
#include <stdexcept>
namespace hem {
struct VocalTrackingPanelTestAccess {
 static void poll(VocalTrackingPanel& p){p.timerCallback();}
 static void ordinary(VocalTrackingPanel& p){
  p.monitor.setSelectedId(2,juce::dontSendNotification);p.monitor.onChange();
  p.level.setValue(-12,juce::dontSendNotification);p.level.onValueChange();
  p.print.setToggleState(true,juce::dontSendNotification);p.print.onClick();
  p.rows[0].bypass.onClick();
 }
 static void stop(VocalTrackingPanel& p){p.stop.onClick();}
 static void remove(VocalTrackingPanel& p){p.insert(0,true);}
};
}
struct SilentVocalSource final:juce::AudioSource {
 void prepareToPlay(int,double)override{} void releaseResources()override{}
 void getNextAudioBlock(const juce::AudioSourceChannelInfo& i)override{i.clearActiveBufferRegion();}
};
static void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
int main(){juce::ScopedJuceInitialiser_GUI gui;try{
 SilentVocalSource source;hem::NativeAudioDeviceHost device(source,false);
 auto ownedManager=std::make_unique<hem::LiveVocalManager>();auto& manager=*ownedManager;manager.prepare(48000,256,2);
 manager.rack.exchange(0,manager.rack.makeNode("dc-subsonic",48000,256));
 int admissions=0;
 hem::VocalTrackingPanel panel(manager,device,[]{},[]{return 0;},[]{return 0;},[]{return juce::String();},[&](const std::function<void()>& f){++admissions;f();});
 for(int i=0;i<30;++i)hem::VocalTrackingPanelTestAccess::poll(panel);
 require(admissions==0,"Read-only vocal polling closed graph admission");
 hem::VocalTrackingPanelTestAccess::ordinary(panel);
 require(admissions==0,"Atomic vocal controls closed graph admission");
 require(manager.printing()&&manager.rack.bypassed(0)&&manager.monitoring()==hem::LiveVocalManager::Monitor::DrySoftware,"Atomic control change failed");
 hem::VocalTrackingPanelTestAccess::stop(panel);require(admissions==1,"Stop must use graph admission");
 manager.setMonitor(hem::LiveVocalManager::Monitor::Off);
 hem::VocalTrackingPanelTestAccess::remove(panel);require(admissions==2&&!manager.rack.processor(0),"Structural rack exchange must use graph admission");
 require(device.getCurrentAudioDevice()==nullptr,"Test opened a hardware device");
 std::cout<<"PASS vocal polling/atomic controls use zero graph admissions; stop and structural removal gated; no hardware opened\n";return 0;
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
