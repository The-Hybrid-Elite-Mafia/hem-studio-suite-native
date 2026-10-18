> Historical recovered design/status document. Current implementation and verification take precedence in README.md, INTEGRATION-STATUS.md and VERIFICATION-2026-10-01.md.

# Background systems — current v0.9 source

Host integration is in SOURCE-CODE.md; these classes retain the v0.8 behavior.

## Source/Systems/AnalysisView.cpp

```cpp
#include "AnalysisView.h"
namespace hem {
void AnalysisView::paint(juce::Graphics& g){
    g.fillAll(juce::Colour(0xff080e16));auto area=getLocalBounds().toFloat().reduced(20);
    g.setColour(juce::Colours::cyan);g.drawText(worker.isOperational()?"MASTER ANALYSIS / BEFORE MONITOR CORRECTION":"ANALYSIS WORKER UNAVAILABLE",area.removeFromTop(26),juce::Justification::centred);
    auto spectrum=area.removeFromTop(area.getHeight()*.55f).reduced(0,8);
    g.setColour(juce::Colour(0xff20414b));for(int i=0;i<5;++i)g.drawHorizontalLine(int(spectrum.getY()+i*spectrum.getHeight()/4),spectrum.getX(),spectrum.getRight());
    if(frame.sampleRate>0){for(int channel=0;channel<2;++channel){juce::Path path;const auto& values=channel==0?frame.leftDb:frame.rightDb;
        for(int x=0;x<int(spectrum.getWidth());++x){double hz=20*std::pow(std::min(20000.,frame.sampleRate*.5)/20.,double(x)/std::max(1.f,spectrum.getWidth()-1));int bin=juce::jlimit(0,AnalysisWorker::bins-1,int(hz*AnalysisWorker::fftSize/frame.sampleRate));
            const float y=spectrum.getBottom()-juce::jlimit(0.f,1.f,(values[size_t(bin)]+100)/100)*spectrum.getHeight();if(x==0)path.startNewSubPath(spectrum.getX(),y);else path.lineTo(spectrum.getX()+x,y);}
        g.setColour(channel==0?juce::Colours::cyan:juce::Colours::orange);g.strokePath(path,juce::PathStrokeType(1.3f));}}
    auto waveform=area.reduced(0,12);waveform.removeFromBottom(28);g.setColour(juce::Colours::cyan);
    for(int i=0;i<frame.validColumns;++i){const auto& p=frame.waveform[size_t(i)];float x=waveform.getX()+float(i)*waveform.getWidth()/std::max(1,frame.validColumns);
        const float low=std::min(p.lowL,p.lowR),high=std::max(p.highL,p.highR);g.drawVerticalLine(int(x),waveform.getCentreY()-juce::jlimit(-1.f,1.f,high)*waveform.getHeight()*.45f,waveform.getCentreY()-juce::jlimit(-1.f,1.f,low)*waveform.getHeight()*.45f);}
    g.setColour(juce::Colours::orange);g.drawText("8192-point stereo FFT / 64-frame peak buckets / dropped display frames: "+juce::String(frame.droppedFrames),getLocalBounds().removeFromBottom(30),juce::Justification::centred);
}
}

```

## Source/Systems/AnalysisView.h

```cpp
#pragma once
#include <JuceHeader.h>
#include "AnalysisWorker.h"
namespace hem {
// Lightweight native display. Drawing uses worker snapshots, never live audio
// pointers or FFT calls on the GUI thread.
class AnalysisView final : public juce::Component,private juce::Timer {
public:
    explicit AnalysisView(AnalysisWorker& w):worker(w){startTimerHz(20);setSize(900,520);}
    void paint(juce::Graphics&) override;
private:
    void timerCallback() override {if(worker.copyLatest(frame))repaint();}
    AnalysisWorker& worker;AnalysisWorker::Snapshot frame;
};
}

```

