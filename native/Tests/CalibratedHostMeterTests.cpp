#include "../Source/Systems/CalibratedMeterView.h"
#include <array>
#include <atomic>
#include <thread>
#include <cmath>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>

namespace hem {
struct CalibratedHostMeterTestAccess {
    static void begin(CalibratedMeterView& v, double ms = 0) { v.stopTimer(); v.observed = false; v.refresh(ms); }
    static void tick(CalibratedMeterView& v, double ms) { v.refresh(ms); }
    static int state(const CalibratedMeterView& v) { return int(v.state); }
    static juce::String status(const CalibratedMeterView& v) { return v.statusText(); }
    static AudioMeters::Snapshot data(const CalibratedMeterView& v) { return v.data; }
    static bool rectanglesFit(const CalibratedMeterView& v) {
        const auto r=v.geometry(); const auto b=v.getLocalBounds().toFloat();
        return b.contains(r.title.toFloat()) && b.contains(r.subtitle.toFloat())
            && b.contains(r.left) && b.contains(r.right) && b.contains(r.status.toFloat())
            && r.title.getBottom()<=r.subtitle.getY() && r.subtitle.getBottom()<r.left.getY()
            && r.left.getBottom()<r.readoutY && r.readoutY+3*r.row<=r.status.getY();
    }
};
}
namespace {
using hem::AudioMeters;
using hem::CalibratedMeterView;
using Access=hem::CalibratedHostMeterTestAccess;
int checks=0, rejected=0;
juce::File artifacts;
std::vector<juce::String> savedNames;
void require(bool value,const juce::String& message) { ++checks; if(!value)throw std::runtime_error(message.toStdString()); }
void near(float value,double expected,const char* message) { require(std::isfinite(value)&&std::abs(value-expected)<.0001,message); }
void constant(AudioMeters& m,float l,float r,int count) { std::vector<float> left(size_t(count),l),right(size_t(count),r);m.push(left.data(),right.data(),count); }
struct Expected { double peak[2],rms[2],held[2]; bool live=true; };
Expected measured() {
    Expected e;
    const double amplitudes[2]={.5,.0625};
    for(int ch=0;ch<2;++ch) {
        e.held[ch]=20*std::log10(amplitudes[ch]);
        e.peak[ch]=e.held[ch]-24*.2;
        e.rms[ch]=e.held[ch]+10*std::log10(1-std::exp(-1536.0/(48000*.3)))-10/std::log(10.0)*(.2/.3);
    }
    return e;
}
Expected dark(bool live=false) {return {{-120,-120},{-120,-120},{-120,-120},live};}
void feedMeasured(AudioMeters& m) { require(m.prepare(48000),"prepare failed");constant(m,.5f,.0625f,1536);m.push(nullptr,nullptr,9600); }
void checkMeasured(const AudioMeters::Snapshot& s,const Expected& e) {
    near(s.left.peakDb,e.peak[0],"left sample peak numeric calibration");near(s.right.peakDb,e.peak[1],"right sample peak numeric calibration");
    near(s.left.rmsDb,e.rms[0],"left 300ms RMS numeric calibration");near(s.right.rmsDb,e.rms[1],"right 300ms RMS numeric calibration");
    near(s.left.heldPeakDb,e.held[0],"left one-second hold");near(s.right.heldPeakDb,e.held[1],"right one-second hold");
}
// Independent pixel oracle: transcribed layout/scale, no production layout,
// normalisation, segment, mark or cached telemetry helper is used here.
struct OracleLayout { float font;int row,readY;juce::Rectangle<float> lanes[2]; };
OracleLayout layoutFor(int w,int h) {
    OracleLayout o;const bool compact=h<180;
    o.font=compact?8.f:std::clamp(w*12.f/149.f,8.f,14.f);o.row=int(std::ceil(o.font*1.3f));
    float top=float(4+(o.row+2)+o.row+9),bottom=float(h-(compact?4:5)*o.row-5);
    float x=std::clamp(w*.18f,20.f,30.f),gap=std::clamp(w*.06f,5.f,10.f),width=(w-x-gap-9.f)/2.f;
    o.lanes[0]={x,top,width,std::max(1.f,bottom-top)};o.lanes[1]=o.lanes[0].translated(width+gap,0);
    o.readY=int(o.lanes[0].getBottom())+1;return o;
}
juce::Image capture(CalibratedMeterView& view,int scale=1) {
    juce::Image i(juce::Image::ARGB,view.getWidth()*scale,view.getHeight()*scale,true);
    juce::Graphics g(i);g.addTransform(juce::AffineTransform::scale(float(scale)));view.paintEntireComponent(g,true);return i;
}
void save(const juce::Image& i,const juce::String& name) {
    // The actual component was painted into i before any PNG encoding.
    const auto file=artifacts.getChildFile(name+".png");
    auto stream=file.createOutputStream();require(stream!=nullptr,"open PNG");
    require(stream->setPosition(0)&&stream->truncate().wasOk(),"truncate previous PNG before re-encoding");
    require(juce::PNGImageFormat().writeImageToStream(i,*stream),"encode actual capture");
    stream->flush();stream.reset();
    const auto decoded=juce::ImageFileFormat::loadFrom(file);
    require(decoded.getWidth()==i.getWidth()&&decoded.getHeight()==i.getHeight(),"decoded PNG dimensions match current capture");
    bool exact=true;for(int y=0;y<i.getHeight();++y)for(int x=0;x<i.getWidth();++x)
        if(i.getPixelAt(x,y)!=decoded.getPixelAt(x,y))exact=false;
    require(exact,"encoded PNG pixels match current pre-encode capture");
    savedNames.push_back(file.getFileName());
}
bool bright(juce::Colour c) {return std::max({int(c.getRed()),int(c.getGreen()),int(c.getBlue())})>95;}
bool hue(juce::Colour c,double db) {
    const int r=c.getRed(),g=c.getGreen(),b=c.getBlue();
    return db>=-3 ? r>g+50&&r>b+50 : db>=-12 ? r>b+50&&g>b+25 : g>r+45&&b>r+45;
}
bool calibratedPixels(const juce::Image& image,int w,int h,int scale,const Expected& expected,juce::String* failure=nullptr) {
    auto fail=[&](juce::String s){if(failure)*failure=s;return false;};const auto o=layoutFor(w,h);
    for(int ch=0;ch<2;++ch) {
        auto lane=o.lanes[ch];const int count=std::clamp(24,1,int(std::clamp(std::floor(lane.getHeight()/2.f),1.f,256.f)));
        for(int segment=0;segment<count;++segment) {
            const double threshold=-60.0+60.0*(segment+1)/count;
            const bool lit=expected.live&&expected.peak[ch]>=threshold;
            const int y=int(std::floor((lane.getBottom()-(segment+.5f)*lane.getHeight()/count)*scale));
            for(float xFraction:{.25f,.75f}) {
                const int x=int(std::floor((lane.getX()+lane.getWidth()*xFraction)*scale));
                auto c=image.getPixelAt(x,y);
                if(bright(c)!=lit||(lit&&!hue(c,threshold)))
                    return fail("ch="+juce::String(ch)+" segment="+juce::String(segment)+" pixel="+juce::String(x)+","+juce::String(y)+" expected="+juce::String(int(lit))+" actual="+c.toDisplayString(true));
            }
        }
        if(expected.live&&lane.getHeight()>50) {
            for(int role=0;role<2;++role) {
                const double db=role?expected.held[ch]:expected.rms[ch];if(db<=-60)continue;
                const float thickness=role?1.f:1.5f;
                const float center=float(lane.getBottom()-lane.getHeight()*std::clamp((db+60)/60,0.0,1.0));
                const float yy=std::clamp(center-thickness*.5f,lane.getY(),lane.getBottom()-thickness);
                const int y=int(std::floor((yy+thickness*.5f)*scale));
                const int x=int(std::floor((lane.getX()+lane.getWidth()*(role?.1f:.5f))*scale));
                auto c=image.getPixelAt(x,y);
                if(role ? !(c.getRed()>130&&c.getGreen()>75&&c.getRed()>c.getBlue()+40)
                        : !(c.getGreen()>125&&c.getBlue()>110&&std::abs(int(c.getGreen())-int(c.getBlue()))<60))
                    return fail("missing or shifted "+juce::String(role?"hold":"RMS")+" marker ch="+juce::String(ch));
            }
        }
    }
    return true;
}
bool numericPixels(const juce::Image& image,int w,int h,int scale,const Expected& e) {
    const auto o=layoutFor(w,h);juce::Image reference(juce::Image::ARGB,w*scale,h*scale,true);
    {juce::Graphics g(reference);g.addTransform(juce::AffineTransform::scale(float(scale)));g.fillAll(juce::Colour(0xff061017));
     g.setFont(juce::Font(juce::FontOptions(o.font*.9f)));
     for(int row=1;row<=2;++row)for(int ch=0;ch<2;++ch) {
        const auto lane=o.lanes[ch];const double db=row==1?e.peak[ch]:e.rms[ch];
        const juce::String value=!e.live?"--":db<=-120?"-inf":juce::String(db,1);
        g.setColour(juce::Colour(row==1?0xffd5faff:0xff55dccc));
        g.drawFittedText(value,int(lane.getX()),o.readY+row*o.row,int(lane.getWidth()),o.row,juce::Justification::centred,1,.75f);
     }}
    for(int row=1;row<=2;++row)for(int ch=0;ch<2;++ch) {
        auto lane=o.lanes[ch];juce::Rectangle<int> r(int(lane.getX())*scale,(o.readY+row*o.row)*scale,int(lane.getWidth())*scale,o.row*scale);
        for(int y=r.getY();y<r.getBottom();++y)for(int x=r.getX();x<r.getRight();++x)
            if(reference.getPixelAt(x,y)!=image.getPixelAt(x,y))return false;
    }
    return true;
}
void verifyCapture(CalibratedMeterView& view,int scale,const Expected& e,const juce::String& name) {
    auto image=capture(view,scale);juce::String failure;
    require(calibratedPixels(image,view.getWidth(),view.getHeight(),scale,e,&failure),name+": "+failure);
    require(numericPixels(image,view.getWidth(),view.getHeight(),scale,e),name+": actual numeric readout pixels");save(image,name);
}
void verifyStateSizes(CalibratedMeterView& view,const Expected& e,const juce::String& name) {
    const int oldWidth=view.getWidth(),oldHeight=view.getHeight();
    for(const auto size:std::array<std::pair<int,int>,3>{{{96,145},{114,314},{149,410}}})for(int scale:{1,2}) {
        view.setSize(size.first,size.second);verifyCapture(view,scale,e,name+"-"+juce::String(size.first)+"x"+juce::String(size.second)+"-"+juce::String(scale)+"x");
    }
    view.setSize(oldWidth,oldHeight);
}
void testConstructorsAndSizes() {
    AudioMeters m;m.prepare(48000);CalibratedMeterView output(m),input(m,"INPUT / dBFS","POST-INSERT MIX"),custom(m,"CUSTOM");
    require(output.getName()=="OUTPUT / dBFS: SAMPLE PEAK + RMS","default constructor labels");
    require(input.getName()=="INPUT / dBFS: POST-INSERT MIX","input constructor labels");
    require(custom.getName()=="CUSTOM: SAMPLE PEAK + RMS","defaulted subtitle");
    Access::begin(output);Access::begin(input);feedMeasured(m);Access::tick(output,1);Access::tick(input,1);checkMeasured(m.snapshot(),measured());
    const std::array<std::pair<int,int>,12> sizes{{{149,410},{155,410},{114,314},{119,314},{163,422},{166,422},{125,323},{127,323},{190,310},{96,190},{96,145},{64,100}}};
    for(const auto& size:sizes)for(int scale:{1,2}) {
        output.setSize(size.first,size.second);input.setSize(size.first,size.second);
        require(Access::rectanglesFit(output),"normal/compact text and lane rectangles fit");
        const juce::String suffix=juce::String(size.first)+"x"+juce::String(size.second)+"-"+juce::String(scale)+"x";
        verifyCapture(output,scale,measured(),"live-output-"+suffix);verifyCapture(input,scale,measured(),"live-input-"+suffix);
    }
    output.setSize(48,72);auto small=capture(output);for(int y=0;y<72;++y)for(int x=0;x<48;++x)
        require(small.getPixelAt(x,y)==juce::Colour(0xff061017),"tiny aperture stays opaque/dark");save(small,"tiny-opaque");
}
void testLifecycle() {
    AudioMeters m;CalibratedMeterView v(m);v.setSize(149,410);Access::begin(v);
    require(Access::state(v)==0,"unprepared unavailable");verifyStateSizes(v,dark(),"unprepared");
    m.prepare(48000);Access::tick(v,10);require(Access::state(v)==1,"prepared waiting, not fabricated live silence");verifyStateSizes(v,dark(),"prepared-waiting");
    m.push(nullptr,nullptr,0);Access::tick(v,20);require(Access::state(v)==1,"zero-length callback is still waiting");
    m.push(nullptr,nullptr,64);Access::tick(v,30);require(Access::state(v)==2,"real silent callback is live");verifyStateSizes(v,dark(true),"real-live-silence");
    for(int i=1;i<=6;++i){m.push(nullptr,nullptr,64);Access::tick(v,30+i*900);require(Access::state(v)==2,"unchanged live silence advances metadata");}
    Access::tick(v,6430);require(Access::state(v)==3,"exact one-second no-callback silence goes stale");verifyStateSizes(v,dark(),"stale-silence");
    feedMeasured(m);Access::tick(v,6500);require(Access::state(v)==2,"fresh prepared signal live");verifyStateSizes(v,measured(),"fresh-reprepared");
    Access::tick(v,7499);require(Access::state(v)==2,"freshness before boundary");
    m.push(nullptr,nullptr,0);Access::tick(v,7500);require(Access::state(v)==3,"zero frames cannot refresh stale signal");verifyStateSizes(v,dark(),"stale-signal");
    const auto before=m.snapshot();Access::tick(v,8000);const auto after=m.snapshot();
    require(before.publication==after.publication&&before.left.peakDb==after.left.peakDb,"GUI freshness never changes measurement state");
    const auto priorGeneration=m.snapshot().generation;feedMeasured(m);
    require(m.snapshot().processedSamples==before.processedSamples&&m.snapshot().generation!=priorGeneration,"same frame count in a new preparation");
    Access::tick(v,8005);require(Access::state(v)==2,"new generation refreshes even at identical processed count");
    m.prepare(0);Access::tick(v,8010);require(Access::state(v)==0,"release invalidates view");verifyStateSizes(v,dark(),"released");
    m.prepare(96000);Access::tick(v,8020);require(Access::state(v)==1,"reprepare invalidates old samples");verifyStateSizes(v,dark(),"reprepared-waiting");
    // Release+reprepare entirely between GUI polls still resets held levels.
    feedMeasured(m);Access::tick(v,8100);m.prepare(0);m.prepare(48000);Access::tick(v,8200);
    require(Access::state(v)==1&&Access::data(v).left.heldPeakDb==-120,"missed release/reprepare is detected by generation");
    // Unchanged steady-state values remain live based on actual samples.
    constant(m,.25f,.125f,480000);Access::tick(v,8300);
    for(int i=1;i<=5;++i){constant(m,.25f,.125f,256);Access::tick(v,8300+i*900);require(Access::state(v)==2,"steady-tone publication is live");}
    m.prepare(std::numeric_limits<double>::quiet_NaN());Access::tick(v,14000);require(Access::state(v)==0,"invalid prepare unavailable");
    verifyStateSizes(v,dark(),"invalid-preparation");
    m.prepare(48000);Access::tick(v,14010);m.push(nullptr,nullptr,64);Access::tick(v,14020);require(Access::state(v)==2,"real audio recovers after invalid prepare");
}
void testFrozenPositiveConstructor() {
    AudioMeters m;feedMeasured(m);
    for(int repeat=0;repeat<2;++repeat) {
        CalibratedMeterView v(m);v.setSize(149,410);Access::begin(v);
        require(Access::state(v)==1,"new/reopened view seeds frozen positive data dark");
        verifyStateSizes(v,dark(),"frozen-positive-open-"+juce::String(repeat));
        Access::tick(v,2000);require(Access::state(v)==1,"elapsed time cannot make frozen initial data fresh");
        m.push(nullptr,nullptr,0);Access::tick(v,2100);require(Access::state(v)==1,"zero-length publication cannot light reopened view");
        m.push(nullptr,nullptr,64);Access::tick(v,2200);require(Access::state(v)==2,"next real callback lights reopened view");
        Access::tick(v,3200);require(Access::state(v)==3,"reopened live sample goes stale after one second");
    }
}
void testZeroFrameContention() {
    AudioMeters m;feedMeasured(m);CalibratedMeterView v(m);Access::begin(v);
    for(int phase=0;phase<2;++phase) {
        const double start=phase*7000.;
        if(phase) {Access::tick(v,start);require(Access::state(v)==3,"previous real callback already stale");}
        std::atomic<bool> stop{false},started{false};
        std::thread writer([&]{started.store(true);while(!stop.load())m.push(nullptr,nullptr,0);});
        while(!started.load())std::this_thread::yield();
        bool falseLive=false;int unavailable=0;
        for(int i=0;i<200000;++i) {
            Access::tick(v,start+i*.02);falseLive=falseLive||Access::state(v)==2;
            unavailable+=Access::state(v)==0;
        }
        stop.store(true);writer.join();
        require(!falseLive,"contended zero-frame publications must never invent fresh samples");
        require(unavailable>0,"actual writer overlap must exercise unavailable snapshot recovery");
        Access::tick(v,start+5000);require(Access::state(v)==(phase?3:1),"coherent unchanged snapshot after contention retains waiting/stale");
        require(m.snapshot().processedSamples==std::uint64_t(11136+64*phase),"zero-frame contention never processed new samples");
        m.push(nullptr,nullptr,64);Access::tick(v,start+5010);require(Access::state(v)==2,"real callback recovers after zero-frame contention");
        std::cout<<"Zero-frame contention "<<(phase?"stale":"waiting")<<": "<<unavailable<<" unavailable GUI observations; no false live frames\n";
    }
}

void testLatches() {
    AudioMeters m;m.prepare(48000);CalibratedMeterView v(m);v.setSize(149,410);Access::begin(v);
    const float l[]={1.01f,std::numeric_limits<float>::quiet_NaN()},r[]={.1f,.1f};m.push(l,r,2);Access::tick(v,1);
    require(m.snapshot().left.clipped&&!m.snapshot().right.clipped&&m.snapshot().nonFiniteSamples==1,"real asymmetric clip and invalid measurement");
    require(Access::status(v).contains("INVALID"),"invalid latch visible");save(capture(v),"live-invalid-clip");
    Access::tick(v,1001);require(Access::state(v)==3&&Access::status(v).contains("INVALID"),"stale hides measurements but retains warning");save(capture(v),"stale-invalid-clip");
    require(v.keyPressed(juce::KeyPress(juce::KeyPress::spaceKey)),"space requests reset");
    require(m.snapshot().nonFiniteSamples==1,"reset does not write audio state from GUI");m.push(nullptr,nullptr,0);Access::tick(v,1100);
    require(!m.snapshot().left.clipped&&m.snapshot().nonFiniteSamples==0&&Access::state(v)==3,"zero-frame reset clears warning without claiming new audio");
    constant(m,1.1f,.1f,1);Access::tick(v,1200);require(Access::status(v).contains("CLIP"),"clip latch visible");
    require(v.keyPressed(juce::KeyPress(juce::KeyPress::returnKey)),"return requests reset");m.push(nullptr,nullptr,0);Access::tick(v,1201);
    require(!m.snapshot().left.clipped&&!v.keyPressed(juce::KeyPress('x')),"reset and unrelated key semantics");
}
void testPixelMutants() {
    AudioMeters m;m.prepare(48000);CalibratedMeterView v(m);v.setSize(149,410);Access::begin(v);feedMeasured(m);Access::tick(v,1);const auto source=capture(v,2);const auto o=layoutFor(149,410);const auto e=measured();
    auto reject=[&](juce::Image mutant,const char* name){require(!calibratedPixels(mutant,149,410,2,e)||!numericPixels(mutant,149,410,2,e),juce::String("mutant escaped: ")+name);++rejected;save(mutant,juce::String("mutant-")+name);};
    auto swapped=source.createCopy();{juce::Graphics g(swapped);for(int c=0;c<2;++c){auto a=o.lanes[c]*2.f,b=o.lanes[1-c]*2.f;g.drawImage(source,int(a.getX()),int(a.getY()),int(a.getWidth()),int(a.getHeight()),int(b.getX()),int(b.getY()),int(b.getWidth()),int(b.getHeight()),false);}}reject(swapped,"swapped-stereo");
    auto normalized=source.createCopy();{juce::Graphics g(normalized);g.setColour(juce::Colour(0xff41d7c5));for(auto lane:o.lanes)g.fillRect(lane*2.f);}reject(normalized,"normalized-fake-fullscale");
    auto missing=source.createCopy();{juce::Graphics g(missing);g.setColour(juce::Colour(0xff10232b));g.fillRect(o.lanes[0]*2.f);}reject(missing,"missing-left-measurement");
    auto noRms=source.createCopy();{juce::Graphics g(noRms);auto lane=o.lanes[0];const float y=float(lane.getBottom()-lane.getHeight()*(e.rms[0]+60)/60);g.setColour(juce::Colour(0xff10232b));g.fillRect(juce::Rectangle<float>(lane.getX()+lane.getWidth()*.3f,y-2,lane.getWidth()*.4f,4)*2.f);}reject(noRms,"missing-rms-mark");
    auto noHold=source.createCopy();{juce::Graphics g(noHold);auto lane=o.lanes[1];const float y=float(lane.getBottom()-lane.getHeight()*(e.held[1]+60)/60);g.setColour(juce::Colour(0xff10232b));g.fillRect(juce::Rectangle<float>(lane.getX(),y-2,lane.getWidth()*.2f,4)*2.f);}reject(noHold,"missing-hold-mark");
    auto wrongDigits=source.createCopy();{juce::Graphics g(wrongDigits);g.setColour(juce::Colour(0xff061017));g.fillRect(juce::Rectangle<int>(int(o.lanes[0].getX())*2,(o.readY+o.row)*2,int(o.lanes[0].getWidth())*2,o.row*2));}reject(wrongDigits,"missing-numeric-peak");
}
void testApprovedArt(const juce::File& file) {
    const auto art=juce::ImageFileFormat::loadFrom(file);require(art.isValid()&&art.getWidth()==1673&&art.getHeight()==940,"actual approved00 dimensions");
    AudioMeters in,out;CalibratedMeterView iv(in,"INPUT / dBFS","POST-INSERT MIX"),ov(out);Access::begin(iv);Access::begin(ov);
    for(int scale:{1,2}) {
        juce::Image composite(juce::Image::ARGB,1673*scale,940*scale,true);
        {juce::Graphics g(composite);g.addTransform(juce::AffineTransform::scale(float(scale)));g.drawImageAt(art,0,0);
         iv.setSize(163,422);ov.setSize(166,422);
         {juce::Graphics::ScopedSaveState state(g);g.addTransform(juce::AffineTransform::translation(78,178));g.reduceClipRegion(iv.getLocalBounds());iv.paintEntireComponent(g,true);}
         {juce::Graphics::ScopedSaveState state(g);g.addTransform(juce::AffineTransform::translation(1436,178));g.reduceClipRegion(ov.getLocalBounds());ov.paintEntireComponent(g,true);}}
        if(scale==1)for(int y=0;y<940;++y)for(int x=0;x<1673;++x)
            if(!juce::Rectangle<int>(78,178,163,422).contains(x,y)&&!juce::Rectangle<int>(1436,178,166,422).contains(x,y))
                require(composite.getPixelAt(x,y)==art.getPixelAt(x,y),"authored frame/brand changed outside approved aperture: "+juce::String(x)+","+juce::String(y)+" got="+composite.getPixelAt(x,y).toDisplayString(true)+" wanted="+art.getPixelAt(x,y).toDisplayString(true));
        // Explicitly scan entire original baked LED strips; only dark inactive
        // lane segments may remain in the repainted, unprepared renderer.
        auto inCapture=capture(iv,scale),outCapture=capture(ov,scale);
        require(calibratedPixels(inCapture,163,422,scale,dark())&&calibratedPixels(outCapture,166,422,scale,dark()),"no fabricated live LEDs over approved art");
        save(composite,"approved00-expanded-meter-apertures-unavailable-"+juce::String(scale)+"x");
    }
}
}
int main(int argc,char** argv) {
    juce::ScopedJuceInitialiser_GUI gui;
    try {
        require(argc==3,"expected artifact directory and approved00 path");artifacts=juce::File(argv[1]);require(artifacts.createDirectory().wasOk(),"create artifact directory");
        testConstructorsAndSizes();testLifecycle();testFrozenPositiveConstructor();testZeroFrameContention();testLatches();testPixelMutants();testApprovedArt(juce::File(argv[2]));
        std::ofstream manifest(artifacts.getParentDirectory().getChildFile("renders-written.txt").getFullPathName().toStdString());for(const auto& name:savedNames)manifest<<name.toStdString()<<"\n";manifest.close();
        std::cout<<"PASS CalibratedHostMeterTests: "<<checks<<" checks; "<<rejected<<" pixel mutants rejected; real AudioMeters metadata and analytic stereo peak/RMS/hold; compact/native/2x actual captures\n";
        return 0;
    } catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<"\n";return 1;}
}
