#pragma once
#include <JuceHeader.h>
#include "HardwareLookAndFeel.h"

// Native document windows keep ownership of editors explicit. Closing hides a
// window; the host destroys it on the message thread before retiring its DSP.
class StudioWindow final : public juce::DocumentWindow {
public:
    explicit StudioWindow(const juce::String& name,juce::Component* content)
        :DocumentWindow(name,juce::Colour(0xff080b10),DocumentWindow::closeButton){
        setLookAndFeel(&look);setUsingNativeTitleBar(false);
        setContentOwned(content,true);setResizable(true,false);
        setResizeLimits(800,500,1920,1200);centreWithSize(1120,680);setVisible(true);
    }
    ~StudioWindow() override {clearContentComponent();setLookAndFeel(nullptr);}
    void closeButtonPressed() override {setVisible(false);}
private: HardwareLookAndFeel look;
};

// Eight rows are fixed-size UI storage. Callbacks execute on the message thread.
class RackView final : public juce::Component {
public:
    struct Row {
        juce::Label name;
        juce::TextButton open{"OPEN"},bypass{"BYP"},up{"UP"},down{"DOWN"},remove{"REMOVE"};
    };
    std::array<Row,8> rows;
    juce::ComboBox tracks,plugins;
    juce::TextButton load{"LOAD TRACK"},insert{"INSERT / REPLACE SELECTED"};
    juce::Label title,status;
    std::function<void(int)> onOpen,onBypass,onUp,onDown,onRemove;
    RackView(){
        setLookAndFeel(&look);
        for(auto* c:std::array<juce::Component*,6>{&tracks,&plugins,&load,&insert,&title,&status})addAndMakeVisible(*c);
        title.setText("HYBRID ELITE MAFIA / PER-TRACK SERIAL INSERT RACK",juce::dontSendNotification);
        for(int i=0;i<8;++i){auto& row=rows[size_t(i)];
            for(auto* c:std::array<juce::Component*,6>{&row.name,&row.open,&row.bypass,&row.up,&row.down,&row.remove})addAndMakeVisible(*c);
            row.open.onClick=[this,i]{if(onOpen)onOpen(i);};row.bypass.onClick=[this,i]{if(onBypass)onBypass(i);};
            row.up.onClick=[this,i]{if(onUp)onUp(i);};row.down.onClick=[this,i]{if(onDown)onDown(i);};row.remove.onClick=[this,i]{if(onRemove)onRemove(i);};
        }
        setSize(1120,640);
    }
    ~RackView() override {setLookAndFeel(nullptr);}
    void paint(juce::Graphics& g) override {
        juce::ColourGradient glass(juce::Colour(0xff242d35),0,0,juce::Colour(0xff06080c),0,float(getHeight()),false);
        g.setGradientFill(glass);g.fillAll();g.setColour(juce::Colour(0xff00d9ea));g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(3),12,2);
        g.setColour(juce::Colour(0x35ffffff));g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(7),9,1);
    }
    void resized() override {
        auto b=getLocalBounds().reduced(16);title.setBounds(b.removeFromTop(28));auto top=b.removeFromTop(40);
        tracks.setBounds(top.removeFromLeft(210).reduced(3));load.setBounds(top.removeFromLeft(140).reduced(3));plugins.setBounds(top.removeFromLeft(290).reduced(3));insert.setBounds(top.reduced(3));
        status.setBounds(b.removeFromBottom(30));const int h=b.getHeight()/8;
        for(auto& r:rows){auto line=b.removeFromTop(h).reduced(3);r.remove.setBounds(line.removeFromRight(80).reduced(2));r.down.setBounds(line.removeFromRight(60).reduced(2));r.up.setBounds(line.removeFromRight(60).reduced(2));r.bypass.setBounds(line.removeFromRight(70).reduced(2));r.open.setBounds(line.removeFromRight(80).reduced(2));r.name.setBounds(line);}
    }
private:HardwareLookAndFeel look;
};
