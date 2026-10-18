#include "../Source/DSP/DrumReplacerCore.h"
#include <iostream>
#include <limits>
#include <stdexcept>
#include <cstdlib>
#include <new>

static bool allocationProbe=false;
static size_t allocationCount=0,deallocationCount=0;
void* operator new(std::size_t n){if(allocationProbe)++allocationCount;if(void* p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void* operator new[](std::size_t n){return ::operator new(n);}
void operator delete(void* p) noexcept {if(p&&allocationProbe)++deallocationCount;std::free(p);}
void operator delete[](void* p) noexcept {::operator delete(p);}
void operator delete(void* p,std::size_t) noexcept {::operator delete(p);}
void operator delete[](void* p,std::size_t) noexcept {::operator delete(p);}

void require(bool x,const char* message){if(!x)throw std::runtime_error(message);}
using hem::drum::Core;using hem::drum::Bank;using hem::drum::Parameters;
int main(){try{
    for(double rate:{8000.,44100.,48000.,96000.,192000.}) {
        Core core;require(core.prepare(rate,{}),"Empty bank must prepare");
        const int latency=int(std::ceil(rate*.003));require(core.latencySamples()==latency,"Latency contract");
        std::vector<float> l(size_t(latency+64),0),r(l);l[0]=.5f;r[0]=-.25f;
        core.process(l.data(),r.data(),int(l.size()),{});
        for(size_t i=0;i<l.size();++i){require(l[i]==(i==size_t(latency)?.5f:0),"Missing bank dry left");require(r[i]==(i==size_t(latency)?-.25f:0),"Missing bank dry right");}
    }
    const double rate=48000;std::array<float,32> sample{},sampleR{};sample[0]=1;sampleR[0]=-.5f;
    Bank bank{};bank[0]={sample.data(),sampleR.data(),32,rate};Parameters params;params.velocityMatch=0;
    Core core;require(core.prepare(rate,bank),"Sample prepare");
    std::vector<float> l(4096),r(4096);l[0]=.25f;core.process(l.data(),r.data(),int(l.size()),params);
    require(l[144]==1 && r[144]==-.5f,"Wet onset aligns to 3ms; preserve sample stereo");
    require(core.telemetryOnAudioThread().acceptedHits==1,"One linked hit");
    require(core.telemetryOnAudioThread().startedLayers==1,"One layer");
    for(size_t i=0;i<l.size();++i)if(i!=144)require(l[i]==0&&r[i]==0,"Unexpected wet sample");
    // Four simultaneous layers; soloing a missing layer cannot suppress valid audio.
    bank[1]={sample.data(),nullptr,32,rate};require(core.prepare(rate,bank),"Two layers");
    params.solo[3]=true;std::fill(l.begin(),l.end(),0);std::fill(r.begin(),r.end(),0);r[0]=1;
    core.process(l.data(),r.data(),int(l.size()),params);require(l[144]==2&&r[144]==.5f,"Missing solo handling and mono duplication");
    params.solo[1]=true;require(core.prepare(rate,bank),"Reprepare");std::fill(l.begin(),l.end(),0);std::fill(r.begin(),r.end(),0);l[0]=1;
    core.process(l.data(),r.data(),int(l.size()),params);require(l[144]==1&&r[144]==1,"Solo valid layer only");
    // Refractory duration and scheduling must be independent of block boundaries.
    params={};params.retriggerMs=1;params.velocityMatch=1;bank[1]={};
    std::vector<float> input(4096),inputR(4096);for(int i=0;i<3800;i+=48){input[size_t(i)]=.25f;inputR[size_t(i)]=-.5f;}
    auto reference=input,referenceR=inputR;require(core.prepare(rate,bank),"Reference prepare");core.process(reference.data(),referenceR.data(),4096,params);
    require(reference[144]==.5f && referenceR[144]==-.25f,"Velocity uses linked onset peak");
    require(core.telemetryOnAudioThread().acceptedHits==80,"Exact retrigger boundary accepts every hit");
    for(int block:{1,17,64,128,257,1024}) {
        Core split;require(split.prepare(rate,bank),"Split prepare");auto a=input,b=inputR;
        for(int pos=0;pos<4096;pos+=block)split.process(a.data()+pos,b.data()+pos,std::min(block,4096-pos),params);
        require(a==reference&&b==referenceR,"Block partition changed scheduling/audio");
        require(split.telemetryOnAudioThread().acceptedHits==80,"Block partition changed hit count");
    }
    // Sustained maximum retrigger density, four long layers, deterministic
    // voice stealing: no callback allocation OR deallocation is permitted.
    std::vector<float> longSample(48000,.125f);Bank dense{};
    for(auto& slot:dense)slot={longSample.data(),nullptr,int(longSample.size()),rate};
    Core stressed,stressedAgain;require(stressed.prepare(rate,dense)&&stressedAgain.prepare(rate,dense),"Dense prepare");
    std::vector<float> stressL(48000),stressR(48000);
    for(int i=0;i<48000;i+=48)stressL[size_t(i)]=1;
    auto repeatL=stressL,repeatR=stressR;
    allocationProbe=true;
    stressed.process(stressL.data(),stressR.data(),48000,params);
    allocationProbe=false;
    require(allocationCount==0&&deallocationCount==0,"Callback heap allocation/deallocation");
    stressedAgain.process(repeatL.data(),repeatR.data(),48000,params);
    require(stressL==repeatL&&stressR==repeatR,"Voice stealing is nondeterministic");
    const auto stress=stressed.telemetryOnAudioThread();
    require(stress.acceptedHits==1000&&stress.stolenVoices>0,"Dense detector/stealing not exercised");
    require(stress.soundingVoices<=32&&stress.fadingVoices<=32,"Voice/tail bound");
    for(int i=0;i<48000;++i)require(std::isfinite(stressL[size_t(i)])&&std::isfinite(stressR[size_t(i)]),"Dense nonfinite output");
    bank[0].sampleRate=44100;require(!core.setBankStopped(bank),"Reject wrong prepared rate");bank[0].sampleRate=rate;
    sample[1]=std::numeric_limits<float>::quiet_NaN();require(!core.setBankStopped(bank),"Reject nonfinite resource");sample[1]=0;
    require(!core.prepare(std::numeric_limits<double>::infinity(),bank),"Reject infinite sample rate");
    require(core.setBankStopped({}),"Clear bank stopped");l.assign(512,0);r.assign(512,0);l[0]=std::numeric_limits<float>::infinity();r[0]=std::numeric_limits<float>::quiet_NaN();core.process(l.data(),r.data(),512,params);
    for(int i=0;i<512;++i)require(std::isfinite(l[size_t(i)])&&std::isfinite(r[size_t(i)]),"Nonfinite input escaped");
    std::cout<<"PASS Drum core: dry/wet latency, stereo layers, missing/valid solo, linked velocity, retrigger boundaries, block partition, resource/input rejection\n";return 0;
}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
