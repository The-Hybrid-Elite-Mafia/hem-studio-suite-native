#include "../Source/Plugins/PluginRegistry.h"
#include "../Source/Plugins/BandEditor.h"
#include "../Source/Systems/OfflineMixRenderer.h"
#include "../Source/Systems/PresetManager.h"
#include "../Source/Systems/OfflineMixPanel.h"
#include "../Source/Plugins/OriginalDynamicEqEditor.h"
#include "../Source/Plugins/OriginalMidSideEqEditor.h"
#include "../Source/DSP/MasterLimiter.h"
#include "../Source/CoreMasterStage.h"
#include "FoundationAllocationProbe.h"
#include "FoundationPlatformAllocations.h"
#include <iostream>
#include <stdexcept>
#include <chrono>
#include <thread>
#include <fstream>
#if JUCE_WINDOWS
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
// Test-only native pump: preserve the production JUCE configuration with modal
// loops disabled. Bound elapsed time even when callbacks keep posting messages.
static void pumpBandMessages(int milliseconds){
#if JUCE_WINDOWS
    const auto end=std::chrono::steady_clock::now()+std::chrono::milliseconds(milliseconds);
    do {MSG message{};while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){
        if(message.message==WM_QUIT){PostQuitMessage(int(message.wParam));return;}
        TranslateMessage(&message);DispatchMessageW(&message);
        if(std::chrono::steady_clock::now()>=end)return;
    }std::this_thread::sleep_for(std::chrono::milliseconds(1));}while(std::chrono::steady_clock::now()<end);
#else
    throw std::runtime_error("Band integration message pump requires Windows");
#endif
}
static constexpr std::array<const char*,4> ids{"dynamic-eq-8","mid-side-eq","multiband-dynamics","multiband-imager"};
static void require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
#include "FmaResidualProof.h"
static void set(hem::ModuleProcessor& p,const juce::String& id,float value){auto* v=p.state.getParameter(id);require(v!=nullptr,"missing parameter");v->setValueNotifyingHost(v->convertTo0to1(value));}
static juce::Component* find(juce::Component& c,const juce::String& id){if(c.getComponentID()==id)return &c;for(auto* child:c.getChildren())if(auto* r=find(*child,id))return r;return nullptr;}
#include "OriginalBandReadabilityAudit.h"
static void fill(juce::AudioBuffer<float>& b,int offset,double rate=48000){for(int n=0;n<b.getNumSamples();++n){const double t=(n+offset)/rate;
    b.setSample(0,n,float(.08*std::sin(2*hem::pi*83*t)+.1*std::sin(2*hem::pi*997*t)+.06*std::cos(2*hem::pi*6500*t)));
    b.setSample(1,n,float(.12*std::cos(2*hem::pi*313*t)-.06*std::sin(2*hem::pi*997*t)));}}
