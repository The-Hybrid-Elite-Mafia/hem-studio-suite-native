#include "PresetManager.h"
#include "BoundedStateInput.h"
#include <stdexcept>
namespace hem {
namespace {void check(bool b,const char* message){if(!b)throw std::runtime_error(message);}}
bool PresetManager::supported(const juce::File& f){return f.hasFileExtension("xml;json");}
juce::var PresetManager::toJson(const juce::ValueTree& tree){
    auto* object=new juce::DynamicObject();juce::var result(object);
    object->setProperty("type",tree.getType().toString());
    auto* properties=new juce::DynamicObject();object->setProperty("properties",juce::var(properties));
    for(int i=0;i<tree.getNumProperties();++i){auto name=tree.getPropertyName(i);properties->setProperty(name,tree[name]);}
    juce::Array<juce::var> children;for(auto child:tree)children.add(toJson(child));
    object->setProperty("children",children);return result;
}
juce::ValueTree PresetManager::fromJson(const juce::var& value,int depth){
    check(depth<=6,"Preset JSON nesting is too deep");auto* object=value.getDynamicObject();check(object!=nullptr,"Preset JSON node must be an object");
    check(object->getProperties().size()==3&&object->hasProperty("type")&&object->hasProperty("properties")&&object->hasProperty("children"),"Invalid preset JSON node fields");
    check(object->getProperty("type").isString(),"Preset JSON node type must be a string");
    auto type=object->getProperty("type").toString();
    check(type=="HEM_SESSION"||type=="MONITOR"||type=="TRACK"||type=="SLOT"||type=="PARAMETERS"||type=="PARAM"||type=="EXTERNAL"||type=="NATIVE_RESOURCE"||type=="RESOURCE"||type=="MATCH_PROFILE"||type=="REFERENCE_MATCH"||type=="SOURCE"||type=="REFERENCE"||type=="PREVIOUS_SOURCE"||type=="PREVIOUS_REFERENCE"||type=="BIN"||type=="REFERENCE_IDENTITY"||type=="PREVIOUS_IDENTITY"||type=="CLIP_EDIT"||type=="MEDIA"||type=="RECIPE","Unknown preset JSON node");
    auto* properties=object->getProperty("properties").getDynamicObject();check(properties!=nullptr,"Preset JSON properties missing");
    auto children=object->getProperty("children");auto* array=children.getArray();check(array!=nullptr&&array->size()<=128,"Invalid preset JSON children");
    check(properties->getProperties().size()<=32,"Too many preset properties");
    juce::ValueTree result{juce::Identifier(type)};
    for(const auto& entry:properties->getProperties()){
        const auto& v=entry.value;check(v.isString()||v.isInt()||v.isInt64()||v.isDouble()||v.isBool(),"Preset property is not a scalar");
        check(!v.isDouble()||std::isfinite(double(v)),"Preset contains non-finite value");
        if((type=="NATIVE_RESOURCE"&&entry.name==juce::Identifier("version"))
            ||(type=="RESOURCE"&&(entry.name==juce::Identifier("index")||entry.name==juce::Identifier("byteSize")
                ||entry.name==juce::Identifier("sourceRate")||entry.name==juce::Identifier("channels")||entry.name==juce::Identifier("frames"))))
            check(v.isInt()||v.isInt64()||(type=="RESOURCE"&&entry.name==juce::Identifier("sourceRate")&&v.isDouble()),"Native JSON integers must be integer values; sourceRate must be numeric");
        if(type=="MATCH_PROFILE"||type=="SOURCE"||type=="REFERENCE"||type=="PREVIOUS_SOURCE"||type=="PREVIOUS_REFERENCE"||type=="BIN")check(v.isInt()||v.isInt64()||v.isDouble()||v.isBool(),"Match profile JSON values must be numeric");
        result.setProperty(entry.name,v,nullptr);
    }
    for(const auto& child:*array)result.addChild(fromJson(child,depth+1),-1,nullptr);
    return result;
}
void PresetManager::save(const SessionSnapshot& snapshot,const juce::File& file){
    check(supported(file),"Use an .xml or .json preset extension");
    if(file.hasFileExtension("xml")){SessionState::save(snapshot,file);return;}
    const auto text=juce::JSON::toString(toJson(SessionState::encode(snapshot,file)),false);
    check(text.getNumBytesAsUTF8()<=SessionState::maxSerializedBytes,"Preset JSON is larger than 8 MB");
    juce::TemporaryFile temporary(file);
    check(temporary.getFile().replaceWithText(text,false,false,nullptr),"Cannot write preset JSON");
    check(temporary.getFile().getSize()==juce::int64(text.getNumBytesAsUTF8()),"Preset JSON temporary file size mismatch");
    check(temporary.overwriteTargetFileWithTemporary(),"Cannot replace preset file");
}
SessionSnapshot PresetManager::load(const juce::File& file){
    check(supported(file),"Use an .xml or .json preset extension");
    SessionSnapshot snapshot;
    if(file.hasFileExtension("xml"))snapshot=SessionState::load(file);
    else {
        check(file.existsAsFile()&&file.getSize()>0&&file.getSize()<=juce::int64(SessionState::maxSerializedBytes),"Preset missing, empty or larger than 8 MB");
        auto stream=file.createInputStream();check(stream!=nullptr,"Cannot read preset JSON");juce::MemoryBlock bytes;
        stream->readIntoMemoryBlock(bytes,SessionState::maxSerializedBytes+1);check(stream->getStatus().wasOk(),"Cannot read complete preset input");
        check(bytes.getSize()>0&&bytes.getSize()<=SessionState::maxSerializedBytes,"Preset JSON is empty or exceeds 8 MB");
        const std::string_view raw(static_cast<const char*>(bytes.getData()),bytes.getSize());
        juce::String text;check(stateinput::decodeFileText(raw,SessionState::maxSerializedBytes,text).wasOk(),"Preset input has invalid or oversized text encoding");
        const auto preflight=stateinput::preflightJson({text.toRawUTF8(),text.getNumBytesAsUTF8()},
            {SessionState::maxSerializedBytes,stateinput::recommendedMaxDepth});
        check(preflight.wasOk(),"Preset JSON failed bounded input validation");
        juce::var document;auto parsed=juce::JSON::parse(text,document);
        check(parsed.wasOk(),"Malformed JSON preset");snapshot=SessionState::decode(fromJson(document),file);
    }
    // Both formats reject invalid plugin parameter states before any host commit.
    for(const auto& t:snapshot.tracks)for(const auto& s:t.slots)if(s.pluginId.isNotEmpty()){
        // decode() already validates the opaque external container. Recreating
        // plug-ins here would execute third-party code on the session worker.
        if(s.pluginId==SessionState::externalPluginId)continue;
        auto processor=PluginRegistry::create(s.pluginId);check(processor!=nullptr,"Unknown preset plugin");
        const auto validated=NativeProcessorState::validate(*processor,s);
        if(validated.failed())throw std::runtime_error(validated.getErrorMessage().toStdString());
        // Preserve legacy convolution validation (including unavailable-reference
        // behavior). Resource providers defer actual I/O to the prepared restore.
        if(!dynamic_cast<NativeResourceStateProvider*>(processor.get())){
            std::unique_ptr<ModuleProcessor> restored;
            const auto result=NativeProcessorState::restoreUnpublished(std::move(processor),s,{},restored);
            if(result.failed())throw std::runtime_error(result.getErrorMessage().toStdString());
        }
    }
    MonitorProcessor monitor;SessionState::restoreParameters(monitor,snapshot.monitorParameters);
    return snapshot;
}
juce::Array<juce::File> PresetManager::list(const juce::File& directory){
    juce::Array<juce::File> result;
    for(const auto& file:directory.findChildFiles(juce::File::findFiles,false))if(supported(file))result.add(file);
    std::sort(result.begin(),result.end(),[](const auto& a,const auto& b){return a.getFileName().compareNatural(b.getFileName())<0;});return result;
}
juce::Result PresetManager::rename(const juce::File& file,const juce::String& name){
    if(!supported(file)||!file.existsAsFile())return juce::Result::fail("Preset file not found");
    if(name.isEmpty()||name!=juce::File::createLegalFileName(name)||name=="."||name=="..")return juce::Result::fail("Invalid preset name");
    const auto target=file.getSiblingFile(name+file.getFileExtension());
    if(target==file)return juce::Result::ok();
    if(target.exists())return juce::Result::fail("A preset with that name already exists");
    return file.moveFileTo(target)?juce::Result::ok():juce::Result::fail("Could not rename preset");
}
juce::Result PresetManager::remove(const juce::File& file){
    if(!supported(file)||!file.existsAsFile())return juce::Result::fail("Preset file not found");
    return file.deleteFile()?juce::Result::ok():juce::Result::fail("Could not delete preset");
}
}

