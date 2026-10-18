#include "ReverbProcessors.h"
#include "ReverbEditors.h"
#include <juce_cryptography/juce_cryptography.h>
#include <cstdlib>
#include <stdexcept>
namespace hem {
namespace {
bool parseNumber(const juce::var&v,double&out){auto text=v.toString().toStdString();char*end=nullptr;out=std::strtod(text.c_str(),&end);return end!=text.c_str()&&!*end&&std::isfinite(out);}
juce::ValueTree validateParameters(const ModuleProcessor& p, const juce::ValueTree& tree, bool allowIR = false) {
    if ((!tree.hasType("PARAMETERS") && !tree.hasType(p.state.state.getType()))
        || tree.getNumChildren() != int(p.parameterSpecs().size())) return {};
    constexpr std::array<const char*, 6> resourceKeys {
        "irResourceVersion", "irPath", "irSha256", "irSampleRate", "irChannels", "irFrames"
    };
    if (tree.getNumProperties() != 0) {
        if (!allowIR || tree.getNumProperties() != int(resourceKeys.size())) return {};
        for (auto* key : resourceKeys) if (!tree.hasProperty(key)) return {};
    }
    std::vector<bool> seen(p.parameterSpecs().size(), false);
    for (const auto child : tree) {
        if (!child.hasType("PARAM") || child.getNumChildren() != 0
            || child.getNumProperties() != 2 || !child.hasProperty("id") || !child.hasProperty("value")) return {};
        size_t index = 0;
        while (index < p.parameterSpecs().size() && child["id"].toString() != p.parameterSpecs()[index].id) ++index;
        if (index == seen.size() || seen[index]) return {};
        seen[index] = true;
        double number = 0;
        if (!parseNumber(child["value"], number)) return {};
        const auto& spec = p.parameterSpecs()[index];
        // APVTS's skew conversion can round an integer Hz default by a few
        // float ULPs. This tolerance admits its own serialized state, not
        // arbitrary fractional enum/toggle values or malformed number strings.
        const double tolerance = 4 * std::numeric_limits<float>::epsilon() * std::max(1., std::abs(number));
        if (number < spec.minimum - tolerance || number > spec.maximum + tolerance
            || (spec.step >= 1 && std::abs(number - std::round(number)) > tolerance)) return {};
    }
    return tree.createCopy();
}

// Constructor-only canonicalization of the new reverb adapters. Shared ranges,
// skew, intervals and defaults are unchanged; no raw atomic is written directly.
void initialiseReverbDefaults(ModuleProcessor& processor) {
    for (const auto& spec : processor.parameterSpecs()) {
        auto* parameter = processor.state.getParameter(spec.id);
        parameter->setValueNotifyingHost(parameter->convertTo0to1(spec.initial));
    }
    auto canonical = processor.state.copyState();
    for (auto child : canonical)
        child.setProperty("value", processor.state.getRawParameterValue(child["id"].toString())->load(), nullptr);
    processor.state.replaceState(canonical);
}

void requireRate(double rate){if(!std::isfinite(rate)||rate<8000||rate>192000)throw std::invalid_argument("Reverb sample rate must be 8-192 kHz");}
}
juce::ValueTree ConvolutionReverbProcessor::migrateParameterState(const juce::ValueTree&t)const{return validateParameters(*this,t,true);}
juce::ValueTree AlgorithmicReverbProcessor::migrateParameterState(const juce::ValueTree&t)const{return validateParameters(*this,t);}

ConvolutionReverbProcessor::ConvolutionReverbProcessor():ModuleProcessor("CONVOLUTION_REVERB","Convolution Reverb",{
 {"predelay","PREDELAY",0,250,0,.1f,"ms"},{"irSize","IR SIZE",25,200,100,.1f,"%"},
 {"lowCut","LOW CUT",20,2000,20,1,"Hz"},{"highCut","HIGH CUT",500,20000,20000,1,"Hz"},
 {"mix","MIX",0,100,0,.1f,"%"},{"output","OUTPUT",-24,6,0,.1f,"dB"},
 {"lowCutOn","LOW CUT ON",0,1,0,1,"",true},{"highCutOn","HIGH CUT ON",0,1,0,1,"",true},
 {"soloWet","SOLO WET",0,1,0,1,"",true},{"bypass","BYPASS",0,1,0,1,"",true}
 }),Thread("HEM convolution IR preparation"){initialiseReverbDefaults(*this);startThread();}
ConvolutionReverbProcessor::~ConvolutionReverbProcessor(){signalThreadShouldExit();notify();stopThread(-1);}
ConvolutionReverbSettings ConvolutionReverbProcessor::settings()const noexcept{ConvolutionReverbSettings p;p.predelay=value(0);p.irSize=value(1);p.lowCut=value(2);p.highCut=value(3);p.mix=value(4);p.output=value(5);p.lowCutOn=value(6)>.5f;p.highCutOn=value(7)>.5f;p.soloWet=value(8)>.5f;p.bypass=value(9)>.5f;return p;}
void ConvolutionReverbProcessor::prepareToPlay(double rate,int block){const std::lock_guard<std::mutex>operation(resourceOperationMutex);ready=false;requireRate(rate);resetMeterTelemetry();actualRate.store(rate);core.resizeOffThread(value(1));core.prepare(rate,block);setLatencySamples(core.latency());publishMeterPreparation();ready=true;}
void ConvolutionReverbProcessor::processBlock(juce::AudioBuffer<float>&b,juce::MidiBuffer&){juce::ScopedNoDenormals guard;if(!processingReady()||b.getNumChannels()<2||b.getNumSamples()<=0)return;captureInput(b);core.process(b.getWritePointer(0),b.getWritePointer(1),b.getNumSamples(),settings());publishMeterBlock(b,core.wetPeak.load());}
juce::AudioProcessorEditor* ConvolutionReverbProcessor::createEditor(){return new ReverbEditor(*this);}
juce::String ConvolutionReverbProcessor::helpText()const{return "Load a mono or stereo WAV, AIFF or FLAC IR, 8-192 kHz, at most 10 s / 32 MiB. No IR is dry-safe, including SOLO WET. IR SIZE changes time and pitch through resampling. Filters affect wet audio only. IR files are referenced by full path and SHA-256; missing or changed files require reload. Very long IRs at small buffers need CPU qualification; IR changes temporarily process two engines. Fixed 4 ms engine latency with aligned dry audio; predelay and onset in the IR are effect time.";}
bool ConvolutionReverbProcessor::parameterActive(const juce::String&id)const{if(id=="lowCut")return value(6)>.5f;if(id=="highCut")return value(7)>.5f;return true;}
ConvolutionReverbProcessor::ResourceInfo ConvolutionReverbProcessor::resourceInfo()const{const std::lock_guard<std::mutex>lock(resourceMutex);auto snapshot=resource;if(filePending){snapshot.loading=true;snapshot.status="Loading IR...";}return snapshot;}
void ConvolutionReverbProcessor::requestIRFile(const juce::File&f){{const std::lock_guard<std::mutex>lock(resourceMutex);requestedFile=f;filePending=true;++requestGeneration;resource.loading=true;resource.status="Loading IR...";}notify();}
void ConvolutionReverbProcessor::resourceFailure(const juce::String&why){const std::lock_guard<std::mutex>lock(resourceMutex);resource.loading=false;resource.status=why+(resource.ready?" (previous IR retained)":"");}
juce::Result ConvolutionReverbProcessor::loadIRFileOffThread(const juce::File&file,const juce::String&expected){const std::lock_guard<std::mutex> operation(resourceOperationMutex);{const std::lock_guard<std::mutex>lock(resourceMutex);++requestGeneration;filePending=false;}return decodeFile(file,expected);}
juce::Result ConvolutionReverbProcessor::decodeFile(const juce::File&file,const juce::String&expected,const ResourceInfo* expectedMetadata,bool* metadataMismatch){
    {const std::lock_guard<std::mutex>lock(resourceMutex);resource.loading=true;resource.status="Loading IR...";}
    constexpr juce::int64 maxBytes=32*1024*1024;
    auto fail=[this](const juce::String&s){resourceFailure(s);return juce::Result::fail(s);};
    if(!file.existsAsFile())return fail("IR file is missing; locate and reload it");
    const auto bytes=file.getSize();if(bytes<=0||bytes>maxBytes)return fail("IR file exceeds the 32 MiB limit or is empty");
    auto input=file.createInputStream();if(!input)return fail("IR file cannot be opened");
    juce::MemoryBlock data;const auto actual=input->readIntoMemoryBlock(data,static_cast<std::ptrdiff_t>(bytes));
    if(actual!=bytes||data.getSize()!=size_t(bytes)||!input->isExhausted())return fail("IR file changed or could not be read completely");
    const auto sha=juce::SHA256(data).toHexString();if(expected.isNotEmpty()&&sha!=expected)return fail("IR file content changed; reload to approve the new response");
    juce::AudioFormatManager formats;formats.registerBasicFormats();
    auto stream=std::make_unique<juce::MemoryInputStream>(data,false);
    auto reader=std::unique_ptr<juce::AudioFormatReader>(formats.createReaderFor(std::move(stream)));
    if(!reader)return fail("IR must be a supported WAV, AIFF or FLAC audio file");
    if(reader->numChannels<1||reader->numChannels>2||!std::isfinite(reader->sampleRate)||reader->sampleRate<8000||reader->sampleRate>192000||reader->lengthInSamples<1||reader->lengthInSamples>ConvolutionReverbCore::maxSourceFrames||reader->lengthInSamples>reader->sampleRate*ConvolutionReverbCore::maxSourceSeconds)return fail("IR must be mono/stereo, 8-192 kHz and no longer than 10 seconds");
    if(expectedMetadata&&(std::abs(reader->sampleRate-expectedMetadata->sourceRate)>1.e-7||int(reader->numChannels)!=expectedMetadata->channels||reader->lengthInSamples!=expectedMetadata->frames)){if(metadataMismatch)*metadataMismatch=true;return fail("IR metadata differs from saved resource");}
    juce::AudioBuffer<float>ir(int(reader->numChannels),int(reader->lengthInSamples));
    if(!reader->read(&ir,0,ir.getNumSamples(),0,true,true))return fail("IR audio decoding failed");
    const auto result=core.loadOffThread(ir,reader->sampleRate,value(1));if(result.failed())return fail(result.getErrorMessage());
    {const std::lock_guard<std::mutex>lock(resourceMutex);resource.path=file.getFullPathName();resource.name=file.getFileName();resource.sha256=sha;resource.sourceRate=reader->sampleRate;resource.channels=int(reader->numChannels);resource.frames=int(reader->lengthInSamples);resource.ready=true;resource.loading=false;resource.status="IR ready";}
    return juce::Result::ok();
}
void ConvolutionReverbProcessor::loadQueuedFile(const juce::File&file,std::uint64_t generation){const std::lock_guard<std::mutex>operation(resourceOperationMutex);bool current=false;{const std::lock_guard<std::mutex>lock(resourceMutex);current=generation==requestGeneration;}if(current)decodeFile(file,{});}
void ConvolutionReverbProcessor::run(){while(!threadShouldExit()){
    juce::File file;bool pending=false;std::uint64_t generation=0;{const std::lock_guard<std::mutex>lock(resourceMutex);if(filePending){file=requestedFile;generation=requestGeneration;filePending=false;pending=true;}}
    try{if(pending)loadQueuedFile(file,generation);
    {const std::lock_guard<std::mutex>operation(resourceOperationMutex);core.resizeOffThread(value(1));core.reclaimOffThread();}}catch(const std::exception&e){resourceFailure(juce::String("IR preparation failed: ")+e.what());}wait(20);
}}
void ConvolutionReverbProcessor::captureResourceState(juce::ValueTree&parameters)const{
    const auto r=resourceInfo();if(r.loading)throw std::runtime_error("Wait for IR loading before saving the session");parameters.setProperty("irResourceVersion",1,nullptr);
    parameters.setProperty("irPath",r.path,nullptr);parameters.setProperty("irSha256",r.sha256,nullptr);
    parameters.setProperty("irSampleRate",r.sourceRate,nullptr);parameters.setProperty("irChannels",r.channels,nullptr);parameters.setProperty("irFrames",r.frames,nullptr);
}
juce::Result ConvolutionReverbProcessor::restoreResourceState(const juce::ValueTree&p){
    const std::lock_guard<std::mutex> operation(resourceOperationMutex);
    const std::array<const char*,6>keys{"irResourceVersion","irPath","irSha256","irSampleRate","irChannels","irFrames"};int present=0;for(auto*k:keys)if(p.hasProperty(k))++present;
    if(present==0){core.clearOffThread();const std::lock_guard<std::mutex>lock(resourceMutex);resource={};filePending=false;++requestGeneration;return juce::Result::ok();}
    double version=0;if(present!=int(keys.size())||!parseNumber(p["irResourceVersion"],version)||version!=1)return juce::Result::fail("Invalid or incomplete IR resource state");
    const auto path=p["irPath"].toString(),hash=p["irSha256"].toString();double rate=0,channels=0,frames=0;if(!parseNumber(p["irSampleRate"],rate)||!parseNumber(p["irChannels"],channels)||!parseNumber(p["irFrames"],frames))return juce::Result::fail("Invalid IR numeric metadata");
    if(path.isEmpty()){
        if(hash.isNotEmpty()||rate!=0||channels!=0||frames!=0)return juce::Result::fail("Empty IR path has contradictory metadata");
        core.clearOffThread();const std::lock_guard<std::mutex>lock(resourceMutex);resource={};filePending=false;++requestGeneration;return juce::Result::ok();
    }
    const bool nativeAbsolute=juce::File::isAbsolutePath(path);const bool windowsAbsolute=path.length()>2&&juce::CharacterFunctions::isLetter(path[0])&&path[1]==':'&&(path[2]=='/'||path[2]=='\\');const bool uncAbsolute=path.startsWith("\\\\");
    if(path.length()>4096||(!nativeAbsolute&&!windowsAbsolute&&!uncAbsolute)||hash.length()!=64||hash.containsAnyOf("ABCDEF")||!hash.containsOnly("0123456789abcdef")||!std::isfinite(rate)||rate<8000||rate>192000||(channels!=1&&channels!=2)||!std::isfinite(frames)||frames<1||frames>ConvolutionReverbCore::maxSourceFrames||frames>rate*10||std::floor(frames)!=frames)return juce::Result::fail("Invalid IR resource path, hash or metadata");
    ResourceInfo expectedResource;expectedResource.path=path;expectedResource.name=path.replaceCharacter('\\','/').fromLastOccurrenceOf("/",false,false);expectedResource.sha256=hash;expectedResource.sourceRate=rate;expectedResource.channels=int(channels);expectedResource.frames=int(frames);
    ResourceInfo previous;
    {const std::lock_guard<std::mutex>lock(resourceMutex);previous=resource;filePending=false;++requestGeneration;}
    if(!nativeAbsolute){core.clearOffThread();expectedResource.status="IR path belongs to another computer; locate and reload it";const std::lock_guard<std::mutex>lock(resourceMutex);resource=expectedResource;return juce::Result::ok();}
    bool metadataMismatch=false;
    const auto load=decodeFile(juce::File(path),hash,&expectedResource,&metadataMismatch);
    if(load.wasOk())return juce::Result::ok();
    if(metadataMismatch){previous.loading=false;previous.status=load.getErrorMessage()+(previous.ready?" (previous IR retained)":"");const std::lock_guard<std::mutex>lock(resourceMutex);resource=previous;return load;}
    // Valid but unavailable resource clears unrelated old audio and remains a
    // loadable session. The original path/hash are kept for later relocation.
    core.clearOffThread();expectedResource.status=load.getErrorMessage();
    {const std::lock_guard<std::mutex>lock(resourceMutex);resource=expectedResource;}
    return juce::Result::ok();
}

void ConvolutionReverbProcessor::getStateInformation(juce::MemoryBlock&dest){auto tree=state.copyState();captureResourceState(tree);if(auto xml=tree.createXml())copyXmlToBinary(*xml,dest);}
void ConvolutionReverbProcessor::setStateInformation(const void*data,int bytes){if(!data||bytes<=0||bytes>1024*1024)return;if(auto xml=getXmlFromBinary(data,bytes)){auto tree=migrateParameterState(juce::ValueTree::fromXml(*xml));if(tree.isValid()&&tree.hasType(state.state.getType())&&restoreResourceState(tree).wasOk())state.replaceState(tree);}}
void AlgorithmicReverbProcessor::setStateInformation(const void*data,int bytes){if(!data||bytes<=0||bytes>1024*1024)return;if(auto xml=getXmlFromBinary(data,bytes)){auto tree=migrateParameterState(juce::ValueTree::fromXml(*xml));if(tree.isValid()&&tree.hasType(state.state.getType()))state.replaceState(tree);}}
AlgorithmicReverbProcessor::AlgorithmicReverbProcessor():ModuleProcessor("ALGORITHMIC_REVERB","Algorithmic Reverb",{
 {"size","SIZE",25,200,100,.1f,"%"},{"decay","DECAY",.1f,20,2,.01f,"s"},{"predelay","PREDELAY",0,250,0,.1f,"ms"},
 {"diffusion","DIFFUSION",0,100,70,.1f,"%"},{"modDepth","MOD DEPTH",0,10,0,.01f,"ms"},{"modRate","MOD RATE",.01f,5,.5f,.01f,"Hz"},
 {"damping","DAMPING",500,20000,8000,1,"Hz"},{"mix","MIX",0,100,0,.1f,"%"},{"output","OUTPUT",-24,6,0,.1f,"dB"},
 {"algorithm","ALGORITHM",0,2,0,1,""},{"soloWet","SOLO WET",0,1,0,1,"",true},{"bypass","BYPASS",0,1,0,1,"",true}
 }){initialiseReverbDefaults(*this);}
AlgorithmicReverbSettings AlgorithmicReverbProcessor::settings()const noexcept{AlgorithmicReverbSettings p;p.size=value(0);p.decay=value(1);p.predelay=value(2);p.diffusion=value(3);p.modDepth=value(4);p.modRate=value(5);p.damping=value(6);p.mix=value(7);p.output=value(8);p.algorithm=int(std::lround(value(9)));p.soloWet=value(10)>.5f;p.bypass=value(11)>.5f;return p;}
void AlgorithmicReverbProcessor::prepareToPlay(double rate,int){ready=false;requireRate(rate);resetMeterTelemetry();actualRate.store(rate);renderedFrames.store(0);core.prepare(rate);setLatencySamples(core.latency());publishMeterPreparation();ready=true;}
void AlgorithmicReverbProcessor::processBlock(juce::AudioBuffer<float>&b,juce::MidiBuffer&){juce::ScopedNoDenormals guard;if(!processingReady()||b.getNumChannels()<2||b.getNumSamples()<=0)return;captureInput(b);core.process(b.getWritePointer(0),b.getWritePointer(1),b.getNumSamples(),settings());renderedFrames.fetch_add(std::uint64_t(b.getNumSamples()));publishMeterBlock(b,core.wetPeak.load());}
juce::AudioProcessorEditor*AlgorithmicReverbProcessor::createEditor(){return new ReverbEditor(*this);}
}
