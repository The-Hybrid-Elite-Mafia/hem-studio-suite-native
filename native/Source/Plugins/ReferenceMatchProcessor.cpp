#include "ReferenceMatchProcessor.h"
#include <cmath>
#include <charconv>
#include "ReferenceMatchEditor.h"
#include "../Systems/NativeProcessorState.h"
namespace hem {
using namespace reference_match;
std::vector<ParameterSpec> ReferenceMatchProcessor::specs(){return {{"matchAmount","Match Amount",0,1,1,.001f,""},{"smoothing","Smoothing",0,3,.5f,.01f,"oct"},{"output","Output",-24,24,0,.1f,"dB"},{"bypass","Bypass",0,1,0,1,"",true}};}
ReferenceMatchProcessor::ReferenceMatchProcessor():ModuleProcessor("REFERENCE_MATCH","Reference Match",specs()),juce::Thread("ReferenceMatch worker"){}
ReferenceMatchProcessor::~ReferenceMatchProcessor(){signalThreadShouldExit();notify();stopThread(-1);}
juce::AudioProcessorEditor* ReferenceMatchProcessor::createEditor(){return new ReferenceMatchEditor(*this);}
void ReferenceMatchProcessor::sourceTimeline(uint64_t frame)noexcept{if(timelineKnown&&expectedTimeline!=frame&&captureState.load()!=0)cancelCapture=true;expectedTimeline=frame;timelineKnown=true;}
void ReferenceMatchProcessor::cancelCaptureWhileCallbackExcluded(){signalThreadShouldExit();notify();stopThread(-1);{std::lock_guard<std::mutex> lock(mutex);captureState=0;capturedReady=false;cancelCapture=true;timelineKnown=false;working=false;}if(prepared.load())startThread();}
ReferenceMatchProcessor::Levels ReferenceMatchProcessor::levels()const noexcept {Levels l;for(size_t i=0;i<2;++i){l.input[i]=levelValues[i].load();l.output[i]=levelValues[i+2].load();}l.serial=levelSerial.load();return l;}
nativeResource::ResourceDescriptor ReferenceMatchProcessor::referenceIdentityOffThread()const{std::lock_guard<std::mutex> lock(mutex);return saved.referenceIdentity;}
juce::Result ReferenceMatchProcessor::loadReferenceFileUnpublished(const juce::File& file){
    if(published.load())return juce::Result::fail("Use private staged candidate");
    constexpr juce::int64 limit=512*1024*1024;const auto bytes=file.getSize();
    if(!file.existsAsFile()||bytes<=0||bytes>limit)return juce::Result::fail("Reference must be at most 512 MiB");
    juce::MemoryBlock data;juce::FileInputStream input(file);if(!input.openedOk())return juce::Result::fail("Cannot open reference");data.setSize(size_t(bytes));
    if(input.read(data.getData(),int(bytes))!=bytes||!input.isExhausted())return juce::Result::fail("Reference changed while reading");
    juce::AudioFormatManager formats;formats.registerBasicFormats();std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(std::make_unique<juce::MemoryInputStream>(data,false)));
    if(!reader||!std::isfinite(reader->sampleRate)||reader->sampleRate<22050||reader->sampleRate>192000||reader->numChannels<1||reader->numChannels>2||reader->lengthInSamples<=0||reader->lengthInSamples>reader->sampleRate*1800)return juce::Result::fail("Reference must be mono/stereo,22.05–192kHz,up to30minutes");
    const int frames=int(std::min(reader->lengthInSamples,juce::int64(reader->sampleRate*30)));juce::AudioBuffer<float> pcm(int(reader->numChannels),frames);if(!reader->read(&pcm,0,frames,0,true,true))return juce::Result::fail("Reference decode failed");
    auto profile=analyze(pcm.getArrayOfReadPointers(),size_t(pcm.getNumChannels()),size_t(frames),reader->sampleRate);if(profile.status!=Status::ready)return juce::Result::fail("First30seconds contain insufficient valid broadband signal");
    nativeResource::ResourceDescriptor identity{"reference",0,file.getFullPathName(),juce::SHA256(data).toHexString(),bytes,reader->sampleRate,int(reader->numChannels),reader->lengthInSamples};
    std::lock_guard<std::mutex> lock(mutex);beginEditLocked();saved.reference=profile;saved.referenceIdentity=std::move(identity);return juce::Result::ok();
}
void ReferenceMatchProcessor::prepareToPlay(double rate,int){
    prepared=false;signalThreadShouldExit();notify();stopThread(-1);
    std::lock_guard<std::mutex> lock(mutex);actualRate=rate;resetMeterTelemetry();core.reset();captureState=0;capturedReady=false;timelineKnown=false;
    if(!std::isfinite(rate)||rate<22050||rate>192000){error="Unsupported analysis rate";return;}
    captureLeft.assign(static_cast<size_t>(rate*10),0);captureRight.assign(captureLeft.size(),0);
    for(auto& m:mailbox)m.state=0;
    if(saved.committed){auto s=fit(saved.source,saved.reference,rate,value(0),value(1));if(!s.valid){error="Stored profile cannot fit device rate";return;}core.publish(s);preparedTail=s.tailSeconds;appliedAmount=value(0);appliedSmoothing=value(1);}
    committed=saved.committed;setLatencySamples(0);prepared=true;publishMeterPreparation();startThread();
}
bool ReferenceMatchProcessor::startSourceCapture()noexcept {if(!prepared.load())return false;int expected=0;if(!captureState.compare_exchange_strong(expected,4))return false;cancelCapture=false;stopCapture=false;captureProgressSamples=0;captureState.store(1);return true;}
juce::String ReferenceMatchProcessor::captureProgressText() const {
    const int state=captureState.load();
    if(state==1||state==4)return "WAITING FOR AUDIO ON THIS TRACK";
    if(state==2)return "CAPTURING "+juce::String(captureProgressSamples.load()/std::max(1.,actualRate.load()),1)+" / 10 s";
    if(state==3)return "ANALYZING CAPTURED SOURCE";
    return {};
}
void ReferenceMatchProcessor::processBlock(juce::AudioBuffer<float>& b,juce::MidiBuffer&){
    published=true;if(b.getNumChannels()<2||!prepared.load())return;captureInput(b);for(int ch=0;ch<2;++ch)levelValues[size_t(ch)]=b.getMagnitude(ch,0,b.getNumSamples());
    auto capture=captureState.load(std::memory_order_acquire);if(capture==1){captureFrames=0;captureStartFrame=timelineKnown?expectedTimeline:0;++captureGeneration;captureState.store(2);capture=2;}
    if(capture==2){size_t n=std::min(static_cast<size_t>(b.getNumSamples()),captureLeft.size()-captureFrames);std::copy_n(b.getReadPointer(0),n,captureLeft.data()+captureFrames);std::copy_n(b.getReadPointer(1),n,captureRight.data()+captureFrames);captureFrames+=n;captureProgressSamples.store(unsigned(captureFrames),std::memory_order_relaxed);if(captureFrames==captureLeft.size()||cancelCapture.load()||stopCapture.load())captureState.store(3,std::memory_order_release);}
    for(auto& m:mailbox){int expected=2;if(m.state.compare_exchange_strong(expected,3,std::memory_order_acquire)){if(m.amount==value(0)&&m.smoothing==value(1)){core.publish(m.solution);preparedTail=std::max(preparedTail.load(),m.solution.tailSeconds);transitionFrames=256;appliedAmount=m.amount;appliedSmoothing=m.smoothing;}m.state.store(0,std::memory_order_release);}}
    core.process(b.getWritePointer(0),b.getWritePointer(1),static_cast<size_t>(b.getNumSamples()),value(2),value(3)>=.5f);transitionFrames=std::max(0,transitionFrames.load()-b.getNumSamples());for(int ch=0;ch<2;++ch)levelValues[size_t(ch+2)]=b.getMagnitude(ch,0,b.getNumSamples());levelSerial.fetch_add(1);if(timelineKnown)expectedTimeline+=uint64_t(b.getNumSamples());
    peak=std::max(b.getMagnitude(0,0,b.getNumSamples()),b.getMagnitude(1,0,b.getNumSamples()));publishMeterBlock(b);
}
void ReferenceMatchProcessor::run(){while(!threadShouldExit()){
    if(captureState.load(std::memory_order_acquire)==3){working=true;const float* channels[]={captureLeft.data(),captureRight.data()};auto p=analyze(channels,2,captureFrames,actualRate.load(),captureGeneration,cancelCapture.load()?1:0);p.startFrame=captureStartFrame;if(cancelCapture.load())p.status=Status::discontinuity;{std::lock_guard<std::mutex> lock(mutex);captured=p;capturedReady=true;}captureState=0;working=false;}
    if(committed.load()&&(value(0)!=appliedAmount.load()||value(1)!=appliedSmoothing.load())){
        // One queued publication at a time prevents an older slot winning later.
        bool occupied=false;for(auto& m:mailbox)occupied|=m.state.load()!=0;
        if(!occupied){working=true;std::lock_guard<std::mutex> lock(mutex);float amount=value(0),smooth=value(1);auto solution=fit(saved.source,saved.reference,actualRate.load(),amount,smooth);
            if(solution.valid){auto& m=mailbox[0];m.state=1;m.solution=solution;m.amount=amount;m.smoothing=smooth;m.state.store(2,std::memory_order_release);}working=false;}
    }
    wait(10);
}}
bool ReferenceMatchProcessor::takeCapturedProfile(Profile& out){std::lock_guard<std::mutex> lock(mutex);if(!capturedReady)return false;out=captured;capturedReady=false;return true;}
void ReferenceMatchProcessor::beginEditLocked(){if(!editing&&saved.committed){saved.previousSource=saved.source;saved.previousReference=saved.reference;saved.previousReferenceIdentity=saved.referenceIdentity;saved.previousAmount=value(0);saved.previousSmoothing=value(1);saved.previous=true;}editing=true;committed=false;}
juce::Result ReferenceMatchProcessor::setReferenceUnpublished(const float* const* pcm,size_t channels,size_t frames,double rate){if(published.load())return juce::Result::fail("Use a private staged candidate");auto p=analyze(pcm,channels,frames,rate);if(p.status!=Status::ready)return juce::Result::fail("Reference capture invalid or insufficient");std::lock_guard<std::mutex> lock(mutex);beginEditLocked();saved.reference=p;saved.referenceIdentity={"reference",0};return juce::Result::ok();}
juce::Result ReferenceMatchProcessor::setSourceUnpublished(const Profile& p){if(published.load())return juce::Result::fail("Use a private staged candidate");if(!fit(p,p,48000).valid)return juce::Result::fail("Invalid source profile");std::lock_guard<std::mutex> lock(mutex);beginEditLocked();saved.source=p;return juce::Result::ok();}
juce::Result ReferenceMatchProcessor::matchUnpublished(){if(published.load())return juce::Result::fail("Use a private staged candidate");std::lock_guard<std::mutex> lock(mutex);if(!saved.referenceIdentity.path.isEmpty()){const juce::File f(saved.referenceIdentity.path);juce::FileInputStream stream(f);if(!f.existsAsFile()||f.getSize()!=saved.referenceIdentity.byteSize||!stream.openedOk()||juce::SHA256(stream).toHexString()!=saved.referenceIdentity.sha256)return juce::Result::fail("Reference missing or changed; reload before rematching");}auto solution=fit(saved.source,saved.reference,actualRate.load(),value(0),value(1));if(!solution.valid)return juce::Result::fail("Two valid broadband profiles required");core.publish(solution);preparedTail=solution.tailSeconds;editing=false;saved.committed=true;committed=true;++saved.revision;appliedAmount=value(0);appliedSmoothing=value(1);return juce::Result::ok();}
juce::Result ReferenceMatchProcessor::revertUnpublished(){if(published.load())return juce::Result::fail("Use a private staged candidate");{std::lock_guard<std::mutex> lock(mutex);if(!saved.previous)return juce::Result::fail("No committed rollback profile");saved.source=saved.previousSource;saved.reference=saved.previousReference;saved.referenceIdentity=saved.previousReferenceIdentity;saved.previous=false;state.getParameter("matchAmount")->setValueNotifyingHost(saved.previousAmount);state.getParameter("smoothing")->setValueNotifyingHost(saved.previousSmoothing/3);}return matchUnpublished();}
juce::Result ReferenceMatchProcessor::captureSnapshot(Snapshot& out){std::unique_lock<std::mutex> lock(mutex,std::try_to_lock);if(!lock.owns_lock()||editing||working.load()||captureInProgress()||transitionFrames.load()>0)return juce::Result::fail("Analysis pending");for(auto& m:mailbox)if(m.state.load()!=0)return juce::Result::fail("Fit publication pending");if(saved.committed&&(value(0)!=appliedAmount.load()||value(1)!=appliedSmoothing.load()))return juce::Result::fail("Parameter refit pending");out=saved;out.parameters=state.copyState();if(saved.committed&&(value(0)!=appliedAmount.load()||value(1)!=appliedSmoothing.load()))return juce::Result::fail("Parameters changed during capture");return juce::Result::ok();}
juce::Result ReferenceMatchProcessor::restoreSnapshotUnpublished(const Snapshot& s,double rate){if(published.load())return juce::Result::fail("Live restore rejected");juce::ValueTree encoded;auto result=encodeSnapshot(s,encoded);if(result.failed())return result;Snapshot checked;result=decodeSnapshot(encoded,checked);if(result.failed())return result;std::lock_guard<std::mutex> lock(mutex);saved=checked;editing=false;core=reference_match::Processor{};state.replaceState(checked.parameters.createCopy());actualRate=rate;committed=saved.committed;if(saved.committed){auto solution=fit(saved.source,saved.reference,rate,value(0),value(1));if(!solution.valid)return juce::Result::fail("Rate/profile mismatch");core.publish(solution);preparedTail=solution.tailSeconds;appliedAmount=value(0);appliedSmoothing=value(1);}return juce::Result::ok();}
juce::String ReferenceMatchProcessor::diagnostic()const{return "Reference Match: measured 16-band fit; source capture 10s; reference analysis and monitor audition first 30s";}
const nativeResource::ExpectedLayout& ReferenceMatchProcessor::expectedNativeResourceLayout()const noexcept{static const nativeResource::ExpectedLayout layout{"REFERENCE_MATCH",{{"reference",0}}};return layout;}
bool ReferenceMatchProcessor::hasPendingNativeStateWork()const noexcept{if(editing.load()||working.load()||captureInProgress()||transitionFrames.load()>0)return true;for(const auto& m:mailbox)if(m.state.load()!=0)return true;return committed.load()&&(value(0)!=appliedAmount.load()||value(1)!=appliedSmoothing.load());}
juce::Result ReferenceMatchProcessor::tryCaptureNativeState(NativeProcessorSnapshot& out){Snapshot s;auto result=captureSnapshot(s);if(result.failed())return result;NativeProcessorSnapshot candidate;candidate.parameters=juce::ValueTree("PARAMETERS");for(auto child:s.parameters)candidate.parameters.addChild(child.createCopy(),-1,nullptr);candidate.native=nativeResource::State{"REFERENCE_MATCH",{s.referenceIdentity}};juce::ValueTree profile;result=encodeSnapshot(s,profile);if(result.failed())return result;candidate.matchProfile=profile;candidate.committedRevision=s.revision;out=std::move(candidate);return juce::Result::ok();}
juce::Result ReferenceMatchProcessor::validateNativeState(const NativeProcessorSnapshot& s)const{
    if(!s.matchProfile||!s.native||!s.parameters.hasType("PARAMETERS")||s.parameters.getNumProperties()!=0||s.parameters.getNumChildren()!=4)return juce::Result::fail("Missing Reference Match whole state");Snapshot decoded;auto result=decodeSnapshot(*s.matchProfile,decoded);if(result.failed())return result;
    for(const auto& spec:parameterSpecs()){auto a=s.parameters.getChildWithProperty("id",spec.id),b=decoded.parameters.getChildWithProperty("id",spec.id);if(!a.isValid()||double(a["value"])!=double(b["value"]))return juce::Result::fail("Match profile/scalar generation mismatch");}
    juce::ValueTree actual,expected;result=nativeResource::encodeManifest(*s.native,expectedNativeResourceLayout(),actual);if(result.failed())return result;result=nativeResource::encodeManifest({"REFERENCE_MATCH",{decoded.referenceIdentity}},expectedNativeResourceLayout(),expected);if(result.failed())return result;if(!actual.isEquivalentTo(expected))return juce::Result::fail("Match profile/reference identity mismatch");return juce::Result::ok();
}
juce::Result ReferenceMatchProcessor::restoreNativeStateOffThread(const NativeProcessorSnapshot& s,const NativeRestoreContext& context){auto result=validateNativeState(s);if(result.failed())return result;Snapshot decoded;result=decodeSnapshot(*s.matchProfile,decoded);if(result.failed())return result;
    // Committed EQ remains usable when reference is missing/changed. Audition and
    // rematching require revalidation; restoration never silently analyzes new bytes.
    if(!decoded.referenceIdentity.path.isEmpty()){const juce::File file(decoded.referenceIdentity.path);if(!file.existsAsFile()||file.getSize()!=decoded.referenceIdentity.byteSize)error="Reference missing or changed; committed EQ retained";else{juce::FileInputStream stream(file);if(!stream.openedOk()||juce::SHA256(stream).toHexString()!=decoded.referenceIdentity.sha256)error="Reference changed; committed EQ retained";}}
    return restoreSnapshotUnpublished(decoded,context.sampleRate);
}
namespace {
bool strictNumber(const juce::var& v,double& out){
    if(v.isInt()||v.isInt64()||v.isDouble()||v.isBool()){out=double(v);return std::isfinite(out);}
    if(!v.isString())return false;auto text=v.toString().toStdString();if(text.empty()||text.size()>64)return false;
    auto parsed=std::from_chars(text.data(),text.data()+text.size(),out,std::chars_format::general);return parsed.ec==std::errc{}&&parsed.ptr==text.data()+text.size()&&std::isfinite(out);
}
bool strictNumbers(const juce::ValueTree& t,int depth=0){
    if(depth>5||t.getNumChildren()>96||t.getNumProperties()>12)return false;
    for(int i=0;i<t.getNumProperties();++i){auto name=t.getPropertyName(i).toString();if(name=="id"||name=="ownerType"||name=="role"||name=="path"||name=="sha256")continue;double number=0;if(!strictNumber(t.getProperty(t.getPropertyName(i)),number))return false;if((name=="committed"||name=="previous")&&number!=0&&number!=1)return false;}
    for(int i=0;i<t.getNumChildren();++i)if(!strictNumbers(t.getChild(i),depth+1))return false;return true;
}
const nativeResource::ExpectedLayout referenceLayout{"REFERENCE_MATCH",{{"reference",0}}};
juce::ValueTree profileTree(const Profile& p,const char* name){juce::ValueTree t(name);t.setProperty("rate",p.sampleRate,nullptr);t.setProperty("frames",static_cast<juce::int64>(p.frames),nullptr);t.setProperty("meanSquare",p.meanSquare,nullptr);t.setProperty("startFrame",static_cast<juce::int64>(p.startFrame),nullptr);t.setProperty("generation",static_cast<juce::int64>(p.generation),nullptr);for(size_t i=0;i<bins;++i){juce::ValueTree b("BIN");b.setProperty("hz",p.hz[i],nullptr);b.setProperty("power",p.power[i],nullptr);t.addChild(b,-1,nullptr);}return t;}
bool readProfile(const juce::ValueTree& t,Profile& p){if(!t.hasProperty("rate")||!t.hasProperty("frames")||!t.hasProperty("meanSquare")||!t.hasProperty("startFrame")||!t.hasProperty("generation")||t.getNumProperties()!=5||t.getNumChildren()!=int(bins))return false;p.sampleRate=double(t["rate"]);double frames=double(t["frames"]);if(!std::isfinite(frames)||frames<0||frames>5760000||std::floor(frames)!=frames)return false;p.frames=static_cast<uint64_t>(frames);double start=double(t["startFrame"]),generation=double(t["generation"]);if(start<0||generation<0||start>9007199254740991.0||generation>9007199254740991.0||std::floor(start)!=start||std::floor(generation)!=generation)return false;p.startFrame=static_cast<uint64_t>(start);p.generation=static_cast<uint64_t>(generation);p.meanSquare=double(t["meanSquare"]);p.status=Status::ready;for(size_t i=0;i<bins;++i){auto b=t.getChild(int(i));if(!b.hasProperty("hz")||!b.hasProperty("power")||!b.hasType("BIN")||b.getNumProperties()!=2||b.getNumChildren())return false;p.hz[i]=double(b["hz"]);p.power[i]=double(b["power"]);}return fit(p,p,48000).valid;}
}
juce::Result ReferenceMatchProcessor::encodeSnapshot(const Snapshot& s,juce::ValueTree& out){
    if(!s.parameters.hasType("REFERENCE_MATCH"))return juce::Result::fail("Wrong parameter type");
    juce::ValueTree t("MATCH_PROFILE");t.setProperty("version",2,nullptr);t.setProperty("committed",s.committed,nullptr);t.setProperty("previous",s.previous,nullptr);t.setProperty("previousAmount",s.previousAmount,nullptr);t.setProperty("previousSmoothing",s.previousSmoothing,nullptr);t.addChild(s.parameters.createCopy(),-1,nullptr);
    for(int i=0;i<2;++i){nativeResource::State resource{"REFERENCE_MATCH",{i?s.previousReferenceIdentity:s.referenceIdentity}};juce::ValueTree manifest;auto result=nativeResource::encodeManifest(resource,referenceLayout,manifest);if(result.failed())return result;juce::ValueTree wrapper(i?"PREVIOUS_IDENTITY":"REFERENCE_IDENTITY");wrapper.addChild(manifest,-1,nullptr);t.addChild(wrapper,-1,nullptr);}
    if(s.committed){if(!fit(s.source,s.reference,48000).valid)return juce::Result::fail("Invalid profile pair");t.addChild(profileTree(s.source,"SOURCE"),-1,nullptr);t.addChild(profileTree(s.reference,"REFERENCE"),-1,nullptr);}
    if(s.previous){if(!fit(s.previousSource,s.previousReference,48000).valid)return juce::Result::fail("Invalid rollback pair");t.addChild(profileTree(s.previousSource,"PREVIOUS_SOURCE"),-1,nullptr);t.addChild(profileTree(s.previousReference,"PREVIOUS_REFERENCE"),-1,nullptr);}out=t;return juce::Result::ok();
}
juce::Result ReferenceMatchProcessor::decodeSnapshot(const juce::ValueTree& t,Snapshot& out){
    Snapshot s;if(!t.hasProperty("version")||!t.hasProperty("committed")||!t.hasProperty("previous")||!t.hasProperty("previousAmount")||!t.hasProperty("previousSmoothing")||!t.hasType("MATCH_PROFILE")||double(t["version"])!=2||t.getNumProperties()!=5||!strictNumbers(t)||t.toXmlString().getNumBytesAsUTF8()>128*1024)return juce::Result::fail("Invalid MATCH_PROFILE schema");s.committed=bool(t["committed"]);s.previous=bool(t["previous"]);s.previousAmount=float(t["previousAmount"]);s.previousSmoothing=float(t["previousSmoothing"]);
    if(!std::isfinite(s.previousAmount)||s.previousAmount<0||s.previousAmount>1||!std::isfinite(s.previousSmoothing)||s.previousSmoothing<0||s.previousSmoothing>3||t.getNumChildren()!=3+(s.committed?2:0)+(s.previous?2:0))return juce::Result::fail("Invalid bounded profile");
    for(int i=0;i<2;++i){auto wrapper=t.getChildWithName(i?"PREVIOUS_IDENTITY":"REFERENCE_IDENTITY");if(wrapper.getNumProperties()!=0||wrapper.getNumChildren()!=1)return juce::Result::fail("Invalid reference identity");nativeResource::State resource;auto result=nativeResource::decodeManifest(wrapper.getChild(0),referenceLayout,resource);if(result.failed())return result;(i?s.previousReferenceIdentity:s.referenceIdentity)=resource.resources[0];}
    s.parameters=t.getChild(0).createCopy();if(!s.parameters.hasType("REFERENCE_MATCH")||s.parameters.getNumProperties()!=0||s.parameters.getNumChildren()!=4)return juce::Result::fail("Invalid scalar layout");
    const char* ids[]={"matchAmount","smoothing","output","bypass"};const double lo[]={0,0,-24,0},hi[]={1,3,24,1};
    for(int i=0;i<4;++i){auto p=s.parameters.getChildWithProperty("id",ids[i]);double v=double(p["value"]);if(!p.hasProperty("id")||!p.hasProperty("value")||!p.hasType("PARAM")||p.getNumChildren()!=0||p.getNumProperties()!=2||p["id"].toString()!=ids[i]||!std::isfinite(v)||v<lo[i]||v>hi[i]||(i==3&&v!=0&&v!=1))return juce::Result::fail("Invalid scalar value");}
    if(s.committed&&(!readProfile(t.getChildWithName("SOURCE"),s.source)||!readProfile(t.getChildWithName("REFERENCE"),s.reference)))return juce::Result::fail("Invalid current profiles");
    if(s.previous&&(!readProfile(t.getChildWithName("PREVIOUS_SOURCE"),s.previousSource)||!readProfile(t.getChildWithName("PREVIOUS_REFERENCE"),s.previousReference)))return juce::Result::fail("Invalid previous profiles");out=s;return juce::Result::ok();
}
std::unique_ptr<juce::XmlElement> ReferenceMatchProcessor::exactProfileXml(const juce::ValueTree& tree){auto xml=tree.createXml();if(!xml)return {};for(int i=0;i<tree.getNumProperties();++i){auto name=tree.getPropertyName(i);auto v=tree[name];if(v.isDouble()){char buffer[64];auto r=std::to_chars(buffer,buffer+64,double(v),std::chars_format::general,std::numeric_limits<double>::max_digits10);if(r.ec!=std::errc{})return {};xml->setAttribute(name.toString(),juce::String::fromUTF8(buffer,int(r.ptr-buffer)));}}for(int i=0;i<tree.getNumChildren();++i){auto child=exactProfileXml(tree.getChild(i));if(!child)return {};xml->replaceChildElement(xml->getChildElement(i),child.release());}return xml;}
void ReferenceMatchProcessor::getStateInformation(juce::MemoryBlock& out){out.reset();NativeProcessorSnapshot s;if(tryCaptureNativeState(s).failed())return;NativeProcessorState::encodeBinary(*this,s,out);}
void ReferenceMatchProcessor::setStateInformation(const void* data,int bytes){if(published.load()||bytes<=0||bytes>128*1024)return;NativeProcessorSnapshot s;if(NativeProcessorState::decodeBinary(*this,data,size_t(bytes),s).wasOk()){NativeRestoreContext context;context.sampleRate=actualRate.load();context.blockSize=512;restoreNativeStateOffThread(s,context);}}
}