## Source/Systems/AnalysisWorker.cpp

```cpp
#include "AnalysisWorker.h"
namespace hem {
AnalysisWorker::AnalysisWorker():juce::Thread("HEM spectrum and waveform"){
    for(int i=0;i<fftSize;++i){window[size_t(i)]=float(.5-.5*std::cos(juce::MathConstants<double>::twoPi*i/(fftSize-1)));windowSum+=window[size_t(i)];}
    latest.leftDb.fill(-160);latest.rightDb.fill(-160);startThread();
}
AnalysisWorker::~AnalysisWorker(){signalThreadShouldExit();notify();waitForThreadToExit(-1);}
void AnalysisWorker::prepare(double rate) noexcept {producerRate=rate;producerFrame=0;++producerEpoch;}
void AnalysisWorker::push(const float* l,const float* r,int count) noexcept {
    for(int offset=0;offset<count;offset+=packetSize){const int n=std::min(packetSize,count-offset);int a,n1,b,n2;fifo.prepareToWrite(1,a,n1,b,n2);
        if(n1+n2==0){drops.fetch_add(unsigned(n),std::memory_order_relaxed);producerFrame+=unsigned(n);continue;}
        auto& p=packets[size_t(n1?a:b)];p.size=n;p.rate=producerRate;p.epoch=producerEpoch;p.first=producerFrame;
        std::copy_n(l+offset,n,p.left.data());std::copy_n(r+offset,n,p.right.data());producerFrame+=unsigned(n);fifo.finishedWrite(1);
    }
}
bool AnalysisWorker::copyLatest(Snapshot& output) const {
    const juce::ScopedTryLock lock(snapshotLock);if(!lock.isLocked())return false;output=latest;return true;
}
void AnalysisWorker::resetHistory(){ringL.fill(0);ringR.fill(0);filled=write=hop=0;peakCount=peakWrite=bucketFill=0;accumulator={};}
void AnalysisWorker::consume(const Packet& p){
    if(p.epoch!=workerEpoch||p.first!=expectedFrame){resetHistory();gap=true;workerEpoch=p.epoch;}
    workerRate=p.rate;
    for(int i=0;i<p.size;++i){const float l=std::isfinite(p.left[size_t(i)])?p.left[size_t(i)]:0.f,r=std::isfinite(p.right[size_t(i)])?p.right[size_t(i)]:0.f;
        ringL[size_t(write)]=l;ringR[size_t(write)]=r;write=(write+1)%fftSize;filled=std::min(fftSize,filled+1);
        if(bucketFill==0)accumulator={l,l,r,r};
        else {accumulator.lowL=std::min(accumulator.lowL,l);accumulator.highL=std::max(accumulator.highL,l);accumulator.lowR=std::min(accumulator.lowR,r);accumulator.highR=std::max(accumulator.highR,r);}
        if(++bucketFill==bucketSize){peaks[size_t(peakWrite)]=accumulator;peakWrite=(peakWrite+1)%columns;peakCount=std::min(columns,peakCount+1);bucketFill=0;}
        expectedFrame=p.first+unsigned(i)+1;
        if(++hop>=fftSize/4&&filled==fftSize){hop=0;publish();}
    }
    expectedFrame=p.first+unsigned(p.size);
}
void AnalysisWorker::publish(){
    fftL.fill(0);fftR.fill(0);
    for(int i=0;i<fftSize;++i){const auto ix=size_t((write+i)%fftSize);fftL[size_t(i)]=ringL[ix]*window[size_t(i)];fftR[size_t(i)]=ringR[ix]*window[size_t(i)];}
    fft.performFrequencyOnlyForwardTransform(fftL.data(),true);fft.performFrequencyOnlyForwardTransform(fftR.data(),true);
    for(int i=0;i<bins;++i){const double scale=(i==0||i==bins-1?1.:2.)/windowSum;
        work.leftDb[size_t(i)]=float(20*std::log10(std::max(1.e-8,double(fftL[size_t(i)])*scale)));
        work.rightDb[size_t(i)]=float(20*std::log10(std::max(1.e-8,double(fftR[size_t(i)])*scale)));}
    work.validColumns=peakCount;for(int i=0;i<peakCount;++i)work.waveform[size_t(i)]=peaks[size_t((peakWrite-peakCount+i+columns)%columns)];
    work.sampleRate=workerRate;work.frameEnd=expectedFrame;work.sequence=++sequence;work.droppedFrames=drops.load();work.discontinuity=gap;gap=false;
    const juce::ScopedLock lock(snapshotLock);latest=work;
}
void AnalysisWorker::run(){
    while(!threadShouldExit()){
        int a,n1,b,n2;fifo.prepareToRead(1,a,n1,b,n2);
        if(n1+n2==0){wait(3);continue;}
        consume(packets[size_t(n1?a:b)]);fifo.finishedRead(1);
    }
}
}

```

