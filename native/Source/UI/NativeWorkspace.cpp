#include "NativeWorkspace.h"
#include "../HardwareLookAndFeel.h"
#include <algorithm>
#include <cmath>

namespace hem {
namespace {
const juce::Colour cyan(0xff71ebf5), amber(0xffffbb60), ink(0xff071018), dim(0xff8da8b3), edge(0xff25434e);
void text(juce::Graphics& g, const juce::String& value, juce::Rectangle<int> bounds,
          float size = 13.0f, juce::Colour colour = cyan, int lines = 1,
          juce::Justification alignment = juce::Justification::centredLeft) {
    g.setColour(colour); g.setFont(juce::Font(juce::FontOptions(size)));
    g.drawFittedText(value, bounds, alignment, lines, 0.85f);
}
juce::String db(float value) { return value <= AudioMeters::silenceFloorDb ? "-inf" : juce::String(value, 1); }
void panel(juce::Graphics& g, juce::Rectangle<int> bounds, bool selected = false) {
    auto r = bounds.toFloat().reduced(1);
    g.setGradientFill(juce::ColourGradient(juce::Colour(0xff17242d),r.getX(),r.getY(),ink,r.getX(),r.getBottom(),false));
    g.fillRoundedRectangle(r,5); g.setColour(selected ? amber : edge); g.drawRoundedRectangle(r,5,1);
}
void identify(juce::Component& c, const juce::String& id, const juce::String& name) { c.setComponentID(id); c.setName(name); }
void setupButton(juce::TextButton& b, const juce::String& id, const juce::String& title) {
    identify(b,id,title); b.setButtonText(title); b.setWantsKeyboardFocus(true);
}
class StereoMeter final : public juce::Component, public juce::SettableTooltipClient {
public:
    AudioMeters::Snapshot value;
    std::function<void()> onReset;
    StereoMeter(){setWantsKeyboardFocus(true);setTooltip("Actual stereo sample peak / RMS / 1 s hold. Click, Enter or Space to reset clip latches; reset is consumed by the next audio block.");}
    void mouseDown(const juce::MouseEvent&) override {if(onReset)onReset();}
    bool keyPressed(const juce::KeyPress& key) override {if(key==juce::KeyPress::returnKey||key==juce::KeyPress::spaceKey){if(onReset)onReset();return true;}return false;}
    void paint(juce::Graphics& g) override {
        auto plot = getLocalBounds().reduced(1); plot.removeFromTop(13); plot.removeFromBottom(36);
        if (plot.getHeight() < 1 || plot.getWidth() < 1) return;
        text(g,"dBFS",{0,0,getWidth(),13},10,dim,1,juce::Justification::centred);
        const int gutter = 21, laneWidth = juce::jmax(3,(plot.getWidth()-gutter-4)/2);
        auto y = [&](float v){return float(plot.getBottom())-float(plot.getHeight())*juce::jlimit(0.0f,1.0f,(v+60)/60);};
        for(float v : {0.f,-12.f,-24.f,-48.f,-60.f}) {
            g.setColour(edge); g.drawHorizontalLine(int(y(v)),float(gutter),float(getWidth()));
            text(g,juce::String(int(v)),{0,int(y(v))-6,gutter-2,12},9,dim,1,juce::Justification::centredRight);
        }
        int x = gutter;int channelIndex=0;
        for(const auto& c : {value.left,value.right}) {
            auto lane = juce::Rectangle<float>(float(x),float(plot.getY()),float(laneWidth),float(plot.getHeight()));
            g.setColour(juce::Colour(0xff020609));g.fillRect(lane);
            if(value.ready) {
                g.setColour(cyan.withAlpha(0.26f));g.fillRect(lane.withTop(y(c.peakDb)));
                g.setColour(juce::Colour(0xff3dc4aa));g.fillRect(lane.reduced(2,0).withTop(y(c.rmsDb)));
                if(c.peakDb>-60){g.setColour(cyan);g.drawLine(lane.getX(),y(c.peakDb),lane.getRight(),y(c.peakDb),2);}
                if(c.heldPeakDb>-60){g.setColour(amber);g.drawLine(lane.getX(),y(c.heldPeakDb),lane.getRight(),y(c.heldPeakDb),1);}
            }
            g.setColour(c.clipped?juce::Colour(0xffff5a48):edge);g.fillRect(x,plot.getY()-3,laneWidth,2);
            text(g,channelIndex++==0?"L":"R",{x,plot.getBottom()+2,laneWidth,13},10,cyan,1,juce::Justification::centred);
            x += laneWidth+3;
        }
        text(g,value.ready ? db(juce::jmax(value.left.peakDb,value.right.peakDb)) : "NO AUDIO",
             {0,getHeight()-17,getWidth(),16},10,value.ready?cyan:dim,1,juce::Justification::centred);
        if(hasKeyboardFocus(true)){g.setColour(cyan);g.drawRect(getLocalBounds(),1);}
    }
};
}

struct NativeWorkspace::Impl {
    struct PluginPage;
    struct MixerPage;
    struct VaultPage;
    struct RoutingPage;
    NativeWorkspace& owner;
    Callbacks cb;
    HardwareLookAndFeel look;
    Page active = Page::plugin;
    std::unique_ptr<PluginPage> plugin;
    std::unique_ptr<MixerPage> mixer;
    std::unique_ptr<VaultPage> vault;
    std::unique_ptr<RoutingPage> routing;
    explicit Impl(NativeWorkspace&,Callbacks);
    ~Impl();
    TrackBank& bank() const {return cb.bank();}
    int track() const {return juce::jlimit(0,12,cb.getSelectedTrack ? cb.getSelectedTrack():0);}
    int slot() const {return juce::jlimit(0,7,cb.getSelectedSlot ? cb.getSelectedSlot():0);}
    void chooseTrack(int i) {if(cb.setSelectedTrack)cb.setSelectedTrack(juce::jlimit(0,12,i));owner.refresh();}
    void openSlot(int i) {if(cb.setSelectedSlot)cb.setSelectedSlot(i);if(cb.onOpenSlot)cb.onOpenSlot(i);owner.refresh();}
};

struct NativeWorkspace::Impl::PluginPage final : juce::Component {
    struct Divider final : juce::Component {
        PluginPage& page;
        explicit Divider(PluginPage& p):page(p){identify(*this,"plugin-divider","Drag to resize stem and plugin panes");setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);}
        void paint(juce::Graphics& g) override {g.setColour(edge);g.fillRoundedRectangle(getLocalBounds().toFloat().reduced(3,0),2);g.setColour(cyan);for(int dy:{-9,0,9})g.fillEllipse(float(getWidth()/2-1),float(getHeight()/2+dy),2,2);}
        void mouseDrag(const juce::MouseEvent& e) override {page.host.owner.setSplitProportion(float(e.getEventRelativeTo(&page).x)/float(juce::jmax(1,page.getWidth())));}
    };
    Impl& host;
    juce::Component left, editorCanvas;
    juce::Viewport leftView, editorView;
    juce::ComboBox trackChoice;
    std::array<juce::TextButton,8> slots, bypass;
    juce::TextButton expand, browse;
    Divider divider{*this};
    std::unique_ptr<juce::AudioProcessorEditor> editor;
    juce::Label leftTitle, editorTitle, editorHint, summary;
    StereoMeter meter;
    float split=.32f;
    bool expanded=false;juce::Point<int> observedEditorSize;
    explicit PluginPage(Impl& h):host(h) {
        identify(*this,"workspace-plugin","Selected stem and plugin editor");
        leftView.setViewedComponent(&left,false);leftView.setScrollBarsShown(true,true);
        editorView.setViewedComponent(&editorCanvas,false);editorView.setScrollBarsShown(true,true);
        identify(leftView,"plugin-stem-viewport","Selected stem and eight insert slots");
        identify(editorView,"plugin-editor-viewport","Native plugin editor with scrolling");
        for(auto* c : std::initializer_list<juce::Component*>{&leftView,&editorView,&divider,&expand,&browse,&editorTitle})addAndMakeVisible(c);
        for(auto* c : std::initializer_list<juce::Component*>{&leftTitle,&trackChoice,&summary,&meter})left.addAndMakeVisible(c);
        editorCanvas.addAndMakeVisible(editorHint);
        leftTitle.setText("SELECTED STEM / 8 SERIAL INSERTS",juce::dontSendNotification);
        leftTitle.setFont(juce::Font(juce::FontOptions(13).withStyle("Bold")));
        identify(trackChoice,"plugin-track-select","Selected audio track");
        identify(meter,"plugin-signal-meter","Selected track stereo meter and clip reset");meter.onReset=[this]{host.bank().tracks[size_t(host.track())].signalMeters.resetClip();};
        trackChoice.onChange=[this]{host.chooseTrack(trackChoice.getSelectedId()-1);};
        for(int i=0;i<8;++i){
            setupButton(slots[size_t(i)],"plugin-slot-"+juce::String(i),"SLOT "+juce::String(i+1));
            setupButton(bypass[size_t(i)],"plugin-bypass-"+juce::String(i),"BYP");
            bypass[size_t(i)].setTooltip("Bypass this insert. Delay compensation stays active.");
            slots[size_t(i)].onClick=[this,i]{host.openSlot(i);};
            bypass[size_t(i)].onClick=[this,i]{auto& chain=host.bank().tracks[size_t(host.track())].chain;chain.bypass(i,!chain.isBypassed(i));host.owner.refresh();};
            left.addAndMakeVisible(slots[size_t(i)]);left.addAndMakeVisible(bypass[size_t(i)]);
        }
        setupButton(expand,"plugin-expand","EXPAND PLUGIN");expand.onClick=[this]{host.owner.setPluginExpanded(!expanded);};
        setupButton(browse,"plugin-browse","PLUGIN VAULT");browse.onClick=[this]{host.owner.setPage(Page::vault);};
        editorHint.setJustificationType(juce::Justification::centred);
        editorHint.setColour(juce::Label::textColourId,dim);editorHint.setFont(juce::Font(juce::FontOptions(17)));
        summary.setFont(juce::Font(juce::FontOptions(11)));summary.setColour(juce::Label::textColourId,dim);
    }
    ~PluginPage() override {clearEditor();editorView.setViewedComponent(nullptr,false);leftView.setViewedComponent(nullptr,false);}
    void clearEditor(){editorView.setViewPosition(0,0);editor.reset();editorHint.setVisible(true);resized();}
    void setEditor(std::unique_ptr<juce::AudioProcessorEditor> next){clearEditor();editor=std::move(next);if(editor){editorCanvas.addAndMakeVisible(*editor);editor->setTopLeftPosition(0,0);editorHint.setVisible(false);}resized();}
    void refresh(){
        if(editor&&observedEditorSize!=juce::Point<int>(editor->getWidth(),editor->getHeight())){observedEditorSize={editor->getWidth(),editor->getHeight()};resized();}
        auto& track=host.bank().tracks[size_t(host.track())];
        const auto old=trackChoice.getSelectedId();bool rebuild=trackChoice.getNumItems()!=13;
        for(int i=0;i<13&&!rebuild;++i)rebuild=trackChoice.getItemText(i)!=juce::String(i+1).paddedLeft('0',2)+"  "+host.bank().tracks[size_t(i)].name;
        if(rebuild){trackChoice.clear(juce::dontSendNotification);for(int i=0;i<13;++i)trackChoice.addItem(juce::String(i+1).paddedLeft('0',2)+"  "+host.bank().tracks[size_t(i)].name,i+1);}
        if(old!=host.track()+1||rebuild)trackChoice.setSelectedId(host.track()+1,juce::dontSendNotification);
        for(int i=0;i<8;++i){
            auto* p=track.chain.activeProcessor(i);const auto name=p?(p->isExternal()&&!p->processingReady()?juce::String("PAUSED / "):juce::String())+p->getName():juce::String("EMPTY SLOT");
            slots[size_t(i)].setButtonText(juce::String(i+1).paddedLeft('0',2)+"  "+name);
            slots[size_t(i)].setToggleState(i==host.slot(),juce::dontSendNotification);
            slots[size_t(i)].setTooltip("Slot "+juce::String(i+1)+": "+name+(track.chain.isBusy(i)?" (transitioning)":"")+(p&&p->isExternal()?" / "+p->externalDiagnostic():juce::String()));
            bypass[size_t(i)].setEnabled(p!=nullptr&&!track.chain.isBusy(i));
            bypass[size_t(i)].setToggleState(p!=nullptr&&track.chain.isBypassed(i),juce::dontSendNotification);
        }
        meter.value=track.signalMeters.snapshot();meter.repaint();
        summary.setText((track.loaded?"SOURCE LOADED":"NO FILE LOADED")+juce::String("  |  Trim ")+juce::String(track.trimDb.load(),1)+" dB\nPost-insert / post-trim signal\n8-slot compensated latency: "+juce::String(track.chain.latencySamples())+" samples",juce::dontSendNotification);
        auto* p=track.chain.activeProcessor(host.slot());
        editorTitle.setText(editor?editor->getAudioProcessor()->getName():"SLOT "+juce::String(host.slot()+1)+" / "+(p?p->getName():"EMPTY"),juce::dontSendNotification);
        editorHint.setText(p?"Open the selected insert to view its native controls":"Select an insert, or choose a module in Plugin Vault\nThe shared transport stays active while you navigate",juce::dontSendNotification);
        expand.setButtonText(expanded?"RESTORE SPLIT":"EXPAND PLUGIN");expand.setToggleState(expanded,juce::dontSendNotification);
        repaint();
    }
    void paint(juce::Graphics& g) override {panel(g,getLocalBounds());}
    void resized() override {
        auto r=getLocalBounds().reduced(8);const int header=38;
        const int leftWidth=expanded?0:juce::jlimit(200,juce::jmax(200,r.getWidth()-280),int(float(r.getWidth())*split));
        leftView.setVisible(!expanded);divider.setVisible(!expanded);
        if(!expanded){leftView.setBounds(r.removeFromLeft(leftWidth));divider.setBounds(r.removeFromLeft(12));}
        auto top=r.removeFromTop(header);expand.setBounds(top.removeFromRight(142).reduced(2));browse.setBounds(top.removeFromRight(132).reduced(2));editorTitle.setBounds(top.reduced(4,0));
        editorView.setBounds(r);
        const int lw=juce::jmax(244,leftView.getWidth()-leftView.getScrollBarThickness());
        const int lh=juce::jmax(450,leftView.getHeight());left.setSize(lw,lh);
        leftTitle.setBounds(8,0,lw-16,25);trackChoice.setBounds(8,27,lw-16,32);
        const int meterWidth=72, buttonWidth=juce::jmax(96,lw-meterWidth-76);
        for(int i=0;i<8;++i){slots[size_t(i)].setBounds(6,70+i*38,buttonWidth,34);bypass[size_t(i)].setBounds(buttonWidth+8,70+i*38,48,34);}
        meter.setBounds(lw-meterWidth-4,72,meterWidth,300);
        summary.setBounds(8,382,lw-16,60);
        const int ew=juce::jmax(1,editorView.getWidth()-editorView.getScrollBarThickness());
        const int eh=juce::jmax(1,editorView.getHeight()-editorView.getScrollBarThickness());
        editorCanvas.setSize(juce::jmax(ew,editor?editor->getWidth():0),juce::jmax(eh,editor?editor->getHeight():0));
        editorHint.setBounds(editorCanvas.getLocalBounds().reduced(20));
    }
};

