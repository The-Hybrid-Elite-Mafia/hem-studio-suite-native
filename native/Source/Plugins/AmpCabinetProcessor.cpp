#include "AmpCabinetProcessor.h"
#include "AmpCabinetEditor.h"
#include "../DSP/ReverbIRResampler.h"
#include "../Systems/NativeProcessorState.h"
#include <complex>
#include <set>
#include <cstdlib>
#include <stdexcept>
namespace hem {
namespace {int modelIndex(float v,int max=2) noexcept {return std::isfinite(v)?int(std::clamp(v,0.f,float(max))):0;}}
AmpCabinetProcessor::AmpCabinetProcessor():ModuleProcessor("AMP_CABINET","Amp & Cabinet Lab",{
 {"ampModel","Original Amp Model",0,2,0,1,""},{"ampEnabled","Amp Enable",0,1,0,1,"",true},
 {"gain","Gain",0,36,0,.01f,"dB"},{"bass","Bass",-12,12,0,.01f,"dB"},{"mid","Mid",-12,12,0,.01f,"dB"},
 {"treble","Treble",-12,12,0,.01f,"dB"},{"master","Power Master",-24,12,0,.01f,"dB"},
 {"cabModel","Cabinet Model",0,3,0,1,""},{"cabEnabled","Cabinet Enable",0,1,0,1,"",true},
 {"micModel","Modeled Microphone",0,2,0,1,""},{"micPosition","Cone Center to Edge",0,1,0,.001f,""},
 {"micDistance","Modeled Distance Coloration",0,1,0,.001f,""},{"micAngle","Off Axis",0,1,0,.001f,""},
 {"micEnabled","Microphone Enable",0,1,0,1,"",true},{"output","Output",-60,12,0,.01f,"dB"},
 {"bypass","Bypass",0,1,0,1,"",true}}){}
bool AmpCabinetProcessor::prepareLocked(double rate){
 ready.store(false);actualRate.store(0);resetMeterTelemetry();
 if(!std::isfinite(rate)||rate<32000||rate>192000){error="Amp/Cabinet requires 32-192 kHz to retain 6 ms ceiling";return false;}
 std::unique_ptr<reverb::PreparedConvolution> prepared;std::array<float,128> display{};
 if(source.getNumSamples()>0){auto pcm=reverb::resampleIR(source,descriptor.sourceRate,rate);
  prepared=std::make_unique<reverb::PreparedConvolution>();prepared->prepare(pcm.getReadPointer(0),pcm.getReadPointer(pcm.getNumChannels()>1?1:0),size_t(pcm.getNumSamples()));
  for(size_t k=0;k<display.size();++k){const double hz=20*std::pow(1000.,double(k)/127);if(hz>=rate*.5){display[k]=0;continue;}
   double power=0;for(int ch=0;ch<pcm.getNumChannels();++ch){std::complex<double> sum{};const auto z=std::polar(1.,-2*pi*hz/rate);std::complex<double> phase{1,0};
    for(int n=0;n<pcm.getNumSamples();++n){sum+=double(pcm.getSample(ch,n))*phase;phase*=z;}power+=std::norm(sum);}display[k]=float(std::sqrt(power/pcm.getNumChannels()));}
 }
 core.prepare(rate);convolution=std::move(prepared);core.setCustomIR(convolution.get());response=display;actualRate.store(rate);setLatencySamples(core.latency());publishMeterPreparation();ready.store(true);error.clear();return true;
}
void AmpCabinetProcessor::prepareToPlay(double rate,int){std::lock_guard<std::mutex> lock(resourceMutex);pending.store(true);
 try{if(!prepareLocked(rate)){pending.store(false);throw std::invalid_argument(error.toStdString());}}catch(...){pending.store(false);throw;}pending.store(false);
}
void AmpCabinetProcessor::releaseResources(){ready.store(false);actualRate.store(0);resetMeterTelemetry();ModuleProcessor::releaseResources();}
AmpCabinetSettings AmpCabinetProcessor::settings()const noexcept{
 AmpCabinetSettings p;p.ampModel=modelIndex(value(0));p.ampEnabled=value(1)>.5f;p.gain=value(2);p.bass=value(3);p.mid=value(4);
 p.treble=value(5);p.master=value(6);p.cabModel=modelIndex(value(7),3);p.cabEnabled=value(8)>.5f;p.micModel=modelIndex(value(9));
 p.micPosition=value(10);p.micDistance=value(11);p.micAngle=value(12);p.micEnabled=value(13)>.5f;p.output=value(14);p.bypass=value(15)>.5f;return p;
}
void AmpCabinetProcessor::processBlock(juce::AudioBuffer<float>& b,juce::MidiBuffer&){
 published.store(true);juce::ScopedNoDenormals guard;
 if(!processingReady()||b.getNumChannels()!=2){b.clear();return;}if(b.getNumSamples()<=0)return;
 captureInput(b);core.process(b.getWritePointer(0),b.getWritePointer(1),b.getNumSamples(),settings());publishMeterBlock(b);
}
float AmpCabinetProcessor::meter()const noexcept{return std::max(core.outputPeak[0].load(std::memory_order_relaxed),core.outputPeak[1].load(std::memory_order_relaxed));}
const char* AmpCabinetProcessor::ampModelName(int n)noexcept{const char* names[]={"Original Clean","Original Edge","Original Lead"};return n>=0&&n<3?names[n]:"Unavailable";}
const char* AmpCabinetProcessor::cabinetModelName(int n)noexcept{const char* names[]={"Synthetic Open","Synthetic Closed","Synthetic Compact","Custom IR"};return n>=0&&n<4?names[n]:"Unavailable";}
const char* AmpCabinetProcessor::micModelName(int n)noexcept{const char* names[]={"Modeled Neutral","Modeled Presence","Modeled Dark"};return n>=0&&n<3?names[n]:"Unavailable";}
juce::AudioProcessorEditor* AmpCabinetProcessor::createEditor(){return new AmpCabinetEditor(*this);}
juce::String AmpCabinetProcessor::helpText()const{return "Original amp designs; synthetic cabinets and modeled microphone coloration. Custom IRs preserve authored gain, stereo and microphone capture; modeled mic adds further coloration. Distance is coloration, not propagation delay. Fixed latency: 32 samples plus 4 ms IR alignment. IR limit 0.5 seconds / 8 MiB; resource changes use private prepared replacement nodes.";}
bool AmpCabinetProcessor::validateTree(const juce::ValueTree& t,bool root)const{
 if(!t.hasType(root?juce::Identifier("AMP_CABINET"):juce::Identifier("PARAMETERS"))||t.getNumProperties()!=0||t.getNumChildren()!=int(parameterSpecs().size()))return false;
 std::set<juce::String> ids;
 for(const auto child:t){if(!child.hasType("PARAM")||child.getNumProperties()!=2||child.getNumChildren()!=0||!child.hasProperty("id")||!child.hasProperty("value"))return false;
 const auto parameterId=child["id"].toString();if(!ids.insert(parameterId).second)return false;
 const auto& definitions=parameterSpecs();const auto it=std::find_if(definitions.begin(),definitions.end(),[&](const auto& s){return parameterId==s.id;});if(it==definitions.end())return false;
 const auto text=child["value"].toString().toStdString();char* end=nullptr;const double x=std::strtod(text.c_str(),&end);
 if(end==text.c_str()||*end||!std::isfinite(x)||x<it->minimum||x>it->maximum)return false;
 if((it->toggle||parameterId.endsWith("Model"))&&x!=std::floor(x))return false;
 }return true;
}
bool AmpCabinetProcessor::validateStoredParameters(const juce::ValueTree& t)const{return validateTree(t,false);}
void AmpCabinetProcessor::setStateInformation(const void*,int){std::lock_guard<std::mutex> lock(resourceMutex);error="Use unpublished whole-state restore with preparation context";}
}
namespace hem {
const nativeResource::ExpectedLayout& AmpCabinetProcessor::expectedNativeResourceLayout()const noexcept{static const nativeResource::ExpectedLayout l{"AMP_CABINET",{{"cabinet-ir",0}}};return l;}
juce::Result AmpCabinetProcessor::decodeIR(const juce::File& file,juce::AudioBuffer<float>& pcm,nativeResource::ResourceDescriptor& d,const nativeResource::ResourceDescriptor* expected){
 const auto fail=[](const char* s){return juce::Result::fail(s);};const auto bytes=file.getSize();
 if(!file.existsAsFile()||bytes<=0||bytes>8*1024*1024)return fail("IR missing or exceeds 8 MiB");
 juce::MemoryBlock data;juce::FileInputStream stream(file);if(!stream.openedOk())return fail("Cannot open IR");data.setSize(size_t(bytes));
 if(stream.read(data.getData(),int(bytes))!=bytes||!stream.isExhausted())return fail("IR changed while reading");
 const auto hash=juce::SHA256(data).toHexString();if(expected&&(expected->sha256!=hash||expected->byteSize!=bytes))return fail("IR content changed");
 juce::AudioFormatManager formats;formats.registerBasicFormats();auto input=std::make_unique<juce::MemoryInputStream>(data,false);
 std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(std::move(input)));
 if(!reader||!std::isfinite(reader->sampleRate)||reader->sampleRate<8000||reader->sampleRate>192000||reader->numChannels<1||reader->numChannels>2||reader->lengthInSamples<=0||reader->lengthInSamples>reader->sampleRate*.5)return fail("IR requires mono/stereo, 8-192 kHz, at most 0.5 seconds");
 if(expected&&(expected->sourceRate!=reader->sampleRate||expected->channels!=int(reader->numChannels)||expected->frames!=reader->lengthInSamples))return fail("IR metadata changed");
 juce::AudioBuffer<float> candidate(int(reader->numChannels),int(reader->lengthInSamples));if(!reader->read(&candidate,0,candidate.getNumSamples(),0,true,true))return fail("IR decode failed");
 for(int c=0;c<candidate.getNumChannels();++c){double energy=0;for(int n=0;n<candidate.getNumSamples();++n){const auto v=candidate.getSample(c,n);if(!std::isfinite(v))return fail("IR contains nonfinite samples");energy+=std::abs(v);}if(energy>64)return fail("IR absolute coefficient sum exceeds 64; reduce IR gain explicitly");}
 d={"cabinet-ir",0,file.getFullPathName(),hash,bytes,reader->sampleRate,int(reader->numChannels),reader->lengthInSamples};pcm=std::move(candidate);return juce::Result::ok();
}
juce::Result AmpCabinetProcessor::loadIRUnpublished(const juce::File& file,double rate){std::lock_guard<std::mutex> lock(resourceMutex);
 if(published.load())return juce::Result::fail("IR load requires an unpublished replacement processor");
 pending.store(true);struct Clear{std::atomic<bool>& p;~Clear(){p.store(false);}}clear{pending};
 juce::AudioBuffer<float> next;nativeResource::ResourceDescriptor d;auto result=decodeIR(file,next,d,nullptr);if(result.failed()){error=result.getErrorMessage();return result;}
 // Mutation affects only the candidate. Host discards candidate on preparation failure.
 source=std::move(next);descriptor=d;try{if(!prepareLocked(rate))return juce::Result::fail(error);}catch(const std::exception& e){error=e.what();return juce::Result::fail(error);}
 auto* choice=state.getParameter("cabModel");choice->setValueNotifyingHost(choice->convertTo0to1(3));++revision;return juce::Result::ok();
}
juce::Result AmpCabinetProcessor::validateNativeState(const NativeProcessorSnapshot& s)const{
 if(s.matchProfile)return juce::Result::fail("Unexpected match profile in cabinet state");if(!s.native)return juce::Result::fail("Missing cabinet resource manifest");auto r=nativeResource::validateManifest(*s.native,expectedNativeResourceLayout());if(r.failed())return r;
 if(!validateStoredParameters(s.parameters))return juce::Result::fail("Invalid Amp/Cabinet scalar state");
 for(const auto c:s.parameters)if(c["id"].toString()=="cabModel"&&double(c["value"])==3&&s.native->resources[0].path.isEmpty())return juce::Result::fail("Custom cabinet selected without IR");
 return juce::Result::ok();
}
juce::Result AmpCabinetProcessor::tryCaptureNativeState(NativeProcessorSnapshot& out){
 if(pending.load())return juce::Result::fail("Cabinet resource work pending");std::unique_lock<std::mutex> lock(resourceMutex,std::try_to_lock);if(!lock.owns_lock()||pending.load())return juce::Result::fail("Cabinet resource work pending");
 NativeProcessorSnapshot s;s.native=nativeResource::State{"AMP_CABINET",{descriptor}};s.committedRevision=revision;
 for(const auto& p:parameterSpecs()){juce::ValueTree c("PARAM");c.setProperty("id",p.id,nullptr);c.setProperty("value",double(state.getRawParameterValue(p.id)->load()),nullptr);s.parameters.addChild(c,-1,nullptr);}
 auto r=validateNativeState(s);if(r.failed())return r;out=std::move(s);return juce::Result::ok();
}
juce::Result AmpCabinetProcessor::restoreNativeStateOffThread(const NativeProcessorSnapshot& s,const NativeRestoreContext& context){
 std::lock_guard<std::mutex> lock(resourceMutex);if(published.load())return juce::Result::fail("Restore requires unpublished Amp/Cabinet candidate");pending.store(true);struct Clear{std::atomic<bool>& p;~Clear(){p.store(false);}}clear{pending};
 auto r=validateNativeState(s);if(r.failed())return r;if(context.cancelled&&context.cancelled())return juce::Result::fail("Cabinet restore cancelled");
 const auto& d=s.native->resources[0];juce::AudioBuffer<float> next;nativeResource::ResourceDescriptor verified=d;
 if(d.path.isNotEmpty()){r=decodeIR(juce::File(d.path),next,verified,&d);if(r.failed())return r;}
 if(context.cancelled&&context.cancelled())return juce::Result::fail("Cabinet restore cancelled");source=std::move(next);descriptor=verified;
 try{if(!prepareLocked(context.sampleRate))return juce::Result::fail(error);}catch(const std::exception& e){error=e.what();return juce::Result::fail(error);}
 for(const auto c:s.parameters){auto* p=state.getParameter(c["id"].toString());p->setValueNotifyingHost(p->convertTo0to1(float(double(c["value"]))));}++revision;return juce::Result::ok();
}
void AmpCabinetProcessor::getStateInformation(juce::MemoryBlock& out){NativeProcessorSnapshot s;if(tryCaptureNativeState(s).wasOk()){juce::MemoryBlock next;if(NativeProcessorState::encodeBinary(*this,s,next).wasOk())out.swapWith(next);}}
nativeResource::ResourceDescriptor AmpCabinetProcessor::irDescriptor()const{std::lock_guard<std::mutex> lock(resourceMutex);return descriptor;}
juce::String AmpCabinetProcessor::lastResourceError()const{std::lock_guard<std::mutex> lock(resourceMutex);return error;}
std::array<float,128> AmpCabinetProcessor::irResponse()const{std::lock_guard<std::mutex> lock(resourceMutex);return response;}
}