## Source/Systems/AnalysisWorker.h

```cpp
#pragma once
#include <juce_core/juce_core.h>
#include <juce_dsp/juce_dsp.h>
#include <array>
namespace hem {
// One audio producer, one analysis consumer. No GUI lock, allocation, FFT or
// event notification occurs in push(). A slow analyser loses display packets,
// never audio. The GUI uses a non-blocking snapshot read.
class AnalysisWorker final : private juce::Thread {
public:
    static constexpr int fftOrder=13,fftSize=1<<fftOrder,bins=fftSize/2+1;
    static constexpr int columns=2048,bucketSize=64,packetSize=256,packetCount=128;
    struct Peak {float lowL=0,highL=0,lowR=0,highR=0;};
    struct Snapshot {
        std::array<float,bins> leftDb{},rightDb{};
        std::array<Peak,columns> waveform{}; // Oldest to newest; first validColumns entries.
        int validColumns=0;
        double sampleRate=0;
        std::uint64_t frameEnd=0,sequence=0;
        std::uint32_t droppedFrames=0;
        bool discontinuity=false;
    };
    AnalysisWorker();~AnalysisWorker() override;
    // Called with the audio callback excluded, e.g. prepareToPlay(). Only
    // producer metadata changes; worker FIFO/history is never reset here.
    bool isOperational() const {return isThreadRunning();}
    void prepare(double sampleRate) noexcept;
    void push(const float* left,const float* right,int count) noexcept;
    bool copyLatest(Snapshot&) const; // Message thread, returns false if busy.
private:
    struct Packet {std::array<float,packetSize> left{},right{};int size=0;double rate=0;std::uint64_t first=0;unsigned epoch=0;};
    void run() override;void consume(const Packet&);void publish();void resetHistory();
    juce::AbstractFifo fifo{packetCount};
    std::array<Packet,packetCount> packets;
    double producerRate=96000,workerRate=96000;unsigned producerEpoch=0,workerEpoch=0;
    std::uint64_t producerFrame=0,expectedFrame=0,sequence=0;
    std::atomic<std::uint32_t> drops{0};
    juce::dsp::FFT fft{fftOrder};
    std::array<float,fftSize> ringL{},ringR{},window{};
    std::array<float,2*fftSize> fftL{},fftR{};
    std::array<Peak,columns> peaks{};
    Peak accumulator;int bucketFill=0,peakWrite=0,peakCount=0,write=0,filled=0,hop=0;
    double windowSum=0;bool gap=false;
    mutable juce::CriticalSection snapshotLock;
    Snapshot latest,work;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AnalysisWorker)
};
}

```

## Source/Systems/MonitorProfile.cpp

