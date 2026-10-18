#include "../Source/Plugins/PluginRegistry.h"
#include "../Source/Plugins/FoundationEditors.h"
#include <iostream>
#include <stdexcept>
#include "FoundationPresentationChecks.h"
#include "FoundationApprovedHostChecks.h"
namespace {
void require(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
juce::Component* find(juce::Component& c,const juce::String& id){if(c.getComponentID()==id)return &c;for(auto* child:c.getChildren())if(auto* p=find(*child,id))return p;return nullptr;}
juce::Image render(juce::Component& c,int scale){juce::Image result(juce::Image::ARGB,c.getWidth()*scale,c.getHeight()*scale,true);juce::Graphics g(result);g.addTransform(juce::AffineTransform::scale(float(scale)));c.paintEntireComponent(g,true);return result;}
void save(juce::Image image,juce::File dir,const juce::String& name){if(dir==juce::File())return;auto file=dir.getChildFile(name+".png");auto out=file.createOutputStream();if(out){out->setPosition(0);out->truncate();}require(out&&juce::PNGImageFormat().writeImageToStream(image,*out),"Export failed");}
void changedControlPixels(juce::Component& component,const char* module){
    juce::Image before,after;
    if(auto* slider=dynamic_cast<juce::Slider*>(&component)){const auto saved=slider->getValue();slider->setValue(slider->getMinimum(),juce::sendNotificationSync);before=render(component,1);slider->setValue(slider->getMaximum(),juce::sendNotificationSync);after=render(component,1);slider->setValue(saved,juce::sendNotificationSync);}
    else if(auto* button=dynamic_cast<juce::ToggleButton*>(&component)){const auto saved=button->getToggleState();button->setToggleState(false,juce::sendNotificationSync);before=render(component,1);presentationChecks::pointerClick(*button);after=render(component,1);button->setToggleState(saved,juce::sendNotificationSync);}
    else return;
    int changed=0;for(int y=0;y<before.getHeight();++y)for(int x=0;x<before.getWidth();++x)changed+=before.getPixelAt(x,y)!=after.getPixelAt(x,y);
    require(changed>5,"Live control/readout did not visibly respond");std::cout<<"UI PIXELS "<<module<<"/"<<component.getComponentID()<<" changed="<<changed<<'\n';
}
void fixedMaterial(juce::Component& component){
    auto* slider=dynamic_cast<juce::Slider*>(&component);if(!slider||!slider->getProperties().contains("bodyRadius"))return;
    const float r=float(slider->getProperties()["bodyRadius"]);if(r<1)return;
    const float cx=float(slider->getProperties()["bodyX"]),cy=float(slider->getProperties()["bodyY"]);
    const auto old=slider->getValue();slider->setValue(slider->getMinimum(),juce::sendNotificationSync);const auto a=render(*slider,1);
    slider->setValue(slider->getMaximum(),juce::sendNotificationSync);const auto b=render(*slider,1);slider->setValue(old,juce::sendNotificationSync);
    int total=0,changed=0,staticFace=0,staticChanged=0,needleChanged=0;
    const auto parameters=slider->getRotaryParameters();
    auto distance=[&](float x,float y,float angle){const float ax=cx+std::sin(angle)*r*.35f,ay=cy-std::cos(angle)*r*.35f;
        const float bx=cx+std::sin(angle)*r*.70f,by=cy-std::cos(angle)*r*.70f;const float dx=bx-ax,dy=by-ay;
        const float t=std::clamp(((x-ax)*dx+(y-ay)*dy)/(dx*dx+dy*dy),0.f,1.f);return std::hypot(x-ax-t*dx,y-ay-t*dy);};
    for(int y=0;y<a.getHeight();++y)for(int x=0;x<a.getWidth();++x){const float radius=std::hypot(x-cx,y-cy);if(radius<r*.98f){++total;changed+=a.getPixelAt(x,y)!=b.getPixelAt(x,y);}
        if(radius<r*.70f){const bool needle=std::min(distance(float(x),float(y),parameters.startAngleRadians),distance(float(x),float(y),parameters.endAngleRadians))<r*.10f+2;
            if(needle)needleChanged+=a.getPixelAt(x,y)!=b.getPixelAt(x,y);else{++staticFace;const auto ca=a.getPixelAt(x,y),cb=b.getPixelAt(x,y);
                const int delta=std::max({std::abs(int(ca.getRed())-int(cb.getRed())),std::abs(int(ca.getGreen())-int(cb.getGreen())),std::abs(int(ca.getBlue())-int(cb.getBlue())),std::abs(int(ca.getAlpha())-int(cb.getAlpha()))});
                staticChanged+=delta>1;}}}
    // The value arc intentionally changes a large fraction of the rim. Test fixed
    // metal face pixels away from both live needle positions instead of limiting
    // all changed pixels to the old baked-pointer renderer's 13 percent budget.
    require(changed>2&&needleChanged>2,"Live value/needle did not move");
    if(staticFace<=total/5||staticChanged!=0)std::cerr<<"FIXED FACE DIAGNOSTIC id="<<component.getComponentID()<<" total="<<total<<" static="<<staticFace<<" changed="<<staticChanged<<" needle="<<needleChanged<<" r="<<r<<" cx="<<cx<<" cy="<<cy<<" start="<<parameters.startAngleRadians<<" end="<<parameters.endAngleRadians<<"\n";
    require(staticFace>total/5&&staticChanged==0,"Metal face lighting moved with parameter (beyond one-channel-level raster rounding)");
    std::cout<<"FIXED MATERIAL "<<component.getComponentID()<<" pixels="<<total<<" markerChanged="<<changed<<'\n';
}
void paired(juce::Image reference,juce::Image off,juce::Image on,juce::File dir,const juce::String& name){
    juce::Image result(juce::Image::RGB,reference.getWidth()*3,reference.getHeight()+35,true);{juce::Graphics g(result);g.fillAll(juce::Colour(0xff101418));
    for(int i=0;i<3;++i){g.setColour(juce::Colours::white);g.setFont(20);g.drawText(i==0?"ORIGINAL REFERENCE":i==1?"LIVE / HOST BYPASS OFF":"LIVE / HOST BYPASS ON",i*reference.getWidth()+12,3,reference.getWidth()-24,29,juce::Justification::centredLeft);g.drawImageAt(i==0?reference:i==1?off:on,i*reference.getWidth(),35);}
    } // Flush the Windows graphics context before reading/exporting the image.
    require(result.getPixelAt(10,45)==reference.getPixelAt(10,10),"Reference comparison image did not finish rendering");
    require(result.getPixelAt(reference.getWidth()+10,45)==off.getPixelAt(10,10),"Live comparison image did not finish rendering");
    save(result,dir,name+"-original-live-pair");
}
void hostVisuals(juce::File dir,const char* id,const juce::Image& reference){
    struct SilentSource final:juce::AudioSource {void prepareToPlay(int,double)override{} void releaseResources()override{} void getNextAudioBlock(const juce::AudioSourceChannelInfo& info)override{info.clearActiveBufferRegion();}} source;
    hem::NativeAudioDeviceHost device(source,false);auto manager=std::make_unique<hem::LiveVocalManager>();manager->prepare(48000,512,2);
    manager->rack.exchange(0,manager->rack.makeNode(id,48000,512));auto* p=manager->rack.processor(0);
    auto panel=std::make_unique<hem::VocalTrackingPanel>(*manager,device,[]{},[]{return 0;},[]{return 0;},[]{return juce::String();},[](const std::function<void()>& fn){fn();});
    std::unique_ptr<juce::AudioProcessorEditor> editor(p->createEditor());auto& module=dynamic_cast<hem::ModuleEditor&>(*editor);panel->attachHostBypass(module,0);editor->setSize(1672,941);
    juce::AudioBuffer<float> audio(2,512);juce::MidiBuffer midi;
    for(int block=0;block<210;++block){for(int i=0;i<512;++i){const float t=float((block*512+i)/48000.);audio.setSample(0,i,.1f*std::sin(2*juce::MathConstants<float>::pi*50*t));audio.setSample(1,i,.08f*std::sin(2*juce::MathConstants<float>::pi*100*t));}p->processBlock(audio,midi);if(block==0)render(*editor,1);}
    if(juce::String(id)=="hum-buzz"){hem::HumAnalysisFrame frame;auto* source=dynamic_cast<hem::HumAnalysisSource*>(p);for(int i=0;i<250&&!source->readHumAnalysis(frame);++i)juce::Thread::sleep(4);require(source->readHumAnalysis(frame)&&frame.detected,"Host Hum evidence requires measured detected signal");}
    module.refreshParameterActivity();auto& bypass=presentationChecks::button(module,"foundation-host-bypass");require(bypass.isEnabled()&&!manager->rack.bypassed(0),"Host visual bypass binding unavailable");
    const auto off=render(*editor,1);presentationChecks::pointerClick(bypass);require(manager->rack.bypassed(0)&&bypass.getToggleState(),"Host visual pointer click did not bypass rack");const auto on=render(*editor,1);
    save(off,dir,juce::String(id)+"-host-off");save(on,dir,juce::String(id)+"-host-on");paired(reference,off,on,dir,id);
    juce::Rectangle<int> detail;
    if(juce::String(id)=="dc-subsonic")detail={430,598,817,284};
    else if(juce::String(id)=="smart-gate")detail={221,643,850,177};
    else if(juce::String(id)=="vocal-rider")detail={725,493,870,352};
    else if(juce::String(id)=="plosive-control")detail={92,546,743,338};
    else detail={708,675,686,197};
    paired(reference.getClippedImage(detail),off.getClippedImage(detail),on.getClippedImage(detail),dir,juce::String(id)+"-detail");
    if(juce::String(id)=="hum-buzz")paired(reference.getClippedImage({140,533,62,58}),off.getClippedImage({140,533,62,58}),on.getClippedImage({140,533,62,58}),dir,"hum-lamp-detail");
    const auto bounds=bypass.getBounds();int changed=0;for(int y=bounds.getY();y<bounds.getBottom();++y)for(int x=bounds.getX();x<bounds.getRight();++x)changed+=off.getPixelAt(x,y)!=on.getPixelAt(x,y);require(changed>20,"Host bypass has no visible state indication");
    std::cout<<"HOST VISUAL "<<id<<" bypassChanged="<<changed<<'\n';require(device.getCurrentAudioDevice()==nullptr,"Visual evidence opened hardware");
}

}
int main(int argc,char** argv){juce::ScopedJuceInitialiser_GUI gui;try{
    approvedHost::run();
    presentationChecks::mappings();presentationChecks::bypassLifetime();presentationChecks::trackingBypassLifetime();
    std::cout<<"PASS mapped sensitivity routes/presets/state, exact width binding, main/tracking bypass lifetime and read-only accessibility\n";
    juce::File dir;if(argc>1){dir=juce::File(argv[1]);require(dir.createDirectory().wasOk(),"Cannot create review output");}
    struct Case {const char* id;const char* extra;const char* bytes;int size;juce::Rectangle<int> ghost,plot;};
    const Case cases[]={
        {"dc-subsonic","mix",ApprovedArt::approved_11_png,ApprovedArt::approved_11_pngSize,{60,598,270,250},{360,265,940,195}},
        {"smart-gate","mix",ApprovedArt::approved_08_png,ApprovedArt::approved_08_pngSize,{65,611,110,210},{360,215,850,200}},
        {"vocal-rider","rise",ApprovedArt::approved_05_png,ApprovedArt::approved_05_pngSize,{80,800,180,100},{380,250,820,130}},
        {"plosive-control","contrast",ApprovedArt::approved_07_png,ApprovedArt::approved_07_pngSize,{60,805,220,95},{320,225,900,180}},
        {"hum-buzz","mix",ApprovedArt::approved_10_png,ApprovedArt::approved_10_pngSize,{65,641,175,200},{400,280,860,205}}
    };
    for(const auto& item:cases){auto p=hem::PluginRegistry::create(item.id);std::unique_ptr<juce::AudioProcessorEditor> editor(p->createEditor());
        require(dynamic_cast<hem::FoundationEditor*>(editor.get()),"Not a foundation-specific editor");
        editor->setSize(1672,941);const auto reference=juce::ImageCache::getFromMemory(item.bytes,item.size),idle=render(*editor,1);
        // Exact authored pixels, not a reference-lookalike test or visual guess.
        for(const auto region:{juce::Rectangle<int>(490,25,650,38),item.ghost})for(int y=region.getY();y<region.getBottom();y+=3)for(int x=region.getX();x<region.getRight();x+=3)
            require(idle.getPixelAt(x,y)==reference.getPixelAt(x,y),"Original crown/wordmark/ghost pixels changed");
        if(juce::String(item.id)=="dc-subsonic"){presentationChecks::set(*p,"subOn",1);dynamic_cast<hem::ModuleEditor*>(editor.get())->refreshParameterActivity();}
        for(const auto& spec:p->parameterSpecs())if(auto* c=find(*editor,spec.id);c&&c->isVisible()){changedControlPixels(*c,item.id);fixedMaterial(*c);}
        for(const char* id:{"riderSensitivity","plosiveSensitivity","notchWidthHz"})if(auto* c=find(*editor,id)){changedControlPixels(*c,item.id);fixedMaterial(*c);}
        if(juce::String(item.id)=="dc-subsonic")presentationChecks::set(*p,"subOn",0);
        dynamic_cast<hem::ModuleEditor*>(editor.get())->refreshParameterActivity();
        auto* extra=find(*editor,item.extra);require(extra&&!extra->isVisible(),"Advanced parameter not retained/hidden initially");
        auto* advanced=dynamic_cast<juce::Button*>(find(*editor,"foundation-advanced"));require(advanced,"Advanced action missing");advanced->setToggleState(true,juce::dontSendNotification);advanced->onClick();require(extra->isVisible(),"Advanced parameter cannot be accessed");
        for(const auto& spec:p->parameterSpecs()){auto* c=find(*editor,spec.id);require(c&&c->isVisible(),"Existing state parameter has no accessible UI");}
        save(render(*editor,1),dir,juce::String(item.id)+"-advanced");advanced->setToggleState(false,juce::dontSendNotification);advanced->onClick();
        for(const auto size:{juce::Point<int>(1000,563),juce::Point<int>(1280,720),juce::Point<int>(1920,1080)}){editor->setSize(size.x,size.y);for(const auto& spec:p->parameterSpecs()){auto* c=find(*editor,spec.id);require(c&&editor->getLocalBounds().contains(c->getBounds()),"Parameter geometry outside editor");}}
        editor->setSize(1672,941);p->prepareToPlay(48000,512);juce::AudioBuffer<float> audio(2,512);juce::MidiBuffer midi;
        for(int block=0;block<200;++block){for(int i=0;i<512;++i){audio.setSample(0,i,.25f+.1f*std::sin(float((block*512+i)*.07)));audio.setSample(1,i,-.125f);}p->processBlock(audio,midi);if(block==0)render(*editor,1);}
        if(juce::String(item.id)=="hum-buzz"){hem::HumAnalysisFrame frame;auto* analysis=dynamic_cast<hem::HumAnalysisSource*>(p.get());for(int i=0;i<250&&!analysis->readHumAnalysis(frame);++i)juce::Thread::sleep(4);require(analysis->readHumAnalysis(frame),"Measured Hum display not published");}
        const auto active=render(*editor,1);int changed=0;
        for(int y=item.plot.getY();y<item.plot.getBottom();++y)for(int x=item.plot.getX();x<item.plot.getRight();++x)if(active.getPixelAt(x,y)!=idle.getPixelAt(x,y))++changed;
        require(changed>30,"Real audio failed to change measured display pixels");
        for(int scale:{1,2})save(render(*editor,scale),dir,juce::String(item.id)+"-active-"+juce::String(scale)+"x");
        hostVisuals(dir,item.id,reference);
        std::cout<<item.id<<" original branding pixels exact; advanced accessible; actual history changed pixels="<<changed<<'\n';
    }
    std::cout<<"PASS five original-art editors, immutable branding, retained parameter access, bounded geometry and real-audio display behavior\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
