#include "NativeWorkspace.h"
#include "SegmentedMeter.h"
#include "../Plugins/InsertFactory.h"
#include "../Plugins/Editors.h"
#include "../Plugins/OriginalBandTopbar.h"
#include "../Plugins/RestorationEditors.h"
#include "../Plugins/ReferenceMatchEditor.h"
#include "../Plugins/DrumReplacerEditor.h"
#include "../Plugins/AmpCabinetEditor.h"
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
// Source-drawn housing: blackened metal, machined lip and recessed glass.
// These are material cues only; state illumination and all meter segments are
// driven by the actual controls and published engine telemetry.
void glassWell(juce::Graphics& g, juce::Rectangle<float> r) {
    if (r.isEmpty()) return;
    g.setColour(juce::Colours::black.withAlpha(.8f));g.fillRoundedRectangle(r.expanded(1),4);
    juce::ColourGradient glass(juce::Colour(0xff11222c),r.getX(),r.getY(),juce::Colour(0xff020609),r.getRight(),r.getBottom(),false);
    glass.addColour(.35,juce::Colour(0xff07121b));g.setGradientFill(glass);g.fillRoundedRectangle(r,3);
    g.setColour(cyan.withAlpha(.16f));g.drawRoundedRectangle(r,3,.8f);
    g.setColour(juce::Colours::white.withAlpha(.065f));g.drawHorizontalLine(int(r.getY()+1),r.getX()+4,r.getRight()-4);
    g.setColour(juce::Colours::black.withAlpha(.7f));g.drawHorizontalLine(int(r.getBottom()-1),r.getX()+4,r.getRight()-4);
}
void panel(juce::Graphics& g, juce::Rectangle<int> bounds, bool selected = false) {
    if (bounds.isEmpty()) return;
    const auto r=bounds.toFloat().reduced(1);
    const juce::Graphics::ScopedSaveState state(g);
    g.reduceClipRegion(bounds);
    g.setColour(juce::Colours::black.withAlpha(.85f));g.fillRoundedRectangle(r.translated(0,1),5);
    juce::ColourGradient metal(juce::Colour(0xff34414b),r.getX(),r.getY(),juce::Colour(0xff060b10),r.getX(),r.getBottom(),false);
    metal.addColour(.035,juce::Colour(0xff141e27));metal.addColour(.55,juce::Colour(0xff0c151e));metal.addColour(.97,juce::Colour(0xff05080c));
    g.setGradientFill(metal);g.fillRoundedRectangle(r,5);
    // Low-contrast horizontal machining stays subordinate to the legends.
    for(int y=bounds.getY()+6;y<bounds.getBottom()-5;y+=3){g.setColour(juce::Colours::white.withAlpha(y%2?.012f:.021f));g.drawHorizontalLine(y,r.getX()+4,r.getRight()-4);}
    g.setColour(juce::Colours::black);g.drawRoundedRectangle(r,5,1.4f);
    g.setColour(juce::Colour(0xff465561).withAlpha(.7f));g.drawRoundedRectangle(r.reduced(1),4,1);
    g.setColour(selected?amber.withAlpha(.8f):cyan.withAlpha(.18f));g.drawRoundedRectangle(r.reduced(3),3,.8f);
    if(selected){g.setColour(amber.withAlpha(.17f));g.drawRoundedRectangle(r.reduced(4),3,3);}
    if(bounds.getWidth()>65&&bounds.getHeight()>60){
        for(float x:{r.getX()+6,r.getRight()-6})for(float y:{r.getY()+6,r.getBottom()-6}){
            const juce::Rectangle<float> screw(x-2,y-2,4,4);
            g.setColour(juce::Colours::black);g.fillEllipse(screw.expanded(.6f));
            g.setGradientFill(juce::ColourGradient(juce::Colour(0xff64717c),x-2,y-2,juce::Colour(0xff111820),x+2,y+2,false));g.fillEllipse(screw);
            g.setColour(juce::Colour(0xff070b10));g.drawLine(x-1,y+1,x+1,y-1,.8f);
        }
    }
}
void headerRail(juce::Graphics& g,juce::Rectangle<int> bounds,const juce::String& title) {
    panel(g,bounds);
    g.setColour(amber.withAlpha(.22f));g.fillRect(bounds.getX()+10,bounds.getBottom()-3,juce::jmax(0,bounds.getWidth()-20),1);
    text(g,title,bounds.reduced(14,1),15,amber);
}
void identify(juce::Component& c, const juce::String& id, const juce::String& name) { c.setComponentID(id); c.setName(name); }
void setupButton(juce::TextButton& b, const juce::String& id, const juce::String& title) {
    identify(b,id,title); b.setButtonText(title); b.setWantsKeyboardFocus(true);
}
// GUI-only liveness. Values are never decayed, normalized or fabricated here.
// Counted frames distinguish a steady tone/silence from a frozen publication.
struct MeterFrame {
    enum class State { unavailable, waiting, live, stale };
    AudioMeters::Snapshot value;
    State state=State::unavailable;
    const void* source=nullptr;
    double lastAdvanceMs=0;
    bool observed=false,hasProgress=false;
    void update(const AudioMeters::Snapshot& next,const void* identity,double nowMs){
        if(next.publication==0){
            // Bounded snapshot contention is unavailable, not a real prepare
            // or a reset to generation0. Retain this source's coherent baseline.
            if(source!=identity){source=identity;observed=false;hasProgress=false;value={};}
            state=State::unavailable;return;
        }
        if(!observed||source!=identity){
            // An old snapshot is not evidence of a callback since this view
            // attached to the source. Seed it dark until progress is observed.
            observed=true;source=identity;hasProgress=false;lastAdvanceMs=nowMs;
        }else if(next.generation!=value.generation){
            hasProgress=next.ready&&next.processedSamples>0;lastAdvanceMs=nowMs;
        }else if(next.processedSamples>value.processedSamples){
            hasProgress=true;lastAdvanceMs=nowMs;
        }else if(next.processedSamples<value.processedSamples){
            hasProgress=false;lastAdvanceMs=nowMs;
        }
        value=next;
        state=!value.ready?State::unavailable:value.processedSamples==0||!hasProgress?State::waiting
             :nowMs-lastAdvanceMs>=1000.?State::stale:State::live;
    }
    bool live()const noexcept{return state==State::live;}
    juce::String status()const{return state==State::unavailable?"NO AUDIO":state==State::waiting?"WAITING":state==State::stale?"STALE":"LIVE";}
};
class StereoMeter final : public juce::Component, public juce::SettableTooltipClient {
public:
    MeterFrame frame;
    void update(AudioMeters& source,double nowMs=juce::Time::getMillisecondCounterHiRes()){
        frame.update(source.snapshot(),&source,nowMs);
        getProperties().set("meterState",int(frame.state));
        setDescription("Measured stereo sample peak/RMS/hold: "+frame.status());repaint();
    }
    std::function<void()> onReset;
    StereoMeter(){setWantsKeyboardFocus(true);setTooltip("Measured stereo dBFS: sample peak LEDs, cyan RMS marks, amber 1 s hold. MAX L/R is the louder channel. INVALID counts non-finite input. Click, Enter or Space to request clip/invalid reset on the next audio block.");}
    void mouseDown(const juce::MouseEvent&) override {if(onReset)onReset();}
    bool keyPressed(const juce::KeyPress& key) override {if(key==juce::KeyPress::returnKey||key==juce::KeyPress::spaceKey){if(onReset)onReset();return true;}return false;}
    void paint(juce::Graphics& g) override {
        const auto& value=frame.value;const bool live=frame.live();
        glassWell(g,getLocalBounds().toFloat().reduced(1));
        // NEW compact geometry: retain two independently measured lanes, a
        // calibrated scale and truthful status in the approved host aperture.
        const bool compact=getHeight()<150;
        auto plot=getLocalBounds().reduced(2);plot.removeFromTop(15);plot.removeFromBottom(compact?31:50);
        if(plot.getHeight()<1||plot.getWidth()<1)return;
        text(g,"dBFS",{0,0,getWidth(),13},10,dim,1,juce::Justification::centred);
        const int gutter=compact?17:21,laneWidth=juce::jmax(3,(plot.getWidth()-gutter-4)/2);
        auto y=[&](float v){return float(plot.getBottom())-float(plot.getHeight())*juce::jlimit(0.f,1.f,(v+60.f)/60.f);};
        for(float v:{0.f,-12.f,-24.f,-48.f,-60.f}){
            if(compact&&(v==-12.f||v==-48.f))continue;
            g.setColour(edge);g.drawHorizontalLine(int(y(v)),float(gutter),float(getWidth()));
            text(g,juce::String(int(v)),{0,int(y(v))-6,gutter-2,12},compact?8.f:9.f,dim,1,juce::Justification::centredRight);
        }
        int x=gutter,channelIndex=0;
        for(const auto& c:{value.left,value.right}){
            const auto lane=juce::Rectangle<float>(float(x),float(plot.getY()),float(laneWidth),float(plot.getHeight()));
            const int segments=juce::jlimit(8,30,plot.getHeight()/3);
            ui::paintSegmentedMeter(g,lane,ui::meterDbPosition(c.peakDb),live,false,segments);
            ui::paintMeterMark(g,lane,ui::meterDbPosition(c.rmsDb),cyan,1.5f,ui::MeterMarkRole::rms,live);
            ui::paintMeterMark(g,lane,ui::meterDbPosition(c.heldPeakDb),amber,1.f,ui::MeterMarkRole::peakHold,live);
            g.setColour(value.ready&&c.clipped?juce::Colour(0xffff5a48):edge);g.fillRect(x,plot.getY()-3,laneWidth,2);
            text(g,channelIndex++==0?"L":"R",{x,plot.getBottom()+1,laneWidth,12},9,cyan,1,juce::Justification::centred);
            x+=laneWidth+3;
        }
        if(!compact)text(g,"MAX L/R",{0,getHeight()-32,getWidth(),12},8,dim,1,juce::Justification::centred);
        const auto status=!live?frame.status():value.nonFiniteSamples>0?juce::String("INVALID"):db(juce::jmax(value.left.peakDb,value.right.peakDb));
        text(g,status,{0,getHeight()-17,getWidth(),16},compact?9.f:10.f,value.nonFiniteSamples?amber:live?cyan:dim,1,juce::Justification::centred);
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
    Page active = Page::plugin, returnPage = Page::plugin;
    juce::Component::SafePointer<juce::Component> returnFocus;
    bool editorFocusPending=false;
    void focusEditorWhenShowing();
    std::unique_ptr<PluginPage> plugin;
    std::unique_ptr<MixerPage> mixer;
    std::unique_ptr<VaultPage> vault;
    std::unique_ptr<RoutingPage> routing;
    explicit Impl(NativeWorkspace&,Callbacks);
    ~Impl();
    TrackBank& bank() const {return cb.bank();}
    int track() const {return juce::jlimit(0,12,cb.getSelectedTrack ? cb.getSelectedTrack():0);}
    int slot() const {return juce::jlimit(0,7,cb.getSelectedSlot ? cb.getSelectedSlot():0);}
    void chooseTrack(int i) {
        const juce::Component::SafePointer<NativeWorkspace> self(&owner);
        const auto select=cb.setSelectedTrack;
        if(select)select(juce::jlimit(0,12,i));
        if(self)self->refresh();
    }
    void openSlot(int i) {
        const juce::Component::SafePointer<NativeWorkspace> self(&owner);
        const auto select=cb.setSelectedSlot;const auto open=cb.onOpenSlot;
        if(select)select(i);
        if(!self)return;
        if(open)open(i);
        if(self)self->refresh();
    }
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
    juce::TextButton expand, browse, moveUp, moveDown;
    std::array<juce::TextButton,3> clipButtons;
    std::function<void(int)> clipCommand;
    Divider divider{*this};
    std::unique_ptr<juce::AudioProcessorEditor> editor;
    std::shared_ptr<int> editorBindingLifetime, rackBindingLifetime;
    bool layingOut=false;
    juce::Label leftTitle, editorTitle, editorHint, summary;
    StereoMeter meter;
    float split=.32f;
    bool previewOnly=false;
    bool expanded=false;juce::Point<int> observedEditorSize;
    explicit PluginPage(Impl& h):host(h) {
        identify(*this,"workspace-plugin","Selected stem and plugin editor");
        leftView.setViewedComponent(&left,false);leftView.setScrollBarsShown(true,true);
        editorView.setViewedComponent(&editorCanvas,false);editorView.setScrollBarsShown(true,true);
        // Parameter drags must not simultaneously scroll the editor underneath
        // the pointer. Scrollbars and wheel remain available for navigation.
        editorView.setScrollOnDragMode(juce::Viewport::ScrollOnDragMode::never);
        identify(leftView,"plugin-stem-viewport","Selected stem and eight insert slots");
        identify(editorView,"plugin-editor-viewport","Complete plugin editor fitted to available space");
        for(auto* c : std::initializer_list<juce::Component*>{&leftView,&editorView,&divider,&expand,&browse,&editorTitle})addAndMakeVisible(c);
        for(auto* c : std::initializer_list<juce::Component*>{&leftTitle,&trackChoice,&summary,&meter,&moveUp,&moveDown})left.addAndMakeVisible(c);
        editorCanvas.addAndMakeVisible(editorHint);
        leftTitle.setText("SELECTED STEM / 8 SERIAL INSERTS",juce::dontSendNotification);
        leftTitle.setFont(juce::Font(juce::FontOptions(13).withStyle("Bold")));
        identify(trackChoice,"plugin-track-select","Selected audio track");
        identify(meter,"plugin-signal-meter","Selected track stereo meter and clip reset");meter.onReset=[this]{host.bank().tracks[size_t(host.track())].signalMeters.resetClip();};
        
        for(int i=0;i<8;++i){
            setupButton(slots[size_t(i)],"plugin-slot-"+juce::String(i),"SLOT "+juce::String(i+1));
            setupButton(bypass[size_t(i)],"plugin-bypass-"+juce::String(i),"BYP");
            bypass[size_t(i)].setTooltip("Bypass this insert. Delay compensation stays active.");


            left.addAndMakeVisible(slots[size_t(i)]);left.addAndMakeVisible(bypass[size_t(i)]);
        }
        setupButton(moveUp,"plugin-slot-up","MOVE UP");setupButton(moveDown,"plugin-slot-down","MOVE DOWN");
        moveUp.setTooltip("Move selected insert one slot earlier. Stop playback before reordering.");
        moveDown.setTooltip("Move selected insert one slot later. Stop playback before reordering.");
        setupButton(expand,"plugin-expand","EXPAND PLUGIN");expand.onClick=[this]{if(editor)host.owner.backFromEditor();else host.owner.setPluginExpanded(!expanded);};
        setupButton(browse,"plugin-browse","PLUGIN VAULT");browse.onClick=[this]{host.owner.setPage(Page::vault);};
        const char* clipNames[]{"PREVIEW","APPLY","UNDO / REDO"};
        for(int i=0;i<3;++i){setupButton(clipButtons[size_t(i)],"clip-host-command-"+juce::String(i),clipNames[i]);addChildComponent(clipButtons[size_t(i)]);
            clipButtons[size_t(i)].onClick=[this,i]{auto action=clipCommand;if(action)action(i);};}
        editorHint.setJustificationType(juce::Justification::centred);
        editorHint.setColour(juce::Label::textColourId,dim);editorHint.setFont(juce::Font(juce::FontOptions(17)));
        summary.setFont(juce::Font(juce::FontOptions(11)));summary.setColour(juce::Label::textColourId,dim);
    }
    ~PluginPage() override {clearEditor();editorView.setViewedComponent(nullptr,false);leftView.setViewedComponent(nullptr,false);}
    void clearEditor(){clipCommand={};for(auto& button:clipButtons)button.setVisible(false);editorBindingLifetime.reset();rackBindingLifetime.reset();editorView.setViewPosition(0,0);editor.reset();editorHint.setVisible(true);resized();}
    void setClipCommands(std::function<void(int)> command,bool busy,bool canUndo){
        clipCommand=std::move(command);for(auto& button:clipButtons)button.setVisible(bool(clipCommand));
        clipButtons[0].setButtonText(busy?"CANCEL":"PREVIEW");clipButtons[1].setEnabled(!busy);clipButtons[2].setEnabled(!busy&&canUndo);resized();
    }
    void setEditor(std::unique_ptr<juce::AudioProcessorEditor> next){
        clearEditor();editor=std::move(next);
        if(editor){
            if(auto* module=dynamic_cast<ModuleEditor*>(editor.get())){
                editorBindingLifetime=std::make_shared<int>(0);
                const std::weak_ptr<int> lifetime=editorBindingLifetime;
                const juce::Component::SafePointer<PluginPage> page(this);
                auto* expected=editor->getAudioProcessor();int originalTrack=-1,originalSlot=-1;
                for(int t=0;t<13;++t)for(int s=0;s<8;++s)
                    if(host.bank().tracks[size_t(t)].chain.activeProcessor(s)==expected){originalTrack=t;originalSlot=s;}
                // No chain/processor dereference survives owner/editor retirement.
                // Resolve the original slot in the current bank on every use;
                // selection changes must never redirect this editor's bypass.
                auto resolve=[page,lifetime,expected,originalTrack,originalSlot]() -> InsertChain* {
                    if(lifetime.expired()||page==nullptr||originalTrack<0||originalSlot<0)return nullptr;
                    auto& chain=page->host.bank().tracks[size_t(originalTrack)].chain;
                    if(chain.isBusy(originalSlot)||chain.activeProcessor(originalSlot)!=expected)return nullptr;
                    return &chain;
                };
                module->setHostBypassBinding({
                    [resolve,originalSlot]() -> std::optional<bool> {if(auto* chain=resolve())return chain->isBypassed(originalSlot);return std::nullopt;},
                    [resolve,originalSlot,page](bool bypassed){if(auto* chain=resolve()){chain->bypass(originalSlot,bypassed);if(page!=nullptr)page->host.owner.refresh();}}
                });
                if(auto* drum=dynamic_cast<DrumReplacerEditor*>(editor.get())) {
                    if(host.cb.onDrumLoad)drum->setLoadRequest([resolve,page,originalTrack,originalSlot,expected](int layer){
                        if(resolve()&&page&&page->host.cb.onDrumLoad)
                            page->host.cb.onDrumLoad(layer,originalTrack,originalSlot,static_cast<ModuleProcessor*>(expected));
                    });
                    const juce::Component::SafePointer<juce::AudioProcessorEditor> expectedEditor(editor.get());
                    drum->setCloseRequest([page,expectedEditor]{
                        juce::MessageManager::callAsync([page,expectedEditor]{
                            if(page&&expectedEditor&&page->editor.get()==expectedEditor.getComponent())page->host.owner.backFromEditor();
                        });
                    });
                }
                if(auto* amp=dynamic_cast<AmpCabinetEditor*>(editor.get())) {
                    if(host.cb.onAmpLoad)amp->setLoadRequest([resolve,page,originalTrack,originalSlot,expected]{
                        if(resolve()&&page&&page->host.cb.onAmpLoad)
                            page->host.cb.onAmpLoad(originalTrack,originalSlot,static_cast<ModuleProcessor*>(expected));
                    });
                    const juce::Component::SafePointer<juce::AudioProcessorEditor> expectedEditor(editor.get());
                    amp->setCloseRequest([page,expectedEditor]{
                        juce::MessageManager::callAsync([page,expectedEditor]{
                            if(page&&expectedEditor&&page->editor.get()==expectedEditor.getComponent())page->host.owner.backFromEditor();
                        });
                    });
                }
            }
            if(auto* original=dynamic_cast<OriginalBandTopbarOwner*>(editor.get())){
                const juce::Component::SafePointer<PluginPage> page(this);
                const juce::Component::SafePointer<juce::AudioProcessorEditor> expected(editor.get());
                original->bandTopbar().setCloseAction([page,expected]{
                    if(page&&expected&&page->editor.get()==expected.getComponent())page->host.owner.backFromEditor();
                });
            }
            if(auto* restoration=dynamic_cast<RestorationEditor*>(editor.get())){
                const juce::Component::SafePointer<PluginPage> page(this);
                const juce::Component::SafePointer<juce::AudioProcessorEditor> expected(editor.get());
                restoration->setCloseRequest([page,expected]{
                    juce::MessageManager::callAsync([page,expected]{
                        if(page&&expected&&page->editor.get()==expected.getComponent())page->host.owner.backFromEditor();
                    });
                });
            }
            if(auto* reference=dynamic_cast<ReferenceMatchEditor*>(editor.get())){
                const juce::Component::SafePointer<PluginPage> page(this);
                const juce::Component::SafePointer<juce::AudioProcessorEditor> expected(editor.get());
                reference->setCloseRequest([page,expected]{
                    juce::MessageManager::callAsync([page,expected]{
                        if(page&&expected&&page->host.active==Page::plugin&&page->editor.get()==expected.getComponent())page->host.owner.backFromEditor();
                    });
                });
            }
            editorCanvas.addAndMakeVisible(*editor);editor->setTopLeftPosition(0,0);editorHint.setVisible(false);
        }
        resized();
    }
    bool canMoveSlot(int destination) const {
        const int source=host.slot();
        if(editor||host.active!=Page::plugin||!host.cb.onMoveSlot||destination<0||destination>=8||destination==source)return false;
        auto& chain=host.bank().tracks[size_t(host.track())].chain;
        return chain.activeProcessor(source)!=nullptr&&!chain.isBusy(source)&&!chain.isBusy(destination);
    }
    // A copied/queued callback is an action on the displayed bank/track/slot,
    // never an instruction to mutate whichever selection happens to be newer.
    void bindRackActions(){
        rackBindingLifetime=std::make_shared<int>(0);
        const std::weak_ptr<int> lifetime=rackBindingLifetime;
        const juce::Component::SafePointer<PluginPage> page(this);
        const juce::Component::SafePointer<juce::AudioProcessorEditor> expectedEditor(editor.get());
        expand.onClick=[page,lifetime,expectedEditor]{
            if(lifetime.expired()||!page||page->editor.get()!=expectedEditor.getComponent())return;
            const juce::Component::SafePointer<PluginPage> self(page);
            if(expectedEditor)self->host.owner.backFromEditor();
            else self->host.owner.setPluginExpanded(!self->expanded);
        };
        auto* expectedBank=&host.bank();const int track=host.track(),source=host.slot();
        auto& chain=expectedBank->tracks[size_t(track)].chain;
        const auto current=[page,lifetime,expectedBank,track,source]{
            return !lifetime.expired()&&page&&(&page->host.bank()==expectedBank)
                &&page->host.track()==track&&page->host.slot()==source&&!page->editor
                &&page->host.active==Page::plugin;
        };
        trackChoice.onChange=[page,current]{
            if(!current())return;
            const juce::Component::SafePointer<PluginPage> self(page);
            self->host.chooseTrack(self->trackChoice.getSelectedId()-1);
        };
        for(int i=0;i<8;++i){
            auto* expected=chain.activeProcessor(i);
            slots[size_t(i)].onClick=[page,current,expected,i]{
                if(!current())return;
                const juce::Component::SafePointer<PluginPage> self(page);
                auto& active=self->host.bank().tracks[size_t(self->host.track())].chain;
                if(active.activeProcessor(i)==expected&&!active.isBusy(i))self->host.openSlot(i);
            };
            bypass[size_t(i)].onClick=[page,current,expected,i]{
                if(current()){
                    const juce::Component::SafePointer<PluginPage> self(page);
                    auto& active=self->host.bank().tracks[size_t(self->host.track())].chain;
                    if(expected&&active.activeProcessor(i)==expected&&!active.isBusy(i))active.bypass(i,!active.isBypassed(i));
                    self->host.owner.refresh();
                }
            };
        }
        for(int delta:{-1,1}){
            const int destination=source+delta;auto* expected=chain.activeProcessor(source);
            auto* neighbour=destination>=0&&destination<8?chain.activeProcessor(destination):nullptr;
            auto action=[page,current,source,destination,expected,neighbour]{
                if(!current())return;
                // Host dispatch may refresh/rebind this very std::function or
                // retire the workspace. Never access captured state afterwards.
                const juce::Component::SafePointer<PluginPage> self(page);
                const auto move=self->host.cb.onMoveSlot;
                auto& active=self->host.bank().tracks[size_t(self->host.track())].chain;
                if(self->canMoveSlot(destination)&&active.activeProcessor(source)==expected&&active.activeProcessor(destination)==neighbour)
                    move(source,destination);
                if(self)self->host.owner.refresh();
            };
            (delta<0?moveUp:moveDown).onClick=std::move(action);
        }
    }
    void refresh(){
        bindRackActions();
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
        moveUp.setEnabled(canMoveSlot(host.slot()-1));moveDown.setEnabled(canMoveSlot(host.slot()+1));
        meter.update(track.signalMeters);
        summary.setText((track.loaded?"SOURCE LOADED":"NO FILE LOADED")+juce::String("  |  Trim ")+juce::String(track.trimDb.load(),1)+" dB\nPost-insert / post-trim signal\n8-slot compensated latency: "+juce::String(track.chain.latencySamples())+" samples",juce::dontSendNotification);
        auto* p=track.chain.activeProcessor(host.slot());
        editorTitle.setText(editor?(previewOnly?juce::String("PREVIEW ONLY - enlarge to 1152 x 686 to edit"):editor->getAudioProcessor()->getName()):"SLOT "+juce::String(host.slot()+1)+" / "+(p?p->getName():"EMPTY"),juce::dontSendNotification);
        editorHint.setText(p?"Open the selected insert to view its native controls":"Select an insert, or choose a module in Plugin Vault\nThe shared transport stays active while you navigate",juce::dontSendNotification);
        expand.setButtonText(editor?"BACK":expanded?"RESTORE SPLIT":"EXPAND PLUGIN");expand.setToggleState(expanded,juce::dontSendNotification);
        repaint();
    }
    void paint(juce::Graphics& g) override {
        panel(g,getLocalBounds());
        if(!editor){
            if(leftView.isVisible())glassWell(g,leftView.getBounds().toFloat().reduced(3));
            glassWell(g,editorView.getBounds().toFloat().reduced(3));
            const auto title=editorTitle.getBounds();g.setColour(amber.withAlpha(.28f));
            g.fillRect(title.getX()+4,title.getBottom()-4,juce::jmax(0,title.getWidth()-8),1);
        }
    }
    void resized() override {
        if(layingOut)return;
        const juce::ScopedValueSetter<bool> layoutGuard(layingOut,true);
        auto r=editor?getLocalBounds():getLocalBounds().reduced(8);const int header=38;
        if(editor)expanded=true;
        browse.setVisible(!editor);
        const int leftWidth=expanded?0:juce::jlimit(200,juce::jmax(200,r.getWidth()-280),int(float(r.getWidth())*split));
        leftView.setVisible(!expanded);divider.setVisible(!expanded);
        if(!expanded){leftView.setBounds(r.removeFromLeft(leftWidth));divider.setBounds(r.removeFromLeft(12));}
        auto top=r.removeFromTop(header);expand.setBounds(top.removeFromRight(142).reduced(2));browse.setBounds(top.removeFromRight(132).reduced(2));
        if(clipCommand)for(int i=2;i>=0;--i)clipButtons[size_t(i)].setBounds(top.removeFromRight(i==2?112:88).reduced(2));
        editorTitle.setBounds(top.reduced(4,0));
        editorView.setBounds(r);
        const int lw=juce::jmax(244,leftView.getWidth()-leftView.getScrollBarThickness());
        const int lh=juce::jmax(450,leftView.getHeight());left.setSize(lw,lh);
        leftTitle.setBounds(8,0,lw-16,25);trackChoice.setBounds(8,27,lw-16,32);
        const int meterWidth=72, buttonWidth=juce::jmax(96,lw-meterWidth-76);
        const int moveWidth=(lw-20)/2;
        moveUp.setBounds(8,65,moveWidth,31);moveDown.setBounds(12+moveWidth,65,moveWidth,31);
        for(int i=0;i<8;++i){slots[size_t(i)].setBounds(6,104+i*35,buttonWidth,31);bypass[size_t(i)].setBounds(buttonWidth+8,104+i*35,48,31);}
        meter.setBounds(lw-meterWidth-4,106,meterWidth,276);summary.setBounds(8,389,lw-16,60);
        const int ew=juce::jmax(1,editorView.getWidth());
        const int eh=juce::jmax(1,editorView.getHeight());
        editorCanvas.setSize(ew,eh);
        if(editor){
            // Keep the processor's native layout and pointer coordinate system,
            // but fit its complete bounds into this host pane at any desktop DPI.
            const float fit=std::nextafter(std::min(float(ew)/float(juce::jmax(1,editor->getWidth())),float(eh)/float(juce::jmax(1,editor->getHeight()))),0.f);
            previewOnly=fit<.9f-0.0001f;
            editor->setEnabled(!previewOnly);
            editor->getProperties().set("previewOnly",previewOnly);
            editorTitle.setText(previewOnly?juce::String("PREVIEW ONLY - enlarge to 1152 x 686 to edit"):editor->getAudioProcessor()->getName(),juce::dontSendNotification);
            editorTitle.setFont(juce::Font(juce::FontOptions(14.f)));
            // NEW supported JUCE host scale. Keep translation in unscaled bounds
            // so editorResized sees exactly the host transform it installed.
            editor->setScaleFactor(fit);
            editor->setTopLeftPosition(int(std::lround((float(ew)/fit-editor->getWidth())*.5f)),
                                      int(std::lround((float(eh)/fit-editor->getHeight())*.5f)));
            observedEditorSize={editor->getWidth(),editor->getHeight()};
        }
        editorView.setViewPosition(0,0);
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
            select.onClick=[this]{
                const juce::Component::SafePointer<Strip> self(this);
                host.chooseTrack(index);
                if(!self)return;
                self->host.owner.setPage(Page::plugin);
                if(self)self->host.openSlot(self->host.slot());
            };
            for(auto* c:std::initializer_list<juce::Component*>{&select,&mute,&solo,&fader,&gainReadout,&meter})addAndMakeVisible(c);
        }
        void refresh(){auto& t=host.bank().tracks[size_t(index)];trackName=t.name;loaded=t.loaded;gainReadout.setText(juce::String(t.trimDb.load(),1)+" dB",juce::dontSendNotification);select.setToggleState(host.track()==index,juce::dontSendNotification);mute.setToggleState(t.muted.load(),juce::dontSendNotification);solo.setToggleState(t.solo.load(),juce::dontSendNotification);if(!fader.isMouseButtonDown())fader.setValue(t.trimDb.load(),juce::dontSendNotification);meter.update(t.signalMeters);repaint();}
        void paint(juce::Graphics& g) override {
            panel(g,getLocalBounds(),host.track()==index);
            glassWell(g,{7.f,25.f,float(getWidth()-14),51.f});
            glassWell(g,gainReadout.getBounds().toFloat().reduced(3,1));
            glassWell(g,{6.f,107.f,float(getWidth()-12),float(juce::jmax(1,getHeight()-187))});
            g.setColour(amber.withAlpha(.3f));g.drawHorizontalLine(getHeight()-79,8.f,float(getWidth()-8));
            text(g,juce::String(index+1).paddedLeft('0',2),{4,5,getWidth()-8,18},12,amber,1,juce::Justification::centred);
            text(g,trackName,{10,28,getWidth()-20,31},12,cyan,2,juce::Justification::centred);
            text(g,loaded?"FILE LOADED":"EMPTY",{8,60,getWidth()-16,15},9,dim,1,juce::Justification::centred);
        }
        void resized() override {const int w=getWidth();auto bottom=getLocalBounds().removeFromBottom(76).reduced(5,4);select.setBounds(bottom.removeFromBottom(31));auto toggles=bottom.removeFromTop(30);mute.setBounds(toggles.removeFromLeft(toggles.getWidth()/2).reduced(1));solo.setBounds(toggles.reduced(1));gainReadout.setBounds(3,79,w-6,24);const int area=juce::jmax(60,getHeight()-199);fader.setBounds(2,113,36,area);meter.setBounds(37,107,juce::jmax(43,w-40),area+10);}
    };
    Impl& host;juce::Viewport viewport;juce::Component content;std::array<std::unique_ptr<Strip>,13> strips;
    explicit MixerPage(Impl& h):host(h){identify(*this,"workspace-mixer","13 track mixer");identify(viewport,"mixer-viewport","Horizontally scrollable mixer strips");viewport.setViewedComponent(&content,false);viewport.setScrollBarsShown(true,true);addAndMakeVisible(viewport);for(int i=0;i<13;++i){strips[size_t(i)]=std::make_unique<Strip>(host,i);content.addAndMakeVisible(*strips[size_t(i)]);}}
    ~MixerPage() override {viewport.setViewedComponent(nullptr,false);}
    void paint(juce::Graphics& g) override {headerRail(g,{4,0,getWidth()-8,28},"13 STEM MIXER");text(g,"Post-insert trim -60 to +6 dB  |  Mute / Solo  |  Stereo peak LEDs / cyan RMS / amber 1 s hold  |  -60 to 0 dBFS",{12,28,getWidth()-24,16},10,dim);}
    void refresh(){for(auto& s:strips)s->refresh();}
    void resized() override {viewport.setBounds(getLocalBounds().withTrimmedTop(46).reduced(8,0));const int w=juce::jmax(13*90,viewport.getWidth()-viewport.getScrollBarThickness());const int h=juce::jmax(270,viewport.getHeight()-viewport.getScrollBarThickness());content.setSize(w,h);for(int i=0;i<13;++i){const int x=w*i/13,next=w*(i+1)/13;strips[size_t(i)]->setBounds(x+2,0,next-x-4,h);}}
};