```cpp
#include "MonitorProfile.h"
#include <stdexcept>
namespace hem {
namespace {
void require(bool ok,const char* text){if(!ok)throw std::runtime_error(text);}
double numeric(const juce::var& v,double low,double high){require(v.isInt()||v.isInt64()||v.isDouble(),"Calibration parameter must be numeric");double n=v;require(std::isfinite(n)&&n>=low&&n<=high,"Calibration parameter outside supported range");return n;}
const char* filterName(MonitorProfile::Filter t){return t==MonitorProfile::Filter::Bell?"bell":t==MonitorProfile::Filter::LowShelf?"lowShelf":"highShelf";}
}
MonitorProfile MonitorProfile::load(const juce::File& f){
    require(f.existsAsFile()&&f.getSize()>0&&f.getSize()<=65536,"Calibration file missing or larger than 64 KB");
    juce::var root;require(juce::JSON::parse(f.loadFileAsString(),root).wasOk()&&root.isObject(),"Invalid calibration JSON");
    require(numeric(root["version"],1,1)==1,"Unsupported calibration version");MonitorProfile p;
    p.name=root["name"].toString();p.device=root["device"].toString();p.connection=root["connection"].toString();p.deviceMode=root["deviceMode"].toString();p.measurementSource=root["measurementSource"].toString();
    require(p.name.isNotEmpty()&&p.device.isNotEmpty()&&p.connection.isNotEmpty()&&p.deviceMode.isNotEmpty()&&p.measurementSource.isNotEmpty(),"Calibration must identify device, connection, mode and measurement source");
    p.preampDb=float(numeric(root["preampDb"],-36,0));auto list=root["bands"];auto* bands=list.getArray();require(bands&&bands->size()<=MonitoringRouter::maxBands,"Calibration supports up to twelve bands");
    for(const auto& b:*bands){require(b.isObject(),"Invalid calibration band");auto type=b["type"].toString();require(type=="bell"||type=="lowShelf"||type=="highShelf","Unsupported calibration filter");
        p.bands.push_back({type=="bell"?Filter::Bell:type=="lowShelf"?Filter::LowShelf:Filter::HighShelf,float(numeric(b["hz"],10,40000)),float(numeric(b["q"],.1,10)),float(numeric(b["db"],-18,12))});}
    return p;
}
MonitoringRouter::Curve MonitorProfile::prepare(double sampleRate) const {
    require(std::isfinite(sampleRate)&&sampleRate>=8000&&sampleRate<=384000,"Unsupported calibration sample rate");
    require(bands.size()<=MonitoringRouter::maxBands&&std::isfinite(preampDb)&&preampDb>=-36&&preampDb<=0,"Invalid calibration headroom");
    MonitoringRouter::Curve curve;curve.count=int(bands.size());curve.preamp=gain(preampDb);
    for(size_t i=0;i<bands.size();++i){const auto& b=bands[i];require(std::isfinite(b.hz)&&std::isfinite(b.q)&&std::isfinite(b.db)&&b.hz>=10&&b.hz<sampleRate*.49&&b.q>=.1&&b.q<=10&&b.db>=-18&&b.db<=12,"Calibration band invalid for current sample rate");auto& f=curve.filters[i];
        require(b.hz<=40000&&(b.type==Filter::Bell||b.type==Filter::LowShelf||b.type==Filter::HighShelf),"Invalid calibration filter type/frequency");
        // Compute directly: no legacy frequency clamping or fixed shelf Q.
        const double a=std::pow(10.,b.db/40.),w=2*pi*b.hz/sampleRate,c=std::cos(w),alpha=std::sin(w)/(2*b.q),r=2*std::sqrt(a)*alpha;
        double a0=1;
        if(b.type==Filter::Bell){
            f.b0=1+alpha*a;f.b1=-2*c;f.b2=1-alpha*a;a0=1+alpha/a;f.a1=-2*c;f.a2=1-alpha/a;
        }else if(b.type==Filter::LowShelf){
            f.b0=a*((a+1)-(a-1)*c+r);f.b1=2*a*((a-1)-(a+1)*c);f.b2=a*((a+1)-(a-1)*c-r);
            a0=(a+1)+(a-1)*c+r;f.a1=-2*((a-1)+(a+1)*c);f.a2=(a+1)+(a-1)*c-r;
        }else{
            f.b0=a*((a+1)+(a-1)*c+r);f.b1=-2*a*((a-1)+(a+1)*c);f.b2=a*((a+1)+(a-1)*c-r);
            a0=(a+1)-(a-1)*c+r;f.a1=2*((a-1)-(a+1)*c);f.a2=(a+1)-(a-1)*c-r;
        }
        f.b0/=a0;f.b1/=a0;f.b2/=a0;f.a1/=a0;f.a2/=a0;
    }
    return curve;
}
void MonitorProfile::save(const MonitorProfile& p,const juce::File& file){
    p.prepare(96000);auto* object=new juce::DynamicObject();juce::var root(object);object->setProperty("version",1);
    object->setProperty("name",p.name);object->setProperty("device",p.device);object->setProperty("connection",p.connection);object->setProperty("deviceMode",p.deviceMode);object->setProperty("measurementSource",p.measurementSource);object->setProperty("preampDb",p.preampDb);
    require(p.name.isNotEmpty()&&p.device.isNotEmpty()&&p.connection.isNotEmpty()&&p.deviceMode.isNotEmpty()&&p.measurementSource.isNotEmpty(),"Calibration metadata is incomplete");
    juce::Array<juce::var> bands;for(const auto& b:p.bands){auto* v=new juce::DynamicObject();v->setProperty("type",filterName(b.type));v->setProperty("hz",b.hz);v->setProperty("q",b.q);v->setProperty("db",b.db);bands.add(juce::var(v));}object->setProperty("bands",bands);
    juce::TemporaryFile temporary(file);require(temporary.getFile().replaceWithText(juce::JSON::toString(root,false))&&temporary.overwriteTargetFileWithTemporary(),"Cannot save calibration profile");
}
MonitorProfileLoader::MonitorProfileLoader(Ready r,Error e):juce::Thread("HEM calibration loader"),ready(std::move(r)),errorHandler(std::move(e)){startTimerHz(15);}
MonitorProfileLoader::~MonitorProfileLoader(){stopTimer();signalThreadShouldExit();waitForThreadToExit(-1);}
bool MonitorProfileLoader::load(const juce::File& f,double sr,unsigned gen){if(busy.load())return false;file=f;rate=sr;generation=gen;error.clear();done=false;busy=true;if(!startThread()){busy=false;errorHandler("Cannot start calibration worker");return false;}return true;}
void MonitorProfileLoader::run(){try{profile=MonitorProfile::load(file);curve=profile.prepare(rate);}catch(const std::exception& e){error=e.what();}done.store(true,std::memory_order_release);}
void MonitorProfileLoader::timerCallback(){if(!done.load(std::memory_order_acquire)||isThreadRunning())return;done=false;busy=false;if(error.isNotEmpty())errorHandler(error);else ready(profile,curve,generation);}
}

```