struct NativeWorkspace::Impl::MixerPage final : juce::Component {
    struct Strip final : juce::Component {
        Impl& host;int index;juce::TextButton select,mute,solo;juce::Slider fader;juce::Label gainReadout;StereoMeter meter;juce::String trackName;bool loaded=false;
        Strip(Impl& h,int i):host(h),index(i){
            identify(*this,"mixer-strip-"+juce::String(i),"Mixer channel "+juce::String(i+1));
            setupButton(select,"mixer-select-"+juce::String(i),"EDIT "+juce::String(i+1));
            setupButton(mute,"mixer-mute-"+juce::String(i),"M");setupButton(solo,"mixer-solo-"+juce::String(i),"S");
            mute.setTooltip("Mute "+juce::String(i+1));solo.setTooltip("Solo "+juce::String(i+1));
            identify(fader,"mixer-gain-"+juce::String(i),"Track "+juce::String(i+1)+" gain in dB");
            identify(meter,"mixer-signal-meter-"+juce::String(i),"Track "+juce::String(i+1)+" stereo meter and clip reset");meter.onReset=[this]{host.bank().tracks[size_t(index)].signalMeters.resetClip();};
            fader.setSliderStyle(juce::Slider::LinearVertical);fader.setRange(-60,6,.1);fader.setValue(0,juce::dontSendNotification);
            fader.setDoubleClickReturnValue(true,0);fader.setTextBoxStyle(juce::Slider::NoTextBox,false,0,0);fader.setTextValueSuffix(" dB");
            identify(gainReadout,"mixer-readout-"+juce::String(i),"Actual track trim in dB");gainReadout.setJustificationType(juce::Justification::centred);gainReadout.setFont(juce::Font(juce::FontOptions(12)));gainReadout.setColour(juce::Label::textColourId,amber);
            fader.setTooltip("Post-insert trim: -60 to +6 dB. Double click for 0 dB.");
            fader.onValueChange=[this]{host.bank().tracks[size_t(index)].trimDb.store(float(fader.getValue()),std::memory_order_relaxed);};
            mute.onClick=[this]{auto& v=host.bank().tracks[size_t(index)].muted;v.store(!v.load());host.owner.refresh();};
            solo.onClick=[this]{auto& v=host.bank().tracks[size_t(index)].solo;v.store(!v.load());host.owner.refresh();};
            select.onClick=[this]{host.chooseTrack(index);host.owner.setPage(Page::plugin);host.openSlot(host.slot());};
            for(auto* c:std::initializer_list<juce::Component*>{&select,&mute,&solo,&fader,&gainReadout,&meter})addAndMakeVisible(c);
        }
        void refresh(){auto& t=host.bank().tracks[size_t(index)];trackName=t.name;loaded=t.loaded;gainReadout.setText(juce::String(t.trimDb.load(),1)+" dB",juce::dontSendNotification);select.setToggleState(host.track()==index,juce::dontSendNotification);mute.setToggleState(t.muted.load(),juce::dontSendNotification);solo.setToggleState(t.solo.load(),juce::dontSendNotification);if(!fader.isMouseButtonDown())fader.setValue(t.trimDb.load(),juce::dontSendNotification);meter.value=t.signalMeters.snapshot();meter.repaint();repaint();}
        void paint(juce::Graphics& g) override {panel(g,getLocalBounds(),host.track()==index);text(g,juce::String(index+1).paddedLeft('0',2),{4,5,getWidth()-8,18},12,amber,1,juce::Justification::centred);text(g,trackName,{5,26,getWidth()-10,34},12,cyan,2,juce::Justification::centred);text(g,loaded?"FILE LOADED":"EMPTY",{4,61,getWidth()-8,16},9,dim,1,juce::Justification::centred);}
        void resized() override {const int w=getWidth();auto bottom=getLocalBounds().removeFromBottom(76).reduced(5,4);select.setBounds(bottom.removeFromBottom(31));auto toggles=bottom.removeFromTop(30);mute.setBounds(toggles.removeFromLeft(toggles.getWidth()/2).reduced(1));solo.setBounds(toggles.reduced(1));gainReadout.setBounds(3,79,w-6,24);const int area=juce::jmax(60,getHeight()-199);fader.setBounds(2,113,36,area);meter.setBounds(37,107,juce::jmax(43,w-40),area+10);}
    };
    Impl& host;juce::Viewport viewport;juce::Component content;std::array<std::unique_ptr<Strip>,13> strips;
    explicit MixerPage(Impl& h):host(h){identify(*this,"workspace-mixer","13 track mixer");identify(viewport,"mixer-viewport","Horizontally scrollable mixer strips");viewport.setViewedComponent(&content,false);viewport.setScrollBarsShown(true,true);addAndMakeVisible(viewport);for(int i=0;i<13;++i){strips[size_t(i)]=std::make_unique<Strip>(host,i);content.addAndMakeVisible(*strips[size_t(i)]);}}
    ~MixerPage() override {viewport.setViewedComponent(nullptr,false);}
    void paint(juce::Graphics& g) override {text(g,"13 STEM MIXER",{12,2,getWidth()-24,24},16,amber);text(g,"Post-insert trim -60 to +6 dB  |  Mute / Solo  |  Stereo sample peak, RMS and 1 s hold  |  Meter scale -60 to 0 dBFS",{12,28,getWidth()-24,22},12,dim);}
    void refresh(){for(auto& s:strips)s->refresh();}
    void resized() override {viewport.setBounds(getLocalBounds().withTrimmedTop(58).reduced(8,0));const int w=juce::jmax(13*90,viewport.getWidth()-viewport.getScrollBarThickness());const int h=juce::jmax(270,viewport.getHeight()-viewport.getScrollBarThickness());content.setSize(w,h);for(int i=0;i<13;++i){const int x=w*i/13,next=w*(i+1)/13;strips[size_t(i)]->setBounds(x+2,0,next-x-4,h);}}
};

