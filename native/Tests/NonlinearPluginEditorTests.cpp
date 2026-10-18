#include "../Source/Plugins/PluginRegistry.h"
#include "../Source/Plugins/NonlinearEditor.h"
#include "../Source/Plugins/FinishingProcessors.h"
#include <iostream>
#include <stdexcept>
static void require(bool condition,const char* text){if(!condition)throw std::runtime_error(text);}
static void set(hem::ModuleProcessor& p,const char* id,float value){auto* q=p.state.getParameter(id);q->setValueNotifyingHost(q->convertTo0to1(value));}
static juce::Image render(juce::Component& c,const juce::File& dir,const juce::String& name){auto image=c.createComponentSnapshot(c.getLocalBounds());juce::FileOutputStream stream(dir.getChildFile(name+".png"));require(stream.openedOk(),"Cannot write raster");require(stream.setPosition(0)&&stream.truncate().wasOk(),"Cannot replace raster");juce::PNGImageFormat png;require(png.writeImageToStream(image,stream),"Cannot encode raster");return image;}
int main(int argc,char** argv){juce::ScopedJuceInitialiser_GUI gui;try{
    const juce::File out(argc>1?argv[1]:"/tmp/nonlinear-ui-renders");require(out.createDirectory().wasOk(),"Cannot create raster directory");
    const char* ids[]{"parallel-distortion","exciter","tape"};
    for(int kind=0;kind<3;++kind){auto p=hem::PluginRegistry::create(ids[kind]);require(bool(p),"Missing registered processor");p->prepareToPlay(48000,512);
        std::unique_ptr<juce::AudioProcessorEditor> ed(p->createEditor());require(dynamic_cast<hem::OriginalNonlinearEditor*>(ed.get()),"Wrong original editor");require(ed->getWidth()<=1280&&ed->getHeight()<=720,"Editor exceeds host default");
        const juce::String name(ids[kind]);
        for(auto size:{juce::Point<int>(1000,563),juce::Point<int>(1280,720),juce::Point<int>(1672,941),juce::Point<int>(3344,1882)}){
            ed->setSize(size.x,size.y);for(const auto& spec:p->parameterSpecs()){auto* c=ed->findChildWithID(spec.id);require(c,"Missing bound control");require(ed->getLocalBounds().contains(c->getBounds()),"Control outside editor");require(c->getWidth()>15&&c->getHeight()>15,"Unreadable control hit area");}
            render(*ed,out,name+"-idle-"+juce::String(size.x));
        }
        ed->setSize(1672,941);
        const auto idle=ed->createComponentSnapshot(ed->getLocalBounds());
        if(kind==2)require(idle.getPixelAt(1378,377)==juce::Colour(0xff02090e),"Tape right-edge baked waveform residue");
        if(kind==1)require(idle.getPixelAt(1438,340)==juce::Colour(0xff02090e),"Exciter right-edge baked spectrum residue");
        if(kind==0){
            require(!ed->isVisible()&&ed->getComponentAt(1570,640)==nullptr,"Hidden-editor JUCE hit-test premise changed");
            ed->setVisible(true);
            for(const auto& pair:{std::pair<const char*,int>{"type",640},{"quality",766}}){auto* choice=ed->findChildWithID(pair.first);auto* hit=ed->getComponentAt(1570,pair.second);require(hit==choice||choice->isParentOf(hit),"Original arrow hit intercepted");}
            const auto image=ed->createComponentSnapshot(ed->getLocalBounds());const auto reference=juce::ImageCache::getFromMemory(ApprovedArt::approved_26_png,ApprovedArt::approved_26_pngSize);
            for(int top:{618,744})for(int y=top;y<top+44;++y)for(int x=1545;x<1592;++x)require(image.getPixelAt(x,y)==reference.getPixelAt(x,y),"Original single-arrow pixels changed");
        }
        for(const auto& spec:p->parameterSpecs()){
            auto* c=ed->findChildWithID(spec.id);
            if(auto* slider=dynamic_cast<juce::Slider*>(c)){
                for(float v:{spec.minimum,spec.initial,spec.maximum}){slider->setValue(v,juce::sendNotificationSync);require(std::abs(p->state.getRawParameterValue(spec.id)->load()-v)<.02f,"Slider did not write physical value");}
                slider->setValue(spec.initial,juce::sendNotificationSync);
                if(spec.minimum<0&&spec.maximum>0)require(std::abs(slider->valueToProportionOfLength(0)-.5)<.001,"Bipolar zero not centered");
            }else if(auto* choice=dynamic_cast<juce::ComboBox*>(c)){
                for(int id=1;id<=choice->getNumItems();++id){choice->setSelectedId(id,juce::sendNotificationSync);require(p->state.getRawParameterValue(spec.id)->load()==id-1,"Discrete selector disconnected");}
                choice->setSelectedId(int(spec.initial)+1,juce::sendNotificationSync);
            }else if(auto* button=dynamic_cast<juce::ToggleButton*>(c)){
                for(bool on:{true,false}){button->setToggleState(on,juce::sendNotificationSync);require(p->state.getRawParameterValue(spec.id)->load()==int(on),"Toggle disconnected");}button->setToggleState(spec.initial>.5f,juce::sendNotificationSync);
            }else require(false,"Unexpected control type");
        }
        juce::MidiBuffer midi;juce::AudioBuffer<float> buffer(2,512);
        for(int block=0;block<50;++block){for(int sample=0;sample<512;++sample){buffer.setSample(0,sample,.5f*std::sin(float((block*512+sample)*2*hem::pi*1000/48000)));buffer.setSample(1,sample,.05f*std::sin(float((block*512+sample)*2*hem::pi*1000/48000)));}p->processBlock(buffer,midi);}
        hem::PluginTelemetry::Levels levels;require(p->pluginTelemetry()&&p->pluginTelemetry()->read(levels)&&levels.valid,"Shared stereo telemetry missing");require(levels.peak[0]>levels.peak[1]*9,"Independent stereo input peaks missing");
        const auto live=render(*ed,out,name+"-live");
        const juce::Rectangle<int> plot=kind==0?juce::Rectangle<int>(300,259,1090,242):kind==1?juce::Rectangle<int>(260,199,1179,160):juce::Rectangle<int>(341,265,1038,223);
        for(float endpoint:{2.f,-2.f}){
            for(int block=0;block<4;++block){for(int sample=0;sample<512;++sample){buffer.setSample(0,sample,endpoint);buffer.setSample(1,sample,endpoint*.1f);}p->processBlock(buffer,midi);}
            const auto extreme=render(*ed,out,name+(endpoint>0?"-positive-endpoint":"-negative-endpoint"));
            for(int y=plot.getY();y<plot.getBottom();++y)if(y<plot.getY()+20||y>=plot.getBottom()-20)
                for(int x=plot.getX();x<plot.getRight();++x)require(extreme.getPixelAt(x,y)==live.getPixelAt(x,y),"Full-scale trace crossed header/footer caption region");
        }
        juce::Thread::sleep(800);const auto stale=render(*ed,out,name+"-stale");
        const int mx=kind==0?105:kind==1?86:107,my=kind==0?245:kind==1?210:235;
        int changed=0;for(int y=my;y<my+95;++y)for(int x=mx;x<mx+20;++x)changed+=live.getPixelAt(x,y)!=stale.getPixelAt(x,y);
        require(changed>100,"Stale meter did not darken");
        std::unique_ptr<juce::AudioProcessorEditor> reopened(p->createEditor());reopened->setSize(1672,941);const auto reopenedImage=render(*reopened,out,name+"-reopened-stale");
        for(int y=my;y<my+95;++y)for(int x=mx;x<mx+20;++x)require(reopenedImage.getPixelAt(x,y)==stale.getPixelAt(x,y),"Reopened editor revives frozen publication");
        p->releaseResources();const auto released=render(*ed,out,name+"-released");require(!p->processingReady()&&!p->meterSnapshot().isPrepared,"Released processor claims ready");
        p->prepareToPlay(48000,512);const auto reprepared=render(*ed,out,name+"-reprepared");require(!p->meterSnapshot().hasProcessedAudio,"Reprepared processor shows old audio");
        for(int y=my;y<my+95;++y)for(int x=mx;x<mx+20;++x){require(released.getPixelAt(x,y)==stale.getPixelAt(x,y),"Released meter residue");require(reprepared.getPixelAt(x,y)==stale.getPixelAt(x,y),"Reprepared meter residue");}
        std::cout<<"PASS original UI sizing, physical controls, stereo telemetry, stale/release/reprepare "<<name<<"\n";
    }return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