struct NativeWorkspace::Impl::VaultPage final : juce::Component {
    struct Card final : juce::Component {
        Impl& host;int index;juce::TextButton action;juce::ComboBox factoryProfile;
        Card(Impl& h,int i):host(h),index(i){identify(*this,"vault-card-"+juce::String(i),PluginRegistry::entries()[size_t(i)].name);setupButton(action,"vault-install-"+juce::String(i),"");addAndMakeVisible(action);if(juce::String(PluginRegistry::entries()[size_t(i)].id)=="smart-gate"){factoryProfile.setComponentID("gate-insert-profile");factoryProfile.addItem("Reference v2 (new insert)",1);factoryProfile.addItem("Legacy factory v1 (new insert)",2);factoryProfile.setSelectedId(1,juce::dontSendNotification);factoryProfile.setTooltip("Applies only when inserting/replacing. Existing parameters are unchanged. Reference v2: ratio 1:1, hysteresis 0 dB. Legacy v1: ratio 2:1, hysteresis 6 dB.");addAndMakeVisible(factoryProfile);}action.onClick=[this]{
            const juce::Component::SafePointer<Card> self(this);
            const bool monitoringOnly=PluginRegistry::entries()[size_t(index)].monitoringOnly;
            const int choice=factoryProfile.getSelectedId()==2?InsertFactory::legacyGateChoice():index;
            const auto monitor=host.cb.onMonitor;const auto install=host.cb.onInstallModule;
            if(monitoringOnly){if(monitor)monitor();}else if(install)install(choice);
            if(self)self->host.owner.refresh();
        };}
        void paint(juce::Graphics& g) override {
            panel(g,getLocalBounds());const auto& e=PluginRegistry::entries()[size_t(index)];
            glassWell(g,{10.f,29.f,float(getWidth()-20),74.f});
            text(g,juce::String(e.category).toUpperCase(),{14,8,getWidth()-28,18},11,amber);
            text(g,e.name,{17,34,getWidth()-34,40},16,cyan,2);
            if(!factoryProfile.isVisible())text(g,e.monitoringOnly?"Listening path only / excluded from export":e.clipOperation?"Offline source operation / original preserved":"Native processor / serial stem insert",{17,77,getWidth()-34,22},11,dim);
            g.setColour(amber.withAlpha(.35f));g.drawHorizontalLine(getHeight()-47,12.f,float(getWidth()-12));
        }
        void refresh(){const auto& e=PluginRegistry::entries()[size_t(index)];action.setButtonText(e.monitoringOnly?"OPEN MONITOR EQ":e.clipOperation?"OPEN SOURCE OPERATION":"INSERT / REPLACE SLOT "+juce::String(host.slot()+1));action.setTooltip(e.monitoringOnly?"Open the existing listening-only monitor EQ":e.clipOperation?"Open offline source processing for the selected track":"Install "+juce::String(e.name)+" on track "+juce::String(host.track()+1)+", slot "+juce::String(host.slot()+1));action.setEnabled(e.monitoringOnly?bool(host.cb.onMonitor):bool(host.cb.onInstallModule)&&!host.bank().tracks[size_t(host.track())].chain.isBusy(host.slot()));}
        void resized() override {factoryProfile.setBounds(10,76,getWidth()-20,27);action.setBounds(10,getHeight()-42,getWidth()-20,32);}
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
        loadExternal.onClick=[this]{const auto load=host.cb.onLoadVst3;if(load)load();};reloadExternal.onClick=[this]{const auto reload=host.cb.onReloadVst3;if(reload)reload();};
        externalInfo.setText("Stereo VST3 effects / 6 ms per-slot limit / native code: load only trusted files",juce::dontSendNotification);externalInfo.setColour(juce::Label::textColourId,dim);externalInfo.setFont(juce::Font(juce::FontOptions(11)));
        if(host.cb.onLoadVst3){addAndMakeVisible(loadExternal);addAndMakeVisible(reloadExternal);addAndMakeVisible(externalInfo);}
        viewport.setViewedComponent(&content,false);viewport.setScrollBarsShown(true,false);
        for(size_t i=0;i<PluginRegistry::entries().size();++i){cards.push_back(std::make_unique<Card>(host,int(i)));content.addAndMakeVisible(*cards.back());}
        filter();
    }
    ~VaultPage() override {viewport.setViewedComponent(nullptr,false);}
    void filter(){visible.clear();const auto query=search.getText().trim();for(int i=0;i<int(PluginRegistry::entries().size());++i){const auto& e=PluginRegistry::entries()[size_t(i)];const bool match=(category.getSelectedId()==1||category.getText()==e.category)&&(query.isEmpty()||(juce::String(e.name)+" "+e.category+" "+e.id).containsIgnoreCase(query));cards[size_t(i)]->setVisible(match);if(match)visible.push_back(i);}resized();repaint();}
    void refresh(){int monitorCount=0;for(const auto& entry:PluginRegistry::entries())if(entry.monitoringOnly)++monitorCount;target.setText("TARGET: "+juce::String(host.track()+1).paddedLeft('0',2)+" "+host.bank().tracks[size_t(host.track())].name+" / SLOT "+juce::String(host.slot()+1)+"     |     "+juce::String(int(PluginRegistry::entries().size())-monitorCount)+" insert processors + "+juce::String(monitorCount)+" listening-only",juce::dontSendNotification);for(auto& c:cards)c->refresh();}
    void paint(juce::Graphics& g) override {headerRail(g,{4,0,getWidth()-8,28},"PLUGIN VAULT / "+juce::String(int(visible.size()))+" OF "+juce::String(int(PluginRegistry::entries().size()))+" REGISTERED");if(visible.empty())text(g,"No registered modules match this search",getLocalBounds().withTrimmedTop(130).reduced(20),16,dim,2,juce::Justification::centred);}
    void resized() override {auto r=getLocalBounds().reduced(10);r.removeFromTop(28);auto row=r.removeFromTop(34);category.setBounds(row.removeFromRight(200));row.removeFromRight(10);search.setBounds(row);r.removeFromTop(4);target.setBounds(r.removeFromTop(27));r.removeFromTop(8);if(host.cb.onLoadVst3){auto actions=r.removeFromBottom(60);externalInfo.setBounds(actions.removeFromBottom(23));loadExternal.setBounds(actions.removeFromLeft(actions.getWidth()/2).reduced(2));reloadExternal.setBounds(actions.reduced(2));}viewport.setBounds(r);const int w=juce::jmax(220,viewport.getWidth()-viewport.getScrollBarThickness());const int columns=juce::jmax(1,w/290),cardWidth=w/columns;const int rows=(int(visible.size())+columns-1)/columns;content.setSize(w,juce::jmax(r.getHeight(),rows*158));for(size_t n=0;n<visible.size();++n)cards[size_t(visible[n])]->setBounds(int(n)%columns*cardWidth+3,int(n)/columns*158,cardWidth-6,152);}
};