struct NativeWorkspace::Impl::VaultPage final : juce::Component {
    struct Card final : juce::Component {
        Impl& host;int index;juce::TextButton action;
        Card(Impl& h,int i):host(h),index(i){identify(*this,"vault-card-"+juce::String(i),PluginRegistry::entries()[size_t(i)].name);setupButton(action,"vault-install-"+juce::String(i),"");addAndMakeVisible(action);action.onClick=[this]{const auto& entry=PluginRegistry::entries()[size_t(index)];if(entry.monitoringOnly){if(host.cb.onMonitor)host.cb.onMonitor();}else if(host.cb.onInstallModule)host.cb.onInstallModule(index);host.owner.refresh();};}
        void paint(juce::Graphics& g) override {panel(g,getLocalBounds());const auto& e=PluginRegistry::entries()[size_t(index)];text(g,e.category,{12,8,getWidth()-24,18},11,amber);text(g,e.name,{12,30,getWidth()-24,43},16,cyan,2);text(g,e.monitoringOnly?"Listening path only / excluded from export":"Native processor / serial stem insert",{12,76,getWidth()-24,25},11,dim);}
        void refresh(){const auto& e=PluginRegistry::entries()[size_t(index)];action.setButtonText(e.monitoringOnly?"OPEN MONITOR EQ":"INSERT / REPLACE SLOT "+juce::String(host.slot()+1));action.setTooltip(e.monitoringOnly?"Open the existing listening-only monitor EQ":"Install "+juce::String(e.name)+" on track "+juce::String(host.track()+1)+", slot "+juce::String(host.slot()+1));action.setEnabled(e.monitoringOnly?bool(host.cb.onMonitor):bool(host.cb.onInstallModule)&&!host.bank().tracks[size_t(host.track())].chain.isBusy(host.slot()));}
        void resized() override {action.setBounds(10,getHeight()-42,getWidth()-20,32);}
    };
    Impl& host;juce::TextEditor search;juce::ComboBox category;juce::Label target,externalInfo;juce::TextButton loadExternal,reloadExternal;juce::Viewport viewport;juce::Component content;
    std::vector<std::unique_ptr<Card>> cards;std::vector<int> visible;
    explicit VaultPage(Impl& h):host(h){
        identify(*this,"workspace-vault","Plugin registry vault");identify(search,"vault-search","Search registered plugin names and categories");identify(category,"vault-category","Plugin category");
        search.setTextToShowWhenEmpty("Search "+juce::String(int(PluginRegistry::entries().size()))+" registered modules",dim);search.setColour(juce::TextEditor::backgroundColourId,ink);search.setColour(juce::TextEditor::textColourId,cyan);search.setColour(juce::TextEditor::outlineColourId,edge);
        category.addItem("All categories",1);juce::StringArray categories;for(const auto& e:PluginRegistry::entries())categories.addIfNotAlreadyThere(e.category);categories.sort(true);for(int i=0;i<categories.size();++i)category.addItem(categories[i],i+2);category.setSelectedId(1,juce::dontSendNotification);
        search.onTextChange=[this]{filter();};category.onChange=[this]{filter();};target.setFont(juce::Font(juce::FontOptions(12)));target.setColour(juce::Label::textColourId,dim);
        for(auto* c:std::initializer_list<juce::Component*>{&search,&category,&target,&viewport})addAndMakeVisible(c);
        setupButton(loadExternal,"vault-load-vst3","LOAD SELECTED VST3");setupButton(reloadExternal,"vault-reload-vst3","RELOAD CURRENT VST3");
        loadExternal.onClick=[this]{if(host.cb.onLoadVst3)host.cb.onLoadVst3();};reloadExternal.onClick=[this]{if(host.cb.onReloadVst3)host.cb.onReloadVst3();};
        externalInfo.setText("Stereo VST3 effects / 6 ms per-slot limit / native code: load only trusted files",juce::dontSendNotification);externalInfo.setColour(juce::Label::textColourId,dim);externalInfo.setFont(juce::Font(juce::FontOptions(11)));
        if(host.cb.onLoadVst3){addAndMakeVisible(loadExternal);addAndMakeVisible(reloadExternal);addAndMakeVisible(externalInfo);}
        viewport.setViewedComponent(&content,false);viewport.setScrollBarsShown(true,false);
        for(size_t i=0;i<PluginRegistry::entries().size();++i){cards.push_back(std::make_unique<Card>(host,int(i)));content.addAndMakeVisible(*cards.back());}
        filter();
    }
    ~VaultPage() override {viewport.setViewedComponent(nullptr,false);}
    void filter(){visible.clear();const auto query=search.getText().trim();for(int i=0;i<int(PluginRegistry::entries().size());++i){const auto& e=PluginRegistry::entries()[size_t(i)];const bool match=(category.getSelectedId()==1||category.getText()==e.category)&&(query.isEmpty()||(juce::String(e.name)+" "+e.category+" "+e.id).containsIgnoreCase(query));cards[size_t(i)]->setVisible(match);if(match)visible.push_back(i);}resized();repaint();}
    void refresh(){int monitorCount=0;for(const auto& entry:PluginRegistry::entries())if(entry.monitoringOnly)++monitorCount;target.setText("TARGET: "+juce::String(host.track()+1).paddedLeft('0',2)+" "+host.bank().tracks[size_t(host.track())].name+" / SLOT "+juce::String(host.slot()+1)+"     |     "+juce::String(int(PluginRegistry::entries().size())-monitorCount)+" insert processors + "+juce::String(monitorCount)+" listening-only",juce::dontSendNotification);for(auto& c:cards)c->refresh();}
    void paint(juce::Graphics& g) override {text(g,"PLUGIN VAULT / "+juce::String(int(visible.size()))+" OF "+juce::String(int(PluginRegistry::entries().size()))+" REGISTERED",{12,0,getWidth()-24,28},16,amber);if(visible.empty())text(g,"No registered modules match this search",getLocalBounds().withTrimmedTop(130).reduced(20),16,dim,2,juce::Justification::centred);}
    void resized() override {auto r=getLocalBounds().reduced(10);r.removeFromTop(28);auto row=r.removeFromTop(34);category.setBounds(row.removeFromRight(200));row.removeFromRight(10);search.setBounds(row);r.removeFromTop(4);target.setBounds(r.removeFromTop(27));r.removeFromTop(8);if(host.cb.onLoadVst3){auto actions=r.removeFromBottom(60);externalInfo.setBounds(actions.removeFromBottom(23));loadExternal.setBounds(actions.removeFromLeft(actions.getWidth()/2).reduced(2));reloadExternal.setBounds(actions.reduced(2));}viewport.setBounds(r);const int w=juce::jmax(220,viewport.getWidth()-viewport.getScrollBarThickness());const int columns=juce::jmax(1,w/290),cardWidth=w/columns;const int rows=(int(visible.size())+columns-1)/columns;content.setSize(w,juce::jmax(r.getHeight(),rows*158));for(size_t n=0;n<visible.size();++n)cards[size_t(visible[n])]->setBounds(int(n)%columns*cardWidth+3,int(n)/columns*158,cardWidth-6,152);}
};

