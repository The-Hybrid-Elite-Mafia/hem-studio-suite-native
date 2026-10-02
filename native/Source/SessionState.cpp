#include "SessionState.h"
#include "Systems/PresetManager.h"
#include "Plugins/ExternalPluginHost.h"
#include <set>
#include <stdexcept>
namespace hem {
namespace {
[[noreturn]] void fail(const juce::String& text){throw std::runtime_error(text.toStdString());}
double number(const juce::ValueTree& t,const juce::Identifier& key,double low,double high){
 if(!t.hasProperty(key))fail("Missing property: "+key.toString());
 const auto text=t[key].toString().trim();
 // String::getDoubleValue silently accepts malformed input. Require complete
 // conversion, including no trailing junk, before using a session value.
 size_t used=0;double v=0;const auto str=text.toStdString();
 try{v=std::stod(str,&used);}catch(...){fail("Invalid number: "+key.toString());}
 if(used!=str.size()||!std::isfinite(v)||v<low||v>high)fail("Out-of-range property: "+key.toString());
 return v;
}
bool flag(const juce::ValueTree& t,const juce::Identifier& key){const auto n=number(t,key,0,1);if(n!=0&&n!=1)fail("Invalid boolean");return n==1;}
void require(bool b,const juce::String& text){if(!b)fail(text);}
void emptyParameters(const juce::ValueTree& parameters){
 require(parameters.hasType("PARAMETERS")&&parameters.getNumChildren()==0&&parameters.getNumProperties()==0,
         "External inserts must have an empty PARAMETERS element");
}
void validateExternal(const ExternalPluginState& external,bool requireFile){
 juce::String error;if(!external.validate(error))fail("Invalid external insert state: "+error);
 juce::PluginDescription description;if(!external.getDescription(description,error))fail("Invalid external insert description: "+error);
 if(requireFile)require(juce::File(description.fileOrIdentifier).exists(),"Missing VST3 plug-in: "+description.fileOrIdentifier);
}
ExternalPluginState decodeExternal(const juce::ValueTree& element){
 require(element.hasType("EXTERNAL")&&element.getNumChildren()==0&&element.getNumProperties()==3
         &&element.hasProperty("descriptionXml")&&element.hasProperty("stateBase64")&&element.hasProperty("stateBytes"),
         "Invalid EXTERNAL container");
 require(element["descriptionXml"].isString()&&element["stateBase64"].isString()&&!element["stateBytes"].isBool(),
         "Invalid EXTERNAL property types");
 ExternalPluginState result;result.descriptionXml=element["descriptionXml"].toString();
 require(result.descriptionXml.getNumBytesAsUTF8()<=ExternalPluginState::maxDescriptionBytes,"External description is too large");
 const auto bytes=number(element,"stateBytes",0,double(ExternalPluginState::maxStateBytes));
 require(bytes==std::floor(bytes),"External state byte count must be an integer");
 const auto encoded=element["stateBase64"].toString();
 const auto expected=((size_t(bytes)+2)/3)*4;
 require(encoded.getNumBytesAsUTF8()==expected&&expected<=SessionState::maxSerializedBytes,"Invalid external Base64 size");
 juce::MemoryOutputStream stream;
 require(juce::Base64::convertFromBase64(stream,encoded),"Malformed external Base64 state");
 require(stream.getDataSize()==size_t(bytes),"External state byte count mismatch");
 require(juce::Base64::toBase64(stream.getData(),stream.getDataSize())==encoded,"Non-canonical external Base64 state");
 result.state=stream.getMemoryBlock();validateExternal(result,true);return result;
}
void requireSerializedSize(const juce::ValueTree& root){
 auto xml=root.createXml();require(xml!=nullptr,"Cannot encode session XML");
 require(xml->toString().getNumBytesAsUTF8()<=SessionState::maxSerializedBytes,"Session is larger than 8 MB");
}
}
juce::ValueTree SessionState::captureParameters(ModuleProcessor& p){
 juce::ValueTree result("PARAMETERS");
 for(const auto& spec:p.parameterSpecs()){
    juce::ValueTree v("PARAM");v.setProperty("id",spec.id,nullptr);
    v.setProperty("value",double(p.state.getRawParameterValue(spec.id)->load()),nullptr);result.addChild(v,-1,nullptr);
 }
 return result;
}
void SessionState::restoreParameters(ModuleProcessor& p,const juce::ValueTree& values){
 require(values.hasType("PARAMETERS")&&values.getNumChildren()==int(p.parameterSpecs().size()),"Parameter schema mismatch: "+p.getName());
 std::set<juce::String> ids;
 // Validate every value BEFORE changing this processor.
 for(auto child:values){
    require(child.hasType("PARAM")&&child.getNumChildren()==0,"Invalid parameter element");
    auto id=child["id"].toString();require(ids.insert(id).second,"Duplicate parameter: "+id);
    auto it=std::find_if(p.parameterSpecs().begin(),p.parameterSpecs().end(),[&](const auto& s){return id==s.id;});
    require(it!=p.parameterSpecs().end(),"Unknown parameter: "+id);
    const auto n=number(child,"value",it->minimum,it->maximum);
    if(it->toggle)require(n==0||n==1,"Invalid toggle: "+id);
 }
 for(auto child:values){
    const auto id=child["id"].toString();
    const auto wanted=float(double(child["value"]));
    auto* parameter=p.state.getParameter(id);
    // An exact no-op restore must not re-quantise JUCE's initial raw default.
    // APVTS initializes that value without snapToLegalValue; ARM arithmetic can
    // distinguish it from another normalize/snap cycle. Validation above still
    // covers every parameter before any change is made.
    if(p.state.getRawParameterValue(id)->load()==wanted)continue;
    parameter->setValueNotifyingHost(parameter->convertTo0to1(wanted));}
}
SessionSnapshot SessionState::capture(const TrackBank& bank,float in,float out,double position,bool loop,ModuleProcessor& monitor,bool includeExternalState){
 SessionSnapshot s;s.inputDb=in;s.outputDb=out;s.positionSeconds=position;s.looping=loop;
 s.monitorParameters=captureParameters(monitor);
 for(size_t i=0;i<s.tracks.size();++i){const auto& t=bank.tracks[i];auto& st=s.tracks[i];
    st.source=t.loaded?t.playback->file:juce::File();st.name=t.name;st.gainDb=t.trimDb.load();st.muted=t.muted.load();st.solo=t.solo.load();
    for(int j=0;j<8;++j){require(!t.chain.isBusy(j),"Wait for insert transitions before saving");auto& slot=st.slots[size_t(j)];slot.bypassed=t.chain.isBypassed(j);
        if(auto* p=t.chain.activeProcessor(j)){slot.pluginId=PluginRegistry::idFor(*p);require(slot.pluginId.isNotEmpty(),"Unregistered processor");
            if(p->isExternal()){
                slot.pluginId=externalPluginId;slot.externalRevision=p->externalStateRevision();
                if(includeExternalState){juce::String error;if(!ExternalPluginHost::capture(*p,slot.external,error))fail("Cannot capture external insert: "+error);}
                else slot.external.descriptionXml=p->externalDescriptionXml();
            }else slot.parameters=captureParameters(*p);}}
 }
 return s;
}
bool SessionState::hasExternal(const SessionSnapshot& snapshot) noexcept {
 for(const auto& track:snapshot.tracks)for(const auto& slot:track.slots)if(slot.pluginId==externalPluginId)return true;
 return false;
}
juce::ValueTree SessionState::encode(const SessionSnapshot& s,const juce::File& file){
 juce::ValueTree root("HEM_SESSION");root.setProperty("version",hasExternal(s)?2:1,nullptr);root.setProperty("tracks",13,nullptr);root.setProperty("slotsPerTrack",8,nullptr);
 root.setProperty("inputDb",s.inputDb,nullptr);root.setProperty("outputDb",s.outputDb,nullptr);root.setProperty("positionSeconds",s.positionSeconds,nullptr);root.setProperty("looping",int(s.looping),nullptr);
 root.setProperty("masterCeilingDbTP",-1.0,nullptr);
 juce::ValueTree monitor("MONITOR");monitor.addChild(s.monitorParameters.createCopy(),-1,nullptr);root.addChild(monitor,-1,nullptr);
 size_t externalBytes=0;
 for(size_t i=0;i<s.tracks.size();++i){const auto& st=s.tracks[i];juce::ValueTree t("TRACK");t.setProperty("index",int(i),nullptr);t.setProperty("name",st.name,nullptr);
    t.setProperty("source",st.source==juce::File()?juce::String():st.source.getRelativePathFrom(file.getParentDirectory()),nullptr);
    t.setProperty("absoluteSource",st.source.getFullPathName(),nullptr);t.setProperty("gainDb",st.gainDb,nullptr);t.setProperty("muted",int(st.muted),nullptr);t.setProperty("solo",int(st.solo),nullptr);
    for(int j=0;j<8;++j){const auto& ss=st.slots[size_t(j)];juce::ValueTree slot("SLOT");slot.setProperty("index",j,nullptr);slot.setProperty("pluginId",ss.pluginId,nullptr);slot.setProperty("bypassed",int(ss.bypassed),nullptr);slot.addChild(ss.parameters.createCopy(),-1,nullptr);
        if(ss.pluginId==externalPluginId){
            emptyParameters(ss.parameters);validateExternal(ss.external,false);
            externalBytes+=((ss.external.state.getSize()+2)/3)*4+ss.external.descriptionXml.getNumBytesAsUTF8();
            require(externalBytes<=maxSerializedBytes,"Session external state is larger than 8 MB");
            juce::ValueTree external("EXTERNAL");external.setProperty("descriptionXml",ss.external.descriptionXml,nullptr);
            external.setProperty("stateBytes",juce::int64(ss.external.state.getSize()),nullptr);
            external.setProperty("stateBase64",juce::Base64::toBase64(ss.external.state.getData(),ss.external.state.getSize()),nullptr);
            slot.addChild(external,-1,nullptr);
        }else require(ss.external.descriptionXml.isEmpty()&&ss.external.state.getSize()==0,"Native or empty insert contains external state");
        t.addChild(slot,-1,nullptr);}
    root.addChild(t,-1,nullptr);
 }
 requireSerializedSize(root);return root;
}
SessionSnapshot SessionState::decode(const juce::ValueTree& root,const juce::File& file){
 require(root.hasType("HEM_SESSION"),"Not a HEM session");const auto version=number(root,"version",1,2);require(version==1||version==2,"Unsupported session version");
 requireSerializedSize(root);
 number(root,"tracks",13,13);number(root,"slotsPerTrack",8,8);number(root,"masterCeilingDbTP",-1,-1);
 require(root.getNumChildren()==14,"Session must contain 13 tracks and monitor settings");
 SessionSnapshot result;result.inputDb=float(number(root,"inputDb",-60,6));result.outputDb=float(number(root,"outputDb",-60,6));result.positionSeconds=number(root,"positionSeconds",0,1.e9);result.looping=flag(root,"looping");
 std::array<bool,13> seen{};bool monitorSeen=false;
 for(auto t:root){
    if(t.hasType("MONITOR")){require(!monitorSeen&&t.getNumChildren()==1,"Invalid monitor state");monitorSeen=true;result.monitorParameters=t.getChild(0).createCopy();continue;}
    require(t.hasType("TRACK"),"Unknown session element");const auto index=number(t,"index",0,12);require(index==int(index)&&!seen[size_t(index)],"Duplicate or invalid track index");seen[size_t(index)]=true;
    auto& st=result.tracks[size_t(index)];st.name=t["name"].toString();st.gainDb=float(number(t,"gainDb",-60,6));st.muted=flag(t,"muted");st.solo=flag(t,"solo");
    const auto relative=t["source"].toString();const auto absolute=t["absoluteSource"].toString();
    if(relative.isNotEmpty()||absolute.isNotEmpty()){
        if(relative.isNotEmpty())st.source=file.getParentDirectory().getChildFile(relative);
        if(!st.source.existsAsFile()&&juce::File::isAbsolutePath(absolute))st.source=juce::File(absolute);
        require(st.source.existsAsFile(),"Missing audio on track "+juce::String(int(index)+1)+": "+relative);
    }
    require(t.getNumChildren()==8,"Each track must have eight slots");std::array<bool,8> slots{};
    for(auto child:t){require(child.hasType("SLOT"),"Invalid insert element");auto si=number(child,"index",0,7);require(si==int(si)&&!slots[size_t(si)],"Duplicate or invalid slot index");slots[size_t(si)]=true;
        auto& ss=st.slots[size_t(si)];ss.pluginId=child["pluginId"].toString();ss.bypassed=flag(child,"bypassed");
        const auto external=ss.pluginId==externalPluginId;
        require(child.getNumChildren()==(external?2:1),"Invalid or mixed insert state");
        ss.parameters=child.getChildWithName("PARAMETERS").createCopy();
        require(ss.parameters.hasType("PARAMETERS"),"Missing parameters");
        if(external){require(version==2,"External insert requires session version 2");emptyParameters(ss.parameters);ss.external=decodeExternal(child.getChildWithName("EXTERNAL"));}
        else if(ss.pluginId.isEmpty())require(ss.parameters.getNumChildren()==0,"Empty slot has parameters");
        else {auto it=std::find_if(PluginRegistry::entries().begin(),PluginRegistry::entries().end(),[&](const auto& e){return ss.pluginId==e.id;});require(it!=PluginRegistry::entries().end()&&!it->monitoringOnly,"Unknown or prohibited insert: "+ss.pluginId);}
    }
 }
 require(monitorSeen&&std::all_of(seen.begin(),seen.end(),[](bool x){return x;}),"Incomplete session");return result;
}
void SessionState::save(const SessionSnapshot& state,const juce::File& file){
 auto xml=encode(state,file).createXml();require(xml!=nullptr,"Cannot encode session XML");const auto text=xml->toString();
 require(text.getNumBytesAsUTF8()<=maxSerializedBytes,"Session is larger than 8 MB");
 juce::TemporaryFile temporary(file);
 require(temporary.getFile().replaceWithText(text,false,false,nullptr),"Could not write session temporary file");
 require(temporary.getFile().getSize()==juce::int64(text.getNumBytesAsUTF8()),"Session temporary file size mismatch");
 require(temporary.overwriteTargetFileWithTemporary(),"Could not replace session file; check permissions");
}
SessionSnapshot SessionState::load(const juce::File& file){
 require(file.existsAsFile()&&file.getSize()>0&&file.getSize()<=juce::int64(maxSerializedBytes),"Session missing, empty, or larger than 8 MB");
 const auto text=file.loadFileAsString();
 require(!text.containsIgnoreCase("<!DOCTYPE")&&!text.containsIgnoreCase("<!ENTITY"),"Session XML declarations are not allowed");
 auto xml=juce::XmlDocument::parse(text);require(xml!=nullptr,"Invalid session XML");return decode(juce::ValueTree::fromXml(*xml),file);
}
SessionWorker::SessionWorker(juce::TimeSliceThread& d,Commit c,Report r,Completion doneCallback,StageExternal externalCallback):juce::Thread("HEM session worker"),disk(d),commit(std::move(c)),report(std::move(r)),completion(std::move(doneCallback)),stageExternal(std::move(externalCallback)){startTimerHz(15);}
SessionWorker::~SessionWorker(){stopTimer();signalThreadShouldExit();waitForThreadToExit(-1);staged.reset();}
bool SessionWorker::save(SessionSnapshot s,const juce::File& f){
 if(isBusy())return false;snapshot=std::move(s);file=f;operation=Operation::Save;error.clear();done=false;busy=true;
 if(!startThread()){busy=false;report("Cannot start session worker");return false;}return true;
}
bool SessionWorker::load(const juce::File& f,Context c){
 if(isBusy())return false;file=f;context=c;operation=Operation::Load;error.clear();done=false;busy=true;
 if(!startThread()){busy=false;report("Cannot start session worker");return false;}return true;
}
void SessionWorker::run(){
 try{
    if(operation==Operation::Retire)staged.reset();
    else if(operation==Operation::Save)PresetManager::save(snapshot,file);
    else{
        if(context.sampleRate<=0||context.blockSize<=0)fail("Session processing context is not prepared");
        snapshot=PresetManager::load(file);
        // Validate monitor settings against their actual registered ranges.
        MonitorProcessor monitor;SessionState::restoreParameters(monitor,snapshot.monitorParameters);
        juce::AudioFormatManager formats;formats.registerBasicFormats();staged=std::make_unique<TrackBank>();
        for(size_t i=0;i<13;++i){if(threadShouldExit())fail("Session load cancelled");auto& track=staged->tracks[i];const auto& saved=snapshot.tracks[i];
            track.trimDb=saved.gainDb;track.muted=saved.muted;track.solo=saved.solo;track.name=saved.name;
            track.prepare(context.sampleRate,context.blockSize);
            if(saved.source!=juce::File()){
                track.playback->prepareFile(saved.source,formats,disk,context.sampleRate,context.blockSize,snapshot.looping);
                track.sourceRate=track.playback->sourceRate;track.loaded=true;
                track.transport().setPosition(std::min(snapshot.positionSeconds,track.transport().getLengthInSeconds()));
            }
            for(int j=0;j<8;++j){const auto& slot=saved.slots[size_t(j)];
                if(slot.pluginId==SessionState::externalPluginId)continue; // Message-thread staging only.
                auto p=PluginRegistry::create(slot.pluginId);
                if(p)SessionState::restoreParameters(*p,slot.parameters);
                auto node=track.chain.stageNode(std::move(p),context.sampleRate,context.blockSize,track.chain.slotLatency());
                track.chain.installInitial(j,std::move(node),slot.bypassed);
            }
        }
    }
 }catch(const std::exception& e){error=e.what();staged.reset();}
 done.store(true,std::memory_order_release);
}
void SessionWorker::timerCallback(){
 if(!done.load(std::memory_order_acquire)||isThreadRunning())return;
 done=false;
 if(operation==Operation::Retire){busy=false;return;}
 if(error.isNotEmpty()){report("Session unchanged: "+error);busy=false;if(completion)completion(operation==Operation::Save,file,juce::Result::fail(error));return;}
 if(operation==Operation::Save){report("Session saved: "+file.getFileName());busy=false;if(completion)completion(true,file,juce::Result::ok());return;}
 auto result=juce::Result::ok();
 try{
    if(SessionState::hasExternal(snapshot)){
        if(!stageExternal)result=juce::Result::fail("External VST3 staging is unavailable; session unchanged");
        else if(!staged)result=juce::Result::fail("No staged session bank; session unchanged");
        else {
            result=stageExternal(*staged,snapshot,context);
            if(result.wasOk())for(size_t i=0;i<snapshot.tracks.size();++i)for(size_t j=0;j<snapshot.tracks[i].slots.size();++j)
                if(snapshot.tracks[i].slots[j].pluginId==SessionState::externalPluginId){
                    const auto* processor=staged->tracks[i].chain.activeProcessor(int(j));
                    require(processor&&processor->isExternal()&&processor->processingReady(),"External insert was not staged; session unchanged");
                }
        }
    }
    if(result.wasOk())result=commit(staged,snapshot,context);
 }catch(const std::exception& e){result=juce::Result::fail("Session unchanged: "+juce::String(e.what()));}
 catch(...){result=juce::Result::fail("Session unchanged: external staging or commit threw an exception");}
 report(result.wasOk()?"Session recalled / transport paused / monitor EQ disabled":result.getErrorMessage());
 if(completion)completion(false,file,result);
 // On success, staged owns the OLD bank. On failure, it owns the rejected bank.
 // External adapters may retire here; ExternalPluginHost retains actual plug-in
 // instances until main-thread collection. Never call back into main from run().
 operation=Operation::Retire;
 if(!startThread()){staged.reset();busy=false;report("Session cleanup completed on message thread");}
}
}
