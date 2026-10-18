#pragma once
#include "../Source/Plugins/OriginalMultibandDynamicsEditor.h"
#include "../Source/Plugins/OriginalMultibandImagerEditor.h"
static void bankOriginalInteractions(const juce::File& evidence){
    for(int module:{2,3}){auto p=hem::PluginRegistry::create(ids[size_t(module)]);p->prepareToPlay(48000,257);std::unique_ptr<juce::AudioProcessorEditor> editor(p->createEditor());
        auto* original=dynamic_cast<hem::OriginalBankEditorBase*>(editor.get());require(original!=nullptr,"original bank editor factory");auto* advanced=dynamic_cast<juce::Button*>(find(*editor,"uiAdvanced"));require(advanced!=nullptr,"bank advanced navigation");
        auto navigate=[&]{const auto before=advanced->getButtonText();advanced->triggerClick();for(int attempt=0;attempt<100&&advanced->getButtonText()==before;++attempt)pumpBandMessages(5);
            require(advanced->getButtonText()!=before,"bank posted navigation completed");};int checked=0;editor->setSize(1000,563);
        for(const auto& spec:p->parameterSpecs()){const juce::String id(spec.id);const bool legacy=id.startsWith("mid")||id=="legacyMidOn";set(*p,"topology",legacy?0.f:1.f);
            for(const auto& s:p->parameterSpecs()){const juce::String key(s.id);if(key.endsWith("On"))set(*p,key,1);if(key.endsWith("Ratio"))set(*p,key,4);if(key.endsWith("Range"))set(*p,key,12);}
            if(module==3)set(*p,"lowMono",0);original->refresh();auto* c=find(*editor,id);require(c!=nullptr,"bank every physical control exists");if(!c->isVisible())navigate();original->refresh();
            if(!c->isVisible()||!c->isEnabled())std::cerr<<"bank availability module="<<ids[size_t(module)]<<" id="<<id<<" visible="<<c->isVisible()<<" enabled="<<c->isEnabled()<<" topology="<<p->state.getRawParameterValue("topology")->load()<<'\n';
            require(c->isVisible()&&c->isEnabled(),"bank control visible/active in matching topology");require(c->getName()==spec.name,"bank accessible identity");
            if(spec.toggle){auto* b=dynamic_cast<juce::ToggleButton*>(c);require(b!=nullptr,"bank toggle");for(bool on:{false,true}){b->setToggleState(on,juce::sendNotificationSync);require(p->state.getRawParameterValue(id)->load()==float(on),"bank toggle binding");}}
            else{auto* s=dynamic_cast<hem::ui::ReferenceSlider*>(c);require(s!=nullptr,"bank physical slider");for(float v:{spec.minimum,spec.maximum,spec.initial}){s->setValue(v,juce::sendNotificationSync);require(std::abs(p->state.getRawParameterValue(id)->load()-v)<=std::max(1e-4f,spec.step*.51f),"bank physical value binding");auditBandControl(*s);}
                if(!s->choices.isEmpty()){for(int value=0;value<s->choices.size();++value){s->onChoice(value);require(p->state.getRawParameterValue(id)->load()==value,"bank topology choice");auditBandControl(*s);}}
                else{s->showTextBox();juce::TextEditor* text=nullptr;for(auto* child:s->getChildren())if(auto* l=dynamic_cast<juce::Label*>(child))if(l->getCurrentTextEditor())text=l->getCurrentTextEditor();require(text!=nullptr,"bank physical text editor");
                    text->setText(juce::String(spec.maximum,4));s->hideTextBox(false);require(std::abs(p->state.getRawParameterValue(id)->load()-spec.maximum)<=std::max(1e-4f,spec.step*.51f),"bank typed physical value");}
                s->setValue(spec.minimum,juce::sendNotificationSync);require(s->keyPressed(juce::KeyPress(juce::KeyPress::rightKey))&&s->getValue()>spec.minimum,"bank keyboard");
                if(s->choices.isEmpty()){s->setValue(spec.minimum+(spec.maximum-spec.minimum)*.5,juce::sendNotificationSync);const auto before=s->getValue();const auto now=juce::Time::getCurrentTime();const auto start=s->dialBounds.getCentre().toFloat();
                    auto mouse=[&](juce::Point<float> point,int flags,bool drag){return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(),point,juce::ModifierKeys(flags),1,0,0,0,0,s,s,now,start,now,1,drag);};
                    s->mouseDown(mouse(start,juce::ModifierKeys::leftButtonModifier,false));s->mouseDrag(mouse(start+juce::Point<float>(0,-35),juce::ModifierKeys::leftButtonModifier,true));s->mouseUp(mouse(start,0,true));require(s->getValue()>before,"bank mouse drag");
                    s->mouseDoubleClick(mouse(start,0,false));require(std::abs(s->getValue()-spec.initial)<.001,"bank double click reset");}
            }++checked;
        }
        require(checked==(module==2?52:27),"bank complete inventory tested");auto* reset=dynamic_cast<juce::Button*>(find(*editor,"uiResetAll"));require(reset!=nullptr,"bank reset command");reset->triggerClick();pumpBandMessages(1);
        for(const auto& spec:p->parameterSpecs())require(std::abs(p->state.getRawParameterValue(spec.id)->load()-spec.initial)<1e-4f,"bank reset values");
        for(int topology:{1,0}){set(*p,"topology",float(topology));activate(*p);original->refresh();if(advanced->getToggleState())navigate();
            if(module==3){set(*p,"lowWidth",37);set(*p,"lowMono",1);original->refresh();require(!find(*editor,"lowWidth")->isEnabled(),"original imager low mono activity");set(*p,"lowMono",0);original->refresh();require(find(*editor,"lowWidth")->isEnabled()&&p->state.getRawParameterValue("lowWidth")->load()==37,"original imager saved width");}
            set(*p,"xMid",18000);juce::AudioBuffer<float> signal(2,257);juce::MidiBuffer midi;for(int block=0;block<80;++block){fill(signal,block*257);allocationProbe::enabled=true;p->processBlock(signal,midi);allocationProbe::enabled=false;
                if(block%8==7){pumpBandMessages(45);juce::Image sampled(juce::Image::ARGB,editor->getWidth(),editor->getHeight(),true);juce::Graphics g(sampled);editor->paintEntireComponent(g,true);}}
            require(p->state.getRawParameterValue("xMid")->load()==18000,"constrained requested crossover must remain saved");
            pumpBandMessages(50);
            for(int panel=0;panel<2;++panel){if(panel)navigate();
                for(auto size:{juce::Point<int>(1000,563),juce::Point<int>(1672,941),juce::Point<int>(1920,1081)})for(int scale:{1,2}){editor->setSize(size.x,size.y);
                    for(int block=0;block<8;++block){fill(signal,block*257);allocationProbe::enabled=true;p->processBlock(signal,midi);allocationProbe::enabled=false;}pumpBandMessages(45);
                    for(const auto& spec:p->parameterSpecs()){auto* c=find(*editor,spec.id);require(c&&editor->getLocalBounds().contains(c->getBounds())&&c->getWidth()>0&&c->getHeight()>0,"bank all control raster bounds");auditBandHit(*editor,*c);}
                    juce::Image pixels(juce::Image::ARGB,size.x*scale,size.y*scale,true);{juce::Graphics g(pixels);g.addTransform(juce::AffineTransform::scale(float(scale)));editor->paintEntireComponent(g,true);}
                    juce::MemoryOutputStream png;require(juce::PNGImageFormat().writeImageToStream(pixels,png),"bank PNG encode");require(evidence.getChildFile("original-"+juce::String(ids[size_t(module)])+"-"+juce::String(topology)+"-"+juce::String(panel)+"-"+juce::String(size.x)+"-"+juce::String(scale)+"x.png").replaceWithData(png.getData(),png.getDataSize()),"bank PNG write");
                }
            }
        }
        std::cout<<"PASS original-layout "<<ids[size_t(module)]<<": "<<checked<<" bindings; physical edits, mouse/keyboard/reset, modern/legacy navigation, actual audio and raster bounds\n";
    }
}