struct NativeWorkspace::Impl::RoutingPage final : juce::Component {
    struct Diagram final : juce::Component {
        Impl& host;RouteTelemetry telemetry;std::array<AudioMeters::Snapshot,13> levels;
        explicit Diagram(Impl& h):host(h){identify(*this,"routing-diagram","Fixed audio signal flow with measured levels");}
        void box(juce::Graphics& g,juce::Rectangle<int> r,const juce::String& title,const juce::String& detail,bool active=false){panel(g,r,active);text(g,title,r.reduced(9).removeFromTop(21),13,active?amber:cyan);text(g,detail,r.reduced(9).withTrimmedTop(23),11,dim,3);}
        void arrow(juce::Graphics& g,juce::Point<float> a,juce::Point<float> b,juce::Colour colour=edge){g.setColour(colour);g.drawArrow({a,b},1.5f,5,5);}
        void paint(juce::Graphics& g) override {
            g.fillAll(ink);text(g,"SOURCE > INPUT GAIN > 8 INSERTS > TRIM / MUTE / SOLO",{10,2,520,24},12,dim);
            constexpr int row=27;const int busX=535;const int sourceWidth=176;
            for(int i=0;i<13;++i){const int y=31+i*row;const auto& t=host.bank().tracks[size_t(i)];const auto& l=levels[size_t(i)];
                const float peak=juce::jmax(l.left.peakDb,l.right.peakDb);const bool signal=l.ready&&peak>-60;
                g.setColour(host.track()==i?amber.withAlpha(.12f):juce::Colour(0xff101e28));g.fillRoundedRectangle(6.f,float(y),514.f,23.f,3);
                text(g,juce::String(i+1).paddedLeft('0',2)+" "+t.name,{10,y,sourceWidth,23},11,t.loaded?cyan:dim);
                for(int s=0;s<8;++s){const int x=192+s*22;auto* p=t.chain.activeProcessor(s);g.setColour(p?((t.chain.isBypassed(s)||!p->processingReady())?amber.withAlpha(.20f):cyan.withAlpha(.22f)):edge.withAlpha(.3f));g.fillRoundedRectangle(float(x),float(y+3),19,17,2);text(g,juce::String(s+1),{x,y+3,19,17},10,p?cyan:dim,1,juce::Justification::centred);}
                text(g,juce::String(t.trimDb.load(),1)+" dB",{373,y,50,23},10,dim);
                const auto meter=juce::Rectangle<float>(429.f,float(y+9),42,5);g.setColour(edge);g.fillRect(meter);if(signal){g.setColour(cyan);g.fillRect(meter.withWidth(meter.getWidth()*juce::jlimit(0.f,1.f,(peak+60)/60)));}
                text(g,l.ready?db(peak):"N/A",{476,y,42,23},10,signal?cyan:dim);
                g.setColour(signal?cyan.withAlpha(juce::jlimit(.12f,.8f,(peak+60)/60)):edge);g.drawLine(521.f,float(y+12),float(busX),float(y+12),1);
            }
            g.setColour(edge);g.drawLine(float(busX),43.f,float(busX),367.f,2);
            arrow(g,{float(busX),207.f},{562.f,207.f});
            box(g,{562,164,156,87},"STEREO SUM","13 parallel tracks\nShared output gain\nAnalysis tap before listening");
            arrow(g,{640,251},{640,272});box(g,{562,273,156,82},"LISTENING COPY","+ vocal monitoring\nRecording/export bypass\nlistening correction");
            arrow(g,{718,313},{738,313});box(g,{740,274,176,83},telemetry.headphoneCorrection?"CORRECTED ROUTE":"DIRECT ROUTE",telemetry.headphoneCorrection?"Measured calibration\nMonitor EQ: "+juce::String(telemetry.monitorReady?"prepared":"not prepared"):"Calibration / monitor EQ\nbypassed at route blend",telemetry.headphoneCorrection);
            arrow(g,{828,274},{828,253});box(g,{740,163,176,89},"OUTPUT SAFETY",juce::String(telemetry.hardwareOutputs==1?"Mono fold-down first\n":telemetry.hardwareOutputs>1?"Stereo output\n":"No active output\n")+"Task 2B final limiter\n"+(telemetry.limiterLatencySamples>0?juce::String(telemetry.limiterLatencySamples)+" samples latency":"Latency unavailable")+(telemetry.limiterReductionDb?" / "+juce::String(*telemetry.limiterReductionDb,2)+" dB GR":""));
            arrow(g,{916,207},{938,207});
            const bool ready=telemetry.output.ready;box(g,{940,164,180,111},"ACTUAL DEVICE OUTPUT",ready?"L peak "+db(telemetry.output.left.peakDb)+" dBFS\nR peak "+db(telemetry.output.right.peakDb)+" dBFS\nL/R RMS "+db(telemetry.output.left.rmsDb)+" / "+db(telemetry.output.right.rmsDb):"Audio meter unavailable");
            text(g,"EXISTING FIXED CONNECTIONS",{565,30,540,22},16,amber);
            text(g,"Lines represent the implemented audio path. Track level bars measure post-trim contributions; final levels measure exact emitted samples. No free rewiring is implemented.",{565,62,548,69},13,dim,3);
            text(g,"Meters: sample peak / RMS, not LUFS or certified dBTP",{563,373,555,23},12,dim);
            text(g,"Insert cells 1-8: filled = installed; amber = bypassed/unavailable. Empty slots retain latency compensation.",{12,390,1105,25},12,dim);
            text(g,(telemetry.outputDevice.isEmpty()?"No output device reported":telemetry.outputDevice)+"  |  "+(telemetry.sampleRate>0?juce::String(telemetry.sampleRate,0)+" Hz / "+juce::String(telemetry.blockSize)+" samples":"Audio unavailable")+"  |  "+juce::String(telemetry.hardwareOutputs)+" active outputs",{12,423,1105,24},13,cyan);
            text(g,telemetry.calibrationName.isEmpty()?"No calibration profile reported":telemetry.calibrationName,{12,452,1105,25},12,dim);
        }
        void refresh(){for(int i=0;i<13;++i)levels[size_t(i)]=host.bank().tracks[size_t(i)].signalMeters.snapshot();telemetry=host.cb.routeTelemetry?host.cb.routeTelemetry():RouteTelemetry{};repaint();}
    };
    juce::Viewport viewport;Diagram diagram;
    explicit RoutingPage(Impl& h):diagram(h){identify(*this,"workspace-routing","Read-only routing telemetry");viewport.setViewedComponent(&diagram,false);viewport.setScrollBarsShown(true,true);addAndMakeVisible(viewport);}
    ~RoutingPage() override {viewport.setViewedComponent(nullptr,false);}
    void paint(juce::Graphics& g) override {text(g,"ROUTING / LIVE SIGNAL FLOW",{12,1,getWidth()-24,30},16,amber);}
    void resized() override {viewport.setBounds(getLocalBounds().withTrimmedTop(39).reduced(8,0));diagram.setSize(juce::jmax(1132,viewport.getWidth()-viewport.getScrollBarThickness()),juce::jmax(482,viewport.getHeight()-viewport.getScrollBarThickness()));}
};

