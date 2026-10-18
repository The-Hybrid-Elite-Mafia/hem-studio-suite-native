#include "DrumReplacerProcessor.h"
#include "DrumReplacerEditor.h"
#include "../Systems/NativeProcessorState.h"
#include <set>
#include <charconv>
namespace hem {
namespace {
juce::Result fail(const juce::String& s) { return juce::Result::fail(s); }
constexpr juce::int64 fileLimit=32*1024*1024;
bool validRate(double r){ return std::isfinite(r)&&r>=8000&&r<=192000; }
struct Pending { std::atomic<bool>& p; explicit Pending(std::atomic<bool>& v):p(v){p.store(true);} ~Pending(){p.store(false);} };
}
std::vector<ParameterSpec> DrumReplacerProcessor::specifications(){return {
 {"sensitivity","Sensitivity",0,1,.5f,.001f,""},{"retrigger","Retrigger",1,500,40,.1f,"ms"},
 {"velocity","Velocity Match",0,1,1,.001f,""},{"blend","Blend",0,1,1,.001f,""},{"output","Output",-24,12,0,.01f,"dB"},
 {"level1","Layer 1 Level",-60,12,0,.01f,"dB"},{"enable1","Layer 1 Enable",0,1,1,1,"",true},{"solo1","Layer 1 Solo",0,1,0,1,"",true},
 {"level2","Layer 2 Level",-60,12,0,.01f,"dB"},{"enable2","Layer 2 Enable",0,1,1,1,"",true},{"solo2","Layer 2 Solo",0,1,0,1,"",true},
 {"level3","Layer 3 Level",-60,12,0,.01f,"dB"},{"enable3","Layer 3 Enable",0,1,1,1,"",true},{"solo3","Layer 3 Solo",0,1,0,1,"",true},
 {"level4","Layer 4 Level",-60,12,0,.01f,"dB"},{"enable4","Layer 4 Enable",0,1,1,1,"",true},{"solo4","Layer 4 Solo",0,1,0,1,"",true}};}
DrumReplacerProcessor::DrumReplacerProcessor():ModuleProcessor("DRUM_REPLACER","Drum Replacer",specifications()){
 for(int i=0;i<4;++i){samples[i].descriptor.role="sample";samples[i].descriptor.index=i;}
}
juce::AudioProcessorEditor* DrumReplacerProcessor::createEditor(){return new DrumReplacerEditor(*this);}
const nativeResource::ExpectedLayout& DrumReplacerProcessor::expectedNativeResourceLayout()const noexcept{
 static const nativeResource::ExpectedLayout layout{"DRUM_REPLACER",{{"sample",0},{"sample",1},{"sample",2},{"sample",3}}};return layout;
}
juce::Result DrumReplacerProcessor::decode(const juce::File& file,int index,Sample& out,const nativeResource::ResourceDescriptor* expected){
 const auto bytes=file.getSize();if(!file.existsAsFile()||bytes<=0||bytes>fileLimit)return fail("Sample file missing or exceeds 32 MiB");
 // Hash and decode the SAME bounded immutable byte buffer, preventing a file-change race.
 juce::MemoryBlock data;{juce::FileInputStream stream(file);if(!stream.openedOk())return fail("Cannot open sample");data.setSize(size_t(bytes));
 if(stream.read(data.getData(),int(bytes))!=bytes||!stream.isExhausted())return fail("Sample changed during read");}
 const auto hash=juce::SHA256(data).toHexString();
 if(expected&&(expected->byteSize!=bytes||expected->sha256!=hash))return fail("Sample identity changed");
 juce::AudioFormatManager formats;formats.registerBasicFormats();
 auto input=std::make_unique<juce::MemoryInputStream>(data,false);
 std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(std::move(input)));
 if(!reader||!validRate(reader->sampleRate)||reader->numChannels<1||reader->numChannels>2||reader->lengthInSamples<=0||
 reader->lengthInSamples>juce::int64(reader->sampleRate*10)||reader->lengthInSamples*reader->numChannels*4>fileLimit)
 return fail("Sample must be finite mono/stereo audio, 8-192 kHz, at most 10 seconds and 32 MiB decoded");
 if(expected&&(expected->sourceRate!=reader->sampleRate||expected->channels!=int(reader->numChannels)||expected->frames!=reader->lengthInSamples))return fail("Sample metadata changed");
 Sample candidate;candidate.source.setSize(int(reader->numChannels),int(reader->lengthInSamples));
 if(!reader->read(&candidate.source,0,candidate.source.getNumSamples(),0,true,true))return fail("Sample decode failed");
 for(int c=0;c<candidate.source.getNumChannels();++c)for(int n=0;n<candidate.source.getNumSamples();++n)
 if(!std::isfinite(candidate.source.getSample(c,n)))return fail("Sample contains nonfinite PCM");
 candidate.descriptor={"sample",index,file.getFullPathName(),hash,bytes,reader->sampleRate,int(reader->numChannels),reader->lengthInSamples};
 out=std::move(candidate);return juce::Result::ok();
}
juce::Result DrumReplacerProcessor::resample(Sample& sample,double rate){
 if(!validRate(rate))return fail("Unsupported drum preparation rate");
 if(sample.source.getNumSamples()==0){sample.prepared.setSize(0,0);return juce::Result::ok();}
 const auto sourceRate=sample.descriptor.sourceRate;
 const int frames=int(std::ceil(sample.source.getNumSamples()*rate/sourceRate));
 if(frames<=0||frames>int(rate*10)||juce::int64(frames)*sample.source.getNumChannels()*4>fileLimit)return fail("Resampled sample exceeds bounds");
 sample.prepared.setSize(sample.source.getNumChannels(),frames);
 if(sourceRate==rate){sample.prepared.makeCopyOf(sample.source);return juce::Result::ok();}
 // PCM windowed-sinc interpolation; anti-alias cutoff follows destination Nyquist.
 // Unit DC gain, no IR coefficient/sample-rate scaling. Preserve position zero.
 const double cutoff=std::min(1.,rate/sourceRate)*.94;const int radius=int(std::ceil(48/cutoff));constexpr double sincPi=3.14159265358979323846;
 for(int n=0;n<frames;++n){const double position=n*sourceRate/rate;const int base=int(std::floor(position));
 double sum=0;std::array<double,2> accum{};
 for(int k=base-radius+1;k<=base+radius;++k){const double x=position-k;if(std::abs(x)>=radius)continue;
 const double sinc=std::abs(x)<1e-12?cutoff:std::sin(sincPi*cutoff*x)/(sincPi*x);
 const double w=sinc*(.5+.5*std::cos(sincPi*x/radius));sum+=w;
 if(k>=0&&k<sample.source.getNumSamples())for(int c=0;c<sample.source.getNumChannels();++c)accum[c]+=sample.source.getSample(c,k)*w;}
 for(int c=0;c<sample.source.getNumChannels();++c){const float y=float(accum[c]/sum);if(!std::isfinite(y))return fail("Resampled PCM overflow");sample.prepared.setSample(c,n,y);}}
 return juce::Result::ok();
}
bool DrumReplacerProcessor::prepareLocked(double rate){
 ready.store(false);drum::Bank bank{};
 for(int i=0;i<4;++i){auto r=resample(samples[i],rate);if(r.failed()){error=r.getErrorMessage();return false;}
 auto& b=samples[i].prepared;if(b.getNumSamples())bank[i]={b.getReadPointer(0),b.getNumChannels()>1?b.getReadPointer(1):nullptr,b.getNumSamples(),rate};}
 if(!core.prepare(rate,bank)){error="Drum core preparation failed";return false;}
 preparedRate=rate;outputPeak.store(0);detectionPeak.store(0);thresholdDb.store(drum::Core::thresholdDb(value(0)));hits.store(0);actualRate.store(rate);setLatencySamples(core.latencySamples());resetMeterTelemetry();publishMeterPreparation();ready.store(true);error.clear();return true;
}
void DrumReplacerProcessor::prepareToPlay(double rate,int){std::lock_guard<std::mutex> lock(resourceMutex);Pending busy(pending);prepareLocked(rate);}
void DrumReplacerProcessor::processBlock(juce::AudioBuffer<float>& b,juce::MidiBuffer&){
 juce::ScopedNoDenormals noDenormals;published.store(true);if(!ready.load()||b.getNumChannels()!=2){b.clear();return;}
 captureInput(b);drum::Parameters p;p.sensitivity=value(0);p.retriggerMs=value(1);p.velocityMatch=value(2);p.blend=value(3);p.outputDb=value(4);
 for(int i=0;i<4;++i){p.levelDb[i]=value(5+3*i);p.enabled[i]=value(6+3*i)>.5f;p.solo[i]=value(7+3*i)>.5f;}
 core.process(b.getWritePointer(0),b.getWritePointer(1),b.getNumSamples(),p);
 float peak=0;for(int c=0;c<2;++c)for(int n=0;n<b.getNumSamples();++n)peak=std::max(peak,std::abs(b.getSample(c,n)));
 outputPeak.store(peak);const auto& t=core.telemetryOnAudioThread();hits.store(t.acceptedHits);detectionPeak.store(t.detectorPeak);thresholdDb.store(t.thresholdDb);publishMeterBlock(b);
}
nativeResource::State DrumReplacerProcessor::manifestLocked()const{nativeResource::State result;result.ownerType="DRUM_REPLACER";for(const auto& s:samples)result.resources.push_back(s.descriptor);return result;}
juce::Result DrumReplacerProcessor::validateNativeState(const NativeProcessorSnapshot& s)const{
 if(!s.native)return fail("Missing drum resource manifest");auto r=nativeResource::validateManifest(*s.native,expectedNativeResourceLayout());if(r.failed())return r;
 if(!s.parameters.hasType("PARAMETERS")||s.parameters.getNumProperties()!=0||s.parameters.getNumChildren()!=int(parameterSpecs().size()))return fail("Invalid drum parameter schema");
 std::set<juce::String> ids;for(auto child:s.parameters){const auto parameterId=child["id"].toString();
 if(!child.hasType("PARAM")||child.getNumChildren()!=0||child.getNumProperties()!=2||!child.hasProperty("value")||!ids.insert(parameterId).second)return fail("Invalid drum parameter");
 auto it=std::find_if(parameterSpecs().begin(),parameterSpecs().end(),[&](const auto& p){return parameterId==p.id;});
 const auto v=child["value"];if(it==parameterSpecs().end())return fail("Unknown drum parameter");
 double x=0; if(v.isString()){const auto text=v.toString().toStdString();const auto parsed=std::from_chars(text.data(),text.data()+text.size(),x);if(parsed.ec!=std::errc{}||parsed.ptr!=text.data()+text.size())return fail("Invalid numeric drum parameter");}else if(v.isInt()||v.isInt64()||v.isDouble())x=double(v);else return fail("Nonnumeric drum parameter");if(!std::isfinite(x)||x<it->minimum||x>it->maximum||(it->toggle&&x!=0&&x!=1))return fail("Drum parameter out of range");}
 return juce::Result::ok();
}
juce::Result DrumReplacerProcessor::tryCaptureNativeState(NativeProcessorSnapshot& out){
 if(pending.load())return fail("Drum resource work pending");std::unique_lock<std::mutex> lock(resourceMutex,std::try_to_lock);if(!lock.owns_lock()||pending.load())return fail("Drum resource work pending");
 NativeProcessorSnapshot result;result.native=manifestLocked();result.committedRevision=revision;
 // Host must serialize control edits with state capture (as for all APVTS engines).
 for(const auto& p:parameterSpecs()){juce::ValueTree v("PARAM");v.setProperty("id",p.id,nullptr);v.setProperty("value",double(state.getRawParameterValue(p.id)->load()),nullptr);result.parameters.addChild(v,-1,nullptr);}
 auto r=validateNativeState(result);if(r.failed())return r;out=std::move(result);return juce::Result::ok();
}
juce::Result DrumReplacerProcessor::restoreNativeStateOffThread(const NativeProcessorSnapshot& s,const NativeRestoreContext& context){
 std::lock_guard<std::mutex> lock(resourceMutex);if(published.load())return fail("Restore requires an unpublished drum candidate");Pending busy(pending);
 auto r=validateNativeState(s);if(r.failed())return r;if(!validRate(context.sampleRate))return fail("Unsupported drum restore rate");
 std::array<Sample,4> candidate;for(const auto& d:s.native->resources){if(context.cancelled&&context.cancelled())return fail("Drum restore cancelled");
 auto& sample=candidate[size_t(d.index)];sample.descriptor=d;if(d.path.isNotEmpty()){r=decode(juce::File(d.path),d.index,sample,&d);if(r.failed())return r;}}
 if(context.cancelled&&context.cancelled())return fail("Drum restore cancelled");samples=std::move(candidate);if(!prepareLocked(context.sampleRate))return fail(error);
 for(auto child:s.parameters){auto* p=state.getParameter(child["id"].toString());p->setValueNotifyingHost(p->convertTo0to1(float(double(child["value"]))));}
 ++revision;return juce::Result::ok();
}
juce::Result DrumReplacerProcessor::loadSampleUnpublished(int index,const juce::File& file,double rate){
 std::lock_guard<std::mutex> lock(resourceMutex);if(published.load())return fail("Load requires a private replacement drum node");if(index<0||index>=4||!validRate(rate))return fail("Invalid sample slot/rate");Pending busy(pending);
 Sample candidate;auto r=decode(file,index,candidate,nullptr);if(r.failed())return r;r=resample(candidate,rate);if(r.failed())return r;
 samples[size_t(index)]=std::move(candidate);if(!prepareLocked(rate))return fail(error);++revision;return juce::Result::ok();
}
juce::Result DrumReplacerProcessor::clearSampleUnpublished(int index,double rate){
 std::lock_guard<std::mutex> lock(resourceMutex);if(published.load())return fail("Clear requires a private replacement drum node");if(index<0||index>=4||!validRate(rate))return fail("Invalid sample slot/rate");Pending busy(pending);
 samples[size_t(index)]=Sample{};samples[size_t(index)].descriptor.role="sample";samples[size_t(index)].descriptor.index=index;
 if(!prepareLocked(rate))return fail(error);++revision;return juce::Result::ok();
}
std::array<nativeResource::ResourceDescriptor,4> DrumReplacerProcessor::sampleDescriptors()const{std::lock_guard<std::mutex> lock(resourceMutex);std::array<nativeResource::ResourceDescriptor,4> result;for(size_t i=0;i<4;++i)result[i]=samples[i].descriptor;return result;}
juce::String DrumReplacerProcessor::lastResourceError()const{std::lock_guard<std::mutex> lock(resourceMutex);return error;}
void DrumReplacerProcessor::getStateInformation(juce::MemoryBlock& out){out.reset();NativeProcessorSnapshot s;if(tryCaptureNativeState(s).wasOk()){juce::MemoryBlock result;if(NativeProcessorState::encodeBinary(*this,s,result).wasOk())out.swapWith(result);}}
void DrumReplacerProcessor::setStateInformation(const void*,int){std::lock_guard<std::mutex> lock(resourceMutex);error="Use unpublished whole-state restore with an explicit preparation context";}
}





