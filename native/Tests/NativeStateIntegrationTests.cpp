#include "../Source/SessionState.h"
#include "../Source/Systems/PresetManager.h"
#include "../Source/Systems/OfflineMixWorker.h"
#include <iostream>
#include <stdexcept>

namespace {
using namespace hem;
int assertions=0;
void check(bool b,const juce::String& message){++assertions;if(!b)throw std::runtime_error(message.toStdString());}
void ok(const juce::Result& result){check(result.wasOk(),result.getErrorMessage());}
template<class Work> bool rejects(Work work){try{work();}catch(const std::exception&){return true;}return false;}
struct Files {
    juce::File folder=juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("hem-native-state-"+juce::Uuid().toString());
    Files(){ok(folder.createDirectory());}~Files(){folder.deleteRecursively();}
    juce::File file(const juce::String& name)const{return folder.getChildFile(name);}
};
juce::ValueTree scalar(float gain){juce::ValueTree tree("PARAMETERS"),p("PARAM");p.setProperty("id","gain",nullptr);p.setProperty("value",double(gain),nullptr);tree.addChild(p,-1,nullptr);return tree;}
NativeProcessorSnapshot fixtureSnapshot(){
    NativeProcessorSnapshot s;s.parameters=scalar(7);s.committedRevision=73;
    nativeResource::ResourceDescriptor resource;resource.role="sample";resource.index=0;
    resource.path="/tmp/hem-native-provider-fixture.wav";resource.sha256=juce::String::repeatedString("a",64);
    resource.byteSize=144;resource.sourceRate=48000.25;resource.channels=2;resource.frames=7;
    s.native=nativeResource::State{"TEST_NATIVE_RESOURCE",{resource,{"sample",1,{},{},0,0,0,0}}};return s;
}
// Test-local dispatch oracle only. It is deliberately NEVER registered, never
// reads a resource and never claims to prove shipping audio/resource behavior.
class FixtureProvider final : public ModuleProcessor,public NativeResourceStateProvider {
public:
    static inline int living=0;
    FixtureProvider():ModuleProcessor("TEST_NATIVE_RESOURCE","Fixture",{{"gain","Gain",-12,12,0,1,"dB"}}){++living;committed=fixtureSnapshot();}
    ~FixtureProvider()override{--living;}
    const nativeResource::ExpectedLayout& expectedNativeResourceLayout()const noexcept override{return layout;}
    bool hasPendingNativeStateWork()const noexcept override{return pending!=0;}
    juce::Result tryCaptureNativeState(NativeProcessorSnapshot& out)override{
        ++captures;out=committed;if(failCapture)return juce::Result::fail("fixture capture failed");
        if(startPendingDuringCapture)pending=1;return juce::Result::ok();
    }
    juce::Result validateNativeState(const NativeProcessorSnapshot& candidate)const override{
        ++validations;return rejectValidation||candidate.parameters.getChild(0)["value"].toString().getDoubleValue()!=double(candidate.native->resources[0].frames)
            ?juce::Result::fail("fixture coherence mismatch"):juce::Result::ok();
    }
    juce::Result restoreNativeStateOffThread(const NativeProcessorSnapshot& candidate,const NativeRestoreContext&)override{
        ++restores;state.getParameter("gain")->setValueNotifyingHost(state.getParameter("gain")->convertTo0to1(7));
        if(throwRestore)throw std::runtime_error("fixture throws after mutation");
        if(failRestore)return juce::Result::fail("fixture fails after mutation");
        committed=NativeProcessorState::deepCopy(candidate);if(leavePending)pending=8;return juce::Result::ok();
    }
    bool processingReady()const noexcept override{return ready;}
    void prepareToPlay(double,int)override{if(pendingOnPrepare)pending=16;if(unreadyOnPrepare)ready=false;}
    void processBlock(juce::AudioBuffer<float>&,juce::MidiBuffer&)override{}
    juce::AudioProcessorEditor* createEditor()override{return nullptr;}
    float meter()const noexcept override{return 0;}juce::String meterLabel()const override{return {};}
    const nativeResource::ExpectedLayout layout{"TEST_NATIVE_RESOURCE",{{"sample",0},{"sample",1}}};
    NativeProcessorSnapshot committed;int pending=0,captures=0,restores=0;mutable int validations=0;
    bool failCapture=false,startPendingDuringCapture=false,rejectValidation=false,failRestore=false,throwRestore=false,leavePending=false;
    bool pendingOnPrepare=false,unreadyOnPrepare=false,ready=true;
};
struct Contract {const char* id;const char* type;const char* parameters;};
const Contract contracts[]{
{"sipa","SIPA","0aeea3ef00b5eeba21e28e80a2dcda95e3f78761b41602b15c62ded42d8eea07"}, // 2026-10 faceplate replacement,
{"surgical-eq","SURGICAL_EQ","dc4753e60be98944c01d111151449ba6001e4a6bb5df2997f08e7a3c1cd351fe"},
{"vca","VCA","66f0e4145236b2fdf4d2197a1d757d3177dd9d2951e1089c1143326f0635536d"},
{"true-peak","TRUE_PEAK","43bf1742a1ac45538fbd94fd26af3c736ea955bbbc38f56380adbb9acd671104"},
{"dhta","DHTA","f86189090c5f49d3855f90fa1a9eaa01027f47dd3fd3087375ddb05924401324"},
{"transient","TRANSIENT","13d439bafac06e07fd894f675fcea9b314e2ad7042f0db289f251a7e18aeb4ad"}, // 2026-10 faceplate replacement,
{"sub-bass","SUB_BASS","79edeb47e15158065c8634bd85b10dcede4db0b4788d30a7e76e807bd107c71f"},
{"spectral-restorer","RESTORER","38339908e6f3b082084b87775d35ba6726497bac7305e9bd3fd47a4543ed0ae1"},
{"vocal-presence","VPR_ARCHITECT","b9ef6ae7bd4fbd8e86387c09522aaaeb76777b84e6921f397f9a49b2574ce0f0"},
{"triode-tube","TRIODE_TUBE","44f9dd78a76fa9e4cff2864ec018a66d47c10e00362bb0cd9ff6715c7d6fac27"},
{"tape","TAPE","34ba6d6fe6a240a0395af231daa208cc8626f74198c1f35b28021d1122f6ea3c"},
{"monitor-eq","MONITOR_EQ","19e6c9d48c78a1f53a0fbbfcdb560717a1a29bacfc56bb52fa9792e798d661f6"},
{"dc-subsonic","DC_SUBSONIC","9235780a7fbcc232879ace142e70f91661dbfc78677c8588c09af717e9046d82"},
{"smart-gate","SMART_GATE","24c33493ffc9722c0fd8bd228b400bb295e5c70218555d415e473eb7378ea770"},
{"vocal-rider","VOCAL_RIDER","4cdb3a9fcf7a48b9489fabc226db29d680746fec3368705320bf765a739bbe64"},
{"plosive-control","PLOSIVE_CONTROL","79089ebd6525527cf6978622d6a3dc5fd0a35b9c7a552e1c66ba750c8f87dbae"},
{"hum-buzz","HUM_BUZZ","2d35f7b9283d110077174d1d00ceb9bf56d553326fe1ef40d08b6290efe5ac5a"},
{"loudness-lab","LOUDNESS_LAB","2c7d66456fa1c992cf7e9e9c57d3f1370995be8a9de5abce8d1c863d91ec4950"},
{"spectrum-spectrogram","SPECTRUM_SPECTROGRAM","c67805f3563b7484825906bd7c69f7037a6cd534d0c0064fd1fde893709a8a53"},
{"delta-monitor","DELTA_MONITOR","951b2d738c8d02e93f803a4209a449b24a063b4e66ef223893a723d62ba21adb"},
{"convolution-reverb","CONVOLUTION_REVERB","4ee51d82508b507029bd62496e298099a1827f1bd6249111364dc6d3a1daa7d1"},
{"algorithmic-reverb","ALGORITHMIC_REVERB","553cb1c37dc768c56f7c49191310f7e438ba4c419119b637d469f087f8382b0a"},
{"chorus-flanger-phaser","CHORUS_FLANGER_PHASER","63d6c65e9db2c2be249fbcac5cfff6e496c3a3368ca80a302dad614d01d33c33"},
{"rotary-tremolo-autopan","ROTARY_TREMOLO_AUTOPAN","02392ab19d3b8ef040c4113ae66f1aa84a87cc897c7a7f3605b7d625f066182b"},
{"multitap-delay","MULTITAP_DELAY","8273efd523e45e1a51df7967eeb987d4e41218c5f03c5b8c65f640ff7fc9b124"},
{"tape-delay","TAPE_DELAY","26d30406862a17805d0acae377af2504e03ccc92afa4861e0c103334718e45ae"},
{"dynamic-eq-8","DYNAMIC_EQ_8","9eb3598f21adf0aa0815aaafc969db8d3062e0f8893a9560501ff317542472cc"},
{"mid-side-eq","MID_SIDE_EQ","734ac629fdb308a7487c95dccd015515f1d0c395444fe142b3951fc0fe82971a"},
{"multiband-dynamics","MULTIBAND_DYNAMICS","eac37f0e5080d71a0659c590d5c48a08344c8fec57cb96bcadbf610bf3aa5fe6"},
{"multiband-imager","MULTIBAND_IMAGER","b71e5bdca3612a173f40d3d99a20b72bbf419ae7af66c83a2ee6130a14227f5f"},
{"parallel-distortion","PARALLEL_DISTORTION","f4a18eba1e3eb3a1145c813a2e5f09dfffb6b07645df2e7ec8462e9626b83735"},
{"exciter","EXCITER","e61b08be6269cbc2ae359bedc71f819a80dc9d631a02ceb66fc6c7d13fed3be3"},
{"dynamic-resonance-suppressor","DYNAMIC_RESONANCE_SUPPRESSOR","46a3cd7e0cddb915a125888763b7439b8b124d99376e12a787dafa7eaa2b8ae4"},
{"breath-mouth-control","BREATH_MOUTH_CONTROL","ddb435e2d86ea14e444fbf2e5fc761bd938680b19a4bf85eaa4109ba3c763212"}, // 2026-10 faceplate replacement,
{"clip-repair","CLIP_REPAIR","3f376138c2fa11588319ecffc5356d23870101e45986899135ea18f7cc6809c3"},
};
void legacyContracts(){
    check(PluginRegistry::entries().size()==41&&std::size(contracts)==35,"Expected preserved 35 identities plus six approved functions");
    const char* added[]{"drum-replacer","amp-cabinet","reference-match"};
    for(size_t i=0;i<std::size(added);++i){
        check(juce::String(PluginRegistry::entries()[35+i].id)==added[i],"Additive registry ordering changed");
        auto processor=PluginRegistry::create(added[i]);
        check(processor&&PluginRegistry::idFor(*processor)==added[i],"New identity roundtrip");
        check(dynamic_cast<NativeResourceStateProvider*>(processor.get())!=nullptr,"New resource processor lacks persistence");
    }
    for(size_t i=0;i<std::size(contracts);++i){const auto& expected=contracts[i];const auto& entry=PluginRegistry::entries()[i];
        check(juce::String(entry.id)==expected.id,"Registry ordering/ID changed");auto p=entry.create();
        check(p->state.state.hasType(expected.type)&&PluginRegistry::idFor(*p)==expected.id,"Stable state type changed");
        check(dynamic_cast<NativeResourceStateProvider*>(p.get())==nullptr,"Existing engine silently acquired new persistence");
        juce::MemoryOutputStream bytes;
        for(const auto& s:p->parameterSpecs()){bytes.writeString(s.id);bytes.writeString(s.name);bytes.writeFloat(s.minimum);bytes.writeFloat(s.maximum);bytes.writeFloat(s.initial);bytes.writeFloat(s.step);bytes.writeString(s.unit);bytes.writeBool(s.toggle);}
        check(juce::SHA256(bytes.getData(),bytes.getDataSize()).toHexString()==expected.parameters,"Independent verified009 parameter contract changed: "+juce::String(expected.id));
        NativeProcessorSnapshot captured;ok(NativeProcessorState::capture(*p,captured));
        check(!captured.native&&captured.parameters.isEquivalentTo(SessionState::captureParameters(*p)),"Legacy scalar capture changed");
        std::unique_ptr<ModuleProcessor> restored;ok(NativeProcessorState::restoreUnpublished(entry.create(),captured,{},restored));
        check(SessionState::captureParameters(*restored).isEquivalentTo(captured.parameters),"Legacy scalar restore changed");
    }
    InsertChain chain;for(double rate:{44100,48000,96000,192000}){chain.prepare(rate,256);check(chain.slotLatency()==int(std::ceil(rate*.006)),"6 ms slot admission changed");}
    ConvolutionReverbProcessor convolution;auto parameters=SessionState::captureParameters(convolution);
    const char* fields[]{"irResourceVersion","irPath","irSha256","irSampleRate","irChannels","irFrames"};
    check(parameters.getNumProperties()==6,"Convolution scalar resource shape changed");for(const auto* field:fields)check(parameters.hasProperty(field),"Convolution legacy field missing");
    juce::MemoryBlock before,after;convolution.getStateInformation(before);convolution.setStateInformation(before.getData(),int(before.getSize()));convolution.getStateInformation(after);
    check(before==after,"Convolution existing binary state changed");
}
SessionSnapshot legacySnapshot(){
    SessionSnapshot s;MonitorProcessor monitor;s.monitorParameters=SessionState::captureParameters(monitor);
    for(const auto* id:{"sipa","convolution-reverb"}){const int i=juce::String(id)=="sipa"?0:1;auto p=PluginRegistry::create(id);s.tracks[0].slots[i].pluginId=id;s.tracks[0].slots[i].parameters=SessionState::captureParameters(*p);}return s;
}
juce::String legacyExternalPath(){return juce::File("/tmp/hem-native-legacy-fixture.vst3").getFullPathName().replaceCharacter(char(92),'/');}
void addExternal(SessionSnapshot& s,const juce::String& path=legacyExternalPath()){
    auto& slot=s.tracks[0].slots[2];slot.pluginId="external-vst3";
    slot.external.descriptionXml="<PLUGIN name=\"Fixture\" format=\"VST3\" file=\""+path+"\" uniqueId=\"12345678\" numInputs=\"2\" numOutputs=\"2\" isInstrument=\"0\"/>";
    const char data[]{0,1,2,127};slot.external.state=juce::MemoryBlock(data,sizeof(data));
}
void legacyBytes(const Files& files){
    auto s=legacySnapshot();const juce::File file("/tmp/hem-native-state-legacy.xml");
    const char* expected[]{"a2fb87c1fe9c757e9bea05c381105de54f36041b66f18b45695b51842f4f62ce","87ce4bfca3be5fb3a3fbb986a74ccfab4c9bae6e504ad68db37ea32078c2ce76"};
    const char* expectedJson[]{"fdc8039be466b227b3986e9438cfd9e8dab837967aa5e2c53bd73a8060e03de6","d2b5f45d9f25bbd11d327435ccdf1829bd6a65afa3a5508ee95b5435707a58e4"};
    for(int version:{1,2}){if(version==2)addExternal(s);const auto root=SessionState::encode(s,file);
        check(int(root["version"])==version,"Legacy writer changed session version");
        const auto xml=root.createXml()->toString();
        check(juce::SHA256(xml.replace(legacyExternalPath(),"/tmp/hem-native-legacy-fixture.vst3").toUTF8()).toHexString()==expected[version-1],"Legacy emitted XML bytes changed");
        const auto json=juce::JSON::toString(PresetManager::toJson(root),false);
        check(juce::SHA256(json.replace(legacyExternalPath(),"/tmp/hem-native-legacy-fixture.vst3").toUTF8()).toHexString()==expectedJson[version-1],"Legacy emitted JSON bytes changed");
        const auto xmlFile=files.file("legacy"+juce::String(version)+".xml"),jsonFile=xmlFile.withFileExtension("json");
        SessionState::save(s,xmlFile);PresetManager::save(s,jsonFile);
        check(xmlFile.loadFileAsString()==xml&&jsonFile.loadFileAsString()==json,"Actual legacy save bytes changed");}
}
void providerHelpers(){
    FixtureProvider provider;check(PluginRegistry::idFor(provider).isEmpty(),"Test provider must remain unregistered");
    provider.state.getParameter("gain")->setValueNotifyingHost(0);
    NativeProcessorSnapshot snapshot;ok(NativeProcessorState::capture(provider,snapshot));
    check(double(snapshot.parameters.getChild(0)["value"])==7&&snapshot.native->resources[0].frames==7&&snapshot.committedRevision==73,"Capture did not dispatch coherent provider generation");
    check(provider.captures==1,"Provider capture not dispatched");
    auto copy=NativeProcessorState::deepCopy(snapshot);copy.parameters.getChild(0).setProperty("value",0,nullptr);copy.native->resources[0].path="changed";
    check(double(snapshot.parameters.getChild(0)["value"])==7&&snapshot.native->resources[0].path!="changed","Whole-state copy shares mutable payload");
    for(bool root:{false,true})for(bool array:{false,true}){
        auto bad=NativeProcessorState::deepCopy(snapshot);juce::var mutableValue;
        if(array){juce::Array<juce::var> values;values.add(1);mutableValue=values;}else mutableValue=juce::var(new juce::DynamicObject());
        (root?bad.parameters:bad.parameters.getChild(0)).setProperty("mutable",mutableValue,nullptr);
        check(rejects([&]{NativeProcessorState::deepCopy(bad);}),"Whole-state deep copy accepted aliased mutable property payload");}
    check(rejects([&]{SessionState::captureParameters(provider);}),"Scalar-only capture drops native state");
    check(rejects([&]{SessionState::restoreParameters(provider,snapshot.parameters);}),"Scalar-only restore bypasses native provider");
    const auto sentinel=NativeProcessorState::deepCopy(snapshot);
    for(int pending:{1,2,4,8,16}){provider.pending=pending;const int calls=provider.captures;
        check(NativeProcessorState::capture(provider,snapshot).failed(),"Busy native capture accepted");
        check(provider.captures==calls&&snapshot.committedRevision==73&&snapshot.parameters.isEquivalentTo(sentinel.parameters),"Busy capture waited/fell back/changed output");}
    provider.pending=0;provider.failCapture=true;check(NativeProcessorState::capture(provider,snapshot).failed(),"Failed provider capture accepted");provider.failCapture=false;
    provider.startPendingDuringCapture=true;check(NativeProcessorState::capture(provider,snapshot).failed(),"Publication raced capture");provider.startPendingDuringCapture=false;provider.pending=0;
    provider.rejectValidation=true;check(NativeProcessorState::capture(provider,snapshot).failed(),"Provider validation skipped");provider.rejectValidation=false;
    check(snapshot.committedRevision==sentinel.committedRevision&&snapshot.parameters.isEquivalentTo(sentinel.parameters),"Failed capture changed output");
    auto missing=NativeProcessorState::deepCopy(snapshot);missing.native.reset();check(NativeProcessorState::validate(provider,missing).failed(),"Provider auxiliary state was optional");
    auto invalid=NativeProcessorState::deepCopy(snapshot);invalid.native->ownerType="SIPA";check(NativeProcessorState::validate(provider,invalid).failed(),"Owner mismatch accepted");
    SipaProcessor scalarOnly;check(NativeProcessorState::validate(scalarOnly,snapshot).failed(),"Nonprovider accepted native state");
    std::unique_ptr<ModuleProcessor> output=std::make_unique<SipaProcessor>();auto* original=output.get();const int living=FixtureProvider::living;
    for(int failure=0;failure<7;++failure){auto candidate=std::make_unique<FixtureProvider>();
        candidate->failRestore=failure==0;candidate->throwRestore=failure==1;candidate->leavePending=failure==2;candidate->rejectValidation=failure==3;
        NativeRestoreContext context{48000,256,120,true,{}};if(failure==4)context.cancelled=[] {return true;};
        int cancellationChecks=0;if(failure==5)context.cancelled=[&]{return ++cancellationChecks>1;};if(failure==6)context.sampleRate=0;
        check(NativeProcessorState::restoreUnpublished(std::move(candidate),snapshot,context,output).failed(),"Invalid candidate restoration succeeded");
        check(output.get()==original&&FixtureProvider::living==living,"Failed candidate was retained or destination changed");}
    ok(NativeProcessorState::restoreUnpublished(std::make_unique<FixtureProvider>(),snapshot,{48000,256,120,true,{}},output));
    check(output.get()!=original&&dynamic_cast<FixtureProvider*>(output.get())->restores==1,"Unpublished restore did not dispatch");
}
void preparedPublicationGate(){
    InsertChain chain;chain.prepare(48000,256);const auto snapshot=fixtureSnapshot();
    for(bool pending:{false,true}){const int living=FixtureProvider::living;auto candidate=std::make_unique<FixtureProvider>();
        candidate->pendingOnPrepare=pending;candidate->unreadyOnPrepare=!pending;std::unique_ptr<ModuleProcessor> restored;
        ok(NativeProcessorState::restoreUnpublished(std::move(candidate),snapshot,{48000,256,120,false,{}},restored));
        auto node=chain.stageNode(std::move(restored),48000,256,chain.slotLatency());
        check(NativeProcessorState::validatePreparedForPublication(*node->processor).failed(),"Host re-preparation bypassed pending/readiness gate");
        node.reset();check(FixtureProvider::living==living&&chain.activeProcessor(0)==nullptr,"Unsettled prepared candidate was installed or retained");}
    std::unique_ptr<ModuleProcessor> restored;ok(NativeProcessorState::restoreUnpublished(std::make_unique<FixtureProvider>(),snapshot,{48000,256,120,false,{}},restored));
    auto ready=chain.stageNode(std::move(restored),48000,256,chain.slotLatency());
    ok(NativeProcessorState::validatePreparedForPublication(*ready->processor));
}
void actualCaptureRoutes(){
    TrackBank bank;auto& track=bank.tracks[0];track.prepare(48000,256);
    auto scalarProcessor=std::make_unique<SipaProcessor>();
    track.chain.installInitial(0,track.chain.stageNode(std::move(scalarProcessor),48000,256,track.chain.slotLatency()),false);
    MonitorProcessor monitor;const auto captured=SessionState::capture(bank,0,0,0,false,monitor);
    check(captured.tracks[0].slots[0].pluginId=="sipa"&&!captured.tracks[0].slots[0].native,"Actual bank capture lost legacy native identity");
    auto* active=track.chain.activeProcessor(0);const auto& first=active->parameterSpecs().front();
    active->state.getParameter(first.id)->setValueNotifyingHost(active->state.getParameter(first.id)->convertTo0to1(first.maximum));
    check(!captured.tracks[0].slots[0].parameters.isEquivalentTo(SessionState::captureParameters(*active)),"Actual bank snapshot aliases live parameters");
    track.chain.installInitial(1,track.chain.stageNode(std::make_unique<FixtureProvider>(),48000,256,track.chain.slotLatency()),false);
    check(rejects([&]{SessionState::capture(bank,0,0,0,false,monitor);}),"Unregistered provider entered session bank capture");
}
void binaryHelpers(){
    FixtureProvider provider;auto snapshot=fixtureSnapshot();juce::MemoryBlock bytes;ok(NativeProcessorState::encodeBinary(provider,snapshot,bytes));
    const auto text=juce::String::fromUTF8(static_cast<const char*>(bytes.getData()),int(bytes.getSize()));
    check(!text.contains("committedRevision")&&!text.contains("revision"),"Transient revision serialized");
    NativeProcessorSnapshot decoded;ok(NativeProcessorState::decodeBinary(provider,bytes.getData(),bytes.getSize(),decoded));
    check(decoded.native&&decoded.committedRevision==0&&decoded.native->resources[0].frames==7&&decoded.native->resources[0].sourceRate==48000.25,"Binary roundtrip lost state");
    for(double rate:{std::nextafter(8000.,10000.),44100.123456789123,std::nextafter(384000.,0.)}){
        auto exact=fixtureSnapshot();exact.native->resources[0].sourceRate=rate;juce::MemoryBlock exactBytes;
        ok(NativeProcessorState::encodeBinary(provider,exact,exactBytes));NativeProcessorSnapshot exactDecoded;
        ok(NativeProcessorState::decodeBinary(provider,exactBytes.getData(),exactBytes.getSize(),exactDecoded));
        check(exactDecoded.native->resources[0].sourceRate==rate,"New binary XML rounded fractional resource metadata");}
    const auto prior=NativeProcessorState::deepCopy(decoded);auto reject=[&](const juce::String& bad){check(NativeProcessorState::decodeBinary(provider,bad.toRawUTF8(),bad.getNumBytesAsUTF8(),decoded).failed(),"Malformed binary accepted");check(decoded.parameters.isEquivalentTo(prior.parameters)&&decoded.native->ownerType==prior.native->ownerType,"Failed binary decode changed output");};
    reject(text.replace("version=\"1\"","version=\"1\" version=\"1\""));reject(text.replace("TEST_NATIVE_RESOURCE","SIPA"));reject(text.replace("NATIVE_RESOURCE","MATCH_PROFILE"));
    reject(text.replace("<PARAM ","<PARAM extra=\"1\" extra=\"2\" "));
    juce::MemoryBlock embedded(bytes);const char nul=0;embedded.append(&nul,1);check(NativeProcessorState::decodeBinary(provider,embedded.getData(),embedded.getSize(),decoded).failed(),"Binary NUL/trailing suffix accepted");
    check(NativeProcessorState::decodeBinary(provider,bytes.getData(),NativeProcessorState::maxBinaryBytes+1,decoded).failed(),"Oversize binary entered parser");
    auto invalid=NativeProcessorState::deepCopy(snapshot);invalid.native->resources[0].sha256="invalid";const auto previous=bytes;
    check(NativeProcessorState::encodeBinary(provider,invalid,bytes).failed()&&bytes==previous,"Failed binary encode changed output");
}
juce::ValueTree manifestTree(){FixtureProvider provider;juce::ValueTree tree;ok(nativeResource::encodeManifest(*fixtureSnapshot().native,provider.layout,tree));return tree;}
void newJsonTypes(){
    const auto native=manifestTree();const auto resource=native.getChild(0);
    for(const auto& pair:std::vector<std::pair<juce::ValueTree,juce::String>>{{native,"version"},{resource,"index"},{resource,"byteSize"},{resource,"channels"},{resource,"frames"}}){
        for(juce::var value:{juce::var("1"),juce::var(true),juce::var(1.0),juce::var(1.25)}){auto bad=pair.first.createCopy();bad.setProperty(pair.second,value,nullptr);check(rejects([&]{PresetManager::fromJson(PresetManager::toJson(bad));}),"New JSON integer accepted string/bool/double");}}
    for(juce::var value:{juce::var("48000"),juce::var(true)}){auto bad=resource.createCopy();bad.setProperty("sourceRate",value,nullptr);check(rejects([&]{PresetManager::fromJson(PresetManager::toJson(bad));}),"JSON source rate accepted string/bool");}
    check(PresetManager::fromJson(PresetManager::toJson(resource)).hasType("RESOURCE"),"Finite fractional source rate rejected");
    auto scalarTree=scalar(1);scalarTree.getChild(0).setProperty("value","1",nullptr);check(PresetManager::fromJson(PresetManager::toJson(scalarTree)).getChild(0)["value"].isString(),"Legacy JSON scalar coercion changed");
    // MATCH_PROFILE is now an approved v4 typed payload. JSON transport may
    // preserve its tag; the actual Reference schema must reject an empty blob.
    ReferenceMatchProcessor::Snapshot decodedProfile;
    check(ReferenceMatchProcessor::decodeSnapshot(PresetManager::fromJson(PresetManager::toJson(juce::ValueTree("MATCH_PROFILE"))),decodedProfile).failed(),"Opaque profile escape hatch accepted");
}
void sessionRejections(const Files& files){
    const auto file=files.file("session.xml");auto s=legacySnapshot();auto root=SessionState::encode(s,file);
    auto rejected=[&](juce::ValueTree tree){check(rejects([&]{SessionState::decode(tree,file);}),"Malformed native session accepted");};
    for(int version:{1,2,3})for(const char* id:{"","sipa","monitor-eq","external-vst3","test-native-resource"}){
        auto bad=root.createCopy();bad.setProperty("version",version,nullptr);auto slot=bad.getChild(1).getChild(0);slot.setProperty("pluginId",id,nullptr);slot.addChild(manifestTree(),-1,nullptr);rejected(bad);}
    for(const char* sibling:{"PARAMETERS","NATIVE_RESOURCE","EXTERNAL","MATCH_PROFILE","CLIP_EDIT"}){auto bad=root.createCopy();auto slot=bad.getChild(1).getChild(0);slot.addChild(juce::ValueTree(sibling),-1,nullptr);rejected(bad);}
    auto beneath=root.createCopy();beneath.getChild(1).getChild(0).getChild(0).addChild(manifestTree(),-1,nullptr);rejected(beneath);
    auto monitor=root.createCopy();monitor.getChild(0).addChild(manifestTree(),-1,nullptr);rejected(monitor);
    auto monitorNested=root.createCopy();monitorNested.getChild(0).getChild(0).addChild(manifestTree(),-1,nullptr);rejected(monitorNested);
    for(const char* id:{"","sipa","monitor-eq","external-vst3","test-native-resource"}){auto bad=legacySnapshot();auto& slot=bad.tracks[0].slots[0];slot.pluginId=id;slot.native=*fixtureSnapshot().native;check(rejects([&]{SessionState::encode(bad,file);}),"In-memory unsupported native state encoded");}
    const auto vst=files.file("fixture.vst3");check(vst.replaceWithText("container-only fixture"),"Cannot make external fixture");
    addExternal(s,vst.getFullPathName());auto external=SessionState::encode(s,file);external.setProperty("version",3,nullptr);
    const auto decoded=SessionState::decode(external,file);check(decoded.tracks[0].slots[2].external.state==s.tracks[0].slots[2].external.state,"V3 rejected/lost external version>=2 state");
    check(int(SessionState::encode(decoded,file)["version"])==2,"Writer emitted V3 without real native provider");
    auto mixed=external.createCopy();mixed.getChild(1).getChild(2).addChild(manifestTree(),-1,nullptr);rejected(mixed);
    const auto xml=root.createXml()->toString();check(file.replaceWithText(xml.replace("version=\"1\"","version=\"1\" version=\"1\"")),"Cannot make duplicate XML fixture");
    check(rejects([&]{SessionState::load(file);}),"Raw duplicate XML attribute reached JUCE overwrite");
    auto jsonFile=files.file("session.json");const auto json=juce::JSON::toString(PresetManager::toJson(root),true);
    check(jsonFile.replaceWithText(json.replaceFirstOccurrenceOf("\"HEM_SESSION\"","\"HEM_SESSION\",\"type\":\"HEM_SESSION\"")),"Cannot make duplicate JSON fixture");
    check(rejects([&]{PresetManager::load(jsonFile);}),"Raw duplicate JSON property reached JUCE overwrite");
    juce::MemoryBlock nul(xml.toRawUTF8(),xml.getNumBytesAsUTF8());const char hidden[]{0,'<','X','/','>'};nul.append(hidden,sizeof(hidden));check(file.replaceWithData(nul.getData(),nul.getSize()),"Cannot make raw NUL fixture");
    check(rejects([&]{SessionState::load(file);}),"File NUL was discarded before raw preflight");
    // Actual session worker must reject before replacing the live bank.
    auto bad=root.createCopy();bad.setProperty("version",3,nullptr);bad.getChild(1).getChild(0).addChild(manifestTree(),-1,nullptr);
    check(file.replaceWithText(bad.createXml()->toString()),"Cannot write invalid worker input");
    juce::TimeSliceThread disk("Native state rejection test");check(disk.startThread(),"Cannot start fixture disk worker");
    auto live=std::make_unique<TrackBank>();live->tracks[0].name="UNCHANGED";auto* original=live.get();int commits=0;bool completed=false,success=true;
    {SessionWorker worker(disk,[&](std::unique_ptr<TrackBank>& bank,const SessionSnapshot&,SessionWorker::Context){++commits;live.swap(bank);return juce::Result::ok();},[](const juce::String&){},
        [&](bool,const juce::File&,const juce::Result& result){completed=true;success=result.wasOk();});
        check(worker.load(file,{48000,256,1}),"Worker did not accept rejection test");
        for(int i=0;i<1000&&worker.isBusy();++i){juce::Thread::sleep(5);juce::Timer::callPendingTimersSynchronously();}
        check(!worker.isBusy()&&completed&&!success&&commits==0&&live.get()==original&&live->tracks[0].name=="UNCHANGED","Rejected resource state changed live bank");}
    disk.stopThread(-1);
}
void legacyFileEncodings(const Files& files){
    auto snapshot=legacySnapshot();snapshot.tracks[0].name=juce::String::fromUTF8("Studio \xce\xa9 \xf0\x9f\x8e\xb5");const auto file=files.file("encoding.xml");const auto root=SessionState::encode(snapshot,file);
    for(bool json:{false,true}){const auto text=json?juce::JSON::toString(PresetManager::toJson(root),true):root.createXml()->toString();const auto path=file.withFileExtension(json?"json":"xml");
        for(int encoding:{0,1,2}){juce::MemoryOutputStream bytes;
            if(encoding==0){const unsigned char bom[]{0xef,0xbb,0xbf};bytes.write(bom,sizeof(bom));bytes.write(text.toRawUTF8(),text.getNumBytesAsUTF8());}
            else {const unsigned char le[]{0xff,0xfe},be[]{0xfe,0xff};bytes.write(encoding==1?le:be,2);
                auto source=text.toUTF16().getAddress();while(*source){const auto value=static_cast<short>(*source++);if(encoding==1)bytes.writeShort(value);else bytes.writeShortBigEndian(value);}}
            check(path.replaceWithData(bytes.getData(),bytes.getDataSize()),"Cannot write legacy BOM input");
            const auto loaded=PresetManager::load(path);check(loaded.tracks[0].slots[0].pluginId=="sipa"&&loaded.tracks[0].slots[1].pluginId=="convolution-reverb"&&loaded.tracks[0].name==snapshot.tracks[0].name,"Legacy BOM encoding rejected/lost state");
            const char nul[]{0,0};bytes.write(nul,encoding==0?1:2);check(path.replaceWithData(bytes.getData(),bytes.getDataSize()),"Cannot write embedded encoding NUL");
            check(rejects([&]{PresetManager::load(path);}),"Encoding decoder discarded NUL before preflight");}
    }
    for(bool json:{false,true}){auto windows=root.createCopy();windows.getChild(1).setProperty("name","WINDOWS_TEXT",nullptr);
        const auto text=json?juce::JSON::toString(PresetManager::toJson(windows),true):windows.createXml()->toString();
        auto raw=text.toStdString();const auto offset=raw.find("WINDOWS_TEXT");check(offset!=std::string::npos,"Missing Windows-1252 fixture marker");
        raw.replace(offset,12,std::string("caf")+char(0xe9)+" "+char(0x80));const auto path=files.file(json?"windows.json":"windows.xml");
        check(path.replaceWithData(raw.data(),raw.size()),"Cannot write Windows-1252 fixture");
        check(PresetManager::load(path).tracks[0].name==juce::String::fromUTF8("caf\xc3\xa9 \xe2\x82\xac"),"Legacy unmarked Windows-1252 fallback changed");}
    auto invalid=snapshot;invalid.tracks[0].slots[0].native=*fixtureSnapshot().native;
    for(const char* extension:{"xml","json"}){const auto destination=files.file("preserve").withFileExtension(extension);check(destination.replaceWithText("KEEP"),"Cannot create preset sentinel");
        check(rejects([&]{PresetManager::save(invalid,destination);})&&destination.loadFileAsString()=="KEEP","Invalid resource save replaced existing preset");}
}
void offlineConservation(const Files& files){
    auto state=legacySnapshot();const auto source=files.file("source.wav");
    {auto stream=source.createOutputStream();check(stream!=nullptr,"Cannot create export source");juce::WavAudioFormat format;
        std::unique_ptr<juce::AudioFormatWriter> writer(format.createWriterFor(stream.get(),48000,2,24,{},0));check(writer!=nullptr,"Cannot encode export source");stream.release();
        juce::AudioBuffer<float> audio(2,64);audio.clear();audio.setSample(0,0,.1f);audio.setSample(1,0,.1f);check(writer->writeFromAudioSampleBuffer(audio,0,64),"Cannot write export source");}
    state.tracks[0].source=source;const auto valid=files.file("valid-output.wav");
    check(OfflineMixRenderer::render(state,valid,{48000,256,0,true}).status.wasOk()&&valid.existsAsFile(),"Positive scalar export control failed");
    state.tracks[0].slots[0].native=*fixtureSnapshot().native;
    const auto destination=files.file("no-output.wav");const auto result=OfflineMixRenderer::render(state,destination,{48000,256,0,true});
    check(result.status.failed()&&!destination.exists(),"Invalid native export created destination");
    const auto retained=files.file("retained.wav");check(retained.replaceWithText("KEEP"),"Cannot create destination sentinel");
    check(OfflineMixRenderer::render(state,retained,{48000,256,0,true}).status.failed()&&retained.loadFileAsString()=="KEEP","Failed native export overwrote destination");
    // Deep-copy and early failure are synchronous; worker must not own or mutate
    // the caller's aliased ValueTree even when malformed input is rejected.
    state.tracks[0].slots[0].parameters.addChild(juce::ValueTree("NATIVE_RESOURCE"),-1,nullptr);
    OfflineMixWorker worker([](const juce::String&){});check(!worker.start(state,destination,{48000,256,0,true})&&!worker.isBusy(),"Malformed worker snapshot started processing");
}
}
int main(){juce::ScopedJuceInitialiser_GUI gui;try{Files files;legacyContracts();legacyBytes(files);providerHelpers();preparedPublicationGate();actualCaptureRoutes();binaryHelpers();newJsonTypes();sessionRejections(files);legacyFileEncodings(files);offlineConservation(files);
    std::cout<<"PASS "<<assertions<<" assertions: verified009 35-ID/type/parameter contracts; exact legacy V1/V2 XML/JSON bytes; unchanged convolution fields/binary; unregistered provider dispatch/busy/coherence/discard/deep-copy/binary; native schema/type/version rejections; real worker/live-bank and export destination conservation. No real provider registration or engine/audio acceptance claimed.\n";return 0;
}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<" after "<<assertions<<" assertions\n";return 1;}}
