#include "../Source/Plugins/PluginRegistry.h"
#include "../Source/Plugins/PluginMeterReadout.h"
#include <atomic>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace {
using namespace hem;
void require(bool value,const juce::String& message){if(!value)throw std::runtime_error(message.toStdString());}
void near(float actual,float expected,float tolerance,const char* message){require(std::abs(actual-expected)<=tolerance,message);}
void prepare(ModuleProcessor& p,double rate=48000,int block=256){p.setPlayConfigDetails(2,2,rate,block);p.prepareToPlay(rate,block);}
void parameter(ModuleProcessor& p,const char* id,float value){auto* v=p.state.getParameter(id);require(v!=nullptr,"Missing test parameter");v->setValueNotifyingHost(v->convertTo0to1(value));}
void process(ModuleProcessor& p,juce::AudioBuffer<float>& b){juce::MidiBuffer midi;p.processBlock(b,midi);}
void constant(juce::AudioBuffer<float>& b,float l,float r){for(int i=0;i<b.getNumSamples();++i){b.setSample(0,i,l);b.setSample(1,i,r);}}
float peak(const juce::AudioBuffer<float>& b){float result=0;for(int ch=0;ch<2;++ch)for(int i=0;i<b.getNumSamples();++i)result=std::max(result,std::abs(b.getSample(ch,i)));return result;}
juce::Rectangle<float> meterArea(ModuleProcessor& p){juce::Rectangle<float> area(16,42,928,77);return dynamic_cast<SurgicalEqProcessor*>(&p)?area.withTrimmedLeft(area.getWidth()*.52f+8):area;}
juce::Image raster(ModuleProcessor& p,int scale){
    std::unique_ptr<juce::AudioProcessorEditor> editor(p.createEditor());
    juce::Image image(juce::Image::ARGB,editor->getWidth()*scale,editor->getHeight()*scale,true);
    // Finish the native rendering context before inspecting or copying pixels.
    // CoreGraphics may defer writes until the Graphics context is destroyed.
    { juce::Graphics g(image);g.addTransform(juce::AffineTransform::scale(float(scale)));editor->paintEntireComponent(g,true); }
    return image;
}
juce::File artifacts;
void save(const juce::Image& image,const juce::String& name){if(artifacts==juce::File())return;auto out=artifacts.getChildFile(name+".png").createOutputStream();require(out!=nullptr,"Cannot write raster artifact");require(juce::PNGImageFormat().writeImageToStream(image,*out),"Cannot encode raster artifact");}
int fillPixels(const juce::Image& image,juce::Rectangle<float> track,int scale){int count=0;const int y=int(track.getCentreY()*float(scale));for(int x=int(track.getX()*float(scale));x<int(track.getRight()*float(scale));++x){const auto c=image.getPixelAt(x,y);if(c==juce::Colour(ui::pluginMeterPositiveColour)||c==juce::Colour(ui::pluginMeterNegativeColour))++count;}return count;}
void noBar(ModuleProcessor& p,const juce::String& name){for(int scale:{1,2}){const auto image=raster(p,scale);require(fillPixels(image,ui::pluginMeterTrack(meterArea(p)),scale)==0,name+" displays a fictitious level bar");save(image,name+"-"+juce::String(scale)+"x");}}
void signedRaster(ModuleProcessor& p,const juce::String& name){
    const auto readout=ui::pluginMeterReadout(p);require(readout.signedScale&&readout.available,"Expected available signed measurement");
    const auto track=ui::pluginMeterTrack(meterArea(p));
    for(int scale:{1,2}){
        const auto image=raster(p,scale);const int y=int(track.getCentreY()*float(scale));
        const auto fill=juce::Colour(readout.value<0?ui::pluginMeterNegativeColour:ui::pluginMeterPositiveColour);
        const float start=track.getX()+track.getWidth()*readout.fillStart(),end=track.getX()+track.getWidth()*readout.fillEnd();
        int first=-1,last=-1;
        for(int x=int(track.getX()*float(scale));x<int(track.getRight()*float(scale));++x)if(image.getPixelAt(x,y)==fill){if(first<0)first=x;last=x;}
        save(image,name+"-"+juce::String(scale)+"x-before-check");
        if(first<0&&readout.value!=0){
            std::cerr<<"raster="<<name<<" scale="<<scale<<" value="<<readout.value<<" expectedColour="<<fill.toString()
                     <<" track="<<track.toString()<<" probe="<<image.getPixelAt(int((start+end)*.5f*float(scale)),y).toString()<<"\n";
        }
        if(readout.value==0)require(first<0,"Signed zero drew a half-full bar");
        else {require(first>=0,"Signed measurement has no raster bar");require(std::abs(float(first)-start*float(scale))<=3*scale&&std::abs(float(last)-end*float(scale))<=3*scale,"Signed raster endpoints are not relative to zero");}
        const int zero=int(std::round((track.getX()+track.getWidth()*.5f)*float(scale)));
        require(image.getPixelAt(zero,y)==juce::Colour(ui::pluginMeterZeroColour),"Signed meter has no explicit zero-reference raster");
        for(int i=0;i<readout.tickCount;++i){const int x=int(std::round(track.getX()+track.getWidth()*readout.position(readout.ticks[size_t(i)])))*scale;require(image.getPixelAt(x,int(track.getBottom()+1)*scale)!=juce::Colour(0xff021018),"Signed scale tick is missing from actual raster");}
        save(image,name+"-"+juce::String(scale)+"x");
    }
}
void testLifecycleAndSilence(){
    for(const auto& entry:PluginRegistry::entries()){
        auto p=entry.create();
        auto readout=ui::pluginMeterReadout(*p);require(!readout.available&&!readout.snapshot.hasProcessedAudio&&readout.valueText=="NO PROCESSED AUDIO",juce::String(entry.id)+" claims audio before prepare");
        noBar(*p,juce::String(entry.id)+"-unprepared");
        prepare(*p);readout=ui::pluginMeterReadout(*p);require(!readout.snapshot.hasProcessedAudio,"prepare fabricated meter readiness");
        require(readout.snapshot.isPrepared&&readout.snapshot.sampleRate==48000&&readout.snapshot.latencySamples==p->getLatencySamples(),"Prepared metadata was not coherently published");
        noBar(*p,juce::String(entry.id)+"-prepared");
        juce::AudioBuffer<float> empty(2,0),mono(1,256);mono.clear();process(*p,empty);process(*p,mono);
        require(!p->meterSnapshot().hasProcessedAudio,"Skipped buffer fabricated processed audio");
        juce::AudioBuffer<float> b(2,256);b.clear();process(*p,b);
        auto snapshot=p->meterSnapshot();require(snapshot.hasProcessedAudio&&snapshot.processedBlocks==1&&snapshot.blockSamples==256,"Actual silent block did not publish readiness");
        require(!snapshot.signalPresent,"Actual silence reports signal activity");
        readout=ui::pluginMeterReadout(*p);
        if(p->meterKind()==PluginMeterKind::correlation)require(!readout.available&&readout.valueText.startsWith("UNDEFINED"),"Silent correlation is fabricated as zero");
        else {require(readout.available,"A valid zero/negative-infinity meter was suppressed");near(snapshot.value,0,1.e-9f,"Fresh silence has nonzero detector value");}
        noBar(*p,juce::String(entry.id)+"-silence");
        constant(b,.25f,.125f);process(*p,b);require(p->meterSnapshot().processedBlocks==2,"Processed block counter did not advance");
        process(*p,empty);require(p->meterSnapshot().processedBlocks==2,"Zero-length callback replaced last real block");
        prepare(*p,96000);require(!p->meterSnapshot().hasProcessedAudio&&!p->meterSnapshot().valueAvailable,"Reprepare exposed stale core meter");
        require(ui::pluginMeterReadout(*p).valueText=="NO PROCESSED AUDIO","Reprepare did not restore explicit waiting state");
        require(p->meterSnapshot().isPrepared&&p->meterSnapshot().sampleRate==96000&&p->meterSnapshot().latencySamples==p->getLatencySamples(),"Reprepare retained stale rate/latency metadata");
    }
}
void testCorrelation(){
    SipaProcessor p;prepare(p);juce::AudioBuffer<float> b(2,256);
    constant(b,.5f,.5f);process(p,b);near(p.meterSnapshot().value,1,1.e-6f,"Correlated signal does not measure +1");signedRaster(p,"correlation-positive");
    constant(b,.5f,-.5f);process(p,b);near(p.meterSnapshot().value,-1,1.e-6f,"Anticorrelated signal does not measure -1");signedRaster(p,"correlation-negative");
    for(int i=0;i<256;++i){const float left[]={.5f,0,-.5f,0},right[]={0,.5f,0,-.5f};b.setSample(0,i,left[i%4]);b.setSample(1,i,right[i%4]);}
    process(p,b);near(p.meterSnapshot().value,0,1.e-6f,"Orthogonal signal does not measure zero correlation");require(p.meterSnapshot().valueAvailable,"Measured zero correlation was treated as silence");signedRaster(p,"correlation-zero");
    constant(b,.5f,0);process(p,b);require(!p.meterSnapshot().valueAvailable,"One silent channel has a fabricated correlation");noBar(p,"correlation-one-channel");
    constant(b,1.e-7f,1.e-7f);process(p,b);require(!p.meterSnapshot().valueAvailable,"StereoCore denominator guard is not respected");
    constant(b,1.e-5f,1.e-5f);process(p,b);require(p.meterSnapshot().valueAvailable,"Usable quiet stereo energy was discarded");
}
void testSamplePeaks(){
    for(const auto& entry:PluginRegistry::entries()){
        auto p=entry.create();if(p->meterKind()!=PluginMeterKind::samplePeak)continue;prepare(*p);juce::AudioBuffer<float> b(2,512);
        constant(b,.5f,.25f);process(*p,b);const auto s=p->meterSnapshot();near(s.value,peak(b),1.e-6f,"Published sample peak differs from actual output");require(s.signalPresent&&s.valueAvailable,"Real sample peak not marked available");
    }
    MonitorProcessor p;prepare(p);juce::AudioBuffer<float> b(2,256);constant(b,.5f,.25f);process(p,b);
    const auto readout=ui::pluginMeterReadout(p);near(readout.value,-6.0206f,.0001f,"Half-scale output is not calibrated to -6.0206 dBFS");
    require(readout.minimum==-60&&readout.maximum==0,"Sample peak display scale changed");
    const auto track=ui::pluginMeterTrack(meterArea(p));
    for(int scale:{1,2}){const auto image=raster(p,scale);int last=-1;const int y=int(track.getCentreY()*float(scale));for(int x=int(track.getX()*float(scale));x<int(track.getRight()*float(scale));++x)if(image.getPixelAt(x,y)==juce::Colour(ui::pluginMeterPositiveColour))last=x;
        const float expected=(track.getX()+track.getWidth()*readout.position(readout.value))*float(scale);
        require(std::abs(float(last)-expected)<=2*scale,"Actual output-peak raster is not calibrated to -60..0 dBFS");
        for(int i=0;i<readout.tickCount;++i){const int x=int(std::round(track.getX()+track.getWidth()*readout.position(readout.ticks[size_t(i)])))*scale;require(image.getPixelAt(x,int(track.getBottom()+1)*scale)!=juce::Colour(0xff021018),"A dBFS raster tick is missing");}
        save(image,"sample-peak-half-scale-"+juce::String(scale)+"x");}
    constant(b,0,0);process(p,b);const auto silent=ui::pluginMeterReadout(p);require(silent.available&&std::isinf(silent.value)&&silent.value<0&&silent.valueText.contains("-inf dBFS"),"Silent peak is displayed as a fabricated finite floor");
    constant(b,2,1);process(p,b);const auto over=ui::pluginMeterReadout(p);near(over.value,6.0206f,.0001f,"Over-scale numeric peak was silently clamped");require(over.valueText.contains("ABOVE SCALE"),"Over-scale peak lacks explicit indication");
}
void testGainReduction(){
    VcaProcessor p;prepare(p);parameter(p,"threshold",-24);parameter(p,"ratio",4);parameter(p,"attack",1);
    juce::AudioBuffer<float> b(2,256);for(int i=0;i<50;++i){constant(b,.5f,.5f);process(p,b);}
    const auto s=p.meterSnapshot();const float expected=(20.f*std::log10(.5f)+24)*.75f;
    near(s.value,expected,.015f,"VCA GR does not match calibrated 4:1 compression");near(s.value,-20.f*std::log10(peak(b)/.5f),.015f,"VCA GR does not describe actual full-wet output attenuation");
    auto readout=ui::pluginMeterReadout(p);require(readout.minimum==0&&readout.maximum==24&&readout.fillStart()==0,"GR scale is not 0..24 dB from zero");
    for(int scale:{1,2}){const auto image=raster(p,scale);const auto track=ui::pluginMeterTrack(meterArea(p));near(float(fillPixels(image,track,scale)),track.getWidth()*readout.position(s.value)*float(scale),2.f*float(scale),"Actual GR bar width differs from measured reduction");save(image,"vca-real-gr-"+juce::String(scale)+"x");}
    b.clear();process(p,b);readout=ui::pluginMeterReadout(p);require(readout.available&&!readout.snapshot.signalPresent&&readout.value>0&&readout.valueText.contains("SILENT OUTPUT"),"Real detector release into silence is mislabeled");
}
void testSignedGain(){
    for(const float amount:{-12.f,0.f,12.f}){
        TransientProcessor p;prepare(p);parameter(p,"attack",amount);juce::AudioBuffer<float> b(2,1024);b.clear();process(p,b);constant(b,.25f,.25f);process(p,b);
        const auto readout=ui::pluginMeterReadout(p);float measured=0;for(int i=0;i<b.getNumSamples();++i){const float change=20.f*std::log10(b.getSample(0,i)/.25f);if(std::abs(change)>std::abs(measured))measured=change;}
        near(readout.value,measured,.001f,"Transient pre-mix envelope reading differs from actual full-wet gain");
        require(readout.minimum==-12&&readout.maximum==12&&readout.zeroPosition()==.5f,"Signed transient calibration lacks zero centre");
        require(readout.heading.contains("PRE-MIX")&&readout.heading.contains("PRE-OUTPUT"),"Transient meter suggests it includes mix/output trim");
        if(amount==0)require(readout.fillStart()==.5f&&readout.fillEnd()==.5f,"Zero gain has a half-full geometry");
        else require(readout.value*amount>0&&std::abs(readout.value)<=12,"Transient gain change lost its sign or bounds");
        signedRaster(p,"transient-"+juce::String(amount,0));
    }
}
void testFirEstimate(){
    TruePeakProcessor p;prepare(p,11025,256);parameter(p,"ceiling",-6);
    require(p.getLatencySamples()==56,"Limiter is not reporting ceil(rate*5ms) actual latency");
    auto readout=ui::pluginMeterReadout(p);require(readout.heading.contains("ESTIMATE")&&readout.heading.contains("8x, 32-TAP")&&readout.detail.contains("56 samples / 5.079 ms"),"Limiter uses an unqualified TP claim or hardcoded 5 ms latency");
    Reconstruction filter;std::array<std::array<float,Reconstruction::taps>,2> history{};int position=0;juce::AudioBuffer<float> b(2,256);
    float reference=0;for(int block=0;block<12;++block){for(int i=0;i<256;++i){const float sample=.95f*std::sin(float(2*pi*1000*(block*256+i)/11025));b.setSample(0,i,sample);b.setSample(1,i,-sample);}process(p,b);reference=0;
        for(int i=0;i<256;++i){for(int ch=0;ch<2;++ch){history[size_t(ch)][size_t(position)]=b.getSample(ch,i);for(const auto& phase:filter.h){double v=0;for(int k=0;k<Reconstruction::taps;++k)v+=phase[size_t(k)]*history[size_t(ch)][size_t((position-k+Reconstruction::taps)%Reconstruction::taps)];reference=std::max(reference,float(std::abs(v)));}}position=(position+1)%Reconstruction::taps;}
        near(p.meterSnapshot().value,reference,1.e-6f,"Limiter meter does not match the actual output's defined 8x FIR reconstruction");
    }
    readout=ui::pluginMeterReadout(p);require(readout.available&&readout.snapshot.auxiliary>0&&readout.valueText.contains("dBTP EST.")&&readout.valueText.contains("GR"),"Measured limiter estimate/reduction is missing");require(reference<=gain(-6)+1.e-5f,"Defined FIR estimate exceeds settled limiter ceiling");
    for(int scale:{1,2})save(raster(p,scale),"fir-estimate-"+juce::String(scale)+"x");
}
void testSnapshotCoherence(){
    MonitorProcessor p;prepare(p);juce::AudioBuffer<float> a(2,16),b(2,32);constant(a,.25f,.25f);constant(b,.5f,.5f);
    std::atomic<bool> done{false};std::atomic<bool> coherent{true};
    std::thread writer([&]{for(int i=1;i<=3000;++i)process(p,i%2?a:b);done.store(true);});
    while(!done.load()){const auto s=p.meterSnapshot();if(!s.hasProcessedAudio)continue;const bool odd=(s.processedBlocks%2)!=0;if(s.blockSamples!=(odd?16u:32u)||s.value!=(odd?.25f:.5f)||!s.valueAvailable||!s.signalPresent||!s.isPrepared||s.sampleRate!=48000||s.latencySamples!=0)coherent.store(false);}
    writer.join();require(coherent.load()&&p.meterSnapshot().processedBlocks==3000,"GUI observed a torn audio-thread telemetry snapshot");
}
void testReprepareMetadataCoherence(){
    TruePeakProcessor p;
    const auto initial=ui::pluginMeterReadout(p);
    require(!initial.snapshot.isPrepared&&initial.snapshot.sampleRate==0&&initial.snapshot.latencySamples==0
            &&initial.detail.contains("LATENCY UNAVAILABLE"),"Unprepared limiter invented latency metadata");
    std::atomic<bool> done{false},go{false},coherent{true};
    std::thread writer([&]{
        juce::AudioBuffer<float> silence(2,16);silence.clear();
        while(!go.load())std::this_thread::yield();
        for(int i=0;i<1200;++i){
            constexpr double rates[]{44100,96000,11025,48000};
            // Same serialized host writer reconfigures then processes. Only the
            // actual GUI readout runs concurrently, never core DSP with prepare.
            prepare(p,rates[i%4],16);process(p,silence);
        }
        done.store(true);
    });
    unsigned observations=0;go.store(true);
    while(!done.load()){
        ++observations;const auto readout=ui::pluginMeterReadout(p);const auto& s=readout.snapshot;
        if(!s.isPrepared){
            if(s.hasProcessedAudio||s.sampleRate!=0||s.latencySamples!=0||!readout.detail.contains("LATENCY UNAVAILABLE"))coherent.store(false);
            continue;
        }
        const bool knownRate=s.sampleRate==44100||s.sampleRate==96000||s.sampleRate==11025||s.sampleRate==48000;
        const int expectedLatency=std::max(Reconstruction::taps,int(std::ceil(s.sampleRate*.005)));
        const auto expectedText=juce::String(expectedLatency)+" samples / "+juce::String(1000.0*double(expectedLatency)/s.sampleRate,3)+" ms";
        if(!knownRate||s.latencySamples!=expectedLatency||!readout.detail.contains(expectedText))coherent.store(false);
        if(s.hasProcessedAudio){
            const auto expectedWindow="16 samples / "+juce::String(16000.0/s.sampleRate,2)+" ms";
            if(s.processedBlocks!=1||s.blockSamples!=16||!readout.detail.contains(expectedWindow))coherent.store(false);
        }else if(s.processedBlocks!=0||s.blockSamples!=0)coherent.store(false);
    }
    writer.join();
    require(observations>0&&coherent.load(),"GUI mixed sample rate, latency, or block metadata across concurrent reprepare");
    const auto final=p.meterSnapshot();require(final.isPrepared&&final.hasProcessedAudio&&final.sampleRate==48000&&final.latencySamples==240,"Final prepared metadata is stale");
}

}
int main(int argc,char** argv){juce::ScopedJuceInitialiser_GUI gui;try{
    if(argc>1){artifacts=juce::File(argv[1]);require(artifacts.createDirectory().wasOk(),"Cannot create artifact directory");}
    testLifecycleAndSilence();testCorrelation();testSamplePeaks();testGainReduction();testSignedGain();testFirEstimate();testSnapshotCoherence();testReprepareMetadataCoherence();
    std::cout<<"PASS: 12 native processor readiness/reset/real-silence states; correlation +/-1/zero/undefined; actual sample peaks and VCA GR; signed transient gain; defined 8x FIR output estimate and real latency; coherent atomic snapshots and concurrent reprepare rate/latency metadata; actual native editor 1x/2x calibrated raster\n";return 0;
}catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}}