## Source/Systems/MonitorProfile.h

```cpp
#pragma once
#include <juce_events/juce_events.h>
#include "../DSP/MonitoringRouter.h"
namespace hem {
struct MonitorProfile {
    enum class Filter {Bell,LowShelf,HighShelf};
    struct Band {Filter type=Filter::Bell;float hz=1000,q=.7071f,db=0;};
    juce::String name,device,connection,deviceMode,measurementSource;
    float preampDb=0;
    std::vector<Band> bands;
    static MonitorProfile load(const juce::File&);
    static void save(const MonitorProfile&,const juce::File&);
    MonitoringRouter::Curve prepare(double sampleRate) const;
};
// JSON I/O and coefficient generation stay away from GUI/audio callbacks.
class MonitorProfileLoader final : private juce::Thread,private juce::Timer {
public:
    using Ready=std::function<void(const MonitorProfile&,const MonitoringRouter::Curve&,unsigned)>;
    using Error=std::function<void(const juce::String&)>;
    MonitorProfileLoader(Ready,Error);~MonitorProfileLoader() override;
    bool load(const juce::File&,double sampleRate,unsigned deviceGeneration);
    bool isBusy() const noexcept {return busy.load();}
private:
    void run() override;void timerCallback() override;
    Ready ready;Error errorHandler;
    juce::File file;double rate=96000;unsigned generation=0;
    MonitorProfile profile;MonitoringRouter::Curve curve;juce::String error;
    std::atomic<bool> busy{false},done{false};
};
}

```

