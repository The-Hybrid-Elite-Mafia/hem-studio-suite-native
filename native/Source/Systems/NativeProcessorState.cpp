#include "NativeProcessorState.h"
#include "../SessionState.h"
#include "BoundedStateInput.h"
#include "../Plugins/ReferenceMatchProcessor.h"
#include <stdexcept>

namespace hem {
namespace {
void require(bool value,const juce::String& error){if(!value)throw std::runtime_error(error.toStdString());}
void checked(const juce::Result& result){require(result.wasOk(),result.getErrorMessage());}
template<class Work> juce::Result attempt(Work&& work){
    try{work();return juce::Result::ok();}
    catch(const std::exception& e){return juce::Result::fail(e.what());}
    catch(...){return juce::Result::fail("Native state operation failed");}
}
NativeResourceStateProvider* providerFor(ModuleProcessor& processor){
    return dynamic_cast<NativeResourceStateProvider*>(&processor);
}
void validateImpl(ModuleProcessor& processor,const NativeProcessorSnapshot& snapshot){
    require(!processor.isExternal(),"External plug-in state cannot use the native state route");
    require(processor.realtimeInsertAllowed(),"Offline clip processors cannot enter native insert state");
    SessionState::validatedParameters(processor,snapshot.parameters);
    auto* provider=providerFor(processor);
    require(!snapshot.matchProfile||dynamic_cast<ReferenceMatchProcessor*>(&processor)!=nullptr,"MATCH_PROFILE is only valid for Reference Match");
    if(!provider){require(!snapshot.native,"Processor does not support NATIVE_RESOURCE");return;}
    require(!processor.monitorOnly(),"Monitor processors cannot contain native resource state");
    require(snapshot.native.has_value(),"Resource provider snapshot is missing NATIVE_RESOURCE");
    const auto& layout=provider->expectedNativeResourceLayout();
    require(layout.ownerType==processor.state.state.getType().toString(),"Native resource owner does not match processor type");
    checked(nativeResource::validateManifest(*snapshot.native,layout));
    checked(provider->validateNativeState(snapshot));
}
}
NativeProcessorSnapshot NativeProcessorState::deepCopy(const NativeProcessorSnapshot& input){
    require(input.parameters.hasType("PARAMETERS")&&input.parameters.getNumChildren()<=128
            &&input.parameters.getNumProperties()<=32,"Invalid whole-state PARAMETERS bounds");
    const auto scalarProperties=[](const juce::ValueTree& tree){for(int i=0;i<tree.getNumProperties();++i){
        const auto& value=tree[tree.getPropertyName(i)];
        require(value.isString()||value.isInt()||value.isInt64()||value.isDouble()||value.isBool(),"Whole-state properties must be value-owned scalars");
    }};
    scalarProperties(input.parameters);
    for(auto child:input.parameters){require(child.hasType("PARAM")&&child.getNumChildren()==0
            &&child.getNumProperties()<=32,"Invalid whole-state scalar child");scalarProperties(child);}
    if(input.native)require(input.native->resources.size()<=nativeResource::maxResources,"Too many native resource descriptors");
    auto copy=input;copy.parameters=input.parameters.createCopy();
    if(input.matchProfile){ReferenceMatchProcessor::Snapshot validated;checked(ReferenceMatchProcessor::decodeSnapshot(*input.matchProfile,validated));copy.matchProfile=input.matchProfile->createCopy();}
    return copy;
}
juce::Result NativeProcessorState::validate(ModuleProcessor& processor,const NativeProcessorSnapshot& snapshot){
    return attempt([&]{validateImpl(processor,snapshot);});
}
juce::Result NativeProcessorState::validatePreparedForPublication(ModuleProcessor& processor){
    return attempt([&]{if(auto* provider=providerFor(processor)){
        require(!provider->hasPendingNativeStateWork(),"Native resource preparation has pending work");
        require(processor.processingReady(),"Native resource processor is not prepared for publication");
    }});
}
juce::Result NativeProcessorState::capture(ModuleProcessor& processor,NativeProcessorSnapshot& out){
    return attempt([&]{
        NativeProcessorSnapshot captured;
        if(auto* provider=providerFor(processor)){
            require(!provider->hasPendingNativeStateWork(),"Wait for native resource work before saving or exporting");
            checked(provider->tryCaptureNativeState(captured));
            require(!provider->hasPendingNativeStateWork(),"Native resource work changed during capture");
        }else captured.parameters=SessionState::captureParameters(processor);
        validateImpl(processor,captured);
        out=deepCopy(captured);
    });
}
juce::Result NativeProcessorState::restoreUnpublished(std::unique_ptr<ModuleProcessor> candidate,
        const NativeProcessorSnapshot& snapshot,const NativeRestoreContext& context,
        std::unique_ptr<ModuleProcessor>& out){
    return attempt([&]{
        require(candidate!=nullptr,"Missing unpublished processor candidate");
        validateImpl(*candidate,snapshot);
        const auto copy=deepCopy(snapshot);
        require(!context.cancelled||!context.cancelled(),"Native state restore cancelled");
        if(auto* provider=providerFor(*candidate)){
            require(std::isfinite(context.sampleRate)&&context.sampleRate>=8000&&context.sampleRate<=384000
                    &&context.blockSize>=1&&context.blockSize<=8192
                    &&std::isfinite(context.tempoBpm)&&context.tempoBpm>=20&&context.tempoBpm<=400,
                    "Invalid native resource restore context");
            checked(provider->restoreNativeStateOffThread(copy,context));
            require(!provider->hasPendingNativeStateWork(),"Native resource restoration has not settled");
        }else SessionState::restoreParameters(*candidate,copy.parameters);
        require(!context.cancelled||!context.cancelled(),"Native state restore cancelled");
        out=std::move(candidate);
    });
}
juce::Result NativeProcessorState::encodeBinary(ModuleProcessor& processor,const NativeProcessorSnapshot& snapshot,juce::MemoryBlock& out){
    return attempt([&]{
        validateImpl(processor,snapshot);
        auto* provider=providerFor(processor);
        require(provider!=nullptr,"New binary state is only for a native resource provider");
        juce::ValueTree root("HEM_NATIVE_PROCESSOR_STATE");root.setProperty("version",1,nullptr);
        root.setProperty("ownerType",processor.state.state.getType().toString(),nullptr);
        root.addChild(snapshot.parameters.createCopy(),-1,nullptr);
        juce::ValueTree native;checked(nativeResource::encodeManifest(*snapshot.native,provider->expectedNativeResourceLayout(),native));
        root.addChild(native,-1,nullptr);
        if(snapshot.matchProfile)root.addChild(snapshot.matchProfile->createCopy(),-1,nullptr);
        auto xml=root.createXml();require(xml!=nullptr,"Cannot encode native binary state");
        std::unique_ptr<juce::XmlElement> precise;checked(nativeResource::encodeManifestXml(*snapshot.native,provider->expectedNativeResourceLayout(),precise));
        auto* previous=xml->getChildByName("NATIVE_RESOURCE");require(previous!=nullptr,"Missing binary native resource state");
        require(xml->replaceChildElement(previous,precise.release()),"Cannot replace binary native resource state");
        if(snapshot.matchProfile){auto exact=ReferenceMatchProcessor::exactProfileXml(*snapshot.matchProfile);require(exact!=nullptr,"Cannot encode exact match profile");auto* child=xml->getChildByName("MATCH_PROFILE");require(child&&xml->replaceChildElement(child,exact.release()),"Missing match profile");}
        const auto text=xml->toString();const auto size=text.getNumBytesAsUTF8();
        require(size>0&&size<=maxBinaryBytes,"Native binary state exceeds 1 MiB");
        juce::MemoryBlock encoded(text.toRawUTF8(),size);out=std::move(encoded);
    });
}
juce::Result NativeProcessorState::decodeBinary(ModuleProcessor& processor,const void* data,size_t size,NativeProcessorSnapshot& out){
    return attempt([&]{
        require(data!=nullptr&&size>0&&size<=maxBinaryBytes,"Native binary state is empty or exceeds 1 MiB");
        auto* provider=providerFor(processor);require(provider!=nullptr,"Processor does not support new native binary state");
        const std::string_view raw(static_cast<const char*>(data),size);
        checked(stateinput::preflightXml(raw,{maxBinaryBytes,stateinput::recommendedMaxDepth}));
        const auto text=juce::String::fromUTF8(raw.data(),int(raw.size()));
        require(text.getNumBytesAsUTF8()==size,"Invalid native binary UTF-8");
        auto xml=juce::XmlDocument::parse(text);require(xml!=nullptr,"Invalid native binary XML");
        const auto root=juce::ValueTree::fromXml(*xml);
        require(root.hasType("HEM_NATIVE_PROCESSOR_STATE")&&root.getNumProperties()==2
                &&root["version"].toString()=="1"&&root["ownerType"].toString()==processor.state.state.getType().toString()
                &&(root.getNumChildren()==2||root.getNumChildren()==3),"Invalid native binary envelope");
        require(root.getChild(0).hasType("PARAMETERS")&&root.getChild(1).hasType("NATIVE_RESOURCE"),"Invalid native binary children");
        NativeProcessorSnapshot decoded;decoded.parameters=root.getChild(0).createCopy();
        nativeResource::State manifest;checked(nativeResource::decodeManifest(root.getChild(1),provider->expectedNativeResourceLayout(),manifest));
        decoded.native=std::move(manifest);if(root.getNumChildren()==3){require(root.getChild(2).hasType("MATCH_PROFILE"),"Unknown native binary auxiliary state");decoded.matchProfile=root.getChild(2).createCopy();}validateImpl(processor,decoded);out=deepCopy(decoded);
    });
}
} // namespace hem
