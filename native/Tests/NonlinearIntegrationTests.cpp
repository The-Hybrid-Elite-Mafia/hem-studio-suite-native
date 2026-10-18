#include "../Source/Plugins/PluginRegistry.h"
#include "../Source/Plugins/NonlinearEditor.h"
#include "../Source/InsertChain.h"
#include <iostream>
#include <stdexcept>
void require(bool b,const char* m){if(!b)throw std::runtime_error(m);}
void set(hem::ModuleProcessor& p,const char* id,float value){auto* parameter=p.state.getParameter(id);parameter->setValueNotifyingHost(parameter->convertTo0to1(value));}
int countId(const juce::Component& c,const juce::String& id){int n=c.getComponentID()==id?1:0;for(int i=0;i<c.getNumChildComponents();++i)n+=countId(*c.getChildComponent(i),id);return n;}
void strictStateFixtures(){
    for(const char* id:{"parallel-distortion","exciter"}){
        auto p=hem::PluginRegistry::create(id);set(*p,"output",-6);const auto original=p->state.copyState();
        const auto make=[&](const char* parameter,const juce::var& value){juce::ValueTree root(p->state.state.getType()),child("PARAM");child.setProperty("id",parameter,nullptr);child.setProperty("value",value,nullptr);root.addChild(child,-1,nullptr);return root;};
        const auto reject=[&](const juce::ValueTree& root){juce::MemoryBlock block;juce::AudioProcessor::copyXmlToBinary(*root.createXml(),block);p->setStateInformation(block.getData(),int(block.getSize()));require(p->state.copyState().isEquivalentTo(original),"Malformed state changed live parameters");};
        for(const auto* text:{"nan","inf","-inf","garbage","1junk","","0x1p0","1e9999","6.1","-24.1","0.03"})reject(make("output",text));
        reject(make("unrecognized",0));auto duplicate=make("output",0);duplicate.addChild(duplicate.getChild(0).createCopy(),-1,nullptr);reject(duplicate);
        auto nested=make("output",0);nested.getChild(0).addChild(juce::ValueTree("PARAM"),-1,nullptr);reject(nested);
        auto extra=make("output",0);extra.getChild(0).setProperty("extra",1,nullptr);reject(extra);
        auto wrong=make("output",0);wrong.setProperty("schemaUnknown",1,nullptr);reject(wrong);
        const char* discrete=juce::String(id)=="exciter"?"lowOn":"quality";reject(make(discrete,.5));
        // Atomic rejection: a valid field must not apply before an invalid one.
        auto transaction=make("output",0);auto bad=make(discrete,2).getChild(0).createCopy();transaction.addChild(bad,-1,nullptr);reject(transaction);
        const char invalid[]{1,2,3,4};p->setStateInformation(invalid,4);require(p->state.copyState().isEquivalentTo(original),"Malformed binary state applied");
        juce::MemoryBlock valid;p->getStateInformation(valid);p->setStateInformation(valid.getData(),int(valid.getSize()));require(p->state.copyState().isEquivalentTo(original),"Valid state rejected or altered");
    }
}
int main(int argc,char** argv){juce::ScopedJuceInitialiser_GUI init;try{
    strictStateFixtures();
    require(hem::PluginRegistry::entries().size()==35,"Catalog size incorrect");
    const char* ids[]{"parallel-distortion","exciter","tape"};const size_t counts[]{7,16,5};
    for(size_t i=0;i<3;++i){auto p=hem::PluginRegistry::create(ids[i]);require(bool(p),"Missing processor");require(hem::PluginRegistry::idFor(*p)==ids[i],"Stable ID changed");require(p->parameterSpecs().size()==counts[i],"Control count mismatch");p->prepareToPlay(48000,257);require(p->getLatencySamples()==16,"Latency mismatch");
        std::unique_ptr<juce::AudioProcessorEditor> ed(p->createEditor());require(dynamic_cast<hem::OriginalNonlinearEditor*>(ed.get())!=nullptr,"Original-art editor missing");
        if(argc==2){juce::File dir(argv[1]);require(dir.createDirectory().wasOk(),"Cannot create visual QA directory");ed->setSize(1672,941);const auto image=ed->createComponentSnapshot(ed->getLocalBounds());juce::FileOutputStream output(dir.getChildFile(juce::String(ids[i])+"-neutral.png"));require(output.openedOk(),"Cannot open visual QA image");juce::PNGImageFormat png;require(png.writeImageToStream(image,output),"Cannot save visual QA image");}
        for(const auto& spec:p->parameterSpecs()){require(countId(*ed,spec.id)==1,"Duplicate or missing interactive control");auto* c=ed->findChildWithID(spec.id);require(c&&ed->getLocalBounds().contains(c->getBounds()),"Control missing/outside artwork");if(auto* knob=dynamic_cast<juce::Slider*>(c)){const double v=spec.maximum;knob->setValue(v,juce::sendNotificationSync);require(std::abs(p->state.getRawParameterValue(spec.id)->load()-v)<.01,"Knob disconnected");}
            else if(auto* choice=dynamic_cast<juce::ComboBox*>(c)){choice->setSelectedId(1,juce::sendNotificationSync);require(p->state.getRawParameterValue(spec.id)->load()==0,"Selector disconnected");}else if(auto* button=dynamic_cast<juce::ToggleButton*>(c)){button->setToggleState(true,juce::sendNotificationSync);require(p->state.getRawParameterValue(spec.id)->load()==1,"Button disconnected");}}
        if(i==0){ed->setVisible(true);ed->setSize(1672,941);auto* type=ed->findChildWithID("type");auto* quality=ed->findChildWithID("quality");
            require(type->getBounds().contains(1570,640)&&quality->getBounds().contains(1570,766),"Original arrow lies outside selector hit area");
            require(type->getBounds()==juce::Rectangle<int>(1295,618,297,44)&&quality->getBounds()==juce::Rectangle<int>(1295,744,297,44),"Selector mask/control region mismatch");
            require(ed->getComponentAt(1570,640)==type||type->isParentOf(ed->getComponentAt(1570,640)),"Arrow hit intercepted by a second control");
            const auto raster=ed->createComponentSnapshot(ed->getLocalBounds()),reference=juce::ImageCache::getFromMemory(ApprovedArt::approved_26_png,ApprovedArt::approved_26_pngSize);
            for(const int top:{618,744}){for(int y=top;y<top+44;++y)for(int x=1545;x<1592;++x)require(raster.getPixelAt(x,y)==reference.getPixelAt(x,y),"Live arrow differs from original single arrow cell");
                for(int y=top+8;y<top+36;++y)for(int x=1510;x<1540;++x)require(raster.getPixelAt(x,y)==juce::Colour(0xff03090c),"Duplicate baked/dynamic arrow survives in text field");}}
        juce::MemoryBlock state;p->getStateInformation(state);auto copy=hem::PluginRegistry::create(ids[i]);copy->setStateInformation(state.getData(),int(state.getSize()));for(const auto& spec:p->parameterSpecs())require(copy->state.getRawParameterValue(spec.id)->load()==p->state.getRawParameterValue(spec.id)->load(),"State roundtrip mismatch");
        juce::AudioBuffer<float> b(2,257);juce::MidiBuffer midi;for(int j=0;j<257;++j)b.setSample(0,j,b.getWritePointer(1)[j]=.1f*float(std::sin(j*.03)));p->processBlock(b,midi);require(p->meterSnapshot().hasProcessedAudio,"Meter not fed");
        std::cout<<"PASS original art/bindings/state/telemetry "<<ids[i]<<"\n";
    }
    hem::ParallelProcessor p;set(p,"blend",100);juce::ValueTree partial("PARALLEL_DISTORTION");juce::ValueTree drive("PARAM");drive.setProperty("id","drive",nullptr);drive.setProperty("value",6.f,nullptr);partial.addChild(drive,-1,nullptr);juce::MemoryBlock data;juce::AudioProcessor::copyXmlToBinary(*partial.createXml(),data);p.setStateInformation(data.getData(),int(data.getSize()));require(p.state.getRawParameterValue("blend")->load()==0,"Missing field did not restore neutral");require(p.state.getRawParameterValue("drive")->load()==6,"Recognized state field lost");
    const auto tape=hem::PluginRegistry::create("tape");require(tape->state.state.hasType("TAPE"),"Legacy TAPE state type changed");
    for(const auto* id:ids){hem::InsertChain chain;chain.prepare(48000,257);chain.installInitial(0,chain.stageNode(hem::PluginRegistry::create(id),48000,257,chain.slotLatency()),false);
        std::array<float,4096> l{},r{};l[0]=r[0]=.5f;chain.process(l.data(),r.data(),4096);require(chain.latencySamples()==2304,"Rack latency contract changed");
        for(size_t i=0;i<l.size();++i)require(std::abs(l[i]-(i==2304?.5f:0.f))<3.e-6f&&std::abs(r[i]-(i==2304?.5f:0.f))<3.e-6f,"New processor not aligned to rack dry compensation");}
    std::cout<<"PASS partial-state migration and legacy tape ID\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
