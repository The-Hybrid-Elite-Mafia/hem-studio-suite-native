#include "../Source/Systems/TakeRecorder.h"
#include "../Source/Systems/NoReplaceFile.h"
#include <iostream>
#include <stdexcept>
#include <cstdlib>
#include <new>
#include <thread>

static thread_local bool callback=false;
static std::atomic<int> allocations{0};
void* operator new(std::size_t n){if(callback)++allocations;if(void* p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void* operator new[](std::size_t n){return ::operator new(n);}
void operator delete(void* p) noexcept{std::free(p);}
void operator delete[](void* p) noexcept{std::free(p);}
void operator delete(void* p,std::size_t) noexcept{std::free(p);}
void operator delete[](void* p,std::size_t) noexcept{std::free(p);}
static void require(bool b,const char* text){if(!b)throw std::runtime_error(text);}

namespace {
using Recorder=hem::TakeRecorder;
namespace publication=hem::noReplaceFile;
constexpr int blockFrames=256,blocks=40,totalFrames=blockFrames*blocks;

struct Fixture {
    juce::File directory=juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getChildFile("hem-take-publication-"+juce::Uuid().toString());
    juce::File wav=directory.getChildFile("vocal.wav");
    juce::File sidecar=directory.getChildFile("vocal.wav.take.json");
    Fixture(){require(directory.createDirectory().wasOk(),"Cannot create fixture directory");}
    ~Fixture(){directory.deleteRecursively();}
    Recorder::Setup setup(int channels=1) const {
        Recorder::Setup s;s.file=wav;s.channels=channels;s.rate=96000;
        s.inputLatency=128;s.outputLatency=128;s.backingLatency=5088;
        s.inputName="Synthetic input";return s;
    }
    juce::Array<juce::File> partials() const {
        return directory.findChildFiles(juce::File::findFiles,false,"*.partial-*.wav");
    }
    juce::Array<juce::File> metadataPartials() const {
        return directory.findChildFiles(juce::File::findFiles,false,"*.take.json.partial-*");
    }
};

void waitForFinish(Recorder& recorder){
    for(int i=0;i<2500&&recorder.busy();++i)juce::Thread::sleep(2);
    require(!recorder.busy(),"Take worker timed out");
}
void arm(Recorder& recorder,const Recorder::Setup& setup){
    require(recorder.start(setup),"Cannot start take");
    for(int i=0;i<2500&&recorder.state()==Recorder::State::Opening;++i)juce::Thread::sleep(2);
    if(recorder.state()!=Recorder::State::Armed)std::cerr<<recorder.result()<<'\n';
    require(recorder.state()==Recorder::State::Armed,"Cannot arm take");
}
std::array<float,blockFrames> samples(){
    std::array<float,blockFrames> result{};
    for(int i=0;i<blockFrames;++i)result[size_t(i)]=.2f*std::sin(float(i)*.1f);
    return result;
}
void pushAudio(Recorder& recorder){
    const auto left=samples();auto right=left;for(auto& x:right)x=-x;
    for(int i=0;i<blocks;++i){
        callback=true;recorder.push(left.data(),right.data(),blockFrames,10.+i*blockFrames/96000.);callback=false;
    }
}
void stopAndWait(Recorder& recorder){recorder.stop();waitForFinish(recorder);}
void verifyWav(const juce::File& file,int channels){
    juce::WavAudioFormat format;
    auto reader=std::unique_ptr<juce::AudioFormatReader>(format.createReaderFor(file.createInputStream().release(),true));
    require(reader&&reader->sampleRate==96000&&reader->numChannels==unsigned(channels)
        &&reader->lengthInSamples==totalFrames&&reader->bitsPerSample==32&&reader->usesFloatingPointData,
        "Incorrect WAV format or frame count");
    juce::AudioBuffer<float> decoded(channels,totalFrames);
    require(reader->read(&decoded,0,totalFrames,0,true,true),"Cannot read take");
    const auto left=samples();
    for(int i=0;i<totalFrames;++i){
        require(std::abs(decoded.getSample(0,i)-left[size_t(i%blockFrames)])<1.e-7,"Dry vocal samples changed");
        if(channels==2)require(std::abs(decoded.getSample(1,i)+left[size_t(i%blockFrames)])<1.e-7,"Stereo print samples changed");
    }
}
void verifyMetadata(const juce::File& file,int channels){
    const auto json=juce::JSON::parse(file);
    require(int(json["frames"])==totalFrames&&int(json["channels"])==channels,"Metadata frame/channel mismatch");
    require(double(json["sampleRate"])==96000.,"Metadata rate mismatch");
    require(std::abs(double(json["estimatedPlacementSeconds"])-(10.-5344./96000))<1.e-9,"Latency metadata mismatch");
}
void verifySentinel(const juce::File& file,const juce::String& contents){
    juce::MemoryBlock actual;
    const auto bytes=contents.getNumBytesAsUTF8();
    const bool identical=file.loadFileAsData(actual)&&actual.getSize()==bytes
        &&std::memcmp(actual.getData(),contents.toRawUTF8(),bytes)==0;
    if(!identical)std::cerr<<file.getFullPathName()<<": expected "<<bytes<<" bytes, found "<<actual.getSize()<<'\n';
    require(identical,"Existing file bytes changed");
}
void writeSentinel(const juce::File& file,const juce::String& contents){
    // replaceWithText defaults to CRLF, even on Linux. Use raw UTF-8 bytes so
    // the fixture itself cannot alter newlines before the preservation test.
    require(file.replaceWithData(contents.toRawUTF8(),contents.getNumBytesAsUTF8()),"Cannot create competing file");
    verifySentinel(file,contents);
}
bool makeDanglingLink(const juce::File& link,const juce::File& target){
   #if JUCE_WINDOWS
    // This workspace already established that the privilege is unavailable.
    // Allow the approved suite to retain the same four explicit skips without
    // retrying a security-denied operation or changing machine policy.
    if(juce::SystemStats::getEnvironmentVariable("HEM_SKIP_KNOWN_SYMLINK_PRIVILEGE",{})=="1"){
        std::cout<<"SKIP: previously established Windows symlink privilege unavailable; no security retry\n";return false;
    }
   #endif
    std::error_code error;
    std::filesystem::create_symlink(publication::nativePath(target),publication::nativePath(link),error);
   #if JUCE_WINDOWS
    if(error){std::cout<<"SKIP: Windows symlink privilege unavailable: "<<error.message()<<'\n';return false;}
   #endif
    require(!error,"Cannot create dangling symlink fixture");return true;
}
void verifyDanglingLink(const juce::File& link,const juce::File& target){
    std::error_code error;
    require(std::filesystem::is_symlink(std::filesystem::symlink_status(publication::nativePath(link),error))&&!error,
        "Existing dangling symlink was replaced");
    require(std::filesystem::read_symlink(publication::nativePath(link))==publication::nativePath(target),"Symlink target changed");
    require(!target.exists(),"Dangling symlink target was created");
}

void normalTakes(){
    auto recorder=std::make_unique<Recorder>(); // Also cover reuse after completion.
    for(int channels:{1,2}){
        Fixture fixture;arm(*recorder,fixture.setup(channels));pushAudio(*recorder);stopAndWait(*recorder);
        if(recorder->state()!=Recorder::State::Complete)std::cerr<<recorder->result()<<'\n';
        require(recorder->state()==Recorder::State::Complete,"Take did not finalize");
        require(recorder->completedFile()==fixture.wav,"Completed take path missing");
        verifyWav(fixture.wav,channels);verifyMetadata(fixture.sidecar,channels);
        require(fixture.partials().isEmpty()&&fixture.metadataPartials().isEmpty(),"Successful take left a partial file");
    }
}
void preexistingDestination(bool danglingLink){
    Fixture fixture;const auto absent=fixture.directory.getChildFile("missing-source.wav");
    const juce::String sentinel="ORIGINAL WAV bytes must survive\n";
    if(danglingLink){if(!makeDanglingLink(fixture.wav,absent))return;}else writeSentinel(fixture.wav,sentinel);
    auto recorder=std::make_unique<Recorder>();require(recorder->start(fixture.setup()),"Cannot start rejection test");waitForFinish(*recorder);
    require(recorder->state()==Recorder::State::Failed,"Existing destination was not rejected");
    require(recorder->result().contains("already exists"),"Existing destination rejection was unclear");
    require(recorder->completedFile()==juce::File()&&recorder->framesWritten()==0,"Rejected take reported audio saved");
    require(fixture.partials().isEmpty()&&!fixture.sidecar.exists(),"Preflight rejection created take data");
    if(danglingLink)verifyDanglingLink(fixture.wav,absent);else verifySentinel(fixture.wav,sentinel);
}
void armedDestinationConflict(bool danglingLink){
    Fixture fixture;auto recorder=std::make_unique<Recorder>();arm(*recorder,fixture.setup(2));pushAudio(*recorder);
    // Another writer claims the destination after recorder preflight, before
    // finalization. Every byte of our recorded take must remain recoverable.
    const auto absent=fixture.directory.getChildFile("missing-raced-source.wav");
    const juce::String sentinel="RACING WAV owner keeps these bytes\n";
    if(danglingLink){if(!makeDanglingLink(fixture.wav,absent)){stopAndWait(*recorder);return;}}else writeSentinel(fixture.wav,sentinel);
    stopAndWait(*recorder);
    require(recorder->state()==Recorder::State::Failed,"Raced destination did not fail publication");
    require(recorder->completedFile()==juce::File(),"Unpublished take claimed completion");
    require(recorder->result().contains("without overwriting")&&recorder->result().contains("partial WAV retained"),"Publication failure did not explain recovery");
    require(recorder->framesWritten()==totalFrames,"Publication conflict lost frame count");
    const auto partials=fixture.partials();require(partials.size()==1,"Publication conflict did not retain one partial WAV");
    require(recorder->result().contains(partials[0].getFullPathName()),"Partial recovery path missing");
    verifyWav(partials[0],2);require(!fixture.sidecar.exists(),"Failed WAV publication wrote metadata");
    if(danglingLink)verifyDanglingLink(fixture.wav,absent);else verifySentinel(fixture.wav,sentinel);
}
void sidecarConflict(bool createWhileArmed,bool danglingLink){
    Fixture fixture;auto recorder=std::make_unique<Recorder>();
    const auto absent=fixture.directory.getChildFile("missing-metadata.json");
    const juce::String sentinel="{\"owner\":\"previous take, preserve byte for byte\"}\n";
    const auto makeConflict=[&]{if(danglingLink)return makeDanglingLink(fixture.sidecar,absent);writeSentinel(fixture.sidecar,sentinel);return true;};
    if(!createWhileArmed&&!makeConflict())return;
    arm(*recorder,fixture.setup());pushAudio(*recorder);
    if(createWhileArmed&&!makeConflict()){stopAndWait(*recorder);return;}
    stopAndWait(*recorder);
    require(recorder->state()==Recorder::State::Complete&&recorder->completedFile()==fixture.wav,"Saved WAV was incorrectly reported as failed");
    require(recorder->result().startsWith("Saved vocal.wav")&&recorder->result().contains("placement metadata not saved"),"Metadata conflict concealed saved WAV or missing metadata");
    verifyWav(fixture.wav,1);
    if(danglingLink)verifyDanglingLink(fixture.sidecar,absent);else verifySentinel(fixture.sidecar,sentinel);
    const auto retained=fixture.metadataPartials();require(retained.size()==1,"Conflicting metadata was not retained for recovery");
    verifyMetadata(retained[0],1);require(recorder->result().contains(retained[0].getFullPathName()),"Metadata recovery path missing");
    require(fixture.partials().isEmpty(),"Saved WAV retained an unnecessary audio duplicate");
}
void finalCommitConflict(){
    Fixture fixture;const auto source=fixture.directory.getChildFile("staged.wav");
    writeSentinel(source,"NEW CLOSED WAV");std::error_code error;
    require(publication::available(fixture.wav,error)&&!error,"Unexpected occupied commit fixture");
    // Deterministically occupy the name in the exact check/commit window that
    // JUCE moveFileTo used to clobber. This is the recorder's actual commit API.
    writeSentinel(fixture.wav,"WINNER AFTER FINAL CHECK");
    require(bool(publication::publish(source,fixture.wav)),"No-replace commit overwrote a race winner");
    verifySentinel(fixture.wav,"WINNER AFTER FINAL CHECK");verifySentinel(source,"NEW CLOSED WAV");
}
void simultaneousCommits(){
    Fixture fixture;const auto a=fixture.directory.getChildFile("a.wav"),b=fixture.directory.getChildFile("b.wav");
    writeSentinel(a,"FIRST COMPLETE TAKE");writeSentinel(b,"SECOND COMPLETE TAKE");
    std::atomic<int> ready{0};std::atomic<bool> go{false};std::error_code first,second;
    const auto publish=[&](const juce::File& source,std::error_code& result){
        ready.fetch_add(1,std::memory_order_release);while(!go.load(std::memory_order_acquire))std::this_thread::yield();
        result=publication::publish(source,fixture.wav);
    };
    std::thread one([&]{publish(a,first);}),two([&]{publish(b,second);});
    while(ready.load(std::memory_order_acquire)!=2)std::this_thread::yield();
    go.store(true,std::memory_order_release);one.join();two.join();
    require(bool(first)!=bool(second),"Atomic publication did not select exactly one winner");
    verifySentinel(fixture.wav,first?"SECOND COMPLETE TAKE":"FIRST COMPLETE TAKE");
    verifySentinel(a,"FIRST COMPLETE TAKE");verifySentinel(b,"SECOND COMPLETE TAKE");
}
void emptyTake(){
    Fixture fixture;auto recorder=std::make_unique<Recorder>();arm(*recorder,fixture.setup());stopAndWait(*recorder);
    require(recorder->state()==Recorder::State::Failed&&recorder->result().contains("No input frames"),"Empty take did not fail clearly");
    require(!fixture.wav.exists()&&!fixture.sidecar.exists()&&fixture.partials().isEmpty(),"Empty take left output files");
}
} // namespace

int main(){try{
    normalTakes();preexistingDestination(false);preexistingDestination(true);
    armedDestinationConflict(false);armedDestinationConflict(true);
    sidecarConflict(false,false);sidecarConflict(true,false);
    sidecarConflict(false,true);sidecarConflict(true,true);
    finalCommitConflict();simultaneousCommits();emptyTake();
    require(allocations==0,"Audio capture allocated memory");
    std::cout<<"PASS: mono/stereo float WAV, exact frames/samples/placement, allocation-free push, existing and raced destination/sidecar preservation, dangling symlinks, retained recovery files, atomic competing commits, truthful saved-WAV metadata warnings, empty-take cleanup\n";
}catch(const std::exception& e){callback=false;std::cerr<<e.what()<<'\n';return 1;}return 0;}