struct NativeWorkspace::Impl::RoutingPage final : juce::Component {
    struct Diagram final : juce::Component {
        Impl& host;RouteTelemetry telemetry;std::array<MeterFrame,13> levels;MeterFrame output;
        explicit Diagram(Impl& h):host(h){identify(*this,"routing-diagram","Fixed audio signal flow with measured levels");}
        void box(juce::Graphics& g,juce::Rectangle<int> r,const juce::String& title,const juce::String& detail,bool active=false){panel(g,r,active);text(g,title,r.reduced(9).removeFromTop(21),13,active?amber:cyan);text(g,detail,r.reduced(9).withTrimmedTop(23),11,dim,3);}
        void arrow(juce::Graphics& g,juce::Point<float> a,juce::Point<float> b,juce::Colour colour=edge){g.setColour(colour);g.drawArrow({a,b},1.5f,5,5);}
        void paint(juce::Graphics& g) override {
            panel(g,getLocalBounds());glassWell(g,{6.f,27.f,515.f,357.f});text(g,"SOURCE > INPUT GAIN > 8 INSERTS > TRIM / MUTE / SOLO",{10,2,520,24},12,dim);
            constexpr int row=27;const int busX=535;const int sourceWidth=176;
            for(int i=0;i<13;++i){const int y=31+i*row;const auto& t=host.bank().tracks[size_t(i)];const auto& frame=levels[size_t(i)];const auto& l=frame.value;
                const float peak=juce::jmax(l.left.peakDb,l.right.peakDb);const bool signal=frame.live()&&peak>-60;
                g.setColour(host.track()==i?amber.withAlpha(.12f):juce::Colour(0xff101e28));g.fillRoundedRectangle(6.f,float(y),514.f,23.f,3);
                text(g,juce::String(i+1).paddedLeft('0',2)+" "+t.name,{10,y,sourceWidth,23},11,t.loaded?cyan:dim);
                for(int s=0;s<8;++s){const int x=192+s*22;auto* p=t.chain.activeProcessor(s);g.setColour(p?((t.chain.isBypassed(s)||!p->processingReady())?amber.withAlpha(.20f):cyan.withAlpha(.22f)):edge.withAlpha(.3f));g.fillRoundedRectangle(float(x),float(y+3),19,17,2);text(g,juce::String(s+1),{x,y+3,19,17},10,p?cyan:dim,1,juce::Justification::centred);}
                text(g,juce::String(t.trimDb.load(),1)+" dB",{373,y,50,23},10,dim);
                const auto meter=juce::Rectangle<float>(429.f,float(y+9),42,5);g.setColour(edge);g.fillRect(meter);if(signal){g.setColour(cyan);g.fillRect(meter.withWidth(meter.getWidth()*juce::jlimit(0.f,1.f,(peak+60)/60)));}
                text(g,frame.live()?db(peak):frame.status(),{476,y,42,23},10,signal?cyan:dim);
                g.setColour(signal?cyan.withAlpha(juce::jlimit(.12f,.8f,(peak+60)/60)):edge);g.drawLine(521.f,float(y+12),float(busX),float(y+12),1);
            }
            g.setColour(edge);g.drawLine(float(busX),43.f,float(busX),367.f,2);
            arrow(g,{float(busX),207.f},{562.f,207.f});
            box(g,{562,164,156,87},"STEREO SUM","13 parallel tracks\nShared output gain\nAnalysis tap before listening");
            arrow(g,{640,251},{640,272});box(g,{562,273,156,82},"LISTENING COPY","+ vocal monitoring\nRecording/export bypass\nlistening correction");
            arrow(g,{718,313},{738,313});box(g,{740,274,176,83},telemetry.headphoneCorrection?"CORRECTED ROUTE":"DIRECT ROUTE",telemetry.headphoneCorrection?"Measured calibration\nMonitor EQ: "+juce::String(telemetry.monitorReady?"prepared":"not prepared"):"Calibration / monitor EQ\nbypassed at route blend",telemetry.headphoneCorrection);
            arrow(g,{828,274},{828,253});box(g,{740,163,176,89},"OUTPUT SAFETY",juce::String(telemetry.hardwareOutputs==1?"Mono fold-down first\n":telemetry.hardwareOutputs>1?"Stereo output\n":"No active output\n")+"Final master limiter\n"+(telemetry.limiterLatencySamples>0?juce::String(telemetry.limiterLatencySamples)+" samples latency":"Latency unavailable")+(telemetry.limiterReductionDb?" / "+juce::String(*telemetry.limiterReductionDb,2)+" dB GR":""));
            arrow(g,{916,207},{938,207});
            const bool ready=output.live();box(g,{940,164,180,111},"ACTUAL DEVICE OUTPUT",ready?"L peak "+db(telemetry.output.left.peakDb)+" dBFS\nR peak "+db(telemetry.output.right.peakDb)+" dBFS\nL/R RMS "+db(telemetry.output.left.rmsDb)+" / "+db(telemetry.output.right.rmsDb):"Audio meter "+output.status());
            text(g,"EXISTING FIXED CONNECTIONS",{565,30,540,22},16,amber);
            text(g,"Lines represent the implemented audio path. Track level bars measure post-trim contributions; final levels measure exact emitted samples. No free rewiring is implemented.",{565,62,548,69},13,dim,3);
            text(g,"Meters: sample peak / RMS, not LUFS or certified dBTP",{563,373,555,23},12,dim);
            text(g,"Insert cells 1-8: filled = installed; amber = bypassed/unavailable. Empty slots retain latency compensation.",{12,390,1105,25},12,dim);
            text(g,(telemetry.outputDevice.isEmpty()?"No output device reported":telemetry.outputDevice)+"  |  "+(telemetry.sampleRate>0?juce::String(telemetry.sampleRate,0)+" Hz / "+juce::String(telemetry.blockSize)+" samples":"Audio unavailable")+"  |  "+juce::String(telemetry.hardwareOutputs)+" active outputs",{12,423,1105,24},13,cyan);
            text(g,telemetry.calibrationName.isEmpty()?"No calibration profile reported":telemetry.calibrationName,{12,452,1105,25},12,dim);
        }
        void refresh(double nowMs=juce::Time::getMillisecondCounterHiRes()){
            for(int i=0;i<13;++i){auto& source=host.bank().tracks[size_t(i)].signalMeters;levels[size_t(i)].update(source.snapshot(),&source,nowMs);}
            telemetry=host.cb.routeTelemetry?host.cb.routeTelemetry():RouteTelemetry{};
            output.update(telemetry.output,&host.bank(),nowMs);repaint();
        }
    };
    juce::Viewport viewport;Diagram diagram;
    explicit RoutingPage(Impl& h):diagram(h){identify(*this,"workspace-routing","Read-only routing telemetry");viewport.setViewedComponent(&diagram,false);viewport.setScrollBarsShown(true,true);addAndMakeVisible(viewport);}
    ~RoutingPage() override {viewport.setViewedComponent(nullptr,false);}
    void paint(juce::Graphics& g) override {headerRail(g,{4,0,getWidth()-8,32},"ROUTING / LIVE SIGNAL FLOW");}
    void resized() override {viewport.setBounds(getLocalBounds().withTrimmedTop(39).reduced(8,0));diagram.setSize(juce::jmax(1132,viewport.getWidth()-viewport.getScrollBarThickness()),juce::jmax(482,viewport.getHeight()-viewport.getScrollBarThickness()));}
};