NativeWorkspace::Impl::Impl(NativeWorkspace& o,Callbacks callbacks):owner(o),cb(std::move(callbacks)){
    jassert(cb.bank);owner.setLookAndFeel(&look);
    plugin=std::make_unique<PluginPage>(*this);mixer=std::make_unique<MixerPage>(*this);vault=std::make_unique<VaultPage>(*this);routing=std::make_unique<RoutingPage>(*this);
    owner.addAndMakeVisible(*plugin);owner.addChildComponent(*mixer);owner.addChildComponent(*vault);owner.addChildComponent(*routing);
}
NativeWorkspace::Impl::~Impl(){owner.setLookAndFeel(nullptr);}
NativeWorkspace::NativeWorkspace(Callbacks callbacks):impl(std::make_unique<Impl>(*this,std::move(callbacks))){identify(*this,"native-workspace","HEM native workspace");setOpaque(true);refresh();startTimerHz(20);}
NativeWorkspace::~NativeWorkspace(){stopTimer();clearEditor();}
void NativeWorkspace::setPage(Page page){impl->active=page;impl->plugin->setVisible(page==Page::plugin);impl->mixer->setVisible(page==Page::mixer);impl->vault->setVisible(page==Page::vault);impl->routing->setVisible(page==Page::routing);refresh();resized();}
NativeWorkspace::Page NativeWorkspace::page() const noexcept{return impl->active;}
void NativeWorkspace::refresh(){if(!impl->cb.bank)return;impl->plugin->refresh();if(impl->active==Page::mixer)impl->mixer->refresh();if(impl->active==Page::vault)impl->vault->refresh();if(impl->active==Page::routing)impl->routing->diagram.refresh();}
void NativeWorkspace::setEditor(std::unique_ptr<juce::AudioProcessorEditor> editor){impl->plugin->setEditor(std::move(editor));refresh();}
void NativeWorkspace::clearEditor(){impl->plugin->clearEditor();}
bool NativeWorkspace::hasEditor() const noexcept{return impl->plugin->editor!=nullptr;}
void NativeWorkspace::setPluginExpanded(bool expanded){impl->plugin->expanded=expanded;impl->plugin->refresh();impl->plugin->resized();}
bool NativeWorkspace::pluginExpanded() const noexcept{return impl->plugin->expanded;}
void NativeWorkspace::setSplitProportion(float proportion){if(!std::isfinite(proportion))return;impl->plugin->split=juce::jlimit(.22f,.60f,proportion);impl->plugin->resized();}
float NativeWorkspace::splitProportion() const noexcept{return impl->plugin->split;}
void NativeWorkspace::setVaultFilter(const juce::String& search,const juce::String& category){impl->vault->search.setText(search,false);auto& menu=impl->vault->category;int id=1;for(int i=0;i<menu.getNumItems();++i)if(menu.getItemText(i)==category)id=menu.getItemId(i);menu.setSelectedId(id,juce::dontSendNotification);impl->vault->filter();}
std::vector<int> NativeWorkspace::filteredRegistryIndices() const{return impl->vault->visible;}
void NativeWorkspace::paint(juce::Graphics& g){g.fillAll(ink);}
void NativeWorkspace::resized(){for(auto* child:std::initializer_list<juce::Component*>{impl->plugin.get(),impl->mixer.get(),impl->vault.get(),impl->routing.get()})child->setBounds(getLocalBounds());}
void NativeWorkspace::timerCallback(){if(isVisible())refresh();}
} // namespace hem
