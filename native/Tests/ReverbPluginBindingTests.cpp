#include "../Source/Plugins/ReverbEditors.h"
#include "../Source/InsertChain.h"
#include "../Source/SessionState.h"
#include <iostream>
#include <thread>
#include <cstring>
#include <new>
#include <cstdlib>
static thread_local bool measuring=false;static thread_local unsigned allocs=0,frees=0;
void*operator new(std::size_t n){if(measuring)++allocs;if(auto*p=std::malloc(n?n:1))return p;throw std::bad_alloc();}void*operator new[](std::size_t n){return ::operator new(n);}void operator delete(void*p)noexcept{if(measuring&&p)++frees;std::free(p);}void operator delete[](void*p)noexcept{::operator delete(p);}void operator delete(void*p,std::size_t)noexcept{::operator delete(p);}void operator delete[](void*p,std::size_t)noexcept{::operator delete(p);}
static unsigned assertions=0;static void check(bool condition,const juce::String&m){++assertions;if(!condition)throw std::runtime_error(m.toStdString());}
static void set(hem::ModuleProcessor&p,const char*id,float value){auto*parameter=p.state.getParameter(id);check(parameter!=nullptr,"missing parameter");parameter->setValueNotifyingHost(parameter->convertTo0to1(value));}
static juce::Component*find(juce::Component&c,const juce::String&id){if(c.getComponentID()==id)return &c;for(int i=0;i<c.getNumChildComponents();++i)if(auto*p=find(*c.getChildComponent(i),id))return p;return nullptr;}
static void writeIR(const juce::File&f,float amplitude=.5f){f.deleteFile();juce::WavAudioFormat format;auto stream=f.createOutputStream();check(stream!=nullptr,"IR fixture file open");auto writer=std::unique_ptr<juce::AudioFormatWriter>(format.createWriterFor(stream.get(),48000,2,24,{},0));check(writer!=nullptr,"IR fixture encoder");stream.release();juce::AudioBuffer<float>b(2,4800);b.clear();b.setSample(0,0,amplitude);b.setSample(1,0,amplitude*.5f);b.setSample(0,1000,-.1f);b.setSample(1,1200,.1f);check(writer->writeFromAudioSampleBuffer(b,0,b.getNumSamples()),"IR fixture encoding");}
static void runBlock(hem::ModuleProcessor&p,juce::AudioBuffer<float>&b,juce::MidiBuffer&m){measuring=true;p.processBlock(b,m);measuring=false;}
static std::unique_ptr<hem::ModuleProcessor> makeReverb(int number) {
    if (number == 19) return std::make_unique<hem::ConvolutionReverbProcessor>();
    return std::make_unique<hem::AlgorithmicReverbProcessor>();
}
static void checkCanonicalValues(hem::ModuleProcessor& p, bool defaults, const juce::String& phase) {
    const auto tree = p.state.copyState();
    for (const auto& spec : p.parameterSpecs()) {
        const auto context = phase + ": " + spec.id;
        const auto raw = p.state.getRawParameterValue(spec.id)->load();
        if (defaults) check(std::abs(raw - spec.initial) < 1.e-4f, context + " startup default mismatch");
        auto* parameter = p.state.getParameter(spec.id);
        check(parameter != nullptr, context + " missing ranged parameter");
        if (auto* value = dynamic_cast<juce::AudioParameterFloat*>(parameter))
            check(value->get() == raw, context + " AudioParameterFloat/raw mismatch");
        if (auto* value = dynamic_cast<juce::AudioParameterBool*>(parameter))
            check(float(value->get()) == raw, context + " AudioParameterBool/raw mismatch");
        bool found = false;
        for (const auto child : tree) if (child["id"].toString() == spec.id) {
            check(double(child["value"]) == double(raw), context + " cached APVTS tree/raw mismatch");
            found = true;
        }
        check(found, context + " missing cached tree child");
    }
}
static void testCanonicalDefaults() {
    for (int number : {19, 20}) {
        auto p = makeReverb(number);
        checkCanonicalValues(*p, true, "constructor");
        if (number == 20) check(p->state.getRawParameterValue("damping")->load() == 8000.f,
                                "damping must be exactly 8000 Hz at construction");
        const auto initial = hem::SessionState::captureParameters(*p);
        p->prepareToPlay(48000, 128);
        checkCanonicalValues(*p, true, "prepare");
        check(hem::SessionState::captureParameters(*p).isEquivalentTo(initial), "prepare changed default state");
        auto editor = std::unique_ptr<juce::AudioProcessorEditor>(p->createEditor());
        checkCanonicalValues(*p, true, "editor");
        check(hem::SessionState::captureParameters(*p).isEquivalentTo(initial), "editor changed exact default state");
        for (int repeat = 0; repeat < 8; ++repeat) {
            for (const auto& spec : p->parameterSpecs()) {
                const auto before = p->state.getRawParameterValue(spec.id)->load();
                auto* parameter = p->state.getParameter(spec.id);
                parameter->setValueNotifyingHost(parameter->convertTo0to1(before));
                check(p->state.getRawParameterValue(spec.id)->load() == before,
                      juce::String("repeated normalized default changed raw value: ") + spec.id);
            }
            checkCanonicalValues(*p, true, "repeated default");
            check(hem::SessionState::captureParameters(*p).isEquivalentTo(initial), "repeated default changed exact cached state");
            auto restored = makeReverb(number);
            hem::SessionState::restoreParameters(*restored, initial);
            checkCanonicalValues(*restored, true, "default SessionState recall");
            check(hem::SessionState::captureParameters(*restored).isEquivalentTo(initial),
                  "default SessionState round trip changed exact values");
        }
        editor.reset();
        // Exercise min, legal mid-grid, max and semantic default via the actual
        // parameter range. No range, snapping policy or assertion is relaxed.
        for (int selection = 0; selection < 4; ++selection) {
            for (const auto& spec : p->parameterSpecs()) {
                auto* parameter = p->state.getParameter(spec.id);
                const auto requested = selection == 0 ? spec.minimum : selection == 1 ?
                    parameter->getNormalisableRange().snapToLegalValue((spec.minimum + spec.maximum) * .5f) :
                    selection == 2 ? spec.maximum : spec.initial;
                parameter->setValueNotifyingHost(parameter->convertTo0to1(requested));
            }
            checkCanonicalValues(*p, selection == 3, "representative state");
            const auto expected = hem::SessionState::captureParameters(*p);
            for (int repeat = 0; repeat < 4; ++repeat) {
                auto restored = makeReverb(number);
                hem::SessionState::restoreParameters(*restored, expected);
                checkCanonicalValues(*restored, selection == 3, "representative SessionState recall");
                check(hem::SessionState::captureParameters(*restored).isEquivalentTo(expected),
                      "representative SessionState round trip changed exact values");
            }
        }
    }
    std::cout << "PASS strict constructor/prepare/editor defaults, exact APVTS raw/tree consistency, repeated normalized recall and SessionState round trips\n";
}