NativeWorkspace::Impl::Impl(NativeWorkspace& o,Callbacks callbacks):owner(o),cb(std::move(callbacks)){
    jassert(cb.bank);owner.setLookAndFeel(&look);
    plugin=std::make_unique<PluginPage>(*this);mixer=std::make_unique<MixerPage>(*this);vault=std::make_unique<VaultPage>(*this);routing=std::make_unique<RoutingPage>(*this);
    owner.addAndMakeVisible(*plugin);owner.addChildComponent(*mixer);owner.addChildComponent(*vault);owner.addChildComponent(*routing);
}
NativeWorkspace::Impl::~Impl(){owner.setLookAndFeel(nullptr);}
void NativeWorkspace::Impl::focusEditorWhenShowing(){
    if(editorFocusPending&&active==Page::plugin&&plugin->editor&&plugin->expand.isShowing()){
        editorFocusPending=false;plugin->expand.grabKeyboardFocus();
    }
}
NativeWorkspace::NativeWorkspace(Callbacks callbacks):impl(std::make_unique<Impl>(*this,std::move(callbacks))){identify(*this,"native-workspace","HEM native workspace");setOpaque(true);refresh();startTimerHz(20);}
NativeWorkspace::~NativeWorkspace(){stopTimer();clearEditor();}
void NativeWorkspace::setPage(Page page){if(page==Page::plugin&&impl->active!=Page::plugin)impl->returnPage=impl->active;impl->active=page;impl->plugin->setVisible(page==Page::plugin);impl->mixer->setVisible(page==Page::mixer);impl->vault->setVisible(page==Page::vault);impl->routing->setVisible(page==Page::routing);refresh();resized();}
NativeWorkspace::Page NativeWorkspace::page() const noexcept{return impl->active;}
void NativeWorkspace::refresh(){if(!impl->cb.bank)return;impl->plugin->refresh();if(impl->active==Page::mixer)impl->mixer->refresh();if(impl->active==Page::vault)impl->vault->refresh();if(impl->active==Page::routing)impl->routing->diagram.refresh();}
void NativeWorkspace::setEditor(std::unique_ptr<juce::AudioProcessorEditor> editor){
 if(editor&&!hasEditor()){impl->returnFocus=juce::Component::getCurrentlyFocusedComponent();if(impl->active!=Page::plugin)impl->returnPage=impl->active;}
 impl->plugin->setEditor(std::move(editor));if(hasEditor()){impl->editorFocusPending=true;setPage(Page::plugin);impl->focusEditorWhenShowing();}refresh();}
