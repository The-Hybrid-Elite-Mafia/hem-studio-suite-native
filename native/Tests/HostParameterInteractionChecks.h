#pragma once
// Test-only adapters for the actual current controls. No native popup is opened.
namespace hostParameterChecks {
inline constexpr std::array<const char*,35> orderedIds{{
 "sipa","surgical-eq","vca","true-peak","dhta","transient","sub-bass","spectral-restorer","vocal-presence","triode-tube","tape","monitor-eq","dc-subsonic","smart-gate","vocal-rider","plosive-control","hum-buzz","loudness-lab","spectrum-spectrogram","delta-monitor","convolution-reverb","algorithmic-reverb","chorus-flanger-phaser","rotary-tremolo-autopan","multitap-delay","tape-delay","dynamic-eq-8","mid-side-eq","multiband-dynamics","multiband-imager","parallel-distortion","exciter","dynamic-resonance-suppressor","breath-mouth-control","clip-repair"
}};
inline juce::String context(const hem::ModuleProcessor& processor,const juce::String& parameter){return hem::PluginRegistry::idFor(processor)+" / "+parameter+": ";}
inline void setRequired(hem::ModuleProcessor& processor,const juce::String& id,float value){
 auto* parameter=processor.state.getParameter(id);
 require(parameter!=nullptr,context(processor,id)+"documented activation parameter missing");
 parameter->setValueNotifyingHost(parameter->convertTo0to1(value));
}
inline void refresh(juce::AudioProcessorEditor& editor){
 if(auto* p=dynamic_cast<hem::ModuleEditor*>(&editor))p->refreshParameterActivity();
 if(auto* p=dynamic_cast<hem::OriginalDynamicEqEditor*>(&editor))p->refresh();
 if(auto* p=dynamic_cast<hem::OriginalBankEditorBase*>(&editor))p->refresh();
 if(auto* p=dynamic_cast<hem::DelayEditor*>(&editor))p->refreshBindings();
}
inline void validatePage(hem::ModuleProcessor& processor,juce::AudioProcessorEditor& editor,const juce::String& id){
 const auto why=context(processor,id);
 if(auto* delay=dynamic_cast<hem::DelayEditor*>(&editor);delay&&id.startsWith("tap"))
  require(delay->selectedTap()==id.substring(3,4).getIntValue()-1,why+"wrong shared delay tap selected");
 if(dynamic_cast<hem::OriginalDynamicEqEditor*>(&editor)&&id.startsWith("b")){
  auto* page=dynamic_cast<juce::ComboBox*>(findControl(editor,"uiBandPage"));
  require(page&&page->getSelectedId()==id.substring(1,2).getIntValue(),why+"wrong dynamics band page selected");
 }
}
inline void activate(hem::ModuleProcessor& processor,juce::AudioProcessorEditor& editor,const juce::String& id){
 const auto type=hem::PluginRegistry::idFor(processor),why=context(processor,id);
 require(processor.state.getParameter(id)!=nullptr,why+"unknown parameter identity");
 // Validate all prerequisites before using the shared family setup, whose legacy
 // callers permit optional keys. This fixture never treats a missing key as a skip.
 if(dynamic_cast<hem::BandModuleProcessor*>(&processor)){
  if(type=="dynamic-eq-8"&&id.startsWith("b")){
   const auto prefix=id.substring(0,2);setRequired(processor,prefix+"On",1);setRequired(processor,prefix+"DynamicOn",1);setRequired(processor,prefix+"Range",6);
   require(dynamic_cast<juce::ComboBox*>(findControl(editor,"uiBandPage"))!=nullptr,why+"band selector missing");
  }else if(type=="mid-side-eq"&&id.length()>2&&juce::CharacterFunctions::isDigit(id[1]))setRequired(processor,id.substring(0,2)+"On",1);
  else if(type=="multiband-dynamics"||type=="multiband-imager"){
   setRequired(processor,"topology",id.startsWith("mid")||id=="legacyMidOn"?0.f:1.f);
   for(const auto* prefix:{"low","lowMid","highMid","high"})setRequired(processor,juce::String(prefix)+"On",1);
   setRequired(processor,"legacyMidOn",1);
   if(type=="multiband-dynamics")for(const auto* prefix:{"low","lowMid","mid","highMid","high"}){setRequired(processor,juce::String(prefix)+"Ratio",4);setRequired(processor,juce::String(prefix)+"Range",12);}
   else setRequired(processor,"lowMono",0);
  }
  bandHostFixture::prepare(processor,editor,id);
 }
 if(auto* delay=dynamic_cast<hem::DelayEditor*>(&editor)){
  if(id.startsWith("tap"))delay->selectTap(id.substring(3,4).getIntValue()-1);
  if(id.endsWith("division")||id.endsWith("time_ms"))setRequired(processor,"sync",id.endsWith("division")?1.f:0.f);
  delay->refreshBindings();
 }
 if(!processor.parameterActive(id)){
  if(type=="dc-subsonic"&&(id=="cutoff"||id=="steep"))setRequired(processor,"subOn",1);
  else if(type=="smart-gate"&&id=="ratio")setRequired(processor,"gate",0);
  else if(type=="convolution-reverb"&&(id=="lowCut"||id=="highCut"))setRequired(processor,id+"On",1);
  else if(type=="chorus-flanger-phaser"&&(id=="delay"||id=="center"||id=="stages"))setRequired(processor,"mode",id=="delay"?0.f:2.f);
  else if(type=="rotary-tremolo-autopan"&&(id=="rotary"||id=="ramp"))setRequired(processor,"mode",0);
  else require(false,why+"inactive control has no documented activation route");
 }
 refresh(editor);validatePage(processor,editor,id);
 require(processor.parameterActive(id),why+"documented activation route did not enable DSP control");
}
inline juce::Point<int> rootTarget(MainComponent& host,juce::Viewport& viewport,juce::Component& control,const juce::String& why){
 require(visibleWithin(host,control)&&!control.getLocalBounds().isEmpty(),why+"widget is hidden or empty");
 auto point=control.getLocalBounds().getCentre();
 if(auto* slider=dynamic_cast<juce::Slider*>(&control))point=slider->getLookAndFeel().getSliderLayout(*slider).sliderBounds.getCentre();
 auto* canvas=viewport.getViewedComponent();require(canvas!=nullptr,why+"viewport has no canvas");
 const auto at=canvas->getLocalPoint(&control,point);viewport.setViewPosition(at.x-viewport.getViewWidth()/2,at.y-viewport.getViewHeight()/2);
 auto* hit=host.getComponentAt(host.getLocalPoint(&control,point));
 require(hit==&control||control.isParentOf(hit),why+"root pointer target escaped to "+(hit?hit->getComponentID():juce::String("<null>")));
 return point;
}
inline void click(juce::Component& control,juce::Point<int> point){
 const auto now=juce::Time::getCurrentTime();auto source=juce::Desktop::getInstance().getMainMouseSource();const auto pos=point.toFloat();
 auto event=[&](int flags){return juce::MouseEvent(source,pos,juce::ModifierKeys(flags),1,0,0,0,0,&control,&control,now,pos,now,1,false);};
 control.mouseDown(event(juce::ModifierKeys::leftButtonModifier));control.mouseUp(event(0));
}
inline juce::Component& prepare(MainComponent& host,hem::ModuleProcessor& processor,juce::AudioProcessorEditor& editor,juce::Viewport& viewport,const hem::ParameterSpec& spec){
 const auto why=context(processor,spec.id);activate(processor,editor,spec.id);
 auto* control=findControl(editor,spec.id);require(control!=nullptr,why+"missing currently bound widget");
 if(!control->isVisible()){
  // Only Foundation has this retained-raw-control navigation contract.
  const auto id=hem::PluginRegistry::idFor(processor);
  require(id=="dc-subsonic"||id=="smart-gate"||id=="vocal-rider"||id=="plosive-control"||id=="hum-buzz",why+"hidden widget has no family navigation route");
  auto* advanced=findControl(editor,"foundation-advanced");require(advanced!=nullptr,why+"Foundation Advanced action missing");
  click(*advanced,rootTarget(host,viewport,*advanced,why+"Advanced: "));
  require(control->isVisible(),why+"Advanced did not expose raw control");
 }
 require(visibleWithin(host,*control)&&control->isEnabled(),why+"active DSP widget is not showing/enabled");
 return *control;
}
inline void supported(const hem::ModuleProcessor& processor,const hem::ParameterSpec& spec,juce::Component& control){
 const bool choice=hem::controlChecks::choice(control)||dynamic_cast<juce::ComboBox*>(&control)!=nullptr;
 require(choice||dynamic_cast<juce::Slider*>(&control)||(spec.toggle&&dynamic_cast<juce::Button*>(&control)),context(processor,spec.id)+"unsupported actual control class");
}
inline void roundTrip(hem::ModuleProcessor& processor,const juce::String& why){
 const auto state=hem::SessionState::captureParameters(processor);auto copy=hem::PluginRegistry::create(hem::PluginRegistry::idFor(processor));
 require(copy!=nullptr,why+"round-trip factory missing");hem::SessionState::restoreParameters(*copy,state.createCopy());
 require(state.isEquivalentTo(hem::SessionState::captureParameters(*copy)),why+"captured complete parameter state changed in round-trip");
}
inline void exercise(MainComponent& host,hem::ModuleProcessor& processor,juce::AudioProcessorEditor& editor,juce::Viewport& viewport,const hem::ParameterSpec& spec,juce::Component& control){
 const auto why=context(processor,spec.id);supported(processor,spec,control);
 const auto point=rootTarget(host,viewport,control,why);
 auto* parameter=processor.state.getParameter(spec.id);auto* raw=processor.state.getRawParameterValue(spec.id);
 require(parameter&&raw,why+"attachment target missing");GestureProbe probe(*parameter);
 if(hem::controlChecks::choice(control)){
  require(hem::controlChecks::exerciseDiscrete(processor,editor,spec,control),why+"choice binding not exercised");
  // Select a different option with a complete actual Button down/up gesture.
  for(int option=0;option<=int(spec.maximum);++option){
   auto* choice=dynamic_cast<juce::Button*>(findControl(editor,option==0?juce::String(spec.id):juce::String(spec.id)+"_"+juce::String(option)));
   require(choice&&choice->isEnabled(),why+"choice button missing/disabled");
   if(raw->load()==float(option))continue;
   probe.clear();click(*choice,rootTarget(host,viewport,*choice,why+"choice "+juce::String(option)+": "));
   require(raw->load()==float(option)&&choice->getToggleState(),why+"choice down/up did not update parameter");
   require(probe.begins==1&&probe.ends==1&&probe.changes==1,why+"choice down/up lacks one complete gesture");roundTrip(processor,why);
  }
 }else if(dynamic_cast<juce::ComboBox*>(&control)){
  require(hem::controlChecks::exerciseDiscrete(processor,editor,spec,control),why+"ComboBox binding not exercised");
  auto& combo=static_cast<juce::ComboBox&>(control);
  combo.setSelectedItemIndex(int(spec.maximum),juce::sendNotificationSync);roundTrip(processor,why);
  // This is actual ComboBox attachment coverage, not a native-popup PASS.
 }else if(auto* slider=dynamic_cast<juce::Slider*>(&control)){
  slider->setValue(spec.minimum+(spec.maximum-spec.minimum)*.5f,juce::sendNotificationSync);const auto before=slider->getValue();
  const auto delta=std::max(double(spec.step),double(spec.maximum-spec.minimum)*.1);const bool increase=before<slider->getMaximum();
  const auto target=increase?std::min(slider->getMaximum(),before+delta):std::max(slider->getMinimum(),before-delta);
  const auto travel=std::abs(slider->valueToProportionOfLength(target)-slider->valueToProportionOfLength(before));
  const auto pixels=float(std::ceil(travel*slider->getMouseDragSensitivity())+2);
  const auto start=point.toFloat(),end=start+juce::Point<float>(0,increase?-pixels:pixels);
  const auto now=juce::Time::getCurrentTime();auto source=juce::Desktop::getInstance().getMainMouseSource();
  auto event=[&](juce::Point<float> pos,int flags,bool dragged){return juce::MouseEvent(source,pos,juce::ModifierKeys(flags),1,0,0,0,0,slider,slider,now,start,now,1,dragged);};
  probe.clear();slider->mouseDown(event(start,juce::ModifierKeys::leftButtonModifier,false));
  slider->mouseDrag(event(end,juce::ModifierKeys::leftButtonModifier,true));slider->mouseUp(event(end,0,true));
  require(increase?slider->getValue()>before:slider->getValue()<before,why+"drag did not cross a legal parameter step");
  require(probe.begins==1&&probe.ends==1&&probe.changes>0,why+"drag lacks one complete gesture");
  require(std::abs(raw->load()-slider->getValue())<=std::max(1.e-4f,spec.step*.51f),why+"drag did not update APVTS");roundTrip(processor,why);
  if(auto* choice=dynamic_cast<hem::ui::ReferenceSlider*>(slider);choice&&!choice->choices.isEmpty()){
   require(bool(choice->onChoice),why+"reference selector callback missing");
   for(int option=0;option<choice->choices.size();++option){choice->onChoice(option);require(raw->load()==float(option),why+"reference selector binding failed");}roundTrip(processor,why);
  }
 }else if(auto* toggle=dynamic_cast<juce::Button*>(&control);toggle&&spec.toggle){
  const bool next=!toggle->getToggleState();probe.clear();click(*toggle,point);
  require(toggle->getToggleState()==next&&raw->load()==float(next),why+"button down/up did not toggle APVTS");
  require(probe.begins==1&&probe.ends==1&&probe.changes==1,why+"toggle lacks one complete gesture");roundTrip(processor,why);
 }else require(false,why+"unsupported actual control class");
}
inline void negativeControls(){
 auto p=hem::PluginRegistry::create("dc-subsonic");bool rejected=false;
 try{setRequired(*p,"nonexistent-activation",1);}catch(const std::exception& e){rejected=juce::String(e.what()).contains("documented activation parameter missing");}
 require(rejected,"Negative control: missing activation did not fail diagnostically");
 juce::Component unsupported;rejected=false;
 try{supported(*p,p->parameterSpecs().front(),unsupported);}catch(const std::exception& e){rejected=juce::String(e.what()).contains("unsupported actual control class");}
 require(rejected,"Negative control: unknown widget class was silently skipped");
 for(const auto& type:{"multitap-delay","dynamic-eq-8"}){
  auto processor=hem::PluginRegistry::create(type);std::unique_ptr<juce::AudioProcessorEditor> editor(processor->createEditor());
  const juce::String id=juce::String(type)=="multitap-delay"?"tap2.time_ms":"b2Threshold";
  if(auto* delay=dynamic_cast<hem::DelayEditor*>(editor.get()))delay->selectTap(0);
  else {auto* page=dynamic_cast<juce::ComboBox*>(findControl(*editor,"uiBandPage"));require(page!=nullptr,"Negative control band selector missing");page->setSelectedId(1,juce::sendNotificationSync);}
  rejected=false;try{validatePage(*processor,*editor,id);}catch(const std::exception& e){rejected=juce::String(e.what()).contains("wrong");}
  require(rejected,"Negative control: wrong delay tap/band page was not detected");
 }
}
} // namespace hostParameterChecks
