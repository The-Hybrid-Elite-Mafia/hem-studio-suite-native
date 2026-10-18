#include "../Source/Plugins/Editors.h"
#include "../Source/UI/NativeWorkspace.h"
#include "../Source/Plugins/DelayEditors.h"
#include "../Source/Plugins/OriginalBandControls.h"
#include "../Source/UI/FoundationPlotGeometry.h"
#include "../Source/UI/FoundationMaterialLook.h"
#include "../Source/DSP/FoundationTelemetry.h"
#include <iostream>
#include <stdexcept>
#include <limits>
#include <set>
#include <thread>
#include <cmath>

namespace hem {
struct NativeWorkspaceTestAccess {static void metersAt(NativeWorkspace& workspace,double nowMs){workspace.refreshMeterFrames(nowMs);}};
}

namespace {
void require(bool condition,const char* message){if(!condition){std::cerr<<"ASSERTION FAILED: "<<message<<std::endl;throw std::runtime_error(message);}}
juce::Component* find(juce::Component& root,const juce::String& id){if(root.getComponentID()==id)return &root;for(auto* child:root.getChildren())if(auto* result=find(*child,id))return result;return nullptr;}
template<class T>T& control(juce::Component& root,const juce::String& id){auto* c=dynamic_cast<T*>(find(root,id));require(c!=nullptr,("Missing production control: "+id).toRawUTF8());return *c;}
void click(juce::Component& root,const juce::String& id){auto& b=control<juce::TextButton>(root,id);require(b.isEnabled(),("Production button was unexpectedly disabled: "+id).toRawUTF8());for(auto* visible=static_cast<juce::Component*>(&b);visible;visible=visible->getParentComponent())require(visible->isVisible(),("Production button has hidden ancestry: "+id).toRawUTF8());require(bool(b.onClick),"Button has no production callback");b.onClick();}
int differences(const juce::Image& a,const juce::Image& b){int count=0;for(int y=0;y<a.getHeight();++y)for(int x=0;x<a.getWidth();++x)if(a.getPixelAt(x,y)!=b.getPixelAt(x,y))++count;return count;}
// Independent host-fit oracle: JUCE's supported host scale is pure scale,
// while centering belongs to unscaled component bounds. A spy below proves the
// virtual setScaleFactor API is actually used, beyond inspecting getTransform.
void requireHostFit(juce::AudioProcessorEditor& editor,juce::Component& canvas){
    const float fit=std::nextafter(std::min(float(canvas.getWidth())/editor.getWidth(),float(canvas.getHeight())/editor.getHeight()),0.f);
    const auto transform=editor.getTransform();
    require(std::abs(transform.mat00-fit)<.000001f&&std::abs(transform.mat11-fit)<.000001f,"Host scale differs from minimum aspect fit");
    require(transform.mat01==0&&transform.mat10==0&&transform.mat02==0&&transform.mat12==0,"Host scale must not contain translation/skew");
    require(editor.getX()==int(std::lround((float(canvas.getWidth())/fit-editor.getWidth())*.5f))&&editor.getY()==int(std::lround((float(canvas.getHeight())/fit-editor.getHeight())*.5f)),"Host centering did not use unscaled rounded bounds");
    require(canvas.getLocalBounds().contains(canvas.getLocalArea(&editor,editor.getLocalBounds())),"Host fit escaped canvas");
}
struct ScaleObservedEditor final : juce::AudioProcessorEditor {
    explicit ScaleObservedEditor(juce::AudioProcessor& p):AudioProcessorEditor(p){setSize(1280,720);}
    int scaleCalls=0,depth=0,maxDepth=0;float hostScale=1;
    void setScaleFactor(float value) override {++scaleCalls;++depth;maxDepth=std::max(maxDepth,depth);hostScale=value;AudioProcessorEditor::setScaleFactor(value);--depth;}
    void paint(juce::Graphics& g) override {g.fillAll(juce::Colours::navy);g.setColour(juce::Colours::white);g.drawRect(getLocalBounds(),2);}
};
int readablePixels(const juce::Image& image){int n=0;for(int y=0;y<image.getHeight();++y)for(int x=0;x<image.getWidth();++x){auto c=image.getPixelAt(x,y);if((c.getGreen()>130&&c.getBlue()>140)||(c.getRed()>180&&c.getGreen()>100))++n;}return n;}
}
int main(int argc,char** argv){juce::ScopedJuceInitialiser_GUI gui;try{
    const juce::File snapshots=argc>1?juce::File(argv[1]):juce::File{};
    auto saveRaster=[&](const juce::Image& image,const juce::String& name){if(snapshots==juce::File{})return;require(snapshots.createDirectory().wasOk(),"Cannot create workspace snapshot directory");juce::MemoryOutputStream png;require(juce::PNGImageFormat().writeImageToStream(image,png),"Cannot encode workspace raster");require(snapshots.getChildFile(name+".png").replaceWithData(png.getData(),png.getDataSize()),"Cannot save workspace raster");};
    // Production paint path: dynamic and registered button legends must retain
    // a visible ON perimeter with hover, pressed and disabled variants.
    {
      hem::SuiteMaterialLook look;
      juce::TextButton textButton("MODE");juce::ToggleButton toggle("ENABLE");
      for(juce::Button* button:{static_cast<juce::Button*>(&textButton),static_cast<juce::Button*>(&toggle)}){
        button->setSize(140,40);button->setLookAndFeel(&look);
        for(bool registered:{false,true}){
          if(registered){juce::Image face(juce::Image::RGB,140,40,true);look.registerButton(*button,face,{0,0,140,40},false);}
          for(bool enabled:{false,true})for(bool hover:{false,true}){
            button->setEnabled(enabled);
            auto paint=[&](bool on){button->setToggleState(on,juce::dontSendNotification);juce::Image image(juce::Image::RGB,140,40,true);juce::Graphics g(image);
              if(auto* b=dynamic_cast<juce::TextButton*>(button)){look.drawButtonBackground(g,*b,juce::Colours::black,hover,false);look.drawButtonText(g,*b,hover,false);}
              else look.drawToggleButton(g,*static_cast<juce::ToggleButton*>(button),hover,false);
              return image;};
            auto off=paint(false),on=paint(true);require(differences(off,on)>150,"ON state lost under legend");
            if(enabled&&!hover){saveRaster(off,registered?"toggle-registered-off":"toggle-off");saveRaster(on,registered?"toggle-registered-on":"toggle-on");}
          }
        }
        button->setLookAndFeel(nullptr);
      }
      for(float gain:{-12.f,0.f,12.f})require(juce::Rectangle<float>(777,510,122,272).contains(hem::ui::riderThumbBounds(gain)),"Rider thumb exceeds aperture");
      const juce::Rectangle<float> outer(10,10,420,180);const auto data=hem::ui::foundationPlotData(outer);
      require(data.getY()>outer.getY()+24&&data.getBottom()<outer.getBottom()-24,"Graph overlaps caption bands");
      for(auto range:{juce::Point<float>(-120,0),juce::Point<float>(-12,12),juce::Point<float>(-24,0),juce::Point<float>(-1,1)}){
        juce::Image graph(juce::Image::RGB,440,200,true);
        for(float v:{range.x,(range.x+range.y)*.5f,range.y}){
          const auto y=hem::ui::foundationPlotY(data,v,range.x,range.y);
          juce::Path trace;trace.startNewSubPath(data.getX(),y);trace.lineTo(data.getRight(),y);{juce::Graphics g(graph);hem::ui::paintFoundationTrace(g,data,trace);}
          int energy=0;for(int dy=-1;dy<=1;++dy)energy+=graph.getPixelAt(int(data.getCentreX()),int(std::round(y))+dy).getGreen();require(energy>70,"Calibrated endpoint not visibly rendered");
        }
        for(int y=10;y<34;++y)require(graph.getPixelAt(200,y)==juce::Colours::black,"Trace covers header");
        for(int y=166;y<190;++y)require(graph.getPixelAt(200,y)==juce::Colours::black,"Trace covers footer");
        saveRaster(graph,"calibrated-range-"+juce::String(range.x));
      }
    }
    // Test-only boundary telemetry through the real processor/editor. No DSP
    // settings or production display overrides are added for these fixtures.
    {
      std::unique_ptr<hem::ModuleProcessor> rider;
      for(const auto& entry:hem::PluginRegistry::entries())if(juce::String(entry.id)=="vocal-rider")rider=entry.create();
      require(rider!=nullptr,"Rider registry entry absent");rider->setPlayConfigDetails(2,2,48000,256);rider->prepareToPlay(48000,256);
      juce::AudioBuffer<float> audio(2,256);audio.clear();juce::MidiBuffer midi;rider->processBlock(audio,midi);
      auto* source=dynamic_cast<hem::FoundationDisplaySource*>(rider.get());require(source!=nullptr,"Rider telemetry missing");
      // The test owns this non-const processor; no audio thread is running.
      auto& telemetry=const_cast<hem::FoundationTelemetry&>(source->displayTelemetry());
      std::unique_ptr<juce::AudioProcessorEditor> editor(rider->createEditor());editor->setVisible(true);
      auto& label=control<juce::Label>(*editor,"rider-live-readonly");
      juce::MemoryBlock stateBefore,stateAfter;rider->getStateInformation(stateBefore);
      for(float gain:{-12.f,0.f,12.f}){
        for(float dpi:{.9f,1.f,2.f}){
          // The live editor requires a real positive post-open processor block.
          // Then publish the test-owned boundary gain frame; do not weaken its
          // present-tense freshness gate just to inject a static GUI fixture.
          // Resume GUI observation before producing fresh audio, including
          // after slow PNG encoding between DPI cases.
          auto* liveEditor=dynamic_cast<hem::ModuleEditor*>(editor.get());
          require(liveEditor!=nullptr,"Rider lacks its real GUI refresh hook");
          liveEditor->refreshParameterActivity();
          audio.clear();rider->processBlock(audio,midi);
          telemetry.prepare(48000);for(int i=0;i<400;++i)telemetry.observe({.1,.1},{.1f,.1f},gain,true);
          hem::FoundationDisplayFrame frame;require(telemetry.read(telemetry.latestSerial(),frame)&&frame.values[10]==gain,"Rider boundary fixture was not published");
          const float sx=editor->getWidth()/1672.f,sy=editor->getHeight()/941.f;
          auto thumb=hem::ui::riderThumbBounds(gain);auto crop=juce::Rectangle<float>(thumb.getX()*sx*dpi,thumb.getY()*sy*dpi,thumb.getWidth()*sx*dpi,thumb.getHeight()*sy*dpi).toNearestInt();
          const auto full=editor->createComponentSnapshot(editor->getLocalBounds(),true,dpi);
          std::vector<juce::Component*> visible;for(auto* child:editor->getChildren())if(child->isVisible()){visible.push_back(child);child->setVisible(false);}
          const auto parentOnly=editor->createComponentSnapshot(editor->getLocalBounds(),true,dpi);
          for(auto* child:visible)child->setVisible(true);
          int changed=0,bright=0;for(int y=crop.getY();y<crop.getBottom();++y)for(int x=crop.getX();x<crop.getRight();++x){changed+=full.getPixelAt(x,y)!=parentOnly.getPixelAt(x,y);bright+=parentOnly.getPixelAt(x,y).getRed()>110;}
          require(bright>100,"Expected actual metal thumb missing from parent render");
          require(changed==0,"Full-editor children occlude the Rider thumb");
          if(gain==-12&&dpi==1.f){
            const auto corrected=label.getBounds();label.setBounds(juce::Rectangle<float>(778*sx,758*sy,118*sx,22*sy).toNearestInt());
            const auto oldPlacement=editor->createComponentSnapshot(editor->getLocalBounds(),true,dpi);label.setBounds(corrected);
            int regression=0;for(int y=crop.getY();y<crop.getBottom();++y)for(int x=crop.getX();x<crop.getRight();++x)regression+=oldPlacement.getPixelAt(x,y)!=parentOnly.getPixelAt(x,y);
            require(regression>20,"Occlusion test does not detect the old label placement");
          }
          // Encode after freshness-sensitive image comparisons.
          saveRaster(full,"rider-boundary-"+juce::String(gain,0)+"dB-"+juce::String(int(dpi*100))+"pct");
        }
      }
      rider->getStateInformation(stateAfter);require(stateBefore==stateAfter,"Boundary rendering changed Rider state");
      std::cout<<"PASS full-editor Rider thumb pixels at -12/0/+12 dB and 90/100/200% raster scale; old-placement negative control detects occlusion; state unchanged\n";
    }
    // The real rack owns eight physical positions. Its reorder controls must
    // remain reachable without opening an editor, and never dispatch a stale
    // request against an empty or transitioning slot.
    {
      auto rackBank=std::make_unique<hem::TrackBank>();
      auto& chain=rackBank->tracks[0].chain;rackBank->tracks[0].prepare(48000,128);
      hem::NativeWorkspace* reentrantRack=nullptr;int selected=0,trackIndex=0;bool playing=false,recording=false,sessionBusy=false;int hostRequests=0;std::vector<std::pair<int,int>> moves;
      hem::NativeWorkspace::Callbacks rackCallbacks;
      rackCallbacks.bank=[&]() -> hem::TrackBank& {return *rackBank;};
      rackCallbacks.getSelectedTrack=[&] {return trackIndex;};rackCallbacks.getSelectedSlot=[&]{return selected;};
      rackCallbacks.setSelectedSlot=[&](int value){selected=value;};
      rackCallbacks.onMoveSlot=[&](int from,int to){
        ++hostRequests;if(playing||recording||sessionBusy)return;
        require(chain.swapSlotsStopped(from,to),"Workspace dispatched invalid stopped reorder");
        moves.emplace_back(from,to);selected=to;if(reentrantRack)reentrantRack->refresh();
      };
      hem::NativeWorkspace rack(rackCallbacks);reentrantRack=&rack;rack.setSize(1046,395);rack.setVisible(true);
      auto& up=control<juce::TextButton>(rack,"plugin-slot-up");
      auto& down=control<juce::TextButton>(rack,"plugin-slot-down");
      auto& rackViewport=control<juce::Viewport>(rack,"plugin-stem-viewport");
      for(auto* button:{&up,&down}){
        require(rackViewport.getViewArea().contains(rackViewport.getViewedComponent()->getLocalArea(button,button->getLocalBounds())),"Reorder control is clipped at default workspace size");
        auto* hit=rack.getComponentAt(rack.getLocalPoint(button,button->getLocalBounds().getCentre()));
        require(hit==button||button->isParentOf(hit),"Reorder control is hidden behind another component");
      }
      require(!up.isEnabled()&&!down.isEnabled(),"Empty selected slot enabled reordering");
      up.onClick();down.onClick();require(moves.empty(),"Empty slot dispatched a stale reorder callback");
      auto node=[&]{return chain.stageNode(hem::PluginRegistry::entries()[0].create(),48000,128,chain.slotLatency());};
      chain.installInitial(0,node(),true);auto* original=chain.activeProcessor(0);rack.refresh();
      require(!up.isEnabled()&&down.isEnabled(),"First-slot reorder boundary is incorrect");
      up.onClick();require(moves.empty(),"First slot moved before zero");
      for(auto* gate:{&playing,&recording,&sessionBusy}){
        *gate=true;const auto before=hostRequests;click(rack,"plugin-slot-down");
        require(hostRequests==before+1&&moves.empty()&&chain.activeProcessor(0)==original,"Host callback gate failed before editor retirement/swap");*gate=false;rack.refresh();
      }
      auto oldTrackMove=down.onClick;trackIndex=1;rack.refresh();oldTrackMove();require(moves.empty(),"Old reorder targeted newer track");trackIndex=0;rack.refresh();
      auto oldSlotMove=down.onClick;selected=1;rack.refresh();oldSlotMove();require(moves.empty(),"Old reorder targeted newer slot");selected=0;rack.refresh();
      auto oldPageMove=down.onClick;rack.setPage(hem::NativeWorkspace::Page::vault);oldPageMove();require(moves.empty(),"Old reorder survived newer navigation");rack.setPage(hem::NativeWorkspace::Page::plugin);
      auto oldEditorMove=down.onClick;rack.setEditor(std::unique_ptr<juce::AudioProcessorEditor>(original->createEditor()));oldEditorMove();require(moves.empty()&&rack.hasEditor(),"Old reorder retired newer editor");rack.backFromEditor();rack.setPage(hem::NativeWorkspace::Page::plugin);require(rackViewport.isVisible(),"Back did not relayout the restored rack sidebar");
      click(rack,"plugin-slot-down");
      require(moves.size()==1&&moves.back()==std::pair<int,int>(0,1)&&selected==1,"Move-down did not dispatch the selected indices");
      require(chain.activeProcessor(1)==original&&chain.activeProcessor(0)==nullptr&&chain.isBypassed(1),"Move-down lost the processor or bypass state");
      require(up.isEnabled()&&down.isEnabled(),"Middle occupied slot cannot move both directions");
      click(rack,"plugin-slot-up");
      require(moves.size()==2&&moves.back()==std::pair<int,int>(1,0)&&selected==0&&chain.activeProcessor(0)==original,"Move-up did not return the same processor");
      auto oldBypassAction=control<juce::TextButton>(rack,"plugin-bypass-0").onClick;
      trackIndex=1;rack.refresh();oldBypassAction();require(chain.isBypassed(0),"Old bypass changed earlier track after newer selection");trackIndex=0;rack.refresh();
      // Destination publication can begin between repaint and button dispatch.
      require(chain.publish(1,node()),"Cannot create busy destination fixture");
      down.onClick();require(moves.size()==2&&!down.isEnabled(),"Busy destination accepted a queued move");
      chain.completePendingStopped();chain.collectRetired(1);rack.refresh();
      auto* neighbour=chain.activeProcessor(1);
      click(rack,"plugin-slot-down");
      require(chain.activeProcessor(1)==original&&chain.activeProcessor(0)==neighbour,"Occupied adjacent reorder did not swap both inserts");
      // Keep an active source while a replacement is pending.
      require(chain.publish(1,node()),"Cannot create busy source fixture");rack.refresh();
      require(!up.isEnabled()&&!down.isEnabled(),"Busy selected source enabled reorder");
      up.onClick();down.onClick();require(moves.size()==3,"Busy selected source dispatched reorder");
      const bool oldBypass=chain.isBypassed(1);control<juce::TextButton>(rack,"plugin-bypass-1").onClick();
      require(chain.isBypassed(1)==oldBypass,"Queued bypass changed a busy slot");
      chain.completePendingStopped();chain.collectRetired(1);rack.refresh();
      require(up.isEnabled()&&down.isEnabled(),"Completed publication did not restore reorder controls");
      require(chain.swapSlotsStopped(1,7),"Cannot set last-slot boundary fixture");selected=7;rack.refresh();
      require(up.isEnabled()&&!down.isEnabled(),"Last-slot reorder boundary is incorrect");
      down.onClick();require(moves.size()==3,"Last slot moved past seven");
      click(rack,"plugin-slot-up");require(moves.back()==std::pair<int,int>(7,6)&&selected==6,"Last insert cannot move upward");
      selected=7;rack.refresh();require(!up.isEnabled()&&!down.isEnabled(),"Vacated source still enables reorder");
      up.onClick();down.onClick();control<juce::TextButton>(rack,"plugin-bypass-7").onClick();
      require(moves.size()==4&&!chain.isBypassed(7),"Queued empty-slot controls mutated the engine");
      selected=6;rackCallbacks.onMoveSlot={};
      hem::NativeWorkspace readOnlyRack(std::move(rackCallbacks));readOnlyRack.setSize(1046,395);readOnlyRack.refresh();
      require(!control<juce::TextButton>(readOnlyRack,"plugin-slot-up").isEnabled()&&!control<juce::TextButton>(readOnlyRack,"plugin-slot-down").isEnabled(),"Missing reorder callback advertises an actionable control");
      control<juce::TextButton>(readOnlyRack,"plugin-slot-up").onClick();require(moves.size()==4,"Missing callback invoked a move");
      // Distinct detector inputs must light distinct native LED lanes. Never
      // duplicate a louder-channel aggregate into a synthetic stereo meter.
      auto& signal=rackBank->tracks[0].signalMeters;
      auto& meter=control<juce::Component>(rack,"plugin-signal-meter");
      // Independent pixel oracle, deliberately not using SegmentedMeter helpers.
      // Calibration is fixed to -60..0 dBFS and samples actual LED centres away
      // from the central RMS and edge hold marks. Test both native and2x raster.
      auto laneEnergy=[&](const juce::Image& image,int channel,int width,int height,float dpi){
        const bool compact=height<150;const int gutter=compact?17:21;
        const int laneWidth=(width-4-gutter-4)/2,top=17,plotHeight=height-(compact?50:69);
        const int count=std::min(std::clamp(plotHeight/3,8,30),std::max(1,plotHeight/2));
        int lit=0;for(int row=0;row<count;++row){
          const float x=gutter+channel*(laneWidth+3)+laneWidth*.25f,y=top+(row+.5f)*plotHeight/count;
          const auto pixel=image.getPixelAt(int(x*dpi),int(y*dpi));
          if(pixel.getGreen()>130||pixel.getRed()>180)++lit;
        }return lit;
      };
      auto expectedLEDs=[](float amplitude,int height){
        const int plotHeight=height-(height<150?50:69),n=std::min(std::clamp(plotHeight/3,8,30),std::max(1,plotHeight/2));
        if(amplitude<=0)return 0;
        return std::clamp(int(std::floor((20.f*std::log10(amplitude)+60.f)/60.f*n)),0,n);
      };
      for(auto geometry:{juce::Point<int>(72,276),juce::Point<int>(46,82),juce::Point<int>(46,149),juce::Point<int>(72,150)}){
        meter.setSize(geometry.x,geometry.y);
        for(auto levels:{juce::Point<float>(.5f,0.f),juce::Point<float>(0.f,.5f),juce::Point<float>(.5f,.0316228f),juce::Point<float>(0.f,0.f)}){
          signal.prepare(48000);rack.refresh();std::vector<float> l(14400,levels.x),r(14400,levels.y);signal.push(l.data(),r.data(),int(l.size()));rack.refresh();
          const auto measured=signal.snapshot();
          for(float dpi:{1.f,2.f}){
            const auto raster=meter.createComponentSnapshot(meter.getLocalBounds(),true,dpi);
            const int actualL=laneEnergy(raster,0,geometry.x,geometry.y,dpi),actualR=laneEnergy(raster,1,geometry.x,geometry.y,dpi);
            require(actualL==expectedLEDs(levels.x,geometry.y)&&actualR==expectedLEDs(levels.y,geometry.y),"Stereo LED count differs from independent dBFS calibration");
            if(levels.x!=levels.y)require(actualL!=actualR,"Asymmetric fixture did not separate stereo lanes");
            // Independently locate centre RMS and edge hold marks at their
            // measured dB, and verify disjoint marks can survive equal levels.
            const int gutter=geometry.y<150?17:21,laneWidth=(geometry.x-4-gutter-4)/2,ph=geometry.y-(geometry.y<150?50:69);
            for(int channel=0;channel<2;++channel){
              const auto& c=channel==0?measured.left:measured.right;
              if(c.peakDb<=-60)continue;
              auto markerPresent=[&](float db,bool rms){
                const float lx=float(gutter+channel*(laneWidth+3));
                const float px=lx+laneWidth*(rms?.5f:.1f);
                const float py=std::clamp(17.f+ph*(1.f-std::clamp((db+60.f)/60.f,0.f,1.f))-(rms?.75f:.5f),17.f,17.f+ph-(rms?1.5f:1.f));
                for(int y=int(py*dpi);y<=int((py+(rms?1.5f:1.f))*dpi);++y){auto c=raster.getPixelAt(int(px*dpi),y);if(rms?(c.getRed()>85&&c.getGreen()>210&&c.getBlue()>220):(c.getRed()>95&&c.getGreen()>70&&c.getRed()>c.getBlue()+35&&c.getGreen()>c.getBlue()+25))return true;}return false;
              };
              require(markerPresent(c.rmsDb,true),"Independent RMS marker is missing");require(markerPresent(c.heldPeakDb,false),"Independent held peak marker is missing");
            }
            saveRaster(raster,"workspace-meter-"+juce::String(geometry.x)+"x"+juce::String(geometry.y)+"-L"+juce::String(levels.x,3)+"-R"+juce::String(levels.y,3)+"-"+juce::String(int(dpi*100)));
          }
        }
        // Current peak decays12dB during0.5s silence; the1s held peak
        // remains at the original level. That separates hold from amber peak
        // LEDs and makes an omitted-hold implementation observably wrong.
        signal.prepare(48000);rack.refresh();std::vector<float> loud(14400,.5f),quiet(24000,0.f);signal.push(loud.data(),loud.data(),int(loud.size()));signal.push(quiet.data(),quiet.data(),int(quiet.size()));rack.refresh();
        const auto held=signal.snapshot();require(held.left.heldPeakDb-held.left.peakDb>11.9f,"Held marker fixture did not separate from sample peak");
        for(float dpi:{1.f,2.f}){
          const auto raster=meter.createComponentSnapshot(meter.getLocalBounds(),true,dpi);
          const int gutter=geometry.y<150?17:21,laneWidth=(geometry.x-4-gutter-4)/2,ph=geometry.y-(geometry.y<150?50:69);
          auto markerPresent=[&](const juce::Image& image,int channel,bool rms){
            const auto& c=channel==0?held.left:held.right;const float value=rms?c.rmsDb:c.heldPeakDb;
            const float x=gutter+channel*(laneWidth+3)+laneWidth*(rms?.5f:.1f);
            const float y=std::clamp(17.f+ph*(1.f-(value+60.f)/60.f)-(rms?.75f:.5f),17.f,17.f+ph-(rms?1.5f:1.f));
            for(int py=int(y*dpi);py<=int((y+(rms?1.5f:1.f))*dpi);++py){const auto c=image.getPixelAt(int(x*dpi),py);if(rms?(c.getRed()>85&&c.getGreen()>210&&c.getBlue()>220):(c.getRed()>95&&c.getGreen()>70&&c.getRed()>c.getBlue()+35&&c.getGreen()>c.getBlue()+25))return true;}return false;
          };
          for(int channel=0;channel<2;++channel)for(bool rms:{false,true}){
            require(markerPresent(raster,channel,rms),rms?"Decayed measured RMS marker missing":"Held sample peak missing above decayed LEDs");
            auto erased=raster.createCopy();const float x=gutter+channel*(laneWidth+3)+laneWidth*(rms?.5f:.1f);
            {juce::Graphics g(erased);g.setColour(juce::Colour(0xff020609));g.fillRect(int(x*dpi),int(17*dpi),1,int(ph*dpi));}
            require(!markerPresent(erased,channel,rms),"Pixel oracle accepted deliberately erased marker");
          }
          saveRaster(raster,"workspace-meter-held-decay-"+juce::String(geometry.x)+"x"+juce::String(geometry.y)+"-"+juce::String(int(dpi*100)));
        }
        const float clipped=1.1f,nan=std::numeric_limits<float>::quiet_NaN();signal.prepare(48000);rack.refresh();signal.push(&clipped,&nan,1);rack.refresh();
        const auto invalid=meter.createComponentSnapshot(meter.getLocalBounds());require(signal.snapshot().left.clipped&&signal.snapshot().nonFiniteSamples==1,"Invalid/clip detector fixture missing");
        meter.keyPressed(juce::KeyPress(juce::KeyPress::returnKey));require(signal.snapshot().left.clipped,"Clip reset must be deferred");signal.push(nullptr,nullptr,0);rack.refresh();const auto reset=meter.createComponentSnapshot(meter.getLocalBounds());require(differences(invalid,reset)>4,"Clip/invalid reset is not visible");
        signal.prepare(0);rack.refresh();const auto disconnected=meter.createComponentSnapshot(meter.getLocalBounds());
        require(laneEnergy(disconnected,0,geometry.x,geometry.y,1)==0&&laneEnergy(disconnected,1,geometry.x,geometry.y,1)==0,"Unavailable meter lights guessed signal");
      }
      {
        using Access=hem::NativeWorkspaceTestAccess;
        auto state=[&]{return int(meter.getProperties()["meterState"]);};
        signal.prepare(48000);Access::metersAt(rack,0);require(state()==1,"Prepared workspace meter is not waiting");
        signal.push(nullptr,nullptr,0);Access::metersAt(rack,50);require(state()==1,"Zero-frame publication fabricated live audio");
        for(int i=1;i<=5;++i){signal.push(nullptr,nullptr,64);Access::metersAt(rack,i*900.);require(state()==2,"Constant silent callbacks did not stay live");}
        Access::metersAt(rack,5499);require(state()==2,"Meter expired before1s");Access::metersAt(rack,5500);require(state()==3,"Frozen silent callbacks did not expire at1s");
        signal.resetClip();signal.push(nullptr,nullptr,0);Access::metersAt(rack,5600);require(state()==3,"Zero-frame clip reset refreshed stale audio");
        signal.prepare(48000);Access::metersAt(rack,5700);require(state()==1,"Reprepare retained old live status");
        std::vector<float> tone(4096,.25f);for(int i=1;i<=5;++i){signal.push(tone.data(),tone.data(),int(tone.size()));Access::metersAt(rack,5700+i*900.);require(state()==2,"Constant tone callbacks did not stay live");}
        const auto unchanged=signal.snapshot();Access::metersAt(rack,11200);require(state()==3,"Frozen tone did not expire");
        require(signal.snapshot().publication==unchanged.publication&&signal.snapshot().left.heldPeakDb==unchanged.left.heldPeakDb,"UI freshness mutated detector publication/hold");
        const auto stale=meter.createComponentSnapshot(meter.getLocalBounds());require(laneEnergy(stale,0,meter.getWidth(),meter.getHeight(),1)==0,"Stale tone continues to light sample LEDs");
        signal.prepare(0);Access::metersAt(rack,11300);require(state()==0,"Unavailable workspace source stayed live");
        signal.prepare(48000);Access::metersAt(rack,11400);require(state()==1,"Reprepared workspace source not waiting");
        signal.push(tone.data(),tone.data(),int(tone.size()));Access::metersAt(rack,11500);require(state()==2,"New real callback failed to become live");
        // Both sources already hold old audio. Changing selection or bank must
        // seed dark instead of borrowing an old source's last-advance timestamp.
        auto& other=rackBank->tracks[1].signalMeters;other.prepare(48000);other.push(tone.data(),tone.data(),int(tone.size()));trackIndex=1;Access::metersAt(rack,11600);require(state()==1,"New source lit a frozen old publication");
        other.push(tone.data(),tone.data(),int(tone.size()));Access::metersAt(rack,11610);require(state()==2,"Source switch did not resume on actual progress");
        auto oldBank=std::make_unique<hem::TrackBank>();oldBank->tracks[1].signalMeters.prepare(48000);oldBank->tracks[1].signalMeters.push(tone.data(),tone.data(),int(tone.size()));rackBank.swap(oldBank);Access::metersAt(rack,11700);require(state()==1,"Replacement bank reused live-source freshness");rackBank.swap(oldBank);trackIndex=0;Access::metersAt(rack,11800);require(state()==1,"Restored bank lit stale old source");
        signal.push(tone.data(),tone.data(),int(tone.size()));Access::metersAt(rack,11810);require(state()==2,"Restored bank did not resume on actual progress");
        Access::metersAt(rack,12810);require(state()==3,"Contention fixture did not start stale");
        const auto frames=signal.snapshot().processedSamples;int unavailableReads=0;
        hem::NativeWorkspace::Callbacks coldCallbacks;coldCallbacks.bank=[&]() -> hem::TrackBank& {return *rackBank;};coldCallbacks.getSelectedTrack=[] {return 0;};
        hem::NativeWorkspace cold(std::move(coldCallbacks));auto& coldMeter=control<juce::Component>(cold,"plugin-signal-meter");
        Access::metersAt(cold,0);require(int(coldMeter.getProperties()["meterState"])==1,"Newly attached old snapshot should wait for progress");
        {
          std::atomic<bool> started{false};std::jthread zeroFrameWriter([&](std::stop_token stop){started.store(true);while(!stop.stop_requested())signal.push(nullptr,nullptr,0);});
          while(!started.load())std::this_thread::yield();
          for(int i=0;i<20000&&(i<2000||unavailableReads==0);++i){Access::metersAt(rack,13000+i);const int observed=state();unavailableReads+=observed==0;require(observed==0||observed==3,"Unavailable snapshot reset baseline and revived stale unchanged audio");Access::metersAt(cold,i);const int waiting=int(coldMeter.getProperties()["meterState"]);require(waiting==0||waiting==1,"Contention revived a new source without callback progress");}
        }
        Access::metersAt(cold,40000);require(int(coldMeter.getProperties()["meterState"])==1,"First coherent no-progress frame became live after contention");
        require(unavailableReads>0,"Real contention fixture never produced an unavailable read");
        Access::metersAt(rack,40000);require(state()==3&&signal.snapshot().processedSamples==frames,"Coherent zero-frame return fabricated progress after contention");
        std::cout<<"PASS workspace zero-frame publication contention: "<<unavailableReads<<" unavailable snapshots; no fresh-audio relight\n";
      }
      rack.resized();
      // Bank replacement invalidates captured controls even when slot indices
      // and the selected page remain identical.
      auto staleBankMove=up.onClick;auto nextBank=std::make_unique<hem::TrackBank>();rackBank.swap(nextBank);rack.refresh();staleBankMove();require(moves.size()==4,"Old reorder survived bank replacement");rackBank.swap(nextBank);rack.refresh();
      saveRaster(rack.createComponentSnapshot(rack.getLocalBounds()),"workspace-reorder-controls-1046x395");
      rackBank->tracks[0].release();
      std::cout<<"PASS reachable rack reorder controls; occupied/empty swaps; selection and bypass retention; first/last bounds; busy/missing-callback/stale-click guards; distinct measured L/R LED pixels\n";
    }

    // Real callbacks may destroy their owner, or refresh/rebind the same
    // std::function that is currently dispatching. Borrow no closure state after
    // crossing host code. Processor bank outlives every victim workspace here.
    {
      auto ownedBank=std::make_unique<hem::TrackBank>();ownedBank->tracks[0].prepare(48000,128);
      auto& chain=ownedBank->tracks[0].chain;chain.installInitial(0,chain.stageNode(hem::PluginRegistry::entries()[0].create(),48000,128,chain.slotLatency()),false);
      std::unique_ptr<hem::NativeWorkspace> victim;int opened=0;
      auto callbacks=[&]{hem::NativeWorkspace::Callbacks cb;cb.bank=[&]() -> hem::TrackBank& {return *ownedBank;};cb.getSelectedTrack=[] {return 0;};cb.getSelectedSlot=[] {return 0;};cb.onOpenSlot=[&](int){++opened;};return cb;};
      auto make=[&](hem::NativeWorkspace::Callbacks cb){victim=std::make_unique<hem::NativeWorkspace>(std::move(cb));victim->setSize(1046,395);victim->setVisible(true);};
      auto cb=callbacks();cb.onMoveSlot=[&](int,int){victim.reset();};make(cb);click(*victim,"plugin-slot-down");require(!victim,"Move did not permit owner retirement");
      cb=callbacks();cb.setSelectedSlot=[&](int){victim.reset();};make(cb);click(*victim,"plugin-slot-0");require(!victim&&opened==0,"Slot open used destroyed owner after selection callback");
      cb=callbacks();cb.setSelectedTrack=[&](int){victim.reset();};make(cb);auto& choice=control<juce::ComboBox>(*victim,"plugin-track-select");choice.setSelectedId(2,juce::dontSendNotification);choice.onChange();require(!victim,"Track selection used destroyed owner");
      make(cb);victim->setPage(hem::NativeWorkspace::Page::mixer);click(*victim,"mixer-select-0");require(!victim&&opened==0,"Mixer Edit opened after owner retirement");
      cb=callbacks();cb.onInstallModule=[&](int){victim.reset();};make(cb);victim->setPage(hem::NativeWorkspace::Page::vault);click(*victim,"vault-install-0");require(!victim,"Vault install used destroyed owner after host callback");
      cb=callbacks();cb.onMonitor=[&]{victim.reset();};make(cb);victim->setPage(hem::NativeWorkspace::Page::vault);click(*victim,"vault-install-11");require(!victim,"Monitor action used destroyed owner after host callback");
      ownedBank->tracks[0].release();std::cout<<"PASS reentrant rack refresh and move/slot/track/mixer/Vault owner-destruction callbacks\n";
    }
    if(argc>2&&juce::String(argv[2])=="--controls-only")return 0;
    // No AudioDeviceManager or hardware. Real engine, registry and UI components.
    juce::AudioBuffer<float> sourceAudio(2,96000);sourceAudio.clear();
    for(int ch=0;ch<2;++ch)for(int i=0;i<sourceAudio.getNumSamples();++i)sourceAudio.setSample(ch,i,.0316228f);
    juce::TemporaryFile fixture(".wav");{std::unique_ptr<juce::OutputStream> stream=fixture.getFile().createOutputStream();juce::WavAudioFormat wav;
      auto writer=wav.createWriterFor(stream,juce::AudioFormatWriterOptions().withSampleRate(96000).withNumChannels(2).withBitsPerSample(32).withSampleFormat(juce::AudioFormatWriterOptions::SampleFormat::floatingPoint));
      require(writer&&writer->writeFromAudioSampleBuffer(sourceAudio,0,sourceAudio.getNumSamples())&&writer->flush(),"Workspace WAV fixture write failed");}
    juce::AudioFormatManager formats;formats.registerBasicFormats();juce::TimeSliceThread disk("Workspace fixture read-ahead");disk.startThread();
    auto bank=std::make_unique<hem::TrackBank>();
    for(int i=0;i<13;++i){auto& track=bank->tracks[size_t(i)];track.name="STEM "+juce::String(i+1);track.prepare(96000,128);}
    auto& first=bank->tracks[0];first.playback->prepareFile(fixture.getFile(),formats,disk,96000,128,true);first.loaded=true;first.transport().start();
    int selectedTrack=0,selectedSlot=0,installed=-1,opened=-1,monitorCalls=0;
    hem::MonitorProcessor monitor;monitor.prepareToPlay(96000,128);
    hem::NativeWorkspace* target=nullptr;
    hem::NativeWorkspace::Callbacks callbacks;
    callbacks.bank=[&]() -> hem::TrackBank& {return *bank;};
    callbacks.getSelectedTrack=[&]{return selectedTrack;};callbacks.getSelectedSlot=[&]{return selectedSlot;};
    callbacks.setSelectedTrack=[&](int i){if(target)target->clearEditor();selectedTrack=i;};
    callbacks.setSelectedSlot=[&](int i){if(target)target->clearEditor();selectedSlot=i;};
    callbacks.onOpenSlot=[&](int slot){opened=slot;if(auto* p=bank->tracks[size_t(selectedTrack)].chain.activeProcessor(slot))target->setEditor(std::unique_ptr<juce::AudioProcessorEditor>(p->createEditor()));};
    callbacks.onInstallModule=[&](int index){installed=index;auto& chain=bank->tracks[size_t(selectedTrack)].chain;require(chain.activeProcessor(selectedSlot)==nullptr,"Test attempted to overwrite an occupied slot");auto node=chain.stageNode(hem::PluginRegistry::entries()[size_t(index)].create(),96000,128,chain.slotLatency());chain.installInitial(selectedSlot,std::move(node),false);};
    callbacks.onMonitor=[&]{++monitorCalls;target->setEditor(std::unique_ptr<juce::AudioProcessorEditor>(monitor.createEditor()));};
    callbacks.routeTelemetry=[] {hem::NativeWorkspace::RouteTelemetry t;t.sampleRate=96000;t.blockSize=128;t.hardwareOutputs=2;t.limiterLatencySamples=384;return t;};
    hem::NativeWorkspace workspace(std::move(callbacks));target=&workspace;workspace.setSize(1255,489);
    workspace.setVisible(true); // offscreen component: JUCE hit testing requires visibility
    require(workspace.page()==hem::NativeWorkspace::Page::plugin,"Initial workspace page is incorrect");
    for(int i=0;i<8;++i)require(find(workspace,"plugin-slot-"+juce::String(i))!=nullptr,"One of eight serial slots is absent");
    require(workspace.filteredRegistryIndices().size()==hem::PluginRegistry::entries().size(),"Vault did not use all real registry entries");
    workspace.setVaultFilter("compressor");require(workspace.filteredRegistryIndices()==std::vector<int>{2},"Text search did not find real VCA registry entry");
    std::vector<int> dynamics;
    for(size_t i=0;i<hem::PluginRegistry::entries().size();++i)
        if(juce::String(hem::PluginRegistry::entries()[i].category)=="Dynamics")dynamics.push_back(int(i));
    workspace.setVaultFilter("","Dynamics");require(workspace.filteredRegistryIndices()==dynamics,"Category filter does not match registry categories");
    workspace.setVaultFilter("tape","Dynamics");require(workspace.filteredRegistryIndices().empty(),"Search/category conjunction is incorrect");
    workspace.setVaultFilter("TaPe");require(workspace.filteredRegistryIndices()==std::vector<int>{10,25},"Search should ignore case across legacy Tape and recovered Tape Delay");
    workspace.setVaultFilter("","All categories");
    control<juce::TextEditor>(workspace,"vault-search").setText("surgical",false);control<juce::TextEditor>(workspace,"vault-search").onTextChange();
    require(workspace.filteredRegistryIndices()==std::vector<int>{1},"Production text editor was not connected to filter");workspace.setVaultFilter("");
    workspace.setPage(hem::NativeWorkspace::Page::vault);click(workspace,"vault-install-0");
    require(installed==0&&bank->tracks[0].chain.activeProcessor(0)!=nullptr,"Vault install did not reach the actual registry/track chain");
    workspace.setPage(hem::NativeWorkspace::Page::plugin);click(workspace,"plugin-slot-0");
    require(opened==0&&workspace.hasEditor(),"Real slot did not open its native editor");
    auto& viewport=control<juce::Viewport>(workspace,"plugin-editor-viewport");
    auto* canvas=viewport.getViewedComponent();require(canvas!=nullptr,"Editor viewport has no canvas");
    juce::AudioProcessorEditor* embedded=nullptr;for(auto* c:canvas->getChildren())if(auto* e=dynamic_cast<juce::AudioProcessorEditor*>(c))embedded=e;
    require(embedded!=nullptr&&embedded->getParentComponent()==canvas,"Native editor was not embedded in this window");
    const int nativeWidth=embedded->getWidth(),nativeHeight=embedded->getHeight();
    saveRaster(workspace.createComponentSnapshot(workspace.getLocalBounds()),"plugin-native-editor");
    juce::Component::SafePointer<juce::AudioProcessorEditor> editorObserver(embedded);
    auto requireFit=[&]{
        require(viewport.getViewArea().contains(canvas->getLocalArea(embedded,embedded->getLocalBounds())),"Complete editor is clipped by its container");
        require(!viewport.getHorizontalScrollBar().isVisible()&&!viewport.getVerticalScrollBar().isVisible(),"Fitted editor unexpectedly needs scrolling");
        require(embedded->getWidth()==nativeWidth&&embedded->getHeight()==nativeHeight,"Fitting mutated the native editor layout");
        requireHostFit(*embedded,*canvas);
        require(viewport.getX()==0&&viewport.getWidth()==workspace.getWidth(),"Editor does not occupy full primary workspace width");
        require(!control<juce::Viewport>(workspace,"plugin-stem-viewport").isVisible(),"Stem sidebar still visible over editor");
    };
    requireFit();
    for(auto size:{juce::Point<int>(1046,395),juce::Point<int>(1255,489),juce::Point<int>(800,320),juce::Point<int>(753,333),juce::Point<int>(1153,687),juce::Point<int>(1882,733)}){
        workspace.setSize(size.x,size.y);
        for(bool expanded:{false,true}){workspace.setPluginExpanded(expanded);requireFit();
            for(float dpi:{1.f,1.25f,1.5f,2.f}){
                auto image=workspace.createComponentSnapshot(workspace.getLocalBounds(),true,dpi);
                require(image.isValid()&&std::abs(image.getWidth()-size.x*dpi)<=1,"High-DPI snapshot geometry mismatch");
                requireFit();
                if(size.x==1046&&dpi==2.f)saveRaster(image,expanded?"plugin-fit-expanded-200pct":"plugin-fit-split-200pct");
            }
        }
    }
    workspace.setSize(1255,489);workspace.setPluginExpanded(false);
    workspace.setPluginExpanded(true);require(workspace.pluginExpanded()&&!control<juce::Viewport>(workspace,"plugin-stem-viewport").isVisible(),"Plugin expansion did not hide stem pane");
    require(embedded->getWidth()==nativeWidth&&embedded->getHeight()==nativeHeight,"Expanding changed original native editor dimensions");
    saveRaster(workspace.createComponentSnapshot(workspace.getLocalBounds()),"plugin-expanded");
    click(workspace,"plugin-expand");require(!workspace.pluginExpanded(),"Production restore-split control was not wired");require(workspace.page()==hem::NativeWorkspace::Page::vault,"Back did not restore the editor origin");workspace.setPage(hem::NativeWorkspace::Page::plugin);
    workspace.setSplitProportion(.52f);const int largeLeft=control<juce::Viewport>(workspace,"plugin-stem-viewport").getWidth();
    workspace.setSplitProportion(.24f);require(control<juce::Viewport>(workspace,"plugin-stem-viewport").getWidth()<largeLeft,"Split proportion did not adjust real geometry");
    workspace.setSplitProportion(2);require(workspace.splitProportion()==.60f,"Split upper bound not enforced");workspace.setSplitProportion(std::numeric_limits<float>::quiet_NaN());require(workspace.splitProportion()==.60f,"Non-finite split corrupted geometry");workspace.setSplitProportion(.32f);
    click(workspace,"plugin-bypass-0");require(bank->tracks[0].chain.isBypassed(0)&&control<juce::TextButton>(workspace,"plugin-bypass-0").getToggleState(),"Bypass did not update real insert and illumination");
    click(workspace,"plugin-bypass-0");require(!bank->tracks[0].chain.isBypassed(0),"Repeated bypass failed");
    const float clippedSample=1.1f;first.signalMeters.push(&clippedSample,&clippedSample,1);workspace.refresh();require(first.signalMeters.snapshot().left.clipped,"Actual meter failed to latch a clipped test sample");
    require(control<juce::Component>(workspace,"plugin-signal-meter").keyPressed(juce::KeyPress(juce::KeyPress::returnKey)),"Plugin meter has no keyboard reset");require(first.signalMeters.snapshot().left.clipped,"Clip reset was not deferred to audio publication");first.signalMeters.push(nullptr,nullptr,0);require(!first.signalMeters.snapshot().left.clipped,"Plugin meter reset did not reach actual clip detector");
    workspace.setPage(hem::NativeWorkspace::Page::mixer);
    first.signalMeters.push(&clippedSample,&clippedSample,1);workspace.refresh();require(control<juce::Component>(workspace,"mixer-signal-meter-0").keyPressed(juce::KeyPress(juce::KeyPress::spaceKey)),"Mixer meter has no keyboard reset");first.signalMeters.push(nullptr,nullptr,0);require(!first.signalMeters.snapshot().left.clipped&&!first.signalMeters.snapshot().right.clipped,"Mixer meter reset did not clear both actual clip latches");
    for(int i=0;i<13;++i){auto& gain=control<juce::Slider>(workspace,"mixer-gain-"+juce::String(i));require(gain.getMinimum()==-60&&gain.getMaximum()==6,"Mixer gain range is not -60..+6 dB");require(!control<juce::TextButton>(workspace,"mixer-mute-"+juce::String(i)).getToggleState(),"Mute lit without engine state");require(!control<juce::TextButton>(workspace,"mixer-solo-"+juce::String(i)).getToggleState(),"Solo lit without engine state");}
    for(int i=0;i<13;++i){
      auto& t=bank->tracks[size_t(i)];const auto old=t.trimDb.load();auto& slider=control<juce::Slider>(workspace,"mixer-gain-"+juce::String(i));
      slider.setValue(-float(i+1),juce::sendNotificationSync);require(t.trimDb.load()==-float(i+1),"A mixer fader targets the wrong track");
      click(workspace,"mixer-mute-"+juce::String(i));require(t.muted.load(),"A mixer mute targets the wrong track");
      click(workspace,"mixer-solo-"+juce::String(i));require(t.solo.load(),"A mixer solo targets the wrong track");
      click(workspace,"mixer-mute-"+juce::String(i));click(workspace,"mixer-solo-"+juce::String(i));slider.setValue(old,juce::sendNotificationSync);
      click(workspace,"mixer-select-"+juce::String(i));require(selectedTrack==i&&workspace.page()==hem::NativeWorkspace::Page::plugin,"A mixer Edit action targets the wrong track");workspace.clearEditor();workspace.setPage(hem::NativeWorkspace::Page::mixer);
    }
    selectedTrack=0;workspace.refresh();
    auto& gain=control<juce::Slider>(workspace,"mixer-gain-3");gain.setValue(-11.2,juce::sendNotificationSync);require(std::abs(bank->tracks[3].trimDb.load()+11.2f)<.001f,"Mixer fader did not change real atomic gain");
    click(workspace,"mixer-mute-3");click(workspace,"mixer-solo-3");require(bank->tracks[3].muted.load()&&bank->tracks[3].solo.load(),"Mute/solo not connected to real track state");
    click(workspace,"mixer-mute-3");click(workspace,"mixer-solo-3");
    click(workspace,"mixer-select-12");require(selectedTrack==12&&workspace.page()==hem::NativeWorkspace::Page::plugin,"Mixer edit did not select thirteenth track");require(editorObserver==nullptr&&!workspace.hasEditor(),"Host selection did not destroy borrowed editor");
    // Navigation alone must preserve running transports, insert and gain state.
    auto render=[&]{const auto deadline=juce::Time::getMillisecondCounterHiRes()+2000;while(!first.transport().availableRt(128)){require(juce::Time::getMillisecondCounterHiRes()<deadline,"Workspace WAV prefill timeout");juce::Thread::sleep(1);}juce::AudioBuffer<float> sum(2,128);sum.clear();std::array<float,128> gains;gains.fill(1);for(auto& track:bank->tracks)track.render(sum,128,false,gains.data());};
    for(int i=0;i<60;++i)render();workspace.setPage(hem::NativeWorkspace::Page::mixer);workspace.refresh();
    const double position=first.transport().getCurrentPosition();const bool playing=first.transport().isPlaying();
    for(auto page:{hem::NativeWorkspace::Page::routing,hem::NativeWorkspace::Page::vault,hem::NativeWorkspace::Page::plugin,hem::NativeWorkspace::Page::mixer})workspace.setPage(page);
    require(first.transport().isPlaying()==playing&&playing,"Workspace navigation interrupted playback");require(first.transport().getCurrentPosition()==position,"Workspace navigation performed a seek");require(bank->tracks[0].chain.activeProcessor(0)!=nullptr&&bank->tracks[3].trimDb.load()==float(gain.getValue()),"Navigation changed audio state");
    render();require(first.transport().getCurrentPosition()>position,"Transport no longer advanced after navigation");
    workspace.refresh();auto withSignal=workspace.createComponentSnapshot(workspace.getLocalBounds());
    first.signalMeters.prepare(0);workspace.refresh();auto noMeter=workspace.createComponentSnapshot(workspace.getLocalBounds());require(differences(withSignal,noMeter)>25,"Mixer did not paint actual atomic signal telemetry");
    // Real production raster at multiple window sizes: every strip reachable,
    // readable geometry retained; narrow layouts use viewport scrolling.
    for(auto size:{juce::Point<int>(1255,489),juce::Point<int>(1046,395),juce::Point<int>(960,420),juce::Point<int>(752,332),juce::Point<int>(753,333),juce::Point<int>(1882,733)}){
        workspace.setSize(size.x,size.y);workspace.setPage(hem::NativeWorkspace::Page::mixer);
        for(int i=0;i<13;++i){auto& strip=control<juce::Component>(workspace,"mixer-strip-"+juce::String(i));require(strip.getWidth()>=86&&strip.getHeight()>=270,"Mixer compressed a strip below readable minimum");require(strip.getBounds().getRight()<=strip.getParentComponent()->getWidth(),"Thirteenth strip fell outside scrollable content");}
        // The prior aperture was1046x395; approved00 exposes752x332. Test both.
        // Test the visible viewport (including horizontal scrollbar loss), not
        // just content bounds, so mute/solo/edit can never hide below it.
        auto& mixerViewport=control<juce::Viewport>(workspace,"mixer-viewport");
        mixerViewport.setViewPosition(0,0);const auto visibleArea=mixerViewport.getViewArea();
        require(!mixerViewport.getVerticalScrollBar().isVisible(),"Mixer requires vertical scrolling at a supported workspace size");
        for(int i=0;i<13;++i){
            for(const auto* prefix:{"mixer-gain-","mixer-readout-","mixer-mute-","mixer-solo-","mixer-select-"}){
                auto& item=control<juce::Component>(workspace,juce::String(prefix)+juce::String(i));
                const auto visibleBounds=mixerViewport.getViewedComponent()->getLocalArea(&item,item.getLocalBounds());
                require(visibleBounds.getY()>=visibleArea.getY()&&visibleBounds.getBottom()<=visibleArea.getBottom(),"Mixer control is vertically clipped by the actual viewport");
            }
        }
        if(size.x==1046&&size.y==395){
            require(mixerViewport.getHorizontalScrollBar().isVisible(),"Default shell should preserve strip width with horizontal scrolling");
            mixerViewport.setViewPosition(mixerViewport.getViewedComponent()->getWidth(),0);
            const auto finalArea=mixerViewport.getViewArea();auto& finalButton=control<juce::Component>(workspace,"mixer-select-12");
            require(finalArea.contains(mixerViewport.getViewedComponent()->getLocalArea(&finalButton,finalButton.getLocalBounds())),"The thirteenth mixer edit button is not fully reachable by horizontal scrolling");
            saveRaster(workspace.createComponentSnapshot(workspace.getLocalBounds()),"mixer-last-strip-1046x395");
            mixerViewport.setViewPosition(0,0);
        }
        for(auto page:{hem::NativeWorkspace::Page::plugin,hem::NativeWorkspace::Page::mixer,hem::NativeWorkspace::Page::vault,hem::NativeWorkspace::Page::routing}){workspace.setPage(page);auto image=workspace.createComponentSnapshot(workspace.getLocalBounds());require(image.isValid()&&image.getWidth()==size.x&&image.getHeight()==size.y,"Workspace raster geometry is invalid");require(readablePixels(image)>1000,"Workspace page lacks readable live UI pixels");saveRaster(image,"workspace-page-"+juce::String(int(page))+"-"+juce::String(size.x)+"x"+juce::String(size.y));}
    }
    workspace.setSize(1255,489);workspace.setPage(hem::NativeWorkspace::Page::vault);click(workspace,"vault-install-11");require(monitorCalls==1&&workspace.hasEditor()&&installed==0,"Monitoring-only registry entry incorrectly installed a stem insert");workspace.clearEditor();require(!workspace.hasEditor(),"Editor cleanup failed");
    {
      hem::SurgicalEqProcessor eq;
      // The workspace owns an editor borrowing eq. Even a failed assertion must
      // detach its parameter attachments BEFORE this local processor is destroyed.
      struct DetachEditorBeforeProcessor {hem::NativeWorkspace& host;~DetachEditorBeforeProcessor(){host.clearEditor();}} detach{workspace};
      std::cerr<<"BEGIN Surgical EQ fit fixture"<<std::endl;
      workspace.setEditor(std::unique_ptr<juce::AudioProcessorEditor>(eq.createEditor()));workspace.setPage(hem::NativeWorkspace::Page::plugin);
      auto* eqCanvas=viewport.getViewedComponent();juce::AudioProcessorEditor* eqEditor=nullptr;
      for(auto* child:eqCanvas->getChildren())if(auto* e=dynamic_cast<juce::AudioProcessorEditor*>(child))eqEditor=e;
      require(eqEditor!=nullptr,"Surgical EQ editor absent");
      for(auto size:{juce::Point<int>(1046,395),juce::Point<int>(1255,489),juce::Point<int>(800,320),juce::Point<int>(753,333),juce::Point<int>(1153,687),juce::Point<int>(1882,733)}){
        workspace.setSize(size.x,size.y);
        for(bool expanded:{false,true}){std::cerr<<"EQ geometry "<<size.x<<"x"<<size.y<<" expanded="<<expanded<<std::endl;workspace.setPluginExpanded(expanded);
          require(viewport.getViewArea().contains(eqCanvas->getLocalArea(eqEditor,eqEditor->getLocalBounds())),"Surgical EQ is clipped after fit");requireHostFit(*eqEditor,*eqCanvas);
          for(const auto& spec:eq.parameterSpecs()){
            auto* item=find(*eqEditor,spec.id);require(item!=nullptr,"Surgical EQ parameter missing");
            require(viewport.getViewArea().contains(eqCanvas->getLocalArea(item,item->getLocalBounds())),"Surgical EQ control clipped");
            auto point=item->getLocalBounds().getCentre();if(auto* slider=dynamic_cast<juce::Slider*>(item))point=slider->getLookAndFeel().getSliderLayout(*slider).sliderBounds.getCentre();
            auto* hit=workspace.getComponentAt(workspace.getLocalPoint(item,point));
            if(hit!=item&&!item->isParentOf(hit))std::cerr<<"EQ hit mismatch parameter="<<spec.id<<" enabled="<<item->isEnabled()<<" actual="<<(hit?hit->getComponentID():"null")<<std::endl;
            require(hit==item||item->isParentOf(hit),"Fitted EQ pointer hits another control");
            require(eqEditor->isEnabled()==(eqEditor->getTransform().mat00>=.8999f),"Preview interaction threshold mismatch");
            require(std::abs(eq.state.getRawParameterValue(spec.id)->load()-spec.initial)<.001f,"Layout changed EQ parameter value");
          }
          if(size.x==1046)saveRaster(workspace.createComponentSnapshot(workspace.getLocalBounds(),true,2.f),expanded?"surgical-fit-expanded-200pct":"surgical-fit-split-200pct");
        }
      }
      workspace.clearEditor();
      std::cerr<<"END Surgical EQ fit fixture"<<std::endl;
    }
    {
      hem::SurgicalEqProcessor processor;
      struct Detach {hem::NativeWorkspace& w;~Detach(){w.clearEditor();}} detach{workspace};
      workspace.setVisible(false);
      auto editor=std::make_unique<ScaleObservedEditor>(processor);auto* actual=editor.get();workspace.setEditor(std::move(editor));
      require(actual->scaleCalls>0,"Host never called supported AudioProcessorEditor::setScaleFactor");
      require(!control<juce::TextButton>(workspace,"plugin-expand").hasKeyboardFocus(false),"Hidden editor stole keyboard focus");
      workspace.setVisible(true);
      for(int repeat=0;repeat<8;++repeat)for(auto size:{juce::Point<int>(129,81),juce::Point<int>(752,332),juce::Point<int>(753,333),juce::Point<int>(1153,687),juce::Point<int>(1281,759)}){
        workspace.setSize(size.x,size.y);workspace.refresh();requireHostFit(*actual,*viewport.getViewedComponent());
        require(actual->scaleCalls<1000&&actual->maxDepth==1,"Editor fit recursed");
        require(actual->isEnabled()==(actual->hostScale>=.8999f),"Supported host scale bypassed preview interaction gate");
        require(!viewport.getHorizontalScrollBar().isVisible()&&!viewport.getVerticalScrollBar().isVisible(),"Repeated editor resize enabled scrollbars");
      }
      for(auto nativeSize:{juce::Point<int>(1281,719),juce::Point<int>(911,613),juce::Point<int>(1673,941)}){
        actual->setSize(nativeSize.x,nativeSize.y);workspace.refresh();requireHostFit(*actual,*viewport.getViewedComponent());
        require(actual->getWidth()==nativeSize.x&&actual->getHeight()==nativeSize.y,"Host rewrote native editor size");
      }
      auto oldBack=control<juce::TextButton>(workspace,"plugin-expand").onClick;
      auto replacementEditor=std::make_unique<ScaleObservedEditor>(processor);auto* replacement=replacementEditor.get();workspace.setEditor(std::move(replacementEditor));oldBack();
      require(workspace.hasEditor()&&replacement->getParentComponent()!=nullptr,"Old Back action closed newer editor");
      auto closedBack=control<juce::TextButton>(workspace,"plugin-expand").onClick;click(workspace,"plugin-expand");closedBack();require(!workspace.hasEditor(),"Repeated Back restored a closed editor");
      workspace.clearEditor();workspace.setPage(hem::NativeWorkspace::Page::mixer);workspace.setSize(1255,489);
      std::cout<<"PASS supported host scale API, odd/tiny/repeated geometry, editor-size changes, recursion guard and hidden focus policy\n";
    }
    const std::vector<juce::String> expectedIdentities={"sipa","surgical-eq","vca","true-peak","dhta","transient","sub-bass","spectral-restorer","vocal-presence","triode-tube","tape","monitor-eq","dc-subsonic","smart-gate","vocal-rider","plosive-control","hum-buzz","loudness-lab","spectrum-spectrogram","delta-monitor","convolution-reverb","algorithmic-reverb","chorus-flanger-phaser","rotary-tremolo-autopan","multitap-delay","tape-delay","dynamic-eq-8","mid-side-eq","multiband-dynamics","multiband-imager","parallel-distortion","exciter","dynamic-resonance-suppressor","breath-mouth-control","clip-repair","drum-replacer","amp-cabinet","reference-match","pitch-formant","time-pitch-stretch"};
    require(hem::PluginRegistry::entries().size()==expectedIdentities.size(),"Stable registry identity count changed");
    for(size_t i=0;i<expectedIdentities.size();++i)require(juce::String(hem::PluginRegistry::entries()[i].id)==expectedIdentities[i],"Stable registry ordering/identity changed");
    std::set<juce::String> visited;
    for(const auto& entry:hem::PluginRegistry::entries()){
      require(visited.insert(entry.id).second,"Duplicate registry identity");
      auto processor=entry.create();
      auto untouched=entry.create();
      processor->setPlayConfigDetails(2,2,48000,256);untouched->setPlayConfigDetails(2,2,48000,256);
      if(entry.realtimeInsert){processor->prepareToPlay(48000,256);untouched->prepareToPlay(48000,256);}else {require(entry.clipOperation&&!entry.trackingAllowed,"Offline capability contract missing");for(auto* p:{processor.get(),untouched.get()}){bool rejected=false;try{p->prepareToPlay(48000,256);}catch(const std::exception&){rejected=true;}require(rejected&&!p->processingReady(),"Offline editor admitted realtime preparation");}}
      struct Detach {hem::NativeWorkspace& w;~Detach(){w.clearEditor();}} detach{workspace};
      juce::MemoryBlock before,after;processor->getStateInformation(before);
      for(int cycle=0;cycle<3;++cycle){
        const auto origin=cycle==0?hem::NativeWorkspace::Page::mixer:cycle==1?hem::NativeWorkspace::Page::routing:hem::NativeWorkspace::Page::vault;
        workspace.setPage(origin);auto openingEditor=std::unique_ptr<juce::AudioProcessorEditor>(processor->createEditor());if(juce::String(entry.id)=="drum-replacer"||entry.clipOperation){require(openingEditor->getWidth()==1280&&openingEditor->getHeight()==720,"New editor default size changed");const auto* initial=find(*openingEditor,entry.clipOperation?"pitchSemitones":"sensitivity");require(initial&&initial->getWidth()>0&&initial->getHeight()>0,"New editor has zero-sized controls before first host resize");}workspace.setEditor(std::move(openingEditor));
        require(workspace.page()==hem::NativeWorkspace::Page::plugin&&workspace.pluginExpanded(),"Open did not take over workspace");
        auto* content=viewport.getViewedComponent();juce::AudioProcessorEditor* actual=nullptr;
        int editorCount=0;for(auto* child:content->getChildren())if(auto* e=dynamic_cast<juce::AudioProcessorEditor*>(child)){actual=e;++editorCount;}
        require(editorCount==1,"Duplicate native editors");
        for(int block=0;block<(entry.realtimeInsert?100:0);++block){
          juce::AudioBuffer<float> a(2,256),b(2,256);juce::MidiBuffer midi;
          for(int ch=0;ch<2;++ch)for(int i=0;i<256;++i){const float sample=.1f*std::sin(float(block*256+i)*(.017f+ch*.006f));a.setSample(ch,i,sample);b.setSample(ch,i,sample);}
          processor->processBlock(a,midi);untouched->processBlock(b,midi);
          for(int ch=0;ch<2;++ch)for(int i=0;i<256;++i)require(a.getSample(ch,i)==b.getSample(ch,i),"Opening/rendering editor changed actual audio output");
        }
        juce::Component::SafePointer<juce::AudioProcessorEditor> observer(actual);
        for(auto size:{juce::Point<int>(800,320),juce::Point<int>(1152,686),juce::Point<int>(1280,758),juce::Point<int>(1882,733)}){
          workspace.setSize(size.x,size.y);
          require(viewport.getWidth()==workspace.getWidth()&&viewport.getX()==0,"Partial-width plugin container");
          require(viewport.getViewArea().contains(content->getLocalArea(actual,actual->getLocalBounds())),"Registry editor cropped");
          require(actual->isEnabled()==(size.x>=1152),"Small preview should be disabled; minimum editor should be enabled");
          require(bool(actual->getProperties()["previewOnly"])==(size.x<1152),"Preview state not exposed");
          requireHostFit(*actual,*content);
          require(!control<juce::Viewport>(workspace,"plugin-stem-viewport").isVisible(),"Plugin sidebar overlaps faceplate");
          auto* delayEditor=dynamic_cast<hem::DelayEditor*>(actual);
          const float savedSync=delayEditor?processor->state.getRawParameterValue("sync")->load():0.f;
          const int savedTap=delayEditor?delayEditor->selectedTap():0;
          for(const auto& spec:processor->parameterSpecs()){
            if(delayEditor){
              const juce::String id(spec.id);
              if(id.startsWith("tap"))delayEditor->selectTap(id.substring(3,4).getIntValue()-1);
              if(id.endsWith("division")||id.endsWith("time_ms")){
                processor->state.getParameter("sync")->setValueNotifyingHost(id.endsWith("division")?1.f:0.f);
                delayEditor->refreshBindings();
              }
            }
            auto* widget=find(*actual,spec.id);if(!widget&&entry.clipOperation&&(juce::String(spec.id)=="algorithm"||juce::String(spec.id)=="timebase"))widget=find(*actual,juce::String(spec.id)+"0");require(widget!=nullptr,("Registry widget missing: "+juce::String(spec.id)).toRawUTF8());
            if(!widget->isVisible())continue; // Foundation advanced controls have separate tested access.
            require(viewport.getViewArea().contains(content->getLocalArea(widget,widget->getLocalBounds())),"Visible registry widget clipped");
            if(auto* slider=dynamic_cast<juce::Slider*>(widget)){
              const auto* bandChoice=dynamic_cast<const hem::ui::ReferenceSlider*>(slider);
              const bool numericEntry=slider->getSliderStyle()==juce::Slider::IncDecButtons||(slider->getProperties().contains("bodyRadius")&&float(slider->getProperties()["bodyRadius"])==0)||(bandChoice&&!bandChoice->choices.isEmpty());
              require(slider->getHeight()>=(numericEntry?24:40),("Rotary/readout control squeezed below usable native height: "+juce::String(entry.id)+" / "+spec.id+" / "+juce::String(slider->getHeight())).toRawUTF8());
            }
          }
          if(delayEditor){processor->state.getParameter("sync")->setValueNotifyingHost(savedSync);delayEditor->selectTap(savedTap);delayEditor->refreshBindings();}
          if(cycle==0)for(float dpi:{1.f,1.25f,1.5f,2.f}){
            auto raster=workspace.createComponentSnapshot(workspace.getLocalBounds(),true,dpi);
            require(raster.isValid()&&std::abs(raster.getWidth()-size.x*dpi)<=1,"Registry DPI raster invalid");
            if(dpi==1.f){saveRaster(raster,juce::String(size.x==800?"preview-":size.x==1152?"minimum-":size.x==1280?"native-":"large-")+entry.id);}
            if(size.x==1152&&dpi==2.f)saveRaster(raster,juce::String("minimum-200pct-")+entry.id);
          }
        }
        if(cycle%2==0)click(workspace,"plugin-expand");else require(workspace.keyPressed(juce::KeyPress(juce::KeyPress::escapeKey)),"Escape did not return");
        require(!workspace.hasEditor()&&observer==nullptr&&workspace.page()==origin,"Back failed page/lifetime restoration");
        processor->getStateInformation(after);require(before==after,"Takeover changed processor state");
      }
    }
    require(visited.size()==40,"Workspace no longer covers the original35 plus approved5 identities");
    std::cout<<"PASS toggle OFF/ON pixels; calibrated graph endpoint pixels and caption exclusion; Rider extrema; preview/minimum interaction policy\n";
    std::cout<<"PASS all "<<hem::PluginRegistry::entries().size()<<" takeover editors, "<<3*hem::PluginRegistry::entries().size()<<" Back/Escape lifetime cycles, aspect fit, DPI rasters, exact state parity; realtime entries sample-exact audio parity against no-editor twins; offline entries reject realtime preparation\n";
    // A replacement bank is resolved through the getter, never a stale reference.
    for(auto& track:bank->tracks)track.release();
    auto replacement=std::make_unique<hem::TrackBank>();replacement->tracks[12].name="RESTORED STEM";bank.swap(replacement);workspace.refresh();
    workspace.setPage(hem::NativeWorkspace::Page::plugin);require(control<juce::ComboBox>(workspace,"plugin-track-select").getText().contains("RESTORED STEM"),"Workspace retained a retired bank reference");
    std::cout<<"PASS production 13-track mixer controls, real metering, registry search/categories, registry install/monitor routing, split/expand and native editor lifetime, multi-size rasters, bank replacement and uninterrupted navigation\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}