## Source/Systems/PresetManager.cpp

```cpp
#include "PresetManager.h"
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
    auto type=object->getProperty("type").toString();
    check(type=="HEM_SESSION"||type=="MONITOR"||type=="TRACK"||type=="SLOT"||type=="PARAMETERS"||type=="PARAM","Unknown preset JSON node");
    auto* properties=object->getProperty("properties").getDynamicObject();check(properties!=nullptr,"Preset JSON properties missing");
    auto children=object->getProperty("children");auto* array=children.getArray();check(array!=nullptr&&array->size()<=128,"Invalid preset JSON children");
    check(properties->getProperties().size()<=32,"Too many preset properties");
    juce::ValueTree result{juce::Identifier(type)};
    for(const auto& entry:properties->getProperties()){
        const auto& v=entry.value;check(v.isString()||v.isInt()||v.isInt64()||v.isDouble()||v.isBool(),"Preset property is not a scalar");
        check(!v.isDouble()||std::isfinite(double(v)),"Preset contains non-finite value");result.setProperty(entry.name,v,nullptr);
    }
    for(const auto& child:*array)result.addChild(fromJson(child,depth+1),-1,nullptr);
    return result;
}
void PresetManager::save(const SessionSnapshot& snapshot,const juce::File& file){
    check(supported(file),"Use an .xml or .json preset extension");
    if(file.hasFileExtension("xml")){SessionState::save(snapshot,file);return;}
    const auto text=juce::JSON::toString(toJson(SessionState::encode(snapshot,file)),false);
    juce::TemporaryFile temporary(file);
    check(temporary.getFile().replaceWithText(text),"Cannot write preset JSON");
    check(temporary.overwriteTargetFileWithTemporary(),"Cannot replace preset file");
}
SessionSnapshot PresetManager::load(const juce::File& file){
    check(supported(file),"Use an .xml or .json preset extension");
    SessionSnapshot snapshot;
    if(file.hasFileExtension("xml"))snapshot=SessionState::load(file);
    else {
        check(file.existsAsFile()&&file.getSize()>0&&file.getSize()<=8*1024*1024,"Preset missing, empty or larger than 8 MB");
        juce::var document;auto parsed=juce::JSON::parse(file.loadFileAsString(),document);
        check(parsed.wasOk(),"Malformed JSON preset");snapshot=SessionState::decode(fromJson(document),file);
    }
    // Both formats reject invalid plugin parameter states before any host commit.
    for(const auto& t:snapshot.tracks)for(const auto& s:t.slots)if(s.pluginId.isNotEmpty()){
        auto processor=PluginRegistry::create(s.pluginId);check(processor!=nullptr,"Unknown preset plugin");
        SessionState::restoreParameters(*processor,s.parameters);
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

```

## Source/Systems/PresetManager.h

```cpp
#pragma once
#include "../SessionState.h"

namespace hem {
// Disk operations run on SessionWorker (or another non-audio worker).
// XML and JSON represent the SAME validated 13 x 8 session schema.
class PresetManager final {
public:
    static void save(const SessionSnapshot&,const juce::File&);
    static SessionSnapshot load(const juce::File&);
    static juce::Array<juce::File> list(const juce::File& directory);
    static juce::Result rename(const juce::File&,const juce::String& newBaseName);
    static juce::Result remove(const juce::File&);
    static juce::var toJson(const juce::ValueTree&);
    static juce::ValueTree fromJson(const juce::var&,int depth=0);
private:
    static bool supported(const juce::File&);
};
}

```

