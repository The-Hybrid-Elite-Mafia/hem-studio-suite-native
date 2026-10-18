#include "../Source/Plugins/RestorationEditors.h"
#include "../Source/Plugins/PluginRegistry.h"
#include "DiscreteControlChecks.h"
#include <iostream>
#include <map>
#include <random>

namespace {
unsigned checks=0;
void check(bool condition,const juce::String& message){++checks;if(!condition)throw std::runtime_error(message.toStdString());}
void set(hem::ModuleProcessor& p,const char* id,float value){auto* parameter=p.state.getParameter(id);check(parameter!=nullptr,juce::String("Missing parameter ")+id);parameter->setValueNotifyingHost(parameter->convertTo0to1(value));}
juce::Component* find(juce::Component& c,const juce::String& id){return hem::controlChecks::find(c,id);}
std::unique_ptr<hem::RestorationProcessor> make(int number){
    const char* id=number==2?"dynamic-resonance-suppressor":number==6?"breath-mouth-control":"clip-repair";
    auto module=hem::PluginRegistry::create(id);check(module!=nullptr,"Restoration registry factory missing");
    auto* typed=dynamic_cast<hem::RestorationProcessor*>(module.get());check(typed!=nullptr,"Registered restoration processor type mismatch");
    module.release();return std::unique_ptr<hem::RestorationProcessor>(typed);
}
float canonical(hem::ModuleProcessor& p,const char* id,float v){auto* q=p.state.getParameter(id);return q->convertFrom0to1(q->convertTo0to1(v));}
void save(const juce::Image& image,const juce::File& file){auto stream=file.createOutputStream();check(stream!=nullptr,"Raster stream unavailable");stream->setPosition(0);stream->truncate();juce::PNGImageFormat format;check(format.writeImageToStream(image,*stream),"PNG encoding failed");}
juce::MouseEvent event(juce::Component& c,int flags=0){const auto at=c.getLocalBounds().getCentre().toFloat();const auto now=juce::Time::getCurrentTime();return {juce::Desktop::getInstance().getMainMouseSource(),at,juce::ModifierKeys(flags),1,0,0,0,0,&c,&c,now,at,now,1,false};}
void click(juce::Component& c){c.mouseDown(event(c,juce::ModifierKeys::leftButtonModifier));c.mouseUp(event(c));}

void controls(hem::RestorationProcessor& p,hem::RestorationEditor& editor,int number){
    for(const auto& spec:p.parameterSpecs()){
        auto* c=find(editor,spec.id);check(c!=nullptr,juce::String("Visible binding missing ")+spec.id);check(c->getName()==spec.name,juce::String("Accessible name mismatch ")+spec.id);
        auto* raw=p.state.getRawParameterValue(spec.id);const float tolerance=std::max(.0001f,spec.step*.51f);
        check(std::abs(raw->load()-spec.initial)<=tolerance,"Editor changed startup default");
        if(hem::controlChecks::choice(*c)){
            for(int value=0;value<=int(spec.maximum);++value){auto* choice=find(editor,value?juce::String(spec.id)+"_"+juce::String(value):juce::String(spec.id));check(choice!=nullptr,"Choice missing");click(*choice);editor.refreshParameterActivity();check(raw->load()==value,"Actual radio click failed APVTS");check(dynamic_cast<juce::Button*>(choice)->getToggleState(),"Choice feedback stale");
                for(int other=0;other<=int(spec.maximum);++other){auto* b=dynamic_cast<juce::Button*>(find(editor,other?juce::String(spec.id)+"_"+juce::String(other):juce::String(spec.id)));check(b->getToggleState()==(other==value),"Choice not exclusive");}}
        }else if(auto* slider=dynamic_cast<juce::Slider*>(c)){
            check(slider->getTextValueSuffix().trim()==spec.unit,"Dial units incorrect");check(slider->getDoubleClickReturnValue()==canonical(p,spec.id,spec.initial),"Double-click value incorrect");
            for(float value:{spec.minimum,spec.maximum,spec.initial}){slider->setValue(value,juce::sendNotificationSync);check(std::abs(raw->load()-value)<=tolerance,juce::String("Dial did not reach APVTS ")+spec.id);}
            slider->setValue(spec.maximum,juce::sendNotificationSync);slider->mouseDoubleClick(event(*slider));check(std::abs(raw->load()-spec.initial)<=tolerance,"Actual double-click did not reset APVTS");
            juce::Label* readout=nullptr;for(auto* child:slider->getChildren())if(auto* label=dynamic_cast<juce::Label*>(child))readout=label;
            check(readout&&readout->getText().isNotEmpty()&&readout->getText().contains(spec.unit),"Pictured dial readout absent or missing units");
            const float middle=spec.minimum+(spec.maximum-spec.minimum)*.5f;slider->setValue(middle,juce::sendNotificationSync);
            auto down=event(*slider,juce::ModifierKeys::leftButtonModifier).withNewPosition(juce::Point<float>(float(slider->getProperties()["bodyX"]),float(slider->getProperties()["bodyY"])));
            slider->mouseDown(down);auto drag=down.withNewPosition(down.position+juce::Point<float>(24,-24));slider->mouseDrag(drag);slider->mouseUp(event(*slider));
            check(std::abs(slider->getValue()-middle)>spec.step*.5f&&std::abs(raw->load()-slider->getValue())<=tolerance,"Actual dial drag did not reach APVTS");
            slider->setValue(middle,juce::sendNotificationSync);const auto before=slider->getValue();check(slider->keyPressed(juce::KeyPress(juce::KeyPress::rightKey)),"Keyboard edit unhandled");check(slider->getValue()>before&&std::abs(raw->load()-slider->getValue())<=tolerance,"Keyboard edit did not reach APVTS");
            for(float value:{spec.minimum,spec.maximum,spec.initial}){set(p,spec.id,value);editor.refreshParameterActivity();check(std::abs(slider->getValue()-value)<=tolerance,"Host automation did not reach visible dial");}
        }else if(auto* toggle=dynamic_cast<juce::ToggleButton*>(c)){
            toggle->setToggleState(false,juce::sendNotificationSync);click(*toggle);check(raw->load()==1,"Actual toggle click did not enable APVTS");click(*toggle);check(raw->load()==0,"Actual toggle click did not disable APVTS");
            set(p,spec.id,1);editor.refreshParameterActivity();check(toggle->getToggleState(),"Host automation did not update toggle");
        }else check(false,"Unknown control kind");
        set(p,spec.id,spec.initial);editor.refreshParameterActivity();
        for(auto size:{juce::Point<int>(1000,563),juce::Point<int>(1280,720),juce::Point<int>(1672,941),juce::Point<int>(3344,1882)}){editor.setSize(size.x,size.y);check(editor.getLocalBounds().contains(c->getBounds()),"Scaled control outside chassis");check(!c->getBounds().isEmpty(),"Scaled control lost hit area");}
    }
    if(number==6){
        auto* first=dynamic_cast<juce::ToggleButton*>(find(editor,"bypass"));auto* second=dynamic_cast<juce::ToggleButton*>(find(editor,"bypass_1"));check(first&&second,"Both pictured bypass buttons required");
        for(auto* clicked:{first,second}){click(*clicked);editor.refreshParameterActivity();check(p.state.getRawParameterValue("bypass")->load()==1&&first->getToggleState()&&second->getToggleState(),"Duplicate bypass enable synchronization failed");click(*clicked);editor.refreshParameterActivity();check(p.state.getRawParameterValue("bypass")->load()==0&&!first->getToggleState()&&!second->getToggleState(),"Duplicate bypass disable synchronization failed");}
        auto* output=find(editor,"display-outputReadout");check(output&&!dynamic_cast<juce::Slider*>(output),"Pictured OUTPUT must be measured readout");check(p.state.getParameter("output")==nullptr,"Unpictured output-gain parameter invented");
    }
}
void factoryStates(hem::RestorationProcessor& p,hem::RestorationEditor& editor,int number){
    if(number==9){check(editor.factoryPresetCount()==0&&!find(editor,"presets")&&!find(editor,"close"),"Unpictured factory selector/close invented");return;}
    const std::vector<std::map<juce::String,float>> changes=number==2?std::vector<std::map<juce::String,float>>{
        {},{{"sensitivity",45},{"low",180},{"high",10000},{"selectivity",55},{"maxReduction",3},{"attack",15},{"release",180},{"profile",0}},
        {{"sensitivity",55},{"low",160},{"high",11000},{"selectivity",70},{"maxReduction",6},{"attack",10},{"release",180},{"profile",1}}
    }:std::vector<std::map<juce::String,float>>{
        {},{{"breathDetection",45},{"breathReduction",4},{"breathLength",120},{"mouthDetection",50},{"mouthReduction",3},{"mouthLength",.4f}},
        {{"breathDetection",60},{"breathReduction",8},{"breathLength",100},{"mouthDetection",65},{"mouthReduction",6},{"mouthLength",.6f},{"high",14000}}
    };
    check(editor.factoryPresetCount()==3,"Factory preset count incorrect");check(editor.factoryPresetName(0)=="Neutral","Neutral preset absent");
    for(int preset=0;preset<3;++preset){
        for(const auto& s:p.parameterSpecs())set(p,s.id,s.maximum);
        editor.applyFactoryPreset(preset);check(editor.currentFactoryPreset()==preset,"Factory state name not derived from actual APVTS snapshot");
        for(const auto& s:p.parameterSpecs()){const auto i=changes[size_t(preset)].find(s.id);const float expected=i==changes[size_t(preset)].end()?s.initial:i->second;check(p.state.getRawParameterValue(s.id)->load()==canonical(p,s.id,expected),juce::String("Factory snapshot differs at ")+s.id);}
        juce::MemoryBlock state;p.getStateInformation(state);auto copy=make(number);copy->setStateInformation(state.getData(),int(state.getSize()));for(const auto& s:p.parameterSpecs())check(copy->state.getRawParameterValue(s.id)->load()==p.state.getRawParameterValue(s.id)->load(),"Factory APVTS state not recallable");
    }
    editor.applyFactoryPreset(-1);check(editor.currentFactoryPreset()==2,"Invalid preset changed state");editor.applyFactoryPreset(3);check(editor.currentFactoryPreset()==2,"Out-of-range preset changed state");
    editor.applyFactoryPreset(0);set(p,"low",2500);check(editor.currentFactoryPreset()==-1,"Edited state falsely claims factory identity");
    if(number==6){auto* previous=find(editor,"previous");auto* next=find(editor,"next");check(previous&&next,"Pictured preset arrows missing");check(previous->getName()=="Previous factory preset"&&next->getName()=="Next factory preset","Preset arrow accessible names missing");click(*next);check(editor.currentFactoryPreset()==0,"Custom NEXT should select Neutral");click(*previous);check(editor.currentFactoryPreset()==2,"Previous preset wrap failed");click(*next);check(editor.currentFactoryPreset()==0,"Next preset wrap failed");click(*next);check(editor.currentFactoryPreset()==1,"Next preset action failed");}
    editor.applyFactoryPreset(0);
    auto* close=dynamic_cast<juce::Button*>(find(editor,"close"));check(close&&!close->isEnabled(),"Unbound CLOSE must be disabled");int closed=0;editor.setCloseRequest([&]{++closed;});check(close->isEnabled(),"Bound CLOSE not enabled");click(*close);check(closed==1,"CLOSE did not call owner");editor.setCloseRequest({});check(!close->isEnabled(),"Removed close binding still active");
    auto* presets=dynamic_cast<juce::Button*>(find(editor,"presets"));check(presets&&presets->isEnabled()&&bool(presets->onClick),"PRESETS popup action unavailable");
}
struct Dial {int x,y,r;};
std::vector<Dial> dials(int number){if(number==2)return {{312,742,50},{474,742,50},{625,742,50},{779,742,50},{931,742,50},{1082,742,50},{1231,742,50},{1379,742,50},{1542,742,50}};if(number==6)return {{400,579,49},{565,579,49},{728,579,49},{1228,579,49},{1389,579,49},{1548,579,49},{671,816,44},{946,816,44}};return {{460,751,69},{793,751,69},{1124,751,69},{1460,751,69}};}
juce::Rectangle<int> plotInterior(int number){if(number==2)return {300,263,820,196};if(number==6)return {235,234,1204,132};return {305,285,1086,136};}
unsigned signalPixels(const juce::Image& image,juce::Rectangle<int> b){unsigned count=0;for(int y=b.getY();y<b.getBottom();++y)for(int x=b.getX();x<b.getRight();++x){auto c=image.getPixelAt(x,y);if((c.getGreen()>170&&c.getBlue()>170&&c.getRed()<90)||(c.getRed()>190&&c.getGreen()>90&&c.getGreen()<210&&c.getBlue()<80))++count;}return count;}
void rings(const juce::Image& image,const juce::Image& approved,int number){
    juce::Image expected(juce::Image::ARGB,image.getWidth(),image.getHeight(),true);{juce::Graphics g(expected);g.drawImage(approved,expected.getBounds().toFloat(),juce::RectanglePlacement::stretchToFit);}
    const float sx=image.getWidth()/1672.f,sy=image.getHeight()/941.f;
    for(auto d:dials(number)){const int inner=d.r+5,outer=d.r+14;
        for(int y=int((d.y-outer)*sy);y<=int((d.y+outer)*sy);++y)for(int x=int((d.x-outer)*sx);x<=int((d.x+outer)*sx);++x){
            const float dx=x/sx-d.x,dy=y/sy-d.y,r=dx*dx+dy*dy;if(r<inner*inner||r>outer*outer)continue;
            check(image.getPixelAt(x,y)==expected.getPixelAt(x,y),juce::String("Authored outer dial ring changed at ")+juce::String(x)+","+juce::String(y)+" width "+juce::String(image.getWidth()));
        }
    }
}
void process(hem::RestorationProcessor& p,int number,bool silence,int count){juce::AudioBuffer<float> buffer(2,128);juce::MidiBuffer midi;std::mt19937 random(901);std::uniform_real_distribution<float> noise(-1,1);
    for(int block=0;block<count;++block){for(int i=0;i<128;++i){const int n=block*128+i;const double t=n/48000.;float value=0;if(!silence){if(number==2)value=float(.4*std::sin(2*hem::pi*997*t)+.03*noise(random));else if(number==6)value=(n%12000<8000?.16f*noise(random):.01f*noise(random))+(n%433==0?.7f:0.f);else value=std::clamp(float(1.6*std::sin(2*hem::pi*430*t)),-.7f,.7f);}buffer.setSample(0,i,value);buffer.setSample(1,i,value*.63f);}p.processBlock(buffer,midi);for(int ch=0;ch<2;++ch)for(int i=0;i<128;++i)check(std::isfinite(buffer.getSample(ch,i)),"Nonfinite live processing");}}
struct Capture {juce::String name;juce::Image image;};
using Captures=std::vector<Capture>;
juce::Image capture(hem::RestorationEditor& editor,Captures& images,const juce::String& name){
    editor.refreshParameterActivity();auto image=editor.createComponentSnapshot(editor.getLocalBounds(),true);images.push_back({name,image});return image;
}
void dark(const juce::Image& image,int number,const char* reason){check(signalPixels(image,plotInterior(number))==0,reason);}
void rasters(hem::RestorationProcessor& p,hem::RestorationEditor& editor,int number,Captures& images){
    const auto approved=hem::ui::approvedFaceplate(number);check(approved.isValid(),"02/06/09 approved binary art must be integrated before raster tests");
    const auto prefix=juce::String(number).paddedLeft('0',2);
    for(auto size:{juce::Point<int>(1000,563),juce::Point<int>(1280,720),juce::Point<int>(1672,941),juce::Point<int>(3344,1882)}){
        editor.setSize(size.x,size.y);const auto image=capture(editor,images,prefix+"-idle-"+juce::String(size.x));
        rings(image,approved,number);if(size.x==1672)dark(image,number,"Idle plot retained invented/artwork signal");
    }
    editor.setSize(1672,941);process(p,number,true,1600);const auto silence=capture(editor,images,prefix+"-silence-1672");check(p.restorationDisplay().valid,"Silence is not valid measured audio");
    if(number==2){set(p,"maxReduction",12);set(p,"sensitivity",80);set(p,"selectivity",70);}else if(number==6){set(p,"breathReduction",8);set(p,"mouthReduction",10);}else {set(p,"threshold",-4);set(p,"amount",100);}
    for(auto size:{juce::Point<int>(1000,563),juce::Point<int>(1280,720),juce::Point<int>(1672,941)}){
        process(p,number,false,1600);editor.setSize(size.x,size.y);const auto image=capture(editor,images,prefix+"-live-"+juce::String(size.x));
        const auto display=p.restorationDisplay();check(display.valid&&display.count>0,"Measured history absent");if(number==2)check(display.spectralCount>0,"Measured constant-Q spectrogram absent");
        rings(image,approved,number);if(size.x==1672)check(signalPixels(image,plotInterior(number))>60,"Real signal not visible");
    }
    const auto live=editor.createComponentSnapshot(editor.getLocalBounds());
    for(float endpoint:{2.f,-2.f}){
        juce::AudioBuffer<float> buffer(2,128);juce::MidiBuffer midi;
        for(int block=0;block<160;++block){for(int ch=0;ch<2;++ch)for(int sample=0;sample<128;++sample)buffer.setSample(ch,sample,endpoint);p.processBlock(buffer,midi);}
        editor.refreshParameterActivity();const auto extreme=editor.createComponentSnapshot(editor.getLocalBounds());
        const auto caption=number==2?juce::Rectangle<int>(302,232,820,22):number==6?juce::Rectangle<int>(237,201,1202,20):juce::Rectangle<int>(361,258,1000,20);
        for(int y=caption.getY();y<caption.getBottom();++y)for(int x=caption.getX();x<caption.getRight();++x)check(extreme.getPixelAt(x,y)==live.getPixelAt(x,y),"Measured full-scale trace crossed caption area");
    }
    process(p,number,false,120);editor.refreshParameterActivity();
    // Open while frozen immediately, without waiting for the stale timeout.
    // The constructor must not timestamp an existing publication as new audio.
    auto reopened=std::unique_ptr<juce::AudioProcessorEditor>(p.createEditor());auto* reopenedEditor=dynamic_cast<hem::RestorationEditor*>(reopened.get());check(reopenedEditor!=nullptr,"Reopened production editor missing");reopenedEditor->setSize(1672,941);
    const auto frozen=capture(*reopenedEditor,images,prefix+"-reopened-frozen-1672");dark(frozen,number,"Reopened editor revived frozen trace");
    juce::Thread::sleep(780);const auto stale=capture(editor,images,prefix+"-stale-1672");dark(stale,number,"Stale trace still claims live signal");
    p.releaseResources();const auto released=capture(editor,images,prefix+"-released-1672");dark(released,number,"Released processor retained live trace");check(!p.processingReady()&&!p.meterSnapshot().isPrepared,"Release readiness did not clear");
    p.prepareToPlay(48000,128);const auto reprepared=capture(editor,images,prefix+"-reprepared-1672");dark(reprepared,number,"Reprepared processor retained previous epoch trace");check(!p.meterSnapshot().hasProcessedAudio,"Reprepare retained old audio evidence");
    process(p,number,false,120);editor.refreshParameterActivity();check(signalPixels(editor.createComponentSnapshot(editor.getLocalBounds()),plotInterior(number))>60,"New epoch did not become live");
    p.reset();editor.refreshParameterActivity();dark(editor.createComponentSnapshot(editor.getLocalBounds()),number,"Reset did not clear trace");
}
void honestMeasurements(hem::RestorationProcessor& p,hem::RestorationEditor& editor,int number,Captures& images){
    editor.setSize(1672,941);
    for(const char* id:{"display-inputMeter","display-outputMeter"}){auto* label=dynamic_cast<juce::Label*>(find(editor,id));check(label&&label->getTooltip().contains("300 ms RMS")&&label->getTooltip().contains("independent"),"Stereo meter label omits independent RMS semantics");}
    if(number==2){
        const auto name=find(editor,"display-spectrum")->getName();check(name.contains("constant-Q")&&name.contains("80 Hz")&&name.contains("0.42")&&name.contains("not an FFT"),"Constant-Q labels do not describe the actual detector bank");
        for(double rate:{8000.,44100.,48000.,96000.}){p.prepareToPlay(rate,128);process(p,number,false,40);editor.refreshParameterActivity();const auto d=p.restorationDisplay();check(d.valid&&d.spectralCount>0,"Frequency verification lacks current measured bands");check(std::abs(d.frequencies.front()-80)<.001&&std::abs(d.frequencies.back()-std::min(18000.,rate*.42))<.02,"Measured frequency endpoints differ from displayed-axis contract");if(rate==8000)capture(editor,images,"02-live-8000Hz-axis-1672");}
    }else if(number==9){
        const auto name=find(editor,"display-repair")->getName();check(name.contains("aligned normal output")&&name.contains("even during Listen Repair")&&name.contains("excluding gain-only"),"Repair legend makes false output/delta claim");
        set(p,"threshold",-4);set(p,"amount",0);set(p,"output",0);set(p,"listenRepair",0);p.prepareToPlay(48000,128);editor.refreshParameterActivity();process(p,number,false,200);
        auto image=capture(editor,images,"09-detected-neutral-1672");const auto d=p.restorationDisplay();check(d.events>0&&d.repaired==0,"Neutral detection fixture did not distinguish detection and repair");check(image.getPixelAt(283,572)==juce::Colour(0xffffa02b),"Detected-but-unrepaired event did not light lamp");
        juce::Thread::sleep(275);editor.refreshParameterActivity();image=editor.createComponentSnapshot(editor.getLocalBounds());check(image.getPixelAt(283,572)==juce::Colour(0xff152129),"Event lamp persisted without a new counter increment");
        p.reset();editor.refreshParameterActivity();image=editor.createComponentSnapshot(editor.getLocalBounds());check(image.getPixelAt(283,572)==juce::Colour(0xff152129),"Reset retained old event lamp");
        process(p,number,false,200);editor.refreshParameterActivity();image=editor.createComponentSnapshot(editor.getLocalBounds());check(image.getPixelAt(283,572)==juce::Colour(0xffffa02b),"Post-reset detected event not visible");
        p.releaseResources();editor.refreshParameterActivity();image=editor.createComponentSnapshot(editor.getLocalBounds());check(image.getPixelAt(283,572)==juce::Colour(0xff152129),"Release retained event lamp");
        p.prepareToPlay(48000,128);editor.refreshParameterActivity();image=editor.createComponentSnapshot(editor.getLocalBounds());check(image.getPixelAt(283,572)==juce::Colour(0xff152129),"Reprepare retained event lamp");
    }
}
}
int main(int argc,char** argv){try{juce::ScopedJuceInitialiser_GUI gui;const auto folder=argc>1?juce::File(argv[1]):juce::File::getCurrentWorkingDirectory().getChildFile("restoration-native-rasters");check(folder.createDirectory().wasOk(),"Raster folder unavailable");Captures images;
    check(hem::PluginRegistry::entries().size()==35,"Restoration expected as appended registered family");
    for(int number:{2,6,9}){auto p=make(number);p->prepareToPlay(48000,128);auto owner=std::unique_ptr<juce::AudioProcessorEditor>(p->createEditor());auto* editor=dynamic_cast<hem::RestorationEditor*>(owner.get());check(editor!=nullptr,"Restoration native editor not returned by registered production processor");controls(*p,*editor,number);factoryStates(*p,*editor,number);rasters(*p,*editor,number,images);honestMeasurements(*p,*editor,number,images);}
    // Snapshot and assertions first; PNG encoding cannot make audio stale.
    for(const auto& item:images)save(item.image,folder.getChildFile(item.name+".png"));
    std::cout<<"PASS "<<checks<<" assertions: registry-factory editors, all30 APVTS bindings, mouse/keyboard/canonical reset, exclusive choices, duplicate06 bypass, full factory states/recall, close callback, "<<images.size()<<" deferred rasters including1x/2x, immutable outer rings, live/stale/reopened/reset/release/reprepare, actual constant-Q axes and detected-neutral clip lamp. Native popup/Windows/device interaction unverified.\n";return 0;
}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
