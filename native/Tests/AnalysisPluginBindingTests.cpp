#include "../Source/Plugins/AnalysisProcessors.h"
#include "../Source/InsertChain.h"
#include "../Source/SessionState.h"
#include <iostream>
#include <thread>
#include <cstring>
#include <cstdlib>
#include <new>
static thread_local bool checking=false;static std::atomic<unsigned>allocations{0};
void*operator new(std::size_t n){if(checking)++allocations;if(auto*p=std::malloc(n?n:1))return p;throw std::bad_alloc();}void*operator new[](std::size_t n){return ::operator new(n);}void operator delete(void*p)noexcept{std::free(p);}void operator delete[](void*p)noexcept{std::free(p);}void operator delete(void*p,std::size_t)noexcept{std::free(p);}void operator delete[](void*p,std::size_t)noexcept{std::free(p);}
static void require(bool b,const char*s){if(!b)throw std::runtime_error(s);}static void set(hem::ModuleProcessor&p,const char*id,float v){auto*x=p.state.getParameter(id);require(x,"parameter missing");x->setValueNotifyingHost(x->convertTo0to1(v));}
class GainFixture final:public hem::ModuleProcessor{public:GainFixture():ModuleProcessor("TEST_GAIN","Test gain",{}){}void prepareToPlay(double rate,int)override{actualRate=rate;setLatencySamples(0);}void processBlock(juce::AudioBuffer<float>&b,juce::MidiBuffer&)override{b.applyGain(2.f);}juce::AudioProcessorEditor*createEditor()override{return nullptr;}float meter()const noexcept override{return 0;}juce::String meterLabel()const override{return {};}};
static void checkSessionRoundTrip(hem::AnalysisProcessor& p){
 for(const auto&spec:p.parameterSpecs())set(p,spec.id,spec.maximum);
 const auto saved=hem::SessionState::captureParameters(p);
 require(saved.hasType("PARAMETERS"),"wrong session transport fixture");
 require(p.migrateParameterState(saved).hasType("PARAMETERS"),"session root not preserved");
 for(const auto&spec:p.parameterSpecs())set(p,spec.id,spec.minimum);
 hem::SessionState::restoreParameters(p,saved);
 require(saved.isEquivalentTo(hem::SessionState::captureParameters(p)),"session round-trip changed analysis parameters");
 juce::MemoryBlock pluginState;p.getStateInformation(pluginState);
 for(const auto&spec:p.parameterSpecs())set(p,spec.id,spec.minimum);
 p.setStateInformation(pluginState.getData(),int(pluginState.getSize()));
 require(saved.isEquivalentTo(hem::SessionState::captureParameters(p)),"APVTS state round-trip changed analysis parameters");
 for(int defect=0;defect<7;++defect){
  auto invalid=saved.createCopy();
  // A valid differing first parameter plus bad LAST parameter catches partial commits.
  invalid.getChild(0).setProperty("value",p.parameterSpecs()[0].minimum,nullptr);
  const int last=invalid.getNumChildren()-1;
  if(defect==0)invalid.getChild(last).setProperty("value",std::numeric_limits<double>::infinity(),nullptr);
  if(defect==1)invalid.getChild(last).setProperty("id",invalid.getChild(0)["id"],nullptr);
  if(defect==2)invalid.removeChild(last,nullptr);
  if(defect==3)invalid.getChild(last).setProperty("value",p.parameterSpecs().back().maximum+100.f,nullptr);
  if(defect==4)invalid.getChild(last).addChild(juce::ValueTree("NESTED"),-1,nullptr);
  if(defect==5)invalid.setProperty("unexpected",1,nullptr);
  if(defect==6)invalid.getChild(last).setProperty("unexpected",1,nullptr);
  bool rejected=false;try{hem::SessionState::restoreParameters(p,invalid);}catch(const std::exception&){rejected=true;}
  require(rejected&&saved.isEquivalentTo(hem::SessionState::captureParameters(p)),"malformed session tree partially committed");
 }
 // A well-formed generic transport is still forbidden as direct plugin state.
 auto generic=saved.createCopy();generic.getChild(0).setProperty("value",p.parameterSpecs()[0].minimum,nullptr);
 juce::MemoryBlock genericBytes;auto genericXml=generic.createXml();juce::AudioProcessor::copyXmlToBinary(*genericXml,genericBytes);
 const auto root=p.state.state.getType();p.setStateInformation(genericBytes.getData(),int(genericBytes.getSize()));
 require(p.state.state.hasType(root)&&saved.isEquivalentTo(hem::SessionState::captureParameters(p)),"generic transport replaced APVTS root");
 juce::ValueTree wrongRoot("UNRELATED");for(auto child:saved)wrongRoot.addChild(child.createCopy(),-1,nullptr);
 require(!p.migrateParameterState(wrongRoot).isValid(),"unrelated migration root accepted");
}
int main(){try{juce::ScopedJuceInitialiser_GUI gui;juce::MidiBuffer midi;
 {hem::LoudnessLabProcessor p;checkSessionRoundTrip(p);}
 {hem::SpectrumSpectrogramProcessor p;checkSessionRoundTrip(p);}
 {hem::DeltaMonitorProcessor p;checkSessionRoundTrip(p);}

 for(int type=0;type<2;++type){std::unique_ptr<hem::AnalysisProcessor>p=type?std::unique_ptr<hem::AnalysisProcessor>(std::make_unique<hem::SpectrumSpectrogramProcessor>()):std::unique_ptr<hem::AnalysisProcessor>(std::make_unique<hem::LoudnessLabProcessor>());for(double sr:{44100.,48000.,96000.})for(int n:{1,31,256,1025}){p->prepareToPlay(sr,256);juce::AudioBuffer<float>b(2,n),original(2,n);for(int c=0;c<2;++c)for(int i=0;i<n;++i)b.setSample(c,i,.3f*std::sin(float(i*.027+c)));original.makeCopyOf(b);checking=true;p->processBlock(b,midi);checking=false;for(int c=0;c<2;++c)require(std::memcmp(b.getReadPointer(c),original.getReadPointer(c),size_t(n)*sizeof(float))==0,"analysis audio changed");require(p->getLatencySamples()==0,"meter latency");}auto good=p->state.copyState();auto bad=good.createCopy();bad.getChild(0).setProperty("value",std::numeric_limits<double>::infinity(),nullptr);require(!p->migrateParameterState(bad).isValid(),"nonfinite state accepted");bad=good.createCopy();bad.addChild(bad.getChild(0).createCopy(),-1,nullptr);require(!p->migrateParameterState(bad).isValid(),"duplicate state accepted");p->releaseResources();require(!p->processingReady(),"release readiness");}
 auto delta=std::make_unique<hem::DeltaMonitorProcessor>();delta->prepareToPlay(48000,127);set(*delta,"soloMode",2);juce::AudioBuffer<float>dry(2,511),b(2,511);for(int c=0;c<2;++c)for(int i=0;i<511;++i)dry.setSample(c,i,.1f*std::sin(float(i*.1)));std::uint64_t timeline=0;for(int k=0;k<8;++k){b.makeCopyOf(dry);delta->setReferenceBlock({dry.getReadPointer(0),dry.getReadPointer(1),511,48000,timeline,true});checking=true;delta->processBlock(b,midi);checking=false;timeline+=511;}require(delta->deltaSnapshot().referenceValid,"reference never warmed");require(b.getMagnitude(0,511)<1.e-6,"delta identical streams not null");b.makeCopyOf(dry);delta->processBlock(b,midi);require(!delta->deltaSnapshot().referenceValid,"stale borrowed reference reused");
 // Auto Align is a one-shot worker measurement committed into APVTS on this
 // message thread, so subsequent audio/render behavior is deterministic.
 delta->prepareToPlay(48000,127);delta->requestAlignment();juce::AudioBuffer<float>alignDry(2,8192),alignProcessed(2,8192);std::uint32_t random=17;for(int i=0;i<8192;++i){random=1664525u*random+1013904223u;float v=(float(random>>8)/float(1u<<24)-.5f)*.2f;for(int c=0;c<2;++c)alignDry.setSample(c,i,v);}for(int c=0;c<2;++c)for(int i=0;i<8192;++i)alignProcessed.setSample(c,i,i>=17?alignDry.getSample(c,i-17):0);delta->setReferenceBlock({alignDry.getReadPointer(0),alignDry.getReadPointer(1),8192,48000,0,true});delta->processBlock(alignProcessed,midi);bool aligned=false;for(int i=0;i<500&&!aligned;++i){aligned=delta->applyPendingAlignment();std::this_thread::sleep_for(std::chrono::milliseconds(4));}require(aligned,"Auto Align result never committed");require(std::abs(delta->state.getRawParameterValue("delayTrim")->load()-17.f/48)<.011f,"Auto Align wrong saved trim");
 hem::InsertChain chain;chain.prepare(48000,127);chain.installInitial(0,chain.stageNode(std::make_unique<GainFixture>(),48000,127,chain.slotLatency()),false);auto d=std::make_unique<hem::DeltaMonitorProcessor>();set(*d,"soloMode",2);auto*observed=d.get();chain.installInitial(1,chain.stageNode(std::move(d),48000,127,chain.slotLatency()),false);std::array<float,511>l{},r{};for(int k=0;k<40;++k){l.fill(.1f);r.fill(.1f);checking=true;chain.process(l.data(),r.data(),int(l.size()));checking=false;}require(observed->deltaSnapshot().referenceValid,"real chain reference unavailable");for(float v:l)require(std::abs(v-.1f)<1.e-5,"reference incorrectly equals processed or latency misaligned");require(chain.latencySamples()==8*288,"host slot budget changed");require(allocations==0,"audio callback allocated");
 std::cout<<"PASS JUCE analysis bindings: exact passthrough across rates/blocks, strict APVTS/session round-trips and transactional rejection, lifecycle, real InsertChain reference difference, stale-reference rejection, fixed host latency, zero callback allocations\n";return 0;
 }catch(const std::exception&e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
