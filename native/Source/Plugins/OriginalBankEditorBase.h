#pragma once
#include "OriginalBandEditorBase.h"
namespace hem {
class OriginalBankEditorBase:public OriginalBandEditorBase {
public:
    OriginalBankEditorBase(BandModuleProcessor& p,const void* data,int bytes):OriginalBandEditorBase(p,data,bytes){
        advancedButton.setComponentID("uiAdvanced");advancedButton.setButtonText("ADVANCED");advancedButton.setTooltip("Show additional controls within this hardware faceplate");advancedButton.setWantsKeyboardFocus(true);addChildComponent(advancedButton);
        advancedButton.onClick=[this]{advanced=!advanced;resized();repaint();};topbar.advanced=[this]{advancedButton.onClick();};
    }
    void refresh() override {const bool next=processor.state.getRawParameterValue("topology")->load()>.5f;if(next!=modern){modern=next;resized();}OriginalBandEditorBase::refresh();}
protected:
    size_t index(const juce::String& id) const {for(size_t i=0;i<controls.size();++i)if(controls[i]->id==id)return i;jassertfalse;return 0;}
    void beginLayout(){layoutTopbar();for(auto& c:controls){auto& component=c->toggle?static_cast<juce::Component&>(c->button):static_cast<juce::Component&>(c->slider);component.setBounds(scaled({400,400,80,35}));component.setVisible(false);}
        advancedButton.setButtonText(advanced?"MAIN VIEW":"ADVANCED");advancedButton.setToggleState(advanced,juce::dontSendNotification);advancedButton.setBounds(scaled(advancedBounds));resetAll.setBounds(scaled({360,180,150,45}));}
    void advancedLayout(const std::vector<juce::String>& ids){advancedIds=ids;if(!advanced)return;
        const bool dynamics=processor.bandKind==bands::Kind::Dynamics;
        choice(index("topology"),dynamics?juce::Rectangle<float>(1110,748,274,28):juce::Rectangle<float>(1110,170,269,35));
        if(!dynamics){button(index("lowMono"),{49,589,149,38});return;}
        const float cell=977.f/float(ids.size()-1);
        for(size_t n=1;n<ids.size();++n){const float x=418+cell*float(n-1);knob(index(ids[n]),{x+4,805,cell-8,77},{(cell-8-44)/2,0,44,44},{0,48,cell-8,28},dialCrop);}
    }
    static juce::String readable(const juce::String& id){juce::String result;for(int c=0;c<id.length();++c){if(c>0&&juce::CharacterFunctions::isUpperCase(id[c]))result+=" ";result+=juce::String::charToString(id[c]);}return result.toUpperCase();}
    void advancedPaint(juce::Graphics& g){if(!advanced)return;
        if(processor.bandKind!=bands::Kind::Dynamics)return;
        // Only the original history glass is exchanged for additional controls.
        // Main hardware, row controls, meters, crown and ghost remain exposed.
        g.setGradientFill(juce::ColourGradient(juce::Colour(0xff132029),418,779,juce::Colour(0xff030a0f),418,909,false));g.fillRect(juce::Rectangle<float>(418,776,977,133));
        g.setColour(juce::Colour(0xff48a7b8));g.drawRect(juce::Rectangle<float>(418,776,977,133),1);
        g.setColour(juce::Colour(0xff030a0f));g.fillRect(juce::Rectangle<float>(530,748,800,28));g.setColour(juce::Colours::whitesmoke);
        text(g,"ADVANCED / KNEE AND MAX REDUCTION",{530,748,550,28},juce::Justification::centred,20,true);
        const float cell=977.f/float(advancedIds.size()-1);
        for(size_t n=1;n<advancedIds.size();++n)text(g,readable(advancedIds[n]),{422+cell*float(n-1),779,cell-8,25});
        text(g,"VALUES IN dB / LEGACY MID RETAINS ITS SAVED THREE-BAND SETTINGS",{426,884,960,25});
    }
    void crossoverStatus(juce::Graphics& g,juce::Rectangle<float>){
        g.setColour(juce::Colours::whitesmoke);const bool pending=measured()&&(frame.topology!=int(modern)||frame.transition);
        const auto state=pending?juce::String("TRANSITION"):stale?juce::String("STALE"):modern?juce::String("FOUR BAND"):juce::String("LEGACY 3");
        if(processor.bandKind==bands::Kind::Dynamics){
            text(g,state,{72,204,184,25});text(g,"APPLIED Hz",{72,234,184,25});
            const float xs[]{403,774,1145},ws[]{150,157,161};for(int i=0;i<3;++i){const auto value=measured()&&(i!=1||frame.topology)?juce::String(frame.crossovers[size_t(i)],0):juce::String("--");
                g.setColour(juce::Colour(0xff030b10));g.fillRect(juce::Rectangle<float>(xs[i],236,ws[i],25));g.setColour(juce::Colours::whitesmoke);text(g,value,{xs[i],236,ws[i],25});}return;
        }
        juce::String label=state+" | APPLIED ";if(measured())label+=juce::String(frame.crossovers[0],0)+" / "+(frame.topology?juce::String(frame.crossovers[1],0)+" / ":juce::String())+juce::String(frame.crossovers[2],0)+" Hz";else label+="--";
        g.setColour(juce::Colour(0xff030b10));g.fillRect(juce::Rectangle<float>(309,397,1063,24));g.setColour(juce::Colours::whitesmoke);text(g,label,{315,397,1050,24},juce::Justification::centredLeft);
    }
    void labelLegacyMiddle(juce::Graphics& g,juce::Rectangle<float> first,juce::Rectangle<float> absent){if(modern)return;
        g.setColour(juce::Colour(0xff030b10));g.fillRect(first);g.fillRect(absent);g.setColour(juce::Colours::whitesmoke);
        text(g,"LEGACY MID",first,juce::Justification::centred,20,true);text(g,"UNUSED / LEGACY MODE",absent.reduced(8));}
    bool bandMeasured() const {return measured()&&frame.topology==int(modern)&&!frame.transition;}
    bool advanced=false,modern=true;juce::TextButton advancedButton;std::vector<juce::String> advancedIds;
    juce::Rectangle<float> advancedBounds{80,65,165,50};juce::Rectangle<int> dialCrop;
};
}