static void testLifecycle(){juce::MidiBuffer midi;for(int type=0;type<2;++type){std::unique_ptr<hem::ModuleProcessor>p=type?std::unique_ptr<hem::ModuleProcessor>(std::make_unique<hem::AlgorithmicReverbProcessor>()):std::unique_ptr<hem::ModuleProcessor>(std::make_unique<hem::ConvolutionReverbProcessor>());
 for(double rate:{8000.,44100.,48000.,96000.,192000.})for(int n:{1,31,256,1025}){p->prepareToPlay(rate,128);check(p->processingReady(),"prepare readiness");juce::AudioBuffer<float>b(2,n),original(2,n);for(int c=0;c<2;++c)for(int i=0;i<n;++i)b.setSample(c,i,.13f*std::sin(float(i*.02+c)));original.makeCopyOf(b);runBlock(*p,b,midi);const int latency=type?0:int(std::ceil(rate*.004));for(int c=0;c<2;++c)for(int i=0;i<n;++i){const float expected=i<latency?0:original.getSample(c,i-latency);check(b.getSample(c,i)==expected,"neutral JUCE processor changed aligned dry samples");}check(p->getLatencySamples()==latency&&latency<=int(std::ceil(rate*.006)),"reported engine latency exceeds/misstates budget");hem::PluginTelemetry::Levels levels;check(p->pluginTelemetry()!=nullptr,"shared ModuleProcessor telemetry hook is missing");check(p->pluginTelemetry()->read(levels)&&levels.valid,"I/O telemetry missing");check(levels.peak[1]>0&&(n<=latency?levels.peak[3]==0:levels.peak[3]>0),"input/output telemetry not both connected");p->releaseResources();check(!p->processingReady(),"release readiness");}
 p->prepareToPlay(48000,128);auto good=p->state.copyState();if(!p->migrateParameterState(good).isValid())std::cerr<<good.toXmlString()<<"\n";check(p->migrateParameterState(good).isValid(),"good state rejected");auto bad=good.createCopy();bad.getChild(0).setProperty("value",std::numeric_limits<double>::infinity(),nullptr);check(!p->migrateParameterState(bad).isValid(),"nonfinite state accepted");bad=good.createCopy();bad.addChild(bad.getChild(0).createCopy(),-1,nullptr);check(!p->migrateParameterState(bad).isValid(),"duplicate state accepted");bad=good.createCopy();bad.getChild(0).setProperty("value","12oops",nullptr);check(!p->migrateParameterState(bad).isValid(),"malformed numeric state accepted");
 bad=good.createCopy();bad.setProperty("unexpectedRoot",123,nullptr);check(!p->migrateParameterState(bad).isValid(),"unknown root field accepted");bad=good.createCopy();bad.getChild(0).setProperty("unexpectedParam",123,nullptr);check(!p->migrateParameterState(bad).isValid(),"unknown PARAM field accepted");
 set(*p,"mix",37.5f);const auto unchanged=p->state.copyState();for(bool extraRoot:{true,false}){auto invalid=unchanged.createCopy();for(auto child:invalid)if(child["id"].toString()=="mix")child.setProperty("value",90,nullptr);if(extraRoot)invalid.setProperty("unexpectedRoot",123,nullptr);else invalid.getChild(0).setProperty("unexpectedParam",123,nullptr);juce::MemoryBlock binary;juce::AudioProcessor::copyXmlToBinary(*invalid.createXml(),binary);p->setStateInformation(binary.getData(),int(binary.getSize()));check(p->state.copyState().isEquivalentTo(unchanged),"rejected binary state partially changed parameters");}set(*p,"mix",0);

 juce::MemoryBlock state;p->getStateInformation(state);set(*p,"mix",80);p->setStateInformation(state.getData(),int(state.getSize()));check(p->state.getRawParameterValue("mix")->load()==0,"parameter roundtrip failed");}
 std::cout<<"PASS JUCE lifecycle, 40 rate/block combinations, exact neutral samples, telemetry, strict state and roundtrip\n";
}
namespace hem {struct ConvolutionReverbTestAccess {
 static void stop(ConvolutionReverbProcessor&p){p.signalThreadShouldExit();p.notify();p.stopThread(-1);}
 static std::pair<juce::File,std::uint64_t> extract(ConvolutionReverbProcessor&p){const std::lock_guard<std::mutex>lock(p.resourceMutex);p.filePending=false;return {p.requestedFile,p.requestGeneration};}
 static void dispatch(ConvolutionReverbProcessor&p,const std::pair<juce::File,std::uint64_t>&request){p.loadQueuedFile(request.first,request.second);}
};}
static void testStaleRequests(const juce::File&folder){const auto a=folder.getChildFile("queued-a.wav"),b=folder.getChildFile("restored-b.wav");writeIR(a,.5f);writeIR(b,.3f);hem::ConvolutionReverbProcessor p;hem::ConvolutionReverbTestAccess::stop(p);check(p.loadIRFileOffThread(b).wasOk(),"prepare B resource");juce::ValueTree state("PARAMETERS");p.captureResourceState(state);const auto expectedHash=p.resourceInfo().sha256;
 for(bool restore:{true,false}){p.requestIRFile(a);const auto captured=hem::ConvolutionReverbTestAccess::extract(p);check(restore?p.restoreResourceState(state).wasOk():p.loadIRFileOffThread(b).wasOk(),"newer B action failed");hem::ConvolutionReverbTestAccess::dispatch(p,captured);check(p.resourceInfo().path==b.getFullPathName()&&p.resourceInfo().sha256==expectedHash,"stale extracted A request overwrote newer restored/explicit B");p.prepareToPlay(48000,128);set(p,"mix",100);juce::AudioBuffer<float>audio(2,512);audio.clear();audio.setSample(0,0,1);juce::MidiBuffer midi;runBlock(p,audio,midi);check(std::abs(audio.getSample(0,192)-.3f)<2.e-5,"stale request changed actual IR coefficients");}
 std::cout<<"PASS deterministic extracted-request/session-restore and explicit-load generation races\n";
}
static void testResource(const juce::File&folder){const auto file=folder.getChildFile("owned-test-impulse.wav");writeIR(file);hem::ConvolutionReverbProcessor p;p.prepareToPlay(48000,128);check(p.loadIRFileOffThread(file).wasOk(),"file IR loading failed");auto info=p.resourceInfo();check(info.ready&&info.channels==2&&info.frames==4800&&info.sha256.length()==64,"resource metadata not real");juce::ValueTree saved("PARAMETERS");p.captureResourceState(saved);auto broken=saved.createCopy();broken.removeProperty("irFrames",nullptr);check(p.restoreResourceState(broken).failed(),"partial resource metadata accepted");broken=saved.createCopy();broken.setProperty("irFrames","10junk",nullptr);check(p.restoreResourceState(broken).failed(),"malformed resource number accepted");
 auto contradictory=saved.createCopy();contradictory.setProperty("irChannels",1,nullptr);check(p.restoreResourceState(contradictory).failed(),"contradictory decoded metadata accepted");check(p.resourceInfo().ready&&p.resourceInfo().sha256==info.sha256,"rejected resource state replaced the valid existing IR");
 hem::ConvolutionReverbProcessor restored;check(restored.restoreResourceState(saved).wasOk(),"resource roundtrip failed");check(restored.resourceInfo().ready,"restored IR not decoded");restored.prepareToPlay(48000,128);set(restored,"mix",100);juce::MidiBuffer midi;juce::AudioBuffer<float>b(2,512);b.clear();b.setSample(0,0,1);b.setSample(1,0,1);runBlock(restored,b,midi);check(std::abs(b.getSample(0,192)-.5f)<2.e-5f&&std::abs(b.getSample(1,192)-.25f)<2.e-5f,"resource IR coefficient or stereo mapping wrong");
 writeIR(file,.3f);hem::ConvolutionReverbProcessor changed;check(changed.restoreResourceState(saved).wasOk(),"valid session with changed IR must remain loadable");check(!changed.resourceInfo().ready&&changed.resourceInfo().status.containsIgnoreCase("changed"),"changed IR silently accepted");changed.prepareToPlay(48000,128);set(changed,"mix",100);set(changed,"soloWet",1);b.clear();b.setSample(0,0,.2f);runBlock(changed,b,midi);check(b.getSample(0,192)==.2f&&b.getSample(0,0)==0,"changed IR not latency-aligned dry-safe");
 file.deleteFile();hem::ConvolutionReverbProcessor missing;check(missing.restoreResourceState(saved).wasOk(),"missing IR blocked entire session");check(!missing.resourceInfo().ready&&missing.resourceInfo().status.containsIgnoreCase("missing"),"missing IR not explicit");auto foreign=saved.createCopy();foreign.setProperty("irPath","Z:\\MovedProject\\test.wav",nullptr);check(missing.restoreResourceState(foreign).wasOk()&&!missing.resourceInfo().ready,"foreign-platform IR path must remain loadable and unavailable");writeIR(file);p.requestIRFile(file);bool loaded=false;for(int i=0;i<500&&!loaded;++i){std::this_thread::sleep_for(std::chrono::milliseconds(2));auto r=p.resourceInfo();loaded=r.ready&&!r.loading;}check(loaded,"async file loader never completed");set(p,"irSize",200);bool resized=false;for(int i=0;i<500&&!resized;++i){std::this_thread::sleep_for(std::chrono::milliseconds(2));resized=p.irDisplay().sizePercent==200;}check(resized,"automated IR size never prepared");
 std::cout<<"PASS real WAV decode, SHA-256 resource roundtrip, partial/malformed rejection, missing/changed dry-safe state, async loading and IR-size automation\n";
}
static void testSessionHooks(const juce::File&folder){const auto file=folder.getChildFile("session-roundtrip.wav");writeIR(file,.5f);hem::ConvolutionReverbProcessor original;check(original.loadIRFileOffThread(file).wasOk(),"session IR source");set(original,"mix",37.5f);set(original,"irSize",150);set(original,"predelay",12.5f);set(original,"output",-3);const auto before=hem::SessionState::captureParameters(original);check(before.getNumProperties()==6&&before.hasProperty("irSha256"),"real SessionState capture resource hook is missing");auto xml=juce::XmlDocument::parse(before.toXmlString());check(xml!=nullptr,"parameter XML encode/decode");auto recalled=juce::ValueTree::fromXml(*xml);hem::ConvolutionReverbProcessor restored;hem::SessionState::restoreParameters(restored,recalled);restored.prepareToPlay(48000,128);check(hem::SessionState::captureParameters(restored).isEquivalentTo(before),"SessionState parameter/resource XML roundtrip differs");check(restored.resourceInfo().ready&&restored.getLatencySamples()==192&&restored.getTailLengthSeconds()>.15,"SessionState did not restore prepared IR/latency/tail");
 for(int kind=0;kind<4;++kind){auto invalid=before.createCopy();for(auto child:invalid)if(child["id"].toString()=="mix")child.setProperty("value",90,nullptr);if(kind==0)invalid.setProperty("unexpectedRoot",true,nullptr);if(kind==1)invalid.getChild(0).setProperty("unexpectedParam",true,nullptr);if(kind==2)invalid.removeProperty("irFrames",nullptr);if(kind==3)invalid.setProperty("irChannels",1,nullptr);bool rejected=false;try{hem::SessionState::restoreParameters(restored,invalid);}catch(const std::exception&){rejected=true;}check(rejected,"invalid SessionState schema/resource accepted");check(hem::SessionState::captureParameters(restored).isEquivalentTo(before),"rejected SessionState restore partially changed parameters/resource");}
 hem::AlgorithmicReverbProcessor algorithmic;auto algorithmState=hem::SessionState::captureParameters(algorithmic);algorithmState.setProperty("irResourceVersion",1,nullptr);bool rejected=false;try{hem::SessionState::restoreParameters(algorithmic,algorithmState);}catch(const std::exception&){rejected=true;}check(rejected,"algorithmic state accepted convolution-only root metadata");
 std::cout<<"PASS real staged SessionState capture/restore hooks, IR metadata XML roundtrip, strict root/PARAM fields and transactional rejection\n";
}
static void testControls(){for(int number:{19,20}){std::unique_ptr<hem::ModuleProcessor>p=number==19?std::unique_ptr<hem::ModuleProcessor>(std::make_unique<hem::ConvolutionReverbProcessor>()):std::unique_ptr<hem::ModuleProcessor>(std::make_unique<hem::AlgorithmicReverbProcessor>());p->prepareToPlay(48000,128);auto editor=std::unique_ptr<juce::AudioProcessorEditor>(p->createEditor());auto*module=dynamic_cast<hem::ModuleEditor*>(editor.get());check(module!=nullptr,"host editor interface missing");for(const auto&spec:p->parameterSpecs()){auto*c=find(*editor,spec.id);check(c!=nullptr,juce::String("missing bound control: ")+spec.id);check(c->getName()==spec.name,juce::String("accessible name mismatch: ")+spec.id);check(std::abs(p->state.getRawParameterValue(spec.id)->load()-spec.initial)<1.e-4f,"editor changed startup values");if(c->getProperties().contains("reverbChoiceParameter")){for(int i=0;i<3;++i){auto*button=dynamic_cast<juce::Button*>(find(*editor,i==0?"algorithm":"algorithm_"+juce::String(i)));check(button&&bool(button->onClick),"algorithm choice missing");button->onClick();module->refreshParameterActivity();check(p->state.getRawParameterValue("algorithm")->load()==i&&button->getToggleState(),"algorithm choice unbound");}}else if(auto*slider=dynamic_cast<juce::Slider*>(c)){check(slider->getDoubleClickReturnValue()==spec.initial,"double-click semantic default incorrect");check(slider->getTextValueSuffix().trim()==spec.unit,"dial units wrong");for(float value:{spec.minimum,spec.maximum,spec.initial}){slider->setValue(value,juce::sendNotificationSync);check(std::abs(p->state.getRawParameterValue(spec.id)->load()-value)<=std::max(.0001f,spec.step*.51f),"dial does not change DSP parameter");}}else if(auto*button=dynamic_cast<juce::ToggleButton*>(c)){button->setToggleState(true,juce::sendNotificationSync);check(p->state.getRawParameterValue(spec.id)->load()==1,"toggle on failed");button->setToggleState(false,juce::sendNotificationSync);check(p->state.getRawParameterValue(spec.id)->load()==0,"toggle off failed");}else check(false,"unknown control type");set(*p,spec.id,spec.initial);for(auto size:{juce::Point<int>(1000,563),juce::Point<int>(1280,720),juce::Point<int>(1672,941)}){editor->setSize(size.x,size.y);check(editor->getLocalBounds().contains(c->getBounds()),"control escaped editor bounds");}}
 if(number==19){check(find(*editor,"loadIR")!=nullptr,"LOAD IR action missing");set(*p,"lowCutOn",0);module->refreshParameterActivity();check(!find(*editor,"lowCut")->isEnabled(),"low-cut enabled state wrong");set(*p,"lowCutOn",1);module->refreshParameterActivity();check(find(*editor,"lowCut")->isEnabled(),"low-cut parameter did not reactivate");}}
 std::cout<<"PASS all 22 APVTS controls, real mode choices, semantic defaults, units, reset, accessibility, activation and resize bounds\n";
}
static void testChain(){hem::InsertChain chain;chain.prepare(48000,128);chain.installInitial(0,chain.stageNode(std::make_unique<hem::ConvolutionReverbProcessor>(),48000,128,chain.slotLatency()),false);chain.installInitial(1,chain.stageNode(std::make_unique<hem::AlgorithmicReverbProcessor>(),48000,128,chain.slotLatency()),false);std::array<float,128>l{},r{};for(int b=0;b<40;++b){l.fill(.2f);r.fill(-.1f);measuring=true;chain.process(l.data(),r.data(),128);measuring=false;}check(chain.latencySamples()==8*288,"shared latency budget changed");check(std::abs(l.back()-.2f)<1.e-6f&&std::abs(r.back()+.1f)<1.e-6f,"insert chain neutral audio wrong");for(double rate:{8000.,44100.,48000.,96000.,192000.}){chain.prepare(rate,128);const int expectedLatency=8*int(std::ceil(rate*.006));std::vector<float>impulse(size_t(expectedLatency+512));for(int start=0;start<int(impulse.size());start+=37){const int n=std::min(37,int(impulse.size())-start);l.fill(0);r.fill(0);if(start==0){l[0]=.5f;r[0]=.25f;}measuring=true;chain.process(l.data(),r.data(),n);measuring=false;for(int i=0;i<n;++i)impulse[size_t(start+i)]=l[size_t(i)];}for(int i=0;i<int(impulse.size());++i)check(impulse[size_t(i)]==(i==expectedLatency?.5f:0.f),"real host residual compensation or reported convolution latency is wrong");}
 std::cout<<"PASS real insert chain: exact impulses at unchanged eight-slot 6 ms budget across five rates\n";}
int main(int argc,char**argv){try{juce::ScopedJuceInitialiser_GUI gui;const auto folder=argc>1?juce::File(argv[1]):juce::File::getCurrentWorkingDirectory().getChildFile("reverb-binding-fixtures");check(folder.createDirectory().wasOk(),"fixture directory");testCanonicalDefaults();testLifecycle();testResource(folder);testStaleRequests(folder);testSessionHooks(folder);testControls();testChain();check(allocs==0&&frees==0,"callback allocated or freed memory");std::cout<<"PASS "<<assertions<<" binding assertions; zero audio-thread allocations/frees\n";return 0;}catch(const std::exception&e){measuring=false;std::cerr<<"FAIL "<<e.what()<<" allocations="<<allocs<<" frees="<<frees<<'\n';return 1;}}