## Source/DSP/MonitoringRouter.h

```cpp
#pragma once
#include "DspCore.h"
#include <stdexcept>
namespace hem {
// Fixed-size prepared coefficients are built away from the audio thread.
// Device routing itself belongs to AudioDeviceManager; this class selects the
// listening signal for that device, never a Bluetooth codec or OS endpoint.
class MonitoringRouter final {
public:
    static constexpr int maxBands=12;
    struct Curve {std::array<Biquad,maxBands> filters;int count=0;float preamp=1;};
    void prepare(double rate) {
        active={};incoming={};transitioning=false;
        profileMix.reset(rate,0);profileMix.length=std::max(1,int(rate*.02));
        routeMix.reset(rate,0);routeMix.length=std::max(1,int(rate*.02));headphones.store(false);
    }
    // Host excludes the audio callback for this bounded struct copy. Reject
    // replacement during a fade, rather than resetting a live IIR mid-signal.
    bool installPrepared(const Curve& curve) noexcept {
        if(transitioning||curve.count<0||curve.count>maxBands)return false;
        incoming=curve;for(auto& f:incoming.filters)f.reset();transitioning=true;
        profileMix.value=profileMix.target=0;profileMix.remaining=0;profileMix.set(1);return true;
    }
    void setHeadphoneMode(bool enabled) noexcept {headphones.store(enabled,std::memory_order_relaxed);}
    bool headphoneMode() const noexcept {return headphones.load(std::memory_order_relaxed);}
    // Step 1: create the listening branch without modifying the master input.
    void renderCalibration(const float* left,const float* right,float* outL,float* outR,int n) noexcept {
        for(int i=0;i<n;++i){
            const float cleanL=std::isfinite(left[i])?std::clamp(left[i],-1000000.f,1000000.f):0.f;
            const float cleanR=std::isfinite(right[i])?std::clamp(right[i],-1000000.f,1000000.f):0.f;
            float aL=cleanL*active.preamp,aR=cleanR*active.preamp;
            for(int b=0;b<active.count;++b){aL=active.filters[size_t(b)].tick(aL,0);aR=active.filters[size_t(b)].tick(aR,1);}
            if(transitioning){float bL=cleanL*incoming.preamp,bR=cleanR*incoming.preamp;
                for(int b=0;b<incoming.count;++b){bL=incoming.filters[size_t(b)].tick(bL,0);bR=incoming.filters[size_t(b)].tick(bR,1);}
                const float mix=profileMix.next();aL+=mix*(bL-aL);aR+=mix*(bR-aR);
                if(profileMix.remaining==0){active=incoming;transitioning=false;}}
            outL[i]=aL;outR[i]=aR;
        }
    }
    // Optional manual listening EQ may process outL/outR between these stages.
    // Step 2: direct mode really bypasses ALL listening correction. Final
    // MasterLimiter must run after this mix; exports consume the master branch.
    void mixRoute(const float* masterL,const float* masterR,float* outL,float* outR,int n) noexcept {
        routeMix.set(headphoneMode()?1.f:0.f);
        for(int i=0;i<n;++i){const float mix=routeMix.next();
            if(mix==0){outL[i]=masterL[i];outR[i]=masterR[i];}
            else {outL[i]=masterL[i]+mix*(outL[i]-masterL[i]);outR[i]=masterR[i]+mix*(outR[i]-masterR[i]);}}
    }
private:
    Curve active,incoming;
    Ramp profileMix,routeMix;
    bool transitioning=false;
    std::atomic<bool> headphones{false};
};
}

```