static void writeBandWave(const juce::File& file,const juce::AudioBuffer<float>& audio,double rate){
    std::unique_ptr<juce::OutputStream> stream=file.createOutputStream();juce::WavAudioFormat wav;
    auto writer=wav.createWriterFor(stream,juce::AudioFormatWriterOptions().withSampleRate(rate).withNumChannels(2).withBitsPerSample(32).withSampleFormat(juce::AudioFormatWriterOptions::SampleFormat::floatingPoint));
    require(writer&&writer->writeFromAudioSampleBuffer(audio,0,audio.getNumSamples())&&writer->flush(),"Band WAV fixture write failed");
}
static void awaitBandStreams(const hem::TrackBank& bank,int frames){
    const auto deadline=juce::Time::getMillisecondCounterHiRes()+2000;
    while(!std::all_of(bank.tracks.begin(),bank.tracks.end(),[&](const auto& t){return !t.loaded||t.transport().availableRt(frames);})){require(juce::Time::getMillisecondCounterHiRes()<deadline,"Band fixture read-ahead timeout");juce::Thread::sleep(1);}
}
static void activate(hem::ModuleProcessor& p){set(p,"enabled",1);
    for(const auto& spec:p.parameterSpecs()){const juce::String id(spec.id);
        if(id.endsWith("Gain")||id.endsWith("Makeup"))set(p,id,3);
        if(id.endsWith("Range"))set(p,id,6);if(id.endsWith("Threshold"))set(p,id,-30);if(id.endsWith("Ratio"))set(p,id,4);
        if(id.endsWith("Width"))set(p,id,150);if(id.endsWith("DynamicOn"))set(p,id,1);}
}
static void schemas(){int total=0;const int sizes[]{90,55,52,27};
    hem::OfflineMixWorker worker([](const juce::String&){});
    hem::OfflineMixPanel exportPanel(worker,[](const juce::File&,const hem::OfflineMixOptions&){return juce::Result::ok();});
    auto* tail=dynamic_cast<juce::Slider*>(find(exportPanel,"exportTailSeconds"));require(tail!=nullptr,"export tail selector absent");
    for(const auto* id:{ids[0],ids[1]}){hem::SessionSnapshot serial;auto p=hem::PluginRegistry::create(id);set(*p,"enabled",1);
        if(p->state.getParameter("b1Type"))set(*p,"b1Type",1);
        for(auto& slot:serial.tracks[0].slots){slot.pluginId=id;slot.parameters=hem::SessionState::captureParameters(*p);}
        const double required=hem::OfflineMixRenderer::requiredFoundationTail(serial);
        require(required==8*p->getTailLengthSeconds(),"serial conservative tail silently capped");
        tail->setValue(required,juce::sendNotificationSync);require(tail->getValue()==required,"export UI cannot select serial conservative tail");
    }
    for(size_t k=0;k<ids.size();++k){auto p=hem::PluginRegistry::create(ids[k]);require(p!=nullptr&&hem::PluginRegistry::idFor(*p)==ids[k],"registry mapping");
        require(p->parameterSpecs().size()==size_t(sizes[k]),"schema parameter count");total+=sizes[k];
        for(const auto& spec:p->parameterSpecs()){
            const auto actual=p->state.getRawParameterValue(spec.id)->load();
            if(std::abs(actual-spec.initial)>=1.e-4f)std::cerr<<ids[k]<<" startup "<<spec.id<<" actual="<<std::setprecision(10)<<actual<<" declared="<<spec.initial<<'\n';
            require(std::abs(actual-spec.initial)<1.e-4f,"strict startup metadata mismatch");
        }
        require(!p->processingReady()&&p->actualRate.load()==0&&!p->meterSnapshot().hasProcessedAudio,"fabricated initial readiness");
        for(double rate:{0.,7999.,384001.,std::numeric_limits<double>::quiet_NaN(),std::numeric_limits<double>::infinity()}){
            p->prepareToPlay(48000,17);bool rejected=false;try{p->prepareToPlay(rate,17);}catch(const std::invalid_argument&){rejected=true;}
            require(rejected&&!p->processingReady()&&p->actualRate.load()==0&&!p->meterSnapshot().isPrepared,"adapter invalid rate left ready");
            juce::AudioBuffer<float> invalid(2,17);juce::MidiBuffer midi;fill(invalid,0);allocationProbe::enabled=true;p->processBlock(invalid,midi);allocationProbe::enabled=false;
            require(invalid.getMagnitude(0,17)==0&&!p->meterSnapshot().hasProcessedAudio,"failed preparation processed audio");
        }
        for(int point=0;point<4;++point){for(const auto& s:p->parameterSpecs())set(*p,s.id,point==0?s.initial:point==1?s.minimum:point==2?s.maximum:s.minimum+(s.maximum-s.minimum)*.37f);
            juce::MemoryBlock bytes;p->getStateInformation(bytes);auto copy=hem::PluginRegistry::create(ids[k]);copy->setStateInformation(bytes.getData(),int(bytes.getSize()));
            if(!hem::SessionState::captureParameters(*p).isEquivalentTo(hem::SessionState::captureParameters(*copy))){std::cerr<<"roundtrip module="<<ids[k]<<" point="<<point<<'\n';for(const auto& s:p->parameterSpecs()){const auto a=p->state.getRawParameterValue(s.id)->load(),b=copy->state.getRawParameterValue(s.id)->load();if(a!=b)std::cerr<<s.id<<" source="<<a<<" copy="<<b<<'\n';}}
            require(hem::SessionState::captureParameters(*p).isEquivalentTo(hem::SessionState::captureParameters(*copy)),"full direct state round trip");
            auto valid=p->state.copyState();
            for(int bad=0;bad<8;++bad){auto tree=valid.createCopy();
                if(bad==0)tree.removeChild(0,nullptr);if(bad==1)tree.getChild(1).setProperty("id",tree.getChild(0)["id"],nullptr);
                if(bad==2)tree.getChild(0).setProperty("value","nan",nullptr);if(bad==3)tree.getChild(0).setProperty("value","1junk",nullptr);
                if(bad==4)tree.getChild(1).setProperty("value",1e30,nullptr);if(bad==5)tree.getChild(0).setProperty("id","unknown",nullptr);
                if(bad==6)tree.getChildWithProperty("id","enabled").setProperty("value",.5,nullptr);if(bad==7)tree.addChild(tree.getChild(0).createCopy(),-1,nullptr);
                juce::MemoryBlock invalid;auto xml=tree.createXml();juce::AudioProcessor::copyXmlToBinary(*xml,invalid);
                copy->setStateInformation(invalid.getData(),int(invalid.getSize()));
                require(hem::SessionState::captureParameters(*p).isEquivalentTo(hem::SessionState::captureParameters(*copy)),"invalid direct state partially committed");
            }
            const auto parameters=hem::SessionState::captureParameters(*p);
            for(int bad=0;bad<5;++bad){auto tree=parameters.createCopy();if(bad==0)tree.removeChild(0,nullptr);if(bad==1)tree.getChild(1).setProperty("id",tree.getChild(0)["id"],nullptr);
                if(bad==2)tree.getChild(0).setProperty("value",std::numeric_limits<double>::infinity(),nullptr);if(bad==3)tree.getChild(0).setProperty("id","unknown",nullptr);
                if(bad==4)tree.getChild(0).setProperty("value",.5,nullptr);bool rejected=false;try{hem::SessionState::restoreParameters(*copy,tree);}catch(...){rejected=true;}
                require(rejected&&parameters.isEquivalentTo(hem::SessionState::captureParameters(*copy)),"invalid session state partially committed");}
            for(auto* instance:{p.get(),copy.get()})instance->prepareToPlay(48000,17);
            juce::AudioBuffer<float> a(2,257),b(2,257);juce::MidiBuffer midi;
            for(int turn=0;turn<50;++turn){fill(a,turn*257);b.makeCopyOf(a);allocationProbe::enabled=true;p->processBlock(a,midi);copy->processBlock(b,midi);allocationProbe::enabled=false;
                for(int c=0;c<2;++c)for(int i=0;i<257;++i)require(std::abs(a.getSample(c,i)-b.getSample(c,i))<=2e-6,"recalled output differs");}
            require(p->getLatencySamples()==0,"module added latency");
        }
    }require(total==224,"not all 224 parameters covered");
}
template<hem::bands::Kind K> static void legacyAudio(const juce::ValueTree& oldParameters,hem::ModuleProcessor& migrated){
    auto p=hem::bands::defaults<K>();for(size_t i=0;i<p.size();++i)p[i]=float(double(oldParameters.getChildWithProperty("id",hem::bands::specs<K>()[i].id)["value"]));
    hem::bands::Core<K> legacy;legacy.prepare(48000,p);migrated.prepareToPlay(48000,64);juce::AudioBuffer<float> audio(2,64);juce::MidiBuffer midi;std::array<float,64> l{},r{};
    for(int turn=0;turn<8;++turn){fill(audio,turn*64);for(int n=0;n<64;++n){l[size_t(n)]=audio.getSample(0,n);r[size_t(n)]=audio.getSample(1,n);}
        allocationProbe::enabled=true;legacy.process(l.data(),r.data(),64,p);migrated.processBlock(audio,midi);allocationProbe::enabled=false;
        for(int n=0;n<64;++n){require(std::abs(double(l[size_t(n)])-audio.getSample(0,n))<=2e-6+2e-6*std::abs(l[size_t(n)]),"v1 golden left audio");
            require(std::abs(double(r[size_t(n)])-audio.getSample(1,n))<=2e-6+2e-6*std::abs(r[size_t(n)]),"v1 golden right audio");}}
}
static void migrations(){
#ifdef HEM_BAND_FIXTURE_DIR
    const auto fixture=juce::File(HEM_BAND_FIXTURE_DIR);
#else
    const auto fixture=juce::File(__FILE__).getParentDirectory().getChildFile("Fixtures/BandV1");
#endif
    for(const auto* name:{"full-13x8.xml","full-13x8.json"}){const auto snapshot=hem::PresetManager::load(fixture.getChildFile(name));
        for(const auto& track:snapshot.tracks)for(const auto& slot:track.slots){auto p=hem::PluginRegistry::create(slot.pluginId);require(p!=nullptr,"golden v1 registry");
            hem::SessionState::restoreParameters(*p,slot.parameters);const auto now=hem::SessionState::captureParameters(*p);
            require(int(now["bandSchemaVersion"])==2,"v1 migration missing version");
            for(const auto child:slot.parameters){const auto key=child["id"].toString();const auto expected=float(double(child["value"]));const auto actual=p->state.getRawParameterValue(key)->load();
                require(actual==expected,"v1 physical parameter changed");}
            if(slot.pluginId==ids[0]){for(int b=1;b<=8;++b)require(p->state.getRawParameterValue("b"+juce::String(b)+"DynamicOn")->load()==1,"v1 dynamics disabled during migration");legacyAudio<hem::bands::Kind::Dynamic>(slot.parameters,*p);}
            if(slot.pluginId==ids[1]){require(p->state.getRawParameterValue("m5On")->load()==0&&p->state.getRawParameterValue("s5On")->load()==0,"v1 fifth band enabled");legacyAudio<hem::bands::Kind::MidSide>(slot.parameters,*p);}
            if(slot.pluginId==ids[2]){require(p->state.getRawParameterValue("topology")->load()==0,"v1 dynamics topology changed");legacyAudio<hem::bands::Kind::Dynamics>(slot.parameters,*p);}
            if(slot.pluginId==ids[3]){require(p->state.getRawParameterValue("topology")->load()==0,"v1 imager topology changed");legacyAudio<hem::bands::Kind::Imager>(slot.parameters,*p);}
            auto direct=hem::PluginRegistry::create(slot.pluginId);juce::ValueTree oldRoot(direct->state.state.getType());for(const auto child:slot.parameters)oldRoot.addChild(child.createCopy(),-1,nullptr);
            juce::MemoryBlock bytes;juce::AudioProcessor::copyXmlToBinary(*oldRoot.createXml(),bytes);direct->setStateInformation(bytes.getData(),int(bytes.getSize()));
            require(now.isEquivalentTo(hem::SessionState::captureParameters(*direct)),"direct/session migration differs");
            auto invalid=now.createCopy();invalid.removeChild(invalid.getNumChildren()-1,nullptr);bool rejected=false;try{hem::SessionState::restoreParameters(*direct,invalid);}catch(...){rejected=true;}
            require(rejected&&now.isEquivalentTo(hem::SessionState::captureParameters(*direct)),"partial v2 mistaken for legacy");
        }}
    std::cout<<"PASS v1 golden direct/XML/JSON migration across 13x8 slots\n";
}
static void dynamicOriginalInteractions(const juce::File& evidence){
    auto p=hem::PluginRegistry::create(ids[0]);p->prepareToPlay(48000,257);
    std::unique_ptr<juce::AudioProcessorEditor> editor(p->createEditor());auto* original=dynamic_cast<hem::OriginalDynamicEqEditor*>(editor.get());require(original!=nullptr,"original dynamic editor factory");
    auto* page=dynamic_cast<juce::ComboBox*>(find(*editor,"uiBandPage"));require(page!=nullptr,"selected band navigation");
    for(int band=1;band<=8;++band){auto* tab=dynamic_cast<juce::Button*>(find(*editor,"uiBandTab"+juce::String(band)));require(tab&&tab->isVisible(),"visible band tab");tab->triggerClick();pumpBandMessages(1);require(page->getSelectedId()==band,"band tab navigation failed");}
    int checked=0;editor->setSize(1000,563);
    for(const auto& spec:p->parameterSpecs()){
        const juce::String id(spec.id);if(id.startsWith("b")){const auto prefix=id.substring(0,2);page->setSelectedId(id.substring(1,2).getIntValue(),juce::sendNotificationSync);set(*p,prefix+"On",1);set(*p,prefix+"DynamicOn",1);set(*p,prefix+"Range",6);}
        original->refresh();auto* component=find(*editor,id);require(component&&component->isVisible()&&component->isEnabled(),"original control unavailable in active context");require(component->getName()==spec.name,"original accessible identity");
        if(spec.toggle){auto* toggle=dynamic_cast<juce::ToggleButton*>(component);require(toggle!=nullptr,"original toggle type");for(bool on:{false,true}){toggle->setToggleState(on,juce::sendNotificationSync);require(p->state.getRawParameterValue(id)->load()==float(on),"original toggle binding");}}
        else{auto* slider=dynamic_cast<hem::ui::ReferenceSlider*>(component);require(slider!=nullptr,"original physical slider type");
            for(float value:{spec.minimum,spec.maximum,spec.initial}){slider->setValue(value,juce::sendNotificationSync);require(std::abs(p->state.getRawParameterValue(id)->load()-value)<=std::max(1e-4f,spec.step*.51f),"original physical value binding");auditBandControl(*slider);}
            if(!slider->choices.isEmpty()){slider->onChoice(2);require(p->state.getRawParameterValue(id)->load()==2,"type menu selection binding");auditBandControl(*slider);}
            else{slider->showTextBox();juce::TextEditor* text=nullptr;for(auto* child:slider->getChildren())if(auto* label=dynamic_cast<juce::Label*>(child))if(label->getCurrentTextEditor())text=label->getCurrentTextEditor();
                require(text!=nullptr,"original editable value field");text->setText(juce::String(spec.maximum,4));slider->hideTextBox(false);require(std::abs(p->state.getRawParameterValue(id)->load()-spec.maximum)<=std::max(1e-4f,spec.step*.51f),"original typed physical value");}
            slider->setValue(spec.minimum,juce::sendNotificationSync);require(slider->keyPressed(juce::KeyPress(juce::KeyPress::rightKey))&&slider->getValue()>spec.minimum,"original keyboard edit");
        }++checked;
    }
    require(checked==90,"not all original Dynamic controls covered");
    auto* reset=dynamic_cast<juce::Button*>(find(*editor,"uiResetAll"));require(reset!=nullptr,"context reset command");reset->triggerClick();pumpBandMessages(1);
    for(const auto& spec:p->parameterSpecs())require(std::abs(p->state.getRawParameterValue(spec.id)->load()-spec.initial)<1e-4f,"original reset all");
    activate(*p);set(*p,"b1DynamicOn",1);set(*p,"b1Range",6);set(*p,"b1Up",0);
    juce::AudioBuffer<float> signal(2,257);juce::MidiBuffer midi;for(int block=0;block<80;++block){fill(signal,block*257);allocationProbe::enabled=true;p->processBlock(signal,midi);allocationProbe::enabled=false;}
    // A headless button's synthetic click flash is released only after a paint.
    // Render that real first frame, then let its normal timer complete.
    {juce::Image firstFrame(juce::Image::ARGB,editor->getWidth(),editor->getHeight(),true);juce::Graphics graphics(firstFrame);editor->paintEntireComponent(graphics,true);}
    // Let synthetic button click flashes complete before pixel evidence.
    pumpBandMessages(160);
    for(int band=1;band<=8;++band){page->setSelectedId(band,juce::sendNotificationSync);original->refresh();
        for(int tab=1;tab<=8;++tab){auto* button=dynamic_cast<juce::Button*>(find(*editor,"uiBandTab"+juce::String(tab)));require(button&&button->getToggleState()==(tab==band),"selected tab indication");}
        for(auto size:{juce::Point<int>(1000,563),juce::Point<int>(1672,941),juce::Point<int>(1920,1081)})for(int scale:{1,2}){
            if(band!=1&&(size.x!=1672||scale!=1))continue;editor->setSize(size.x,size.y);
            for(const auto& spec:p->parameterSpecs()){auto* c=find(*editor,spec.id);require(c&&editor->getLocalBounds().contains(c->getBounds())&&c->getWidth()>0&&c->getHeight()>0,"original dynamic control raster bounds");auditBandHit(*editor,*c);}
            juce::Image pixels(juce::Image::ARGB,size.x*scale,size.y*scale,true);{juce::Graphics g(pixels);g.addTransform(juce::AffineTransform::scale(float(scale)));editor->paintEntireComponent(g,true);}
            juce::MemoryOutputStream png;require(juce::PNGImageFormat().writeImageToStream(pixels,png),"original dynamic PNG encode");
            require(evidence.getChildFile("original-dynamic-"+juce::String(band)+"-"+juce::String(size.x)+"-"+juce::String(scale)+"x.png").replaceWithData(png.getData(),png.getDataSize()),"original dynamic PNG write");
        }
    }
    std::cout<<"PASS original-layout Dynamic EQ: 90 bindings, 8 tabs, typed/keyboard/type selection, reset, real audio and raster bounds\n";
}
static void midSideOriginalInteractions(const juce::File& evidence){
    auto p=hem::PluginRegistry::create(ids[1]);p->prepareToPlay(48000,257);std::unique_ptr<juce::AudioProcessorEditor> editor(p->createEditor());
    auto* original=dynamic_cast<hem::OriginalMidSideEqEditor*>(editor.get());require(original!=nullptr,"original Mid/Side editor factory");int checked=0;editor->setSize(1000,563);
    for(const auto& spec:p->parameterSpecs()){const juce::String id(spec.id);
        if((id.startsWith("m")||id.startsWith("s"))&&id.length()>2&&juce::CharacterFunctions::isDigit(id[1]))set(*p,id.substring(0,2)+"On",1);
        original->refresh();auto* c=find(*editor,id);require(c&&c->isVisible()&&c->isEnabled(),"Mid/Side all controls visible and active");require(c->getName()==spec.name,"Mid/Side accessible identity");
        if(spec.toggle){auto* b=dynamic_cast<juce::ToggleButton*>(c);require(b!=nullptr,"Mid/Side toggle");for(bool on:{false,true}){b->setToggleState(on,juce::sendNotificationSync);require(p->state.getRawParameterValue(id)->load()==float(on),"Mid/Side toggle binding");}}
        else{auto* s=dynamic_cast<hem::ui::ReferenceSlider*>(c);require(s!=nullptr,"Mid/Side physical slider");
            for(float v:{spec.minimum,spec.maximum,spec.initial}){s->setValue(v,juce::sendNotificationSync);require(std::abs(p->state.getRawParameterValue(id)->load()-v)<=std::max(1e-4f,spec.step*.51f),"Mid/Side physical binding");auditBandControl(*s);}
            if(!s->choices.isEmpty()){for(int type=0;type<3;++type){s->onChoice(type);require(p->state.getRawParameterValue(id)->load()==type,"Mid/Side type choice");auditBandControl(*s);}}
            else{s->showTextBox();juce::TextEditor* text=nullptr;for(auto* child:s->getChildren())if(auto* l=dynamic_cast<juce::Label*>(child))if(l->getCurrentTextEditor())text=l->getCurrentTextEditor();require(text!=nullptr,"Mid/Side editable text");
                text->setText(juce::String(spec.maximum,4));s->hideTextBox(false);require(std::abs(p->state.getRawParameterValue(id)->load()-spec.maximum)<=std::max(1e-4f,spec.step*.51f),"Mid/Side typed physical binding");}
            s->setValue(spec.minimum,juce::sendNotificationSync);require(s->keyPressed(juce::KeyPress(juce::KeyPress::rightKey))&&s->getValue()>spec.minimum,"Mid/Side keyboard");
        }++checked;
    }
    require(checked==55,"Mid/Side all 55 controls checked");auto* reset=dynamic_cast<juce::Button*>(find(*editor,"uiResetAll"));require(reset!=nullptr,"Mid/Side reset command");reset->triggerClick();pumpBandMessages(1);
    for(const auto& spec:p->parameterSpecs())require(std::abs(p->state.getRawParameterValue(spec.id)->load()-spec.initial)<1e-4f,"Mid/Side reset values");
    activate(*p);set(*p,"width",150);set(*p,"m5Type",2);set(*p,"s5Type",2);juce::AudioBuffer<float> signal(2,257);juce::MidiBuffer midi;
    for(int block=0;block<80;++block){fill(signal,block*257);allocationProbe::enabled=true;p->processBlock(signal,midi);allocationProbe::enabled=false;}
    pumpBandMessages(50);
    for(auto size:{juce::Point<int>(1000,563),juce::Point<int>(1672,941),juce::Point<int>(1920,1081)})for(int scale:{1,2}){editor->setSize(size.x,size.y);
        for(const auto& spec:p->parameterSpecs()){auto* c=find(*editor,spec.id);require(c&&c->isVisible()&&editor->getLocalBounds().contains(c->getBounds())&&c->getWidth()>0&&c->getHeight()>0,"Mid/Side raster bounds");auditBandHit(*editor,*c);}
        juce::Image pixels(juce::Image::ARGB,size.x*scale,size.y*scale,true);{juce::Graphics g(pixels);g.addTransform(juce::AffineTransform::scale(float(scale)));editor->paintEntireComponent(g,true);}
        juce::MemoryOutputStream png;require(juce::PNGImageFormat().writeImageToStream(pixels,png),"Mid/Side PNG encode");require(evidence.getChildFile("original-mid-side-"+juce::String(size.x)+"-"+juce::String(scale)+"x.png").replaceWithData(png.getData(),png.getDataSize()),"Mid/Side PNG write");
    }
    std::cout<<"PASS original-layout Mid/Side EQ: 55 visible bindings, ten type selectors, typed/keyboard edits, reset, real audio and raster bounds\n";
}
#include "OriginalBankEditorFixtures.h"
#include "BandVisualPixelFixtures.h"
#include "OriginalBandPlotPixelFixtures.h"
#include "BandTelemetryCalibration.h"
static void pagesAndInteractions(const juce::File& evidence){
    bandPlotPixels(evidence);
    bandTelemetryCalibration();
    bandVisualPixels(evidence);
    bankOriginalInteractions(evidence);
    for(const auto* id:ids){auto p=hem::PluginRegistry::create(id);activate(*p);std::unique_ptr<juce::AudioProcessorEditor> editor(p->createEditor());
        if(juce::String(id)==ids[0]){editor.reset();dynamicOriginalInteractions(evidence);continue;}
        if(juce::String(id)==ids[1]){editor.reset();midSideOriginalInteractions(evidence);continue;}
        if(juce::String(id)==ids[2]||juce::String(id)==ids[3])continue;
        auto* bandEditor=dynamic_cast<hem::BandEditor*>(editor.get());require(bandEditor!=nullptr,"missing paged editor");
        auto* page=dynamic_cast<juce::ComboBox*>(find(*editor,"uiBandPage"));auto* lane=dynamic_cast<juce::ComboBox*>(find(*editor,"uiLanePage"));
        const bool ms=juce::String(id)=="mid-side-eq";const int pages=juce::String(id)=="dynamic-eq-8"?8:ms?4:juce::String(id)=="multiband-dynamics"?3:1;
        for(int side=1;side<=(ms?2:1);++side)for(int band=1;band<=pages;++band){lane->setSelectedId(side,juce::sendNotificationSync);page->setSelectedId(band,juce::sendNotificationSync);bandEditor->refresh();
            for(const auto& spec:p->parameterSpecs()){auto* component=find(*editor,spec.id);require(component!=nullptr,"missing attached hidden parameter");if(!component->isVisible())continue;
                if(!p->parameterActive(spec.id)){const juce::String key(spec.id);if(key=="lowWidth")set(*p,"lowMono",0);
                    else if(key.startsWith("b")){set(*p,key.substring(0,2)+"On",1);set(*p,key.substring(0,2)+"Range",6);}
                    else if(ms)set(*p,key.substring(0,2)+"On",1);
                    else for(const auto* prefix:{"low","mid","high"}){set(*p,juce::String(prefix)+"Ratio",4);set(*p,juce::String(prefix)+"Range",6);}bandEditor->refresh();}
                require(component->isEnabled(),"active fixture control disabled");
                if(spec.toggle){auto* button=dynamic_cast<juce::ToggleButton*>(component);require(button!=nullptr,"toggle widget");
                    button->setToggleState(false,juce::sendNotificationSync);button->triggerClick();pumpBandMessages(1);
                    require(p->state.getRawParameterValue(spec.id)->load()==1,"toggle click binding");
                }else{auto* slider=dynamic_cast<juce::Slider*>(component);require(slider!=nullptr,"numeric widget");
                    for(float v:{spec.minimum,spec.maximum,spec.initial}){slider->setValue(v,juce::sendNotificationSync);require(std::abs(p->state.getRawParameterValue(spec.id)->load()-v)<=std::max(.001f,spec.step*.51f),"physical range binding");}
                    slider->showTextBox();juce::TextEditor* text=nullptr;
                    for(auto* child:slider->getChildren())if(auto* label=dynamic_cast<juce::Label*>(child))if(label->getCurrentTextEditor())text=label->getCurrentTextEditor();
                    require(text!=nullptr,"physical text editor unavailable");text->setText(juce::String(spec.maximum,4));slider->hideTextBox(false);
                    require(std::abs(p->state.getRawParameterValue(spec.id)->load()-spec.maximum)<=std::max(.001f,spec.step*.51f),"typed physical value binding");
                    slider->setValue(spec.minimum+(spec.maximum-spec.minimum)*.5,juce::sendNotificationSync);const auto before=slider->getValue();
                    require(slider->keyPressed(juce::KeyPress(juce::KeyPress::rightKey))&&slider->getValue()>before,"keyboard edit");
                    const auto now=juce::Time::getCurrentTime();const auto start=slider->getLocalBounds().getCentre().toFloat();
                    auto mouse=[&](juce::Point<float> at,int flags,bool drag){return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(),at,juce::ModifierKeys(flags),1,0,0,0,0,slider,slider,now,start,now,1,drag);};
                    const auto prior=slider->getValue();slider->mouseDown(mouse(start,juce::ModifierKeys::leftButtonModifier,false));slider->mouseDrag(mouse(start+juce::Point<float>(0,-35),juce::ModifierKeys::leftButtonModifier,true));slider->mouseUp(mouse(start,0,true));
                    require(slider->getValue()>prior,"mouse drag");slider->mouseDoubleClick(mouse(start,0,false));require(std::abs(slider->getValue()-spec.initial)<.001,"double click reset");
                }
            }
            for(auto size:{juce::Point<int>(1000,563),juce::Point<int>(1280,720),juce::Point<int>(1920,1080)})for(int scale:{1,2}){
                editor->setSize(size.x,size.y);juce::Image pixels(juce::Image::ARGB,size.x*scale,size.y*scale,true);
                {juce::Graphics g(pixels);g.addTransform(juce::AffineTransform::scale(float(scale)));editor->paintEntireComponent(g,true);}
                for(const auto& spec:p->parameterSpecs()){auto* component=find(*editor,spec.id);require(editor->getLocalBounds().contains(component->getBounds())&&component->getWidth()>0,"control outside native editor");}
                juce::MemoryOutputStream png;require(juce::PNGImageFormat().writeImageToStream(pixels,png),"PNG encode");const auto name=juce::String(id)+"-"+juce::String(side)+"-"+juce::String(band)+"-"+juce::String(size.x)+"-"+juce::String(scale)+"x.png";
                require(evidence.getChildFile(name).replaceWithData(png.getData(),png.getDataSize()),"raster evidence write");
            }
        }
        if(juce::String(id)=="multiband-imager"){set(*p,"lowWidth",37);set(*p,"lowMono",1);bandEditor->refresh();require(!find(*editor,"lowWidth")->isEnabled(),"low mono activity");set(*p,"lowMono",0);bandEditor->refresh();require(find(*editor,"lowWidth")->isEnabled()&&p->state.getRawParameterValue("lowWidth")->load()==37,"low width lost on unlock");}
    }
}
static void fullSessions(const juce::File& dir){hem::SessionSnapshot snapshot;hem::MonitorProcessor monitor;snapshot.monitorParameters=hem::SessionState::captureParameters(monitor);
    for(size_t t=0;t<13;++t)for(size_t s=0;s<8;++s){auto& slot=snapshot.tracks[t].slots[s];slot.pluginId=ids[(t*8+s)%4];slot.bypassed=s%2!=0;auto p=hem::PluginRegistry::create(slot.pluginId);
        for(const auto& spec:p->parameterSpecs())set(*p,spec.id,s%3==0?spec.minimum:s%3==1?spec.maximum:spec.initial);slot.parameters=hem::SessionState::captureParameters(*p);}
    for(bool json:{false,true}){const auto file=dir.getChildFile(json?"full-13x8.json":"full-13x8.xml");if(json)hem::PresetManager::save(snapshot,file);else hem::SessionState::save(snapshot,file);
        const auto copy=json?hem::PresetManager::load(file):hem::SessionState::load(file);
        for(size_t t=0;t<13;++t)for(size_t s=0;s<8;++s){const auto& a=snapshot.tracks[t].slots[s];const auto& b=copy.tracks[t].slots[s];
            require(a.pluginId==b.pluginId&&a.bypassed==b.bypassed&&a.parameters.getType()==b.parameters.getType()&&a.parameters.getNumChildren()==b.parameters.getNumChildren(),"full session state/order/bypass/solos");
            // XML attributes are strings; compare exact APVTS float values and
            // ordered IDs rather than requiring the pre-serialization var type.
            for(int i=0;i<a.parameters.getNumChildren();++i){const auto av=a.parameters.getChild(i),bv=b.parameters.getChild(i);
                require(av.getType()==bv.getType()&&av["id"].toString()==bv["id"].toString()&&float(double(av["value"]))==float(double(bv["value"])),"full session exact parameter value/order");}}}
}
static void offline(const juce::File& dir){
    const auto input=dir.getChildFile("source.wav");{std::unique_ptr<juce::OutputStream> stream=input.createOutputStream();juce::WavAudioFormat format;
        auto writer=format.createWriterFor(stream,juce::AudioFormatWriterOptions().withSampleRate(48000).withNumChannels(2).withBitsPerSample(32).withSampleFormat(juce::AudioFormatWriterOptions::SampleFormat::floatingPoint));
        require(writer!=nullptr,"source writer");juce::AudioBuffer<float> b(2,48000);fill(b,0);require(writer->writeFromAudioSampleBuffer(b,0,48000)&&writer->flush(),"source write");}
    for(const auto* id:ids){hem::SessionSnapshot state;hem::MonitorProcessor monitor;state.monitorParameters=hem::SessionState::captureParameters(monitor);state.tracks[0].source=input;
        auto p=hem::PluginRegistry::create(id);activate(*p);if(p->state.getParameter("midSolo"))set(*p,"midSolo",1);
        auto& slot=state.tracks[0].slots[0];slot.pluginId=id;slot.parameters=hem::SessionState::captureParameters(*p);
        hem::OfflineMixOptions options;options.tailSeconds=0;const auto rejected=dir.getChildFile(juce::String(id)+"-rejected.wav");
        require(hem::OfflineMixRenderer::render(state,rejected,options).status.failed()&&!rejected.exists(),"short tail wrote a file");
        options.allowTailTruncation=true;options.tailSeconds=.25;const auto shortRender=hem::OfflineMixRenderer::render(state,dir.getChildFile(juce::String(id)+"-truncated.wav"),options);
        if(shortRender.status.failed()||shortRender.frames!=60000)std::cerr<<"truncated render module="<<id<<" frames="<<shortRender.frames<<" error="<<shortRender.status.getErrorMessage()<<"\n";require(shortRender.status.wasOk()&&shortRender.frames==60000,"explicit exact-length truncation");options.allowTailTruncation=false;options.tailSeconds=p->getTailLengthSeconds();
        hem::InsertChain live;live.prepare(48000,options.blockSize);live.installInitial(0,live.stageNode(std::move(p),48000,options.blockSize,live.slotLatency()),false);
        hem::MasterTruePeakLimiter limiter;limiter.prepare(48000,options.blockSize,2);juce::AudioBuffer<float> expected(2,options.blockSize);double pre=0,post=0;
        auto compare=[&](const juce::AudioBuffer<float>& actual,juce::int64 position,int count,bool after){
            if(!after){fill(expected,int(position));for(int i=0;i<count;++i)if(position+i>=48000){expected.setSample(0,i,0);expected.setSample(1,i,0);}live.process(expected.getWritePointer(0),expected.getWritePointer(1),count);}
            else limiter.process(expected,count);
            for(int c=0;c<2;++c)for(int i=0;i<count;++i){const double error=std::abs(double(actual.getSample(c,i))-expected.getSample(c,i));auto& max=after?post:pre;max=std::max(max,error);}};
        const auto result=hem::OfflineMixRenderer::render(state,dir.getChildFile(juce::String(id)+"-parity.wav"),options,{},{},nullptr,compare);
        require(result.status.wasOk()&&pre<=2e-6&&post<=2e-6,"live/offline parity including printed solo");
        std::cout<<id<<" live_offline_pre="<<pre<<" post="<<post<<'\n';
        auto disabled=hem::PluginRegistry::create(id);slot.parameters=hem::SessionState::captureParameters(*disabled);
        require(hem::OfflineMixRenderer::requiredFoundationTail(state)==0,"disabled frozen tail not skipped");
    }
    hem::SessionSnapshot chain;auto p=hem::PluginRegistry::create(ids[0]);activate(*p);
    for(auto& slot:chain.tracks[0].slots){slot.pluginId=ids[0];slot.parameters=hem::SessionState::captureParameters(*p);}
    require(hem::OfflineMixRenderer::requiredFoundationTail(chain)==96,"eight-node conservative chain bound");chain.tracks[0].muted=true;require(hem::OfflineMixRenderer::requiredFoundationTail(chain)==0,"muted chain tail");
}
static void timingAndRegistry(const juce::File& dir){std::ofstream csv(dir.getChildFile("timings.csv").getFullPathName().toStdString());csv<<"scope,rate,block,iteration,seconds,deadline,miss\n";
    for(double rate:{44100.,48000.,96000.})for(int block:{1,17,64,128,257}){
        auto run=[&](int tracks){std::vector<std::unique_ptr<hem::InsertChain>> chains;
            for(int t=0;t<tracks;++t){auto c=std::make_unique<hem::InsertChain>();c->prepare(rate,block);for(int s=0;s<8;++s){auto p=hem::PluginRegistry::create(ids[0]);activate(*p);c->installInitial(s,c->stageNode(std::move(p),rate,block,c->slotLatency()),false);}chains.push_back(std::move(c));}
            std::vector<std::vector<float>> left(size_t(tracks),std::vector<float>(size_t(block),.1f)),right(size_t(tracks),std::vector<float>(size_t(block),-.03f));std::array<double,300> times{};
            for(size_t i=0;i<times.size();++i){for(int t=0;t<tracks;++t){std::fill(left[size_t(t)].begin(),left[size_t(t)].end(),.1f);std::fill(right[size_t(t)].begin(),right[size_t(t)].end(),-.03f);}const auto start=std::chrono::steady_clock::now();
                allocationProbe::enabled=true;for(int t=0;t<tracks;++t)chains[size_t(t)]->process(left[size_t(t)].data(),right[size_t(t)].data(),block);allocationProbe::enabled=false;
                times[i]=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();csv<<(tracks==1?"eight_dynamic_eq":"13_independent_eight_slot_chains")<<','<<rate<<','<<block<<','<<i<<','<<times[i]<<','<<block/rate<<','<<(times[i]>block/rate)<<'\n';}
            const auto cold=times[0];std::sort(times.begin(),times.end());std::cout<<"tracks="<<tracks<<" rate="<<rate<<" block="<<block<<" cold="<<cold<<" p50="<<times[150]<<" p95="<<times[285]<<" p99="<<times[297]<<" max="<<times.back()<<" deadline="<<block/rate<<'\n';};
        run(1);run(13);
        // Actual TrackEngine render path, 13 independently prepared WAV streams,
        // track trim/meters, summing and unchanged final master; no device I/O.
        juce::AudioBuffer<float> source(2,block*8),sum(2,block);fill(source,0,rate);source.applyGain(.05f);
        juce::TemporaryFile fixture(".wav");{std::unique_ptr<juce::OutputStream> stream=fixture.getFile().createOutputStream();juce::WavAudioFormat wav;
            auto writer=wav.createWriterFor(stream,juce::AudioFormatWriterOptions().withSampleRate(rate).withNumChannels(2).withBitsPerSample(32).withSampleFormat(juce::AudioFormatWriterOptions::SampleFormat::floatingPoint));
            require(writer&&writer->writeFromAudioSampleBuffer(source,0,source.getNumSamples())&&writer->flush(),"timing WAV fixture");}
        juce::AudioFormatManager formats;formats.registerBasicFormats();juce::TimeSliceThread disk("Band fixture read-ahead");disk.startThread();hem::TrackBank bank;
        std::vector<float> inputGains(size_t(block),1.f);hem::CoreMasterStage master;master.prepare(rate,block);
        for(auto& track:bank.tracks){track.prepare(rate,block);
            track.playback->prepareFile(fixture.getFile(),formats,disk,rate,block,true);track.loaded=true;track.transport().start();
            for(int slot=0;slot<8;++slot){auto p=hem::PluginRegistry::create(ids[0]);activate(*p);track.chain.installInitial(slot,track.chain.stageNode(std::move(p),rate,block,track.chain.slotLatency()),false);}}
        for(int iteration=0;iteration<300;++iteration){
            // Offline fixture pumps faster than wall clock. Wait for whole-group
            // availability outside the measured/probed callback; report only DSP
            // execution time, not this worker wait, and never time partial audio.
            const auto deadline=juce::Time::getMillisecondCounterHiRes()+2000;
            while(!std::all_of(bank.tracks.begin(),bank.tracks.end(),[&](const auto& t){return t.transport().availableRt(block);})){require(juce::Time::getMillisecondCounterHiRes()<deadline,"timing WAV prefill timeout");juce::Thread::sleep(1);}
            sum.clear();const auto start=std::chrono::steady_clock::now();allocationProbe::enabled=true;
            for(auto& track:bank.tracks)track.render(sum,block,false,inputGains.data());master.process(sum.getWritePointer(0),sum.getWritePointer(1),block);allocationProbe::enabled=false;
            const double elapsed=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();csv<<"13_TrackEngine_sum_master_prepared_WAV,"<<rate<<','<<block<<','<<iteration<<','<<elapsed<<','<<block/rate<<','<<(elapsed>block/rate)<<'\n';}
        for(auto& track:bank.tracks)track.release();
    }
    for(const auto& entry:hem::PluginRegistry::entries()){if(entry.monitoringOnly)continue;if(!entry.realtimeInsert){require(entry.clipOperation&&!entry.trackingAllowed,"offline registry capability");auto p=entry.create();bool rejected=false;try{p->prepareToPlay(48000,64);}catch(const std::exception&){rejected=true;}require(rejected&&!p->processingReady(),"offline registry admitted callback");continue;}hem::InsertChain chain;chain.prepare(48000,64);
        chain.installInitial(0,chain.stageNode(entry.create(),48000,64,chain.slotLatency()),false);std::array<float,64> l{},r{};
        for(int n=0;n<100;++n){chain.bypass(0,n%10<5);allocationProbe::enabled=true;chain.process(l.data(),r.data(),64);allocationProbe::enabled=false;}
        require(chain.publishStopped(0,nullptr),"registry removal");chain.collectRetired(0);require(chain.publishStopped(0,chain.stageNode(entry.create(),48000,64,chain.slotLatency())),"registry reinsert");chain.collectRetired(0);
    }
}
#include "BandRealisticTiming.h"
#include "BandMatchedTiming.h"
#include "OriginalBandTopbarFixtures.h"
int main(int argc,char** argv){juce::ScopedJuceInitialiser_GUI gui;allocationProbe::testThread=true;try{OriginalBandReadabilityAudit textAudit;
    const bool stateOnly=argc==2&&juce::String(argv[1])=="--state-only";
    const int hooks=platformProbe::install();allocationProbe::enabled=true;void* p=::operator new(23);allocationProbe::enabled=false;::operator delete(p);
    require(allocationProbe::count==1&&(hooks==0||platformProbe::count>0),"allocation positive calibration");
    const auto before=allocationProbe::count.load();std::thread other([]{allocationProbe::enabled=true;void* p=::operator new(31);::operator delete(p);allocationProbe::enabled=false;});other.join();
    require(allocationProbe::count==before&&allocationProbe::otherThreadCount>0,"thread attribution calibration");allocationProbe::count=0;platformProbe::count=0;
    const auto dir=juce::File::getCurrentWorkingDirectory().getChildFile("band-integration-evidence-"+juce::Uuid().toString());require(dir.createDirectory().wasOk(),"evidence directory");
    if(argc==2&&juce::String(argv[1])=="--topbar-only"){originalBandTopbarInteractions(dir);return 0;}
    if(argc==2&&juce::String(argv[1])=="--fma-proof-only"){writeFmaResidualProof("fma-native");return 0;}
    if(argc==2&&juce::String(argv[1])=="--plot-pixels-only"){bandPlotPixels(dir);std::cout<<"PASS PLOT PIXEL SUBSET; evidence="<<dir.getFullPathName()<<'\n';return 0;}
    if(argc==2&&juce::String(argv[1])=="--band-meters-only"){bandVisualPixels(dir);require(allocationProbe::count==0&&platformProbe::count==0,"band visual callback allocated");std::cout<<"PASS BAND METER SUBSET; other integration gates not run; evidence="<<dir.getFullPathName()<<'\n';return 0;}
    if(argc==2&&juce::String(argv[1])=="--telemetry-only"){bandTelemetryCalibration();require(allocationProbe::count==0&&platformProbe::count==0,"band telemetry callback allocated");std::cout<<"PASS TELEMETRY SUBSET; other integration gates not run\n";return 0;}
    if(argc==2&&juce::String(argv[1])=="--realistic-timing-only"){realisticBandTiming(dir);require(allocationProbe::count==0&&platformProbe::count==0,"realistic timing callback allocated");std::cout<<"COMPLETED REALISTIC TIMING SUBSET; all misses retained, no capacity assertion; evidence="<<dir.getFullPathName()<<'\n';return 0;}
    if(argc==2&&juce::String(argv[1])=="--matched-timing-only"){matchedBandTiming(dir);require(allocationProbe::count==0&&platformProbe::count==0,"matched timing callback allocated");std::cout<<"COMPLETED MATCHED TIMING SUBSET; all misses retained, no capacity assertion; evidence="<<dir.getFullPathName()<<'\n';return 0;}
    if(argc==2&&juce::String(argv[1])=="--dynamic-editor-only"){dynamicOriginalInteractions(dir);require(allocationProbe::count==0&&platformProbe::count==0,"dynamic editor callback allocated");std::cout<<"PASS DYNAMIC EDITOR SUBSET; other integration gates not run; evidence="<<dir.getFullPathName()<<'\n';return 0;}
    if(argc==2&&juce::String(argv[1])=="--mid-side-editor-only"){midSideOriginalInteractions(dir);require(allocationProbe::count==0&&platformProbe::count==0,"Mid/Side editor callback allocated");std::cout<<"PASS MID/SIDE EDITOR SUBSET; other integration gates not run; evidence="<<dir.getFullPathName()<<'\n';return 0;}
    if(argc==2&&juce::String(argv[1])=="--bank-editors-only"){bankOriginalInteractions(dir);require(allocationProbe::count==0&&platformProbe::count==0,"bank editor callback allocated");std::cout<<"PASS BANK EDITORS SUBSET; other integration gates not run; evidence="<<dir.getFullPathName()<<'\n';return 0;}
    if(argc==2&&juce::String(argv[1])=="--timing-only"){timingAndRegistry(dir);require(allocationProbe::count==0&&platformProbe::count==0,"timing callback allocated");std::cout<<"COMPLETED NATIVE TIMING/REGISTRY SUBSET; all deadline misses retained, no capacity assertion; evidence="<<dir.getFullPathName()<<'\n';return 0;}
    schemas();migrations();fullSessions(dir);offline(dir);if(!stateOnly){pagesAndInteractions(dir);timingAndRegistry(dir);}
    require(allocationProbe::count==0&&platformProbe::count==0,"callback allocation");std::cout<<"PASS band integration "<<(stateOnly?"STATE/OFFLINE SUBSET":"FULL")<<"; hooks="<<hooks<<"; internal allocator calls inside DLLs not instrumented; evidence="<<dir.getFullPathName()<<'\n';return 0;
}catch(const std::exception& e){allocationProbe::enabled=false;std::cerr<<e.what()<<'\n';return 1;}}