void NativeWorkspace::clearEditor(){impl->editorFocusPending=false;impl->plugin->clearEditor();}
void NativeWorkspace::setClipCommands(std::function<void(int)> action,bool busy,bool canUndo){impl->plugin->setClipCommands(std::move(action),busy,canUndo);}
void NativeWorkspace::backFromEditor(){
 if(!hasEditor())return;
 auto focus=impl->returnFocus;const auto page=impl->returnPage;
 clearEditor();impl->plugin->expanded=false;impl->plugin->resized();setPage(page);
 if(impl->cb.onEditorBack)impl->cb.onEditorBack();
 if(focus&&focus->isShowing()&&focus->isEnabled())focus->grabKeyboardFocus();
}
bool NativeWorkspace::keyPressed(const juce::KeyPress& key){if(key==juce::KeyPress::escapeKey&&hasEditor()&&page()==Page::plugin){backFromEditor();return true;}return false;}
bool NativeWorkspace::hasEditor() const noexcept{return impl->plugin->editor!=nullptr;}
void NativeWorkspace::setPluginExpanded(bool expanded){impl->plugin->expanded=expanded;impl->plugin->refresh();impl->plugin->resized();}
bool NativeWorkspace::pluginExpanded() const noexcept{return impl->plugin->expanded;}
void NativeWorkspace::setSplitProportion(float proportion){if(!std::isfinite(proportion))return;impl->plugin->split=juce::jlimit(.22f,.60f,proportion);impl->plugin->resized();}
float NativeWorkspace::splitProportion() const noexcept{return impl->plugin->split;}
void NativeWorkspace::setVaultFilter(const juce::String& search,const juce::String& category){impl->vault->search.setText(search,false);auto& menu=impl->vault->category;int id=1;for(int i=0;i<menu.getNumItems();++i)if(menu.getItemText(i)==category)id=menu.getItemId(i);menu.setSelectedId(id,juce::dontSendNotification);impl->vault->filter();}
std::vector<int> NativeWorkspace::filteredRegistryIndices() const{return impl->vault->visible;}
void NativeWorkspace::paint(juce::Graphics& g){g.fillAll(juce::Colour(0xff03070c));panel(g,getLocalBounds());}
void NativeWorkspace::resized(){for(auto* child:std::initializer_list<juce::Component*>{impl->plugin.get(),impl->mixer.get(),impl->vault.get(),impl->routing.get()})child->setBounds(getLocalBounds());}
void NativeWorkspace::refreshMeterFrames(double nowMs){
    impl->plugin->meter.update(impl->bank().tracks[size_t(impl->track())].signalMeters,nowMs);
    for(int i=0;i<13;++i)impl->mixer->strips[size_t(i)]->meter.update(impl->bank().tracks[size_t(i)].signalMeters,nowMs);
    impl->routing->diagram.refresh(nowMs);
}
void NativeWorkspace::visibilityChanged(){if(impl)impl->focusEditorWhenShowing();}
void NativeWorkspace::parentHierarchyChanged(){if(impl)impl->focusEditorWhenShowing();}
void NativeWorkspace::timerCallback(){if(isVisible()){refresh();impl->focusEditorWhenShowing();}}
} // namespace hem
