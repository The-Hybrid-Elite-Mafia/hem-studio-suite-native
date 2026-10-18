#include "Editors.h"
#include "PluginMeterReadout.h"
#include "../UI/ModuleEditorGeometry.h"
#include "../UI/FaceplateLayers.h"
namespace hem {
ModuleEditor::ModuleEditor(ModuleProcessor& p,bool useGeneric):AudioProcessorEditor(p),processor(p){
 if(!useGeneric){setSize(ui::moduleEditorWidth,ui::moduleEditorHeight);startTimerHz(30);return;}
 faceplate=juce::ImageCache::getFromMemory(ControlArt::reviewed_hardware_reference_png,ControlArt::reviewed_hardware_reference_pngSize);
 brandReference=juce::ImageCache::getFromMemory(ControlArt::approved_module_reference_png,ControlArt::approved_module_reference_pngSize);
 setLookAndFeel(&look);for(const auto& spec:p.parameterSpecs()){
    auto c=std::make_unique<Control>();c->isToggle=spec.toggle;
    if(spec.toggle){c->toggle.setComponentID(spec.id);c->toggle.setName(spec.name);addAndMakeVisible(c->toggle);c->toggle.setButtonText(spec.name);c->buttonAttachment=std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(p.state,spec.id,c->toggle);}
    else {addAndMakeVisible(c->label);c->label.setText(juce::String(spec.id)=="slope"?"HP SLOPE":spec.name,juce::dontSendNotification);c->label.setJustificationType(juce::Justification::centred);
        c->knob.setComponentID(spec.id);c->knob.setName(spec.name);c->knob.setTooltip(juce::String(spec.name)+" / "+spec.unit);addAndMakeVisible(c->knob);c->knob.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);c->knob.setTextBoxStyle(juce::Slider::TextBoxBelow,false,110,22);c->knob.setTextValueSuffix(juce::String(" ")+spec.unit);
        c->knob.setRotaryParameters(juce::MathConstants<float>::pi*1.2f,juce::MathConstants<float>::pi*2.8f,true);c->knob.setMouseDragSensitivity(250);c->knob.setDoubleClickReturnValue(true,spec.initial);
        look.registerDial(spec.id,juce::ImageCache::getFromMemory(OriginalArt::native_transient_png,OriginalArt::native_transient_pngSize),{252,603,166,166});
        c->sliderAttachment=std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(p.state,spec.id,c->knob);}
    if(spec.toggle)c->toggle.onClick=[this]{refreshParameterActivity();};
    controls.push_back(std::move(c));}
 configureSections();
 juce::StringArray orderedIds;for(const auto& section:sections)for(const auto& id:section.ids)orderedIds.add(id);
 sections.clear();sections.push_back({"PROCESSING CONTROLS",{178,450,1063,218},orderedIds});
 setInterceptsMouseClicks(true,true);timerCallback();
 setResizable(true,true);setResizeLimits(1000,563,1920,1080);if(auto* c=getConstrainer())c->setFixedAspectRatio(1672.0/941.0);setSize(ui::moduleEditorWidth,ui::moduleEditorHeight);startTimerHz(25);
}
void ModuleEditor::refreshParameterActivity(){
 for(auto& control:controls){auto& c=*control;const bool enabled=processor.parameterActive(c.isToggle?c.toggle.getComponentID():c.knob.getComponentID());
  c.toggle.setEnabled(enabled);c.knob.setEnabled(enabled);c.label.setEnabled(enabled);}
 repaint();
}
void ModuleEditor::timerCallback(){refreshParameterActivity();}
ModuleEditor::~ModuleEditor(){stopTimer();controls.clear();setLookAndFeel(nullptr);}
void ModuleEditor::configureSections(){
 auto add=[&](const char* title,juce::Rectangle<float> area,const char* ids){sections.push_back({title,area,juce::StringArray::fromTokens(ids," ","")});};
 const auto type=processor.state.state.getType().toString();
 if(type=="SURGICAL_EQ"){
  add("SUB FILTER",{220,322,282,158},"hpOn hp slope");add("MUD",{510,322,292,158},"mudFreq mudGain mudQ");
  add("CORE",{810,322,292,158},"midFreq midGain midQ");add("HARSHNESS",{220,490,340,158},"harshFreq harshGain harshQ");add("AIR",{568,490,330,158},"airFreq airGain");add("OUTPUT",{906,490,196,158},"output");
 }else if(type=="VCA"){
  add("DETECTOR / COMPRESSION",{220,278,570,178},"threshold ratio knee");add("TIMING",{220,465,570,182},"attack release autoRelease");add("SIDECHAIN / MIX",{800,278,302,369},"scFreq scOn makeup mix");
 }else if(type=="TRUE_PEAK"){
  add("DRIVE",{220,324,278,324},"input");add("CEILING",{510,324,290,324},"ceiling");add("RECOVERY",{812,324,290,324},"release");
 }else if(type=="DHTA"){
  add("FREQUENCY TARGET",{220,277,420,183},"frequency q wide");add("DETECTOR",{650,277,452,183},"threshold range");add("TIMING / AUDITION",{220,470,570,178},"attack release delta");add("BLEND / OUTPUT",{800,470,302,178},"mix output");
 }else if(type=="SUB_BASS"){
  add("LOW-END FOUNDATION",{220,278,560,370},"crossover lowGain drive mono");add("SUBSONIC FILTER",{790,278,312,181},"subCut cutEnabled");add("BLEND / OUTPUT",{790,469,312,179},"mix output");
 }else if(type=="RESTORER"){
  add("NOISE PROFILE",{220,278,300,370},"floor learn");add("REDUCTION / PRESERVE",{530,278,572,181},"amount protect release");add("BLEND / OUTPUT",{530,469,572,179},"mix output");
 }else if(type=="VPR_ARCHITECT"){
  add("VOCAL TONE",{220,278,882,178},"cutoff body mud presence brightness air");add("RESONANCE CONTROL",{220,466,510,182},"resonance q threshold range");add("TIMING / OUTPUT",{740,466,362,182},"attack release output cutEnabled");
 }else if(type=="TRIODE_TUBE"){
  add("TRIODE CHARACTER",{220,305,550,343},"drive bias");add("TONE",{780,305,322,164},"tone");add("BLEND / OUTPUT",{780,479,322,169},"mix output");
 }else if(type=="TAPE"){
  add("TAPE DRIVE",{220,305,292,343},"drive");add("HEAD / HIGH-FREQUENCY LOSS",{522,305,580,167},"warmth rolloff");add("BLEND / OUTPUT",{522,482,580,166},"mix output");
 }else if(type=="MONITOR_EQ"){
  add("MONITOR TONE / LISTENING ONLY",{220,325,670,323},"bass body presence air");add("HEADROOM",{900,325,202,323},"preamp enabled");
 }
}
void ModuleEditor::resized(){
 const float sx=float(getWidth())/1280.f,sy=float(getHeight())/720.f;
 auto scaled=[&](juce::Rectangle<float> b){return juce::Rectangle<float>(b.getX()*sx,b.getY()*sy,b.getWidth()*sx,b.getHeight()*sy).toNearestInt();};
 for(const auto& section:sections){
  const int count=section.ids.size();const int cols=count<=11?count:(count+1)/2;
  const int rows=(count+cols-1)/cols;const float cw=(section.bounds.getWidth()-12)/cols,ch=(section.bounds.getHeight()-22)/rows;
  for(int i=0;i<count;++i)for(auto& c:controls){if((c->isToggle?c->toggle.getComponentID():c->knob.getComponentID())!=section.ids[i])continue;
   auto b=juce::Rectangle<float>(section.bounds.getX()+6+(i%cols)*cw,section.bounds.getY()+20+(i/cols)*ch,cw,ch).reduced(3);
   if(c->isToggle)c->toggle.setBounds(scaled(b.withSizeKeepingCentre(b.getWidth(),std::min(40.f,b.getHeight()))));
   else {c->label.setFont(ui::hardwareFont(std::max(11.f,13*sy),true));c->label.setBounds(scaled(b.withHeight(16)));
    c->knob.setBounds(scaled(b.withTrimmedTop(16)));auto& props=c->knob.getProperties();
    const float readHeight=rows>1?19.f:23.f;const float bodyHeight=c->knob.getHeight()-readHeight*sy-3;
    props.set("bodyRadius",std::min(c->knob.getWidth()*.48f,bodyHeight*.49f));props.set("bodyX",c->knob.getWidth()*.5f);props.set("bodyY",bodyHeight*.5f);
    const int readWidth=int(std::min(120.f,b.getWidth())*sx);props.set("readX",(c->knob.getWidth()-readWidth)/2);props.set("readY",int(bodyHeight+3));props.set("readW",readWidth);props.set("readH",int(readHeight*sy));
    c->knob.setTextBoxStyle(juce::Slider::TextBoxBelow,false,readWidth,int(readHeight*sy));c->knob.resized();}
  }
 }
}
void ModuleEditor::paint(juce::Graphics& g){
 const juce::Graphics::ScopedSaveState state(g);g.fillAll(juce::Colour(0xff010407));g.setImageResamplingQuality(juce::Graphics::highResamplingQuality);
 g.drawImage(faceplate,getLocalBounds().toFloat(),juce::RectanglePlacement::stretchToFit);
 g.addTransform(juce::AffineTransform::scale(getWidth()/1280.f,getHeight()/720.f));
 // Cover authored action buttons and all baked values/controls. Only actual widgets are interactive.
 ui::hardwareReadout(g,{62,43,144,49},"STUDIO MODULE",17);
 ui::hardwareReadout(g,{1080,43,141,49},"STEREO",19);
 ui::hardwareReadout(g,{283,95,804,31},processor.state.state.hasType("SUB_BASS")?"SUB-BASS & 808 ARCHITECT":processor.getName(),24);
 ui::hardwareGlass(g,{172,416,1076,264},false);
 for(const auto& section:sections){ui::hardwareGlass(g,section.bounds);g.setColour(juce::Colour(0xffffbd60));g.setFont(ui::hardwareFont(13,true));g.drawText(section.title,section.bounds.withHeight(25).reduced(12,0).toNearestInt(),juce::Justification::centred);}
 telemetryView.poll(processor.pluginTelemetry());
 telemetryView.level(g,{50,143,112,330},false);telemetryView.level(g,{1125,143,112,330},true);
 const juce::Rectangle<float> graph(178,143,936,270);const auto type=processor.state.state.getType().toString();
 if(processor.meterKind()==PluginMeterKind::gainReduction)telemetryView.reduction(g,graph,processor.pluginTelemetry(),ui::pluginMeterReadout(processor).heading);
 else if(type=="TRUE_PEAK")telemetryView.reduction(g,graph,processor.pluginTelemetry(),"LIMITER / ACTUAL GAIN REDUCTION / dB",true);
 else if(type=="TRIODE_TUBE")telemetryView.waveform(g,graph);
 else telemetryView.spectrum(g,graph,type=="MONITOR_EQ"?"MONITOR OUTPUT SPECTRUM / LISTENING ONLY":"POST-FX STEREO SPECTRUM");
 const auto measured=ui::pluginMeterReadout(processor);
 ui::hardwareReadout(g,{179,418,935,27},telemetryView.isLive()?measured.heading+"  /  "+measured.valueText:telemetryView.statusText(),14);
 g.setColour(juce::Colour(0xffb8d2dc));g.setFont(ui::hardwareFont(11));g.drawFittedText(processor.helpText(),{180,683,1060,19},juce::Justification::centred,1);
}
}
