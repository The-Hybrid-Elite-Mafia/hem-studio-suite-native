#include "../Source/Plugins/PluginRegistry.h"
#include "../Source/Plugins/Editors.h"
#include "../Source/Plugins/PluginMeterReadout.h"
#include "../Source/UI/HardwarePanel.h"
#include "../Source/UI/ModuleEditorGeometry.h"
#include <atomic>
#include <iostream>
#include <limits>
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
juce::Rectangle<float> meterArea(ModuleProcessor& p){return ui::moduleMeterReadoutBounds(dynamic_cast<SurgicalEqProcessor*>(&p)!=nullptr);}
juce::Image rasterEditor(juce::AudioProcessorEditor& editor,int scale){
    require(editor.getWidth()==ui::moduleEditorWidth&&editor.getHeight()==ui::moduleEditorHeight,"Native meter raster layout differs from shared geometry");
    juce::Image image(juce::Image::ARGB,editor.getWidth()*scale,editor.getHeight()*scale,true);
    juce::Graphics g(image);g.addTransform(juce::AffineTransform::scale(float(scale)));editor.paintEntireComponent(g,true);return image;
}
juce::Image raster(ModuleProcessor& p,int scale){
    std::unique_ptr<juce::AudioProcessorEditor> editor(p.createEditor());return rasterEditor(*editor,scale);
}
juce::File artifacts;
void save(const juce::Image& image,const juce::String& name){if(artifacts==juce::File())return;auto out=artifacts.getChildFile(name+".png").createOutputStream();require(out!=nullptr,"Cannot write raster artifact");require(juce::PNGImageFormat().writeImageToStream(image,*out),"Cannot encode raster artifact");}
int fillPixels(const juce::Image& image,juce::Rectangle<float> track,int scale){int count=0;const int y=int(track.getCentreY()*float(scale));for(int x=int(track.getX()*float(scale));x<int(track.getRight()*float(scale));++x){const auto c=image.getPixelAt(x,y);if(c==juce::Colour(ui::pluginMeterPositiveColour)||c==juce::Colour(ui::pluginMeterNegativeColour))++count;}return count;}
// BEGIN actual-level helper
// Test-only, independently transcribed editor geometry. Integrate inside the
// PluginMeterDisplayTests.cpp anonymous namespace after require(). No production
// geometry, colours, readout positions, or renderer helpers are called here.
// Levels are expected measurements; the verifier never reads live telemetry.
struct ActualLevelLayout {
    const char* id;
    float referenceWidth, referenceHeight;
    std::array<juce::Rectangle<float>, 2> panels;
};

const ActualLevelLayout& actualLevelLayout(const ModuleProcessor& p) {
    // Spell out the nine preserved legacy processors. Approved chassis meters
    // have their own explicit 24-segment profiles and must never fall back here.
    static const std::array<ActualLevelLayout, 9> layouts {{
        {"surgical-eq",        1280, 720, {{{50,143,112,330}, {1125,143,112,330}}}},
        {"vca",                1280, 720, {{{50,143,112,330}, {1125,143,112,330}}}},
        {"true-peak",          1280, 720, {{{50,143,112,330}, {1125,143,112,330}}}},
        {"dhta",               1280, 720, {{{50,143,112,330}, {1125,143,112,330}}}},
        {"sub-bass",           1280, 720, {{{50,143,112,330}, {1125,143,112,330}}}},
        {"spectral-restorer",   1280, 720, {{{50,143,112,330}, {1125,143,112,330}}}},
        {"vocal-presence",      1280, 720, {{{50,143,112,330}, {1125,143,112,330}}}},
        {"triode-tube",         1280, 720, {{{50,143,112,330}, {1125,143,112,330}}}},
        {"monitor-eq",         1280, 720, {{{50,143,112,330}, {1125,143,112,330}}}}
    }};
    const auto id = PluginRegistry::idFor(p);
    for (const auto& layout : layouts) if (id == layout.id) return layout;
    throw std::runtime_error(("No explicit actual I/O meter layout for " + id).toStdString());
}

juce::Rectangle<float> actualLevelLane(const ActualLevelLayout& layout, int index) {
    const auto panel = layout.panels[size_t(index / 2)];
    const float plotWidth = panel.getWidth() - 49.f;
    const float laneWidth = (plotWidth - 5.f) * .5f;
    return {panel.getX() + 13.f + (index % 2) * (laneWidth + 5.f),
            panel.getY() + 60.f, laneWidth, std::max(35.f, panel.getHeight() - 164.f)};
}

juce::Rectangle<float> actualLevelSegment(const ActualLevelLayout& layout, int laneIndex, int segment) {
    const auto lane = actualLevelLane(layout, laneIndex);
    const float step = lane.getHeight() / 60.f;
    return {lane.getX(), lane.getBottom() - (segment + 1) * step,
            lane.getWidth(), std::max(.6f, step * .72f)};
}

juce::Rectangle<float> actualMeterPixels(const juce::Image& image, const ActualLevelLayout& layout,
                                       juce::Rectangle<float> area) {
    const float sx = image.getWidth() / layout.referenceWidth;
    const float sy = image.getHeight() / layout.referenceHeight;
    return {area.getX() * sx, area.getY() * sy, area.getWidth() * sx, area.getHeight() * sy};
}

bool actualMeterRasterSize(const juce::Image& image) {
    return (image.getWidth() == 1280 && image.getHeight() == 720)
        || (image.getWidth() == 2560 && image.getHeight() == 1440);
}

bool actualMeterBright(juce::Colour c) {
    // A dim 7.5%/10% segment on the opaque glass remains far below this.
    // Fractional-height lit segment coverage at 1x still remains above it.
    return std::max({int(c.getRed()), int(c.getGreen()), int(c.getBlue())}) >= 100;
}

bool actualMeterHue(juce::Colour c, int segment, bool reduction = false) {
    const int r = c.getRed(), g = c.getGreen(), b = c.getBlue();
    if (reduction || (segment >= 42 && segment < 57))
        return r >= 100 && g >= 55 && r > b + 50 && g > b + 25;
    if (segment >= 57) return r >= 100 && r > g + 60 && r > b + 60;
    return g >= 100 && b >= 100 && g > r + 60 && b > r + 60;
}

// Exclude only the independently calculated 1.8-canonical-pixel RMS marker
// footprint, including raster antialiasing. A missing or wrongly located RMS
// line is not allowed to excuse arbitrary bright rows. Peak evidence is sampled
// in each of the other segment centers, at three interior X positions.
bool actualLevelRmsRow(const juce::Image& image, const ActualLevelLayout& layout,
                       int laneIndex, int y, float rms, bool live) {
    if (!live) return false;
    const auto lane = actualLevelLane(layout, laneIndex);
    const double db = rms > 0 ? 20.0 * std::log10(double(rms)) : -160.0;
    const double fraction = std::clamp((db + 60.0) / 60.0, 0.0, 1.0);
    const double sy = image.getHeight() / double(layout.referenceHeight);
    const double marker = (lane.getBottom() - fraction * lane.getHeight()) * sy;
    return std::abs((y + .5) - marker) <= .9 * sy + .75;
}

bool verifyActualLevels(const juce::Image& image, const ModuleProcessor& p,
                        const PluginTelemetry::Levels& expected, bool live,
                        juce::String* failure = nullptr) {
    const auto& layout = actualLevelLayout(p); // unsupported layouts throw
    auto fail = [&](const juce::String& message) {
        if (failure) *failure = juce::String(layout.id) + ": " + message;
        return false;
    };
    if (!actualMeterRasterSize(image)) return fail("expected actual 1280x720 editor raster at 1x or 2x");
    for (int laneIndex = 0; laneIndex < 4; ++laneIndex) {
        const float peak = expected.peak[size_t(laneIndex)];
        const float rms = expected.rms[size_t(laneIndex)];
        if (live && (!std::isfinite(peak) || !std::isfinite(rms) || peak < 0 || rms < 0))
            return fail("invalid expected peak/RMS measurement");
        const float db = peak > 0 ? float(20.0 * std::log10(double(peak))) : -160.f;
        int checkedSegments = 0;
        for (int segment = 0; segment < 60; ++segment) {
            const auto pixels = actualMeterPixels(image, layout, actualLevelSegment(layout, laneIndex, segment));
            const int y = int(std::floor(pixels.getCentreY()));
            if (actualLevelRmsRow(image, layout, laneIndex, y, rms, live)) continue;
            ++checkedSegments;
            // Compare dB in float precision, matching the measured display domain.
            // This avoids treating 0.1f as infinitesimally above nominal -20 dB.
            // Threshold is the segment's lower edge, not a GUI readout fraction.
            const bool shouldBeLit = live && db > (-60.0 + segment);
            for (float horizontal : {.2f, .5f, .8f}) {
                const int x = int(std::floor(pixels.getX() + pixels.getWidth() * horizontal));
                if (!image.getBounds().contains(x, y)) return fail("mapped probe lies outside the editor image");
                const auto colour = image.getPixelAt(x, y);
                if (actualMeterBright(colour) != shouldBeLit
                    || (shouldBeLit && !actualMeterHue(colour, segment))) {
                    return fail(juce::String(laneIndex < 2 ? "input " : "output ")
                        + (laneIndex % 2 ? "R" : "L") + " segment " + juce::String(segment)
                        + " at (" + juce::String(x) + "," + juce::String(y) + ") expected "
                        + (shouldBeLit ? "lit calibrated hue" : "dark") + ", got " + colour.toDisplayString(true));
                }
            }
        }
        // Excluding one thin RMS marker must never turn this into an empty or
        // sparse passing assertion. Typical layouts inspect 58-60/60 segments.
        if (checkedSegments < 56) return fail("RMS exclusion removed too much peak evidence");
    }
    if (failure) failure->clear();
    return true;
}

// Positive and mutant images are all evaluated by exactly the same verifier.
// Waiting/silence exercises fabricated-fill rejection. Live fixtures additionally
// prove that erased lanes, a moved lane, and one corrupted segment cannot pass.
void assertActualLevelNegativeControls(const juce::Image& image, const ModuleProcessor& p,
                                       const PluginTelemetry::Levels& expected, bool live) {
    juce::String problem;
    require(verifyActualLevels(image, p, expected, live, &problem), problem);
    const auto& layout = actualLevelLayout(p);
    auto fabricated = image.createCopy();
    {
        juce::Graphics g(fabricated);
        g.setColour(juce::Colours::cyan);
        g.fillRect(actualMeterPixels(image, layout, actualLevelLane(layout, 0)));
    }
    require(!verifyActualLevels(fabricated, p, expected, live), "Actual I/O raster gate accepted fabricated full fill");

    int activeLane = -1, activeSegment = -1;
    if (live) for (int laneIndex = 0; laneIndex < 4 && activeLane < 0; ++laneIndex) {
        const float peak = expected.peak[size_t(laneIndex)];
        const float db = peak > 0 ? float(20.0 * std::log10(double(peak))) : -160.f;
        for (int segment = 0; segment < 60; ++segment) {
            const auto pixels = actualMeterPixels(image, layout, actualLevelSegment(layout, laneIndex, segment));
            const int y = int(std::floor(pixels.getCentreY()));
            if (db > -60.0 + segment
                && !actualLevelRmsRow(image, layout, laneIndex, y, expected.rms[size_t(laneIndex)], true)) {
                activeLane = laneIndex;
                activeSegment = segment;
                break;
            }
        }
    }
    if (activeLane < 0) return; // no real lit segment exists in waiting/silence
    const auto lanePixels = actualMeterPixels(image, layout, actualLevelLane(layout, activeLane));
    auto erased = image.createCopy();
    { juce::Graphics g(erased); g.setColour(juce::Colours::black); g.fillRect(lanePixels); }
    require(!verifyActualLevels(erased, p, expected, live), "Actual I/O raster gate accepted an erased lit lane");

    auto moved = image.createCopy();
    const auto sourceRect = lanePixels.getSmallestIntegerContainer();
    const auto oldLane = image.getClippedImage(sourceRect).createCopy();
    {
        juce::Graphics g(moved);
        g.setColour(juce::Colours::black);
        g.fillRect(sourceRect);
        g.drawImageAt(oldLane, sourceRect.getX() + std::max(3, int(std::ceil(lanePixels.getWidth() * .6f))), sourceRect.getY());
    }
    require(!verifyActualLevels(moved, p, expected, live), "Actual I/O raster gate accepted a moved lit lane");

    auto corrupted = image.createCopy();
    const auto badSegment = actualMeterPixels(image, layout, actualLevelSegment(layout, activeLane, activeSegment));
    corrupted.setPixelAt(int(std::floor(badSegment.getCentreX())), int(std::floor(badSegment.getCentreY())), juce::Colours::magenta);
    require(!verifyActualLevels(corrupted, p, expected, live), "Actual I/O raster gate accepted one corrupted segment");
}

// Independent 24-segment reduction lanes for approved Gate08 / Plosive07.
// These represent 0..24 dB detector attenuation, growing top-down. They never
// share the four I/O peak/RMS oracle or infer reduction from an output level.
juce::Rectangle<float> actualFoundationReductionArea(const ModuleProcessor& p) {
    const auto id=PluginRegistry::idFor(p);
    if(id=="smart-gate")return {1346,240,44,108};
    if(id=="plosive-control")return {1358,231,43,108};
    throw std::runtime_error(("No separate foundation reduction aperture for "+id).toStdString());
}
juce::Rectangle<float> foundationReductionPixels(const juce::Image& image,juce::Rectangle<float> r) {
    return {r.getX()*image.getWidth()/1672.f,r.getY()*image.getHeight()/941.f,
            r.getWidth()*image.getWidth()/1672.f,r.getHeight()*image.getHeight()/941.f};
}
bool verifyFoundationReduction(const juce::Image& image,const ModuleProcessor& p,
                                float expectedDb,bool valid,juce::String* failure=nullptr) {
    const auto lane=actualFoundationReductionArea(p);
    const auto fail=[&](const juce::String& message){if(failure)*failure=PluginRegistry::idFor(p)+" reduction: "+message;return false;};
    if(!actualMeterRasterSize(image))return fail("expected actual 1280x720 editor at 1x or 2x");
    if(valid&&!std::isfinite(expectedDb))return fail("nonfinite expected reduction");
    juce::Image reference(juce::Image::ARGB,image.getWidth(),image.getHeight(),true);juce::Graphics g(reference);
    g.addTransform(juce::AffineTransform::scale(image.getWidth()/1672.f,image.getHeight()/941.f));
    g.setColour(juce::Colour(0xff020609));g.fillRect(lane);
    for(int bin=1;bin<=24;++bin){
        const bool lit=valid&&expectedDb>=float(bin);
        g.setColour(juce::Colour(lit?0xffffbd59:0xff10232b));
        // 108/24 = 4.5-unit pitch; shared meter's gap is 20% of that
        // compact pitch (0.9), independently specified here.
        g.fillRect(juce::Rectangle<float>(lane.getX(),lane.getY()+(bin-1)*4.5f+.45f,lane.getWidth(),3.6f));
    }
    const auto pixels=foundationReductionPixels(image,lane);int inspected=0;
    for(int y=int(std::ceil(pixels.getY()))+1;y<int(std::floor(pixels.getBottom()))-1;++y)
        for(int x=int(std::ceil(pixels.getX()))+1;x<int(std::floor(pixels.getRight()))-1;++x){
            ++inspected;const auto a=image.getPixelAt(x,y),b=reference.getPixelAt(x,y);
            if(std::abs(int(a.getRed())-b.getRed())>4||std::abs(int(a.getGreen())-b.getGreen())>4||std::abs(int(a.getBlue())-b.getBlue())>4)
                return fail("calibrated top-down segment/gap pixel differs");
        }
    if(inspected<1000)return fail("insufficient whole reduction lane evidence");
    if(failure)failure->clear();return true;
}
void assertFoundationReductionNegativeControl(const juce::Image& image,const ModuleProcessor& p,
                                               float expectedDb,bool valid) {
    juce::String problem;require(verifyFoundationReduction(image,p,expectedDb,valid,&problem),problem);
    const auto area=foundationReductionPixels(image,actualFoundationReductionArea(p));
    auto fabricated=image.createCopy();{juce::Graphics g(fabricated);g.setColour(juce::Colours::magenta);g.fillRect(area);}
    require(!verifyFoundationReduction(fabricated,p,expectedDb,valid),"Foundation reduction gate accepted fabricated fill");
    if(valid&&expectedDb>=1.f&&expectedDb<24.f){
        require(!verifyFoundationReduction(image,p,0,true),"Foundation reduction gate accepted erased expected attenuation");
        auto inverted=image.createCopy();const auto r=area.getSmallestIntegerContainer();
        for(int y=r.getY();y<r.getBottom();++y)for(int x=r.getX();x<r.getRight();++x)
            inverted.setPixelAt(x,y,image.getPixelAt(x,r.getBottom()-1-(y-r.getY())));
        require(!verifyFoundationReduction(inverted,p,expectedDb,true),"Foundation reduction gate accepted reversed segment direction");
    }
}
// END actual-level helper
// Analysis chassis use separate approved meter lanes, not the original-editor I/O panels.
std::vector<juce::Rectangle<float>> recoveredFamilyLanes(ModuleProcessor& p){
 const auto id=PluginRegistry::idFor(p);
 if(id=="convolution-reverb")return {{93,227,23,299},{173,227,23,299},{1476,227,23,299},{1557,227,23,299}};
 if(id=="algorithmic-reverb")return {{126,149,26,292},{191,149,26,292},{1455,149,26,292},{1520,149,26,292}};
 if(id=="chorus-flanger-phaser")return {{97,228,24,245},{179,228,24,245},{1474,228,24,245},{1556,228,24,245}};
 if(id=="rotary-tremolo-autopan")return {{95,210,21,268},{144,210,21,268},{1509,210,21,268},{1558,210,21,268}};
 if(id=="multitap-delay")return {{90,119,21,168},{171,119,21,168},{1481,119,21,168},{1570,119,21,168}};
 if(id=="tape-delay")return {{86,219,21,209},{141,219,21,209},{1511,219,21,209},{1564,219,21,209}};
 return {};
}
bool verifyRecoveredFamilyLevels(const juce::Image& image,ModuleProcessor& p,const std::array<float,4>& expected,bool valid){
 const auto lanes=recoveredFamilyLanes(p);require(lanes.size()==4,"Recovered family meter layout missing");
 // Independent24-segment contract, not production geometry/model calls.
 for(int channel=0;channel<4;++channel){const auto lane=lanes[size_t(channel)];const float db=expected[size_t(channel)]>0?20.f*std::log10(expected[size_t(channel)]):-160.f;
  for(int segment=0;segment<24;++segment){const float threshold=-60.f+2.5f*(segment+1);const bool lit=valid&&std::isfinite(db)&&db>=threshold;
   const juce::Colour colour=!lit?juce::Colour(0xff10232b):threshold>=-3?juce::Colour(0xffff655b):threshold>=-12?juce::Colour(0xffffbd59):juce::Colour(0xff41d7c5);
   const int x=int(std::floor((lane.getX()+lane.getWidth()*.5f)*image.getWidth()/1672.f));
   const int y=int(std::floor((lane.getY()+(23-segment+.5f)*lane.getHeight()/24.f)*image.getHeight()/941.f));
   if(image.getPixelAt(x,y)!=colour)return false;
  }}return true;
}
std::vector<juce::Rectangle<float>> analysisMeterLanes(ModuleProcessor& p){
 if(auto lanes=recoveredFamilyLanes(p);!lanes.empty())return lanes;
 if(dynamic_cast<LoudnessLabProcessor*>(&p))return {{88,227,84,319},{218,227,84,319},{1375,227,84,319},{1492,227,84,319}};
 if(dynamic_cast<SpectrumSpectrogramProcessor*>(&p))return {{1551,228,37,244}};
 if(dynamic_cast<DeltaMonitorProcessor*>(&p))return {{78,221,43,232},{133,221,43,232},{1170,221,42,232},{1221,221,42,232},{1312,221,42,232},{1363,221,42,232}};
 return {};
}
int actualAnalysisLitPixels(const juce::Image& image,const std::vector<juce::Rectangle<float>>& lanes){
 int result=0;const float sx=image.getWidth()/1672.f,sy=image.getHeight()/941.f;
 for(auto lane:lanes){lane=lane.reduced(3);const int x=int(lane.getCentreX()*sx);
  for(int y=int(std::ceil(lane.getY()*sy));y<int(std::floor(lane.getBottom()*sy));++y){const auto c=image.getPixelAt(x,y);if(c.getRed()>100||c.getGreen()>100||c.getBlue()>100)++result;}}
 return result;
}
bool correlationRasterMatches(const juce::Image&,float,bool);
void freshProcess(ModuleProcessor&,juce::AudioBuffer<float>&);
#include "BandMeterRasterFixture.h"
#include "NonlinearMeterRasterFixture.h"
#include "ChassisMeterRasterFixture.h"
#include "RestorationMeterRasterFixture.h"
void noBar(ModuleProcessor& p,const juce::String& name){if(chassisNoBar(p,name))return;if(restorationNoBar(p,name))return;if(nonlinearNoBar(p,name))return;if(bandNoBar(p,name,artifacts))return;for(int scale:{1,2}){const auto image=raster(p,scale);const auto lanes=analysisMeterLanes(p);
 if(lanes.empty()){
  PluginTelemetry::Levels expected;const bool captured=p.pluginTelemetry()&&p.pluginTelemetry()->read(expected);const bool live=captured&&expected.valid&&p.pluginTelemetry()->analysis.lastConsumedAt()>0&&juce::Time::getMillisecondCounterHiRes()-p.pluginTelemetry()->analysis.lastConsumedAt()<750;
  juce::String failure;require(verifyActualLevels(image,p,expected,live,&failure),name+": "+failure);assertActualLevelNegativeControls(image,p,expected,live);
  if(dynamic_cast<SipaProcessor*>(&p))require(correlationRasterMatches(image,0,false),name+" has fabricated correlation or missing zero reference");
  if(dynamic_cast<SmartGateProcessor*>(&p)||dynamic_cast<PlosiveProcessor*>(&p)){auto* source=dynamic_cast<FoundationDisplaySource*>(&p);FoundationDisplayFrame frame;const bool valid=p.meterSnapshot().hasProcessedAudio&&source&&source->displayTelemetry().read(source->displayTelemetry().latestSerial(),frame);require(verifyFoundationReduction(image,p,valid?frame.values[10]:0,valid,&failure),name+": "+failure);assertFoundationReductionNegativeControl(image,p,valid?frame.values[10]:0,valid);}
 }
 else {require(actualAnalysisLitPixels(image,lanes)==0,name+" displays a fictitious analysis level bar");
  auto falseSignal=image.createCopy();{juce::Graphics g(falseSignal);g.setColour(juce::Colours::cyan);const auto c=lanes.front().getCentre();g.fillRect(int(c.x*image.getWidth()/1672.f)-1,int(c.y*image.getHeight()/941.f)-1,3,3);}
  require(actualAnalysisLitPixels(falseSignal,lanes)>0,"Analysis no-signal raster check failed its fabricated-bar negative control");}
 save(image,name+"-"+juce::String(scale)+"x");}}

bool actualReadoutMatches(const juce::Image& image,juce::Rectangle<float> area,const juce::String& text,float font,float width=1280,float height=720){
 juce::Image expected(juce::Image::ARGB,image.getWidth(),image.getHeight(),true);juce::Graphics g(expected);const float sx=image.getWidth()/width,sy=image.getHeight()/height;g.addTransform(juce::AffineTransform::scale(sx,sy));
 ui::hardwareGlass(g,area,false);g.setColour(juce::Colour(0xffffc374));g.setFont(ui::hardwareFont(font));g.drawFittedText(text,area.reduced(6,2).toNearestInt(),juce::Justification::centred,2);
 for(int y=int(std::ceil((area.getY()+2)*sy));y<int(std::floor((area.getBottom()-2)*sy));++y)for(int x=int(std::ceil((area.getX()+3)*sx));x<int(std::floor((area.getRight()-3)*sx));++x){auto a=image.getPixelAt(x,y),b=expected.getPixelAt(x,y);if(std::abs(int(a.getRed())-b.getRed())>3||std::abs(int(a.getGreen())-b.getGreen())>3||std::abs(int(a.getBlue())-b.getBlue())>3)return false;}return true;
}
void assertGenericReadout(const juce::Image& image,const juce::String& text){
 require(actualReadoutMatches(image,{179,418,935,27},text,14),"Actual generic numeric strip has incorrect units, value, or aperture");
 require(!actualReadoutMatches(image,{179,418,935,27},"CORRUPTED / +99.99 dB",14),"Wrong generic numeric strip passed same verifier");
}
// Worker-backed editors must consume queued samples. Levels-only Band and
// Parallel/Exciter and Restoration editors require a fresh coherent epoch/block publish.
void freshProcess(ModuleProcessor& p,juce::AudioBuffer<float>& b){
 if(dynamic_cast<BandModuleProcessor*>(&p)||dynamic_cast<ParallelProcessor*>(&p)||dynamic_cast<ExciterProcessor*>(&p)||restorationMeterLayout(p)){
  auto* telemetry=p.pluginTelemetry();require(telemetry&&!telemetry->analysis.operational(),"Levels-only telemetry unexpectedly started an analysis worker");
  PluginTelemetry::Levels old,now;telemetry->read(old);const auto epoch=telemetry->analysis.epoch();process(p,b);
  require(telemetry->read(now)&&now.valid&&now.epoch==epoch&&now.blocks!=old.blocks&&now.samples==unsigned(b.getNumSamples())&&now.rate==p.actualRate.load(),"Levels-only audio failed to publish a fresh coherent level block");return;
 }
 const double before=juce::Time::getMillisecondCounterHiRes();process(p,b);
 auto* t=p.pluginTelemetry();require(t!=nullptr,"Missing actual editor telemetry source");
 for(int i=0;i<300&&t->analysis.lastConsumedAt()<before;++i)juce::Thread::sleep(2);
 const double consumed=t->analysis.lastConsumedAt();
 require(consumed>=before&&juce::Time::getMillisecondCounterHiRes()-consumed<750,"Actual analysis worker did not consume fresh processor audio");
}
// Test-owned approved native-pair typography. Values and aperture geometry are
// independent; no production readout/paint/layout helper is used as an oracle.
void nativeChassisText(juce::Graphics& g,juce::Rectangle<float> area,const juce::String& text,float size,juce::Colour colour) {
 g.setColour(colour);g.setFont(juce::Font(juce::FontOptions("Arial Narrow",size,juce::Font::plain)));
 g.drawFittedText(text,area.toNearestInt(),juce::Justification::centred,2);
}
bool nativeChassisAreaMatches(const juce::Image& actual,const juce::Image& expected,juce::Rectangle<float> area){
 const auto pixels=chassisMeterPixels(actual,area);
 for(int y=int(std::ceil(pixels.getY()))+1;y<int(std::floor(pixels.getBottom()))-1;++y)
 for(int x=int(std::ceil(pixels.getX()))+1;x<int(std::floor(pixels.getRight()))-1;++x){
  const auto a=actual.getPixelAt(x,y),b=expected.getPixelAt(x,y);
  if(std::abs(int(a.getRed())-b.getRed())>3||std::abs(int(a.getGreen())-b.getGreen())>3||std::abs(int(a.getBlue())-b.getBlue())>3)return false;
 }return true;
}
bool nativeChassisReadoutMatches(const juce::Image& image,juce::Rectangle<float> area,const juce::String& text,float size){
 juce::Image expected(juce::Image::ARGB,image.getWidth(),image.getHeight(),true);juce::Graphics g(expected);
 g.addTransform(juce::AffineTransform::scale(image.getWidth()/1672.f,image.getHeight()/941.f));
 g.setColour(juce::Colour(0xff020709));g.fillRect(area);
 nativeChassisText(g,area,text,size,juce::Colour(0xffffc374));
 return nativeChassisAreaMatches(image,expected,area);
}
juce::Image expectedCorrelation(const juce::Image& image,float value,bool available){
 juce::Image expected(juce::Image::ARGB,image.getWidth(),image.getHeight(),true);juce::Graphics g(expected);
 g.addTransform(juce::AffineTransform::scale(image.getWidth()/1672.f,image.getHeight()/941.f));
 g.reduceClipRegion(juce::Rectangle<int>(858,303,489,80));g.setColour(juce::Colour(0xff020709));g.fillRect(858,303,489,80);
 g.setColour(juce::Colour(0xff71838b));g.drawLine(869,348,1337,348,1);g.drawLine(1103,312,1103,371,1);
 // Independent signed -1..+1 mapping: 468 units with a centre at1103.
 if(available){const float x=1103+std::clamp(value,-1.f,1.f)*234;
  g.setColour(juce::Colour(value<0?0xffffb36b:value>0?0xff00cddd:0xffd9e5ea));
  if(value==0)g.fillRect(1102.f,341.f,2.f,14.f);else g.fillRect(std::min(x,1103.f),342.f,std::abs(x-1103),12.f);
 }
 nativeChassisText(g,{860,361,65,20},"-1",12,juce::Colour(0xffb8dce5));
 nativeChassisText(g,{1078,361,50,20},"0",12,juce::Colour(0xffb8dce5));
 nativeChassisText(g,{1280,361,65,20},"+1",12,juce::Colour(0xffb8dce5));
 return expected;
}
bool correlationRasterMatches(const juce::Image& image,float value,bool available=true){
 return nativeChassisAreaMatches(image,expectedCorrelation(image,value,available),{858,303,489,80});
}
void signedRaster(ModuleProcessor& p,juce::AudioProcessorEditor& editor,const juce::String& name){
 const auto readout=ui::pluginMeterReadout(p);require(dynamic_cast<SipaProcessor*>(&p)!=nullptr,"Unsupported signed raster editor");
 require(readout.signedScale&&readout.available,"Expected available signed measurement");
 const std::array<juce::Image,2> images{{chassisRaster(editor,1),chassisRaster(editor,2)}};
 for(int scale:{1,2}){const auto& image=images[size_t(scale-1)];require(correlationRasterMatches(image,readout.value),"SIPA approved signed aperture/zero-reference is incorrect");
  const auto value=(readout.value>0?juce::String("+"):juce::String())+juce::String(readout.value,3);
  require(nativeChassisReadoutMatches(image,{1007,406,192,34},value,20),"SIPA actual numerical correlation differs from calibrated fixture");
  require(!nativeChassisReadoutMatches(image,{1007,406,192,34},"+9.999",20),"SIPA incorrect number passed the same numeric verifier");
  auto corrupt=image.createCopy();{juce::Graphics g(corrupt);g.setColour(juce::Colours::black);g.fillRect(chassisMeterPixels(image,{858,303,489,80}));}
  require(!correlationRasterMatches(corrupt,readout.value),"Erased correlation/zero-reference passed the same verifier");
  const float wrongValue=readout.value==0?.5f:-readout.value;
  auto wrongSign=image.createCopy();const auto wrong=expectedCorrelation(image,wrongValue,true);
  const auto bounds=chassisMeterPixels(image,{858,303,489,80}).getSmallestIntegerContainer();
  for(int y=bounds.getY();y<bounds.getBottom();++y)for(int x=bounds.getX();x<bounds.getRight();++x)wrongSign.setPixelAt(x,y,wrong.getPixelAt(x,y));
  require(!correlationRasterMatches(wrongSign,readout.value),"Wrong-sign/fake correlation passed the same verifier");
  save(image,name+"-"+juce::String(scale)+"x");
 }
}
// Expected strings are assembled independently of the production gain labels.
// The original signed preMix/postMix sample-based assertions remain below.
juce::Image expectedGainPanels(const juce::Image& image,const std::array<float,4>& gains){
 juce::Image expected(juce::Image::ARGB,image.getWidth(),image.getHeight(),true);juce::Graphics g(expected);
 g.addTransform(juce::AffineTransform::scale(image.getWidth()/1672.f,image.getHeight()/941.f));
 const auto number=[](float v){return(v>0?juce::String("+"):juce::String())+juce::String(v,2);};
 for(int branch=0;branch<2;++branch){
  const auto heading=branch?juce::Rectangle<float>(1447,451,151,23):juce::Rectangle<float>(76,451,150,23);
  const auto read=branch?juce::Rectangle<float>(1454,482,135,31):juce::Rectangle<float>(83,482,136,31);
  g.setColour(juce::Colour(0xff020709));g.fillRect(heading);g.fillRect(read);
  nativeChassisText(g,heading,branch?"SUSTAIN GAIN dB":"ATTACK GAIN dB",17,juce::Colours::whitesmoke);
  nativeChassisText(g,read,"PRE "+number(gains[size_t(branch)])+"\nPOST "+number(gains[size_t(branch+2)]),13,juce::Colour(0xffffc374));
 }return expected;
}
bool gainPanelsMatch(const juce::Image& actual,const juce::Image& expected){
 for(const auto area:std::array<juce::Rectangle<float>,4>{{{76,451,150,23},{1447,451,151,23},{83,482,136,31},{1454,482,135,31}}})
  if(!nativeChassisAreaMatches(actual,expected,area))return false;
 return true;
}
void transientRaster(ModuleProcessor& p,juce::AudioProcessorEditor& editor,float mix,const juce::String& name){
 PluginTelemetry::Levels levels;require(p.pluginTelemetry()->read(levels)&&levels.valid,"Transient levels unavailable");
 std::array<float,4> expected=levels.gains;
 for(int branch=0;branch<2;++branch){expected[branch+2]=20*std::log10(1+mix*(std::pow(10.f,expected[branch]/20)-1));near(levels.gains[branch+2],expected[branch+2],.002f,"Transient branch postMix is not independently calibrated or includes output trim");}
 const std::array<juce::Image,2> images{{chassisRaster(editor,1),chassisRaster(editor,2)}};
 for(int scale:{1,2}){const auto& image=images[size_t(scale-1)];const auto reference=expectedGainPanels(image,expected);
  require(gainPanelsMatch(image,reference),"Actual transient numeric branch panels differ from independently expected preMix/postMix values");
  for(size_t channel=0;channel<4;++channel){auto wrong=expected;wrong[channel]+=1;
   require(!gainPanelsMatch(image,expectedGainPanels(image,wrong)),"Corrupted transient pre/post branch number passed same verifier");}
  auto erased=image.createCopy();{juce::Graphics g(erased);g.setColour(juce::Colours::black);g.fillRect(chassisMeterPixels(image,{83,482,136,31}));}
  require(!gainPanelsMatch(erased,reference),"Erased transient values passed same verifier");
  save(image,name+"-"+juce::String(scale)+"x");}
}
bool reductionGraphMatches(const juce::Image& image,const std::vector<float>& reductions){
 const float sx=image.getWidth()/1280.f,sy=image.getHeight()/720.f;
 juce::Image expected(juce::Image::ARGB,image.getWidth(),image.getHeight(),true);juce::Graphics g(expected);g.addTransform(juce::AffineTransform::scale(sx,sy));juce::Path curve;
 for(size_t i=0;i<reductions.size();++i){const float x=1070-float(reductions.size()-1-i)*256.f/48000.f/4.f*848;
  const float y=191+std::clamp(reductions[i],0.f,24.f)*174/24;
  if(i==0)curve.startNewSubPath(x,y);else curve.lineTo(x,y);}
 g.setColour(juce::Colour(0xffffba50));g.strokePath(curve,juce::PathStrokeType(1.8f));
 const auto bright=[](juce::Colour c){return c.getRed()>220&&c.getGreen()>145&&c.getGreen()<210&&c.getBlue()<110;};
 int count=0;
 for(int y=int(192*sy);y<int(364*sy);++y)for(int x=int(223*sx);x<int(1072*sx);++x){const auto e=expected.getPixelAt(x,y);if(e.getAlpha()>220){++count;if(!bright(image.getPixelAt(x,y)))return false;}
  if(bright(image.getPixelAt(x,y))&&e.getAlpha()<100)return false;}
 return count>10;
}
void actualReductionRaster(ModuleProcessor& p,const std::vector<float>& reductions){
 for(int scale:{1,2}){auto image=raster(p,scale);require(reductionGraphMatches(image,reductions),"Actual VCA history graph does not match measured dB/4-second geometry");assertGenericReadout(image,"DETECTOR GAIN REDUCTION / PRE-MIX + PRE-MAKEUP / dB  /  "+juce::String(reductions.back(),2)+" dB");
  auto bad=reductions;for(auto& value:bad)value+=3;require(!reductionGraphMatches(image,bad),"Wrong-calibration VCA graph passed same verifier");
  auto erased=image.createCopy();{juce::Graphics g(erased);g.setColour(juce::Colours::black);g.fillRect(222*scale,191*scale,850*scale,175*scale);}require(!reductionGraphMatches(erased,reductions),"Erased VCA graph passed same verifier");
  save(image,"vca-real-gr-"+juce::String(scale)+"x");}
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
        juce::AudioBuffer<float> b(2,256);b.clear();if(p->pluginTelemetry())freshProcess(*p,b);else process(*p,b);
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
    SipaProcessor p;prepare(p);std::unique_ptr<juce::AudioProcessorEditor> editor(p.createEditor());juce::AudioBuffer<float> b(2,256);
    constant(b,.5f,.5f);chassisFreshProcess(p,*editor,b);near(p.meterSnapshot().value,1,1.e-6f,"Correlated signal does not measure +1");signedRaster(p,*editor,"correlation-positive");
    constant(b,.5f,-.5f);chassisFreshProcess(p,*editor,b);near(p.meterSnapshot().value,-1,1.e-6f,"Anticorrelated signal does not measure -1");signedRaster(p,*editor,"correlation-negative");
    for(int i=0;i<256;++i){const float left[]={.5f,0,-.5f,0},right[]={0,.5f,0,-.5f};b.setSample(0,i,left[i%4]);b.setSample(1,i,right[i%4]);}
    chassisFreshProcess(p,*editor,b);near(p.meterSnapshot().value,0,1.e-6f,"Orthogonal signal does not measure zero correlation");require(p.meterSnapshot().valueAvailable,"Measured zero correlation was treated as silence");signedRaster(p,*editor,"correlation-zero");
    for(float correlation:{-.5f,.5f}){for(int i=0;i<256;++i){const float left=.5f*std::sin(float(2*pi*i/64)),quadrature=.5f*std::cos(float(2*pi*i/64));b.setSample(0,i,left);b.setSample(1,i,correlation*left+std::sqrt(1-correlation*correlation)*quadrature);}chassisFreshProcess(p,*editor,b);near(p.meterSnapshot().value,correlation,1.e-6f,"Half-scale correlation fixture is incorrect");signedRaster(p,*editor,"correlation-half-"+juce::String(correlation,1));}
    const auto undefinedRaster=[&](const juce::String& name){
        const std::array<juce::Image,2> images{{chassisRaster(*editor,1),chassisRaster(*editor,2)}};
        for(int scale:{1,2}){const auto& image=images[size_t(scale-1)];
            require(correlationRasterMatches(image,0,false),"Undefined correlation retained a fabricated signed bar");
            require(nativeChassisReadoutMatches(image,{1007,406,192,34},"UNDEFINED",20),"Live undefined correlation was presented as a value or stale state");
            require(!correlationRasterMatches(image,.5f,true),"Fabricated positive correlation passed the undefined verifier");
            save(image,name+"-"+juce::String(scale)+"x");
        }
    };
    constant(b,.5f,0);chassisFreshProcess(p,*editor,b);require(!p.meterSnapshot().valueAvailable,"One silent channel has a fabricated correlation");undefinedRaster("correlation-live-one-channel");noBar(p,"correlation-one-channel");
    constant(b,0,0);chassisFreshProcess(p,*editor,b);require(!p.meterSnapshot().valueAvailable,"Silence has a fabricated correlation");undefinedRaster("correlation-live-silence");
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
    freshProcess(p,b);PluginTelemetry::Levels expectedLevels;require(p.pluginTelemetry()->read(expectedLevels),"Monitor levels missing");
    for(int scale:{1,2}){const auto image=raster(p,scale);juce::String failure;require(verifyActualLevels(image,p,expectedLevels,true,&failure),failure);assertActualLevelNegativeControls(image,p,expectedLevels,true);assertGenericReadout(image,"OUTPUT SAMPLE PEAK / dBFS  /  -6.02 dBFS");save(image,"sample-peak-half-scale-"+juce::String(scale)+"x");}
    constant(b,0,0);process(p,b);const auto silent=ui::pluginMeterReadout(p);require(silent.available&&std::isinf(silent.value)&&silent.value<0&&silent.valueText.contains("-inf dBFS"),"Silent peak is displayed as a fabricated finite floor");
    constant(b,2,1);process(p,b);const auto over=ui::pluginMeterReadout(p);near(over.value,6.0206f,.0001f,"Over-scale numeric peak was silently clamped");require(over.valueText.contains("ABOVE SCALE"),"Over-scale peak lacks explicit indication");
}
void testGainReduction(){
    VcaProcessor p;prepare(p);parameter(p,"threshold",-24);parameter(p,"ratio",4);parameter(p,"attack",1);
    juce::AudioBuffer<float> b(2,256);std::vector<float> reductions;for(int i=0;i<50;++i){constant(b,.5f,.5f);freshProcess(p,b);reductions.push_back(p.meterSnapshot().value);}
    const auto s=p.meterSnapshot();const float expected=(20.f*std::log10(.5f)+24)*.75f;
    near(s.value,expected,.015f,"VCA GR does not match calibrated 4:1 compression");near(s.value,-20.f*std::log10(peak(b)/.5f),.015f,"VCA GR does not describe actual full-wet output attenuation");
    auto readout=ui::pluginMeterReadout(p);require(readout.minimum==0&&readout.maximum==24&&readout.fillStart()==0,"GR scale is not 0..24 dB from zero");
    actualReductionRaster(p,reductions);
    b.clear();process(p,b);readout=ui::pluginMeterReadout(p);require(readout.available&&!readout.snapshot.signalPresent&&readout.value>0&&readout.valueText.contains("SILENT OUTPUT"),"Real detector release into silence is mislabeled");
}
void testSignedGain(){
    for(const float amount:{-12.f,0.f,12.f}){
        TransientProcessor p;prepare(p);std::unique_ptr<juce::AudioProcessorEditor> editor(p.createEditor());parameter(p,"attack",amount);juce::AudioBuffer<float> b(2,1024);b.clear();process(p,b);constant(b,.25f,.25f);chassisFreshProcess(p,*editor,b);
        const auto readout=ui::pluginMeterReadout(p);float measured=0;for(int i=0;i<b.getNumSamples();++i){const float change=20.f*std::log10(b.getSample(0,i)/.25f);if(std::abs(change)>std::abs(measured))measured=change;}
        near(readout.value,measured,.001f,"Transient pre-mix envelope reading differs from actual full-wet gain");
        require(readout.minimum==-12&&readout.maximum==12&&readout.zeroPosition()==.5f,"Signed transient calibration lacks zero centre");
        require(readout.heading.contains("PRE-MIX")&&readout.heading.contains("PRE-OUTPUT"),"Transient meter suggests it includes mix/output trim");
        if(amount==0)require(readout.fillStart()==.5f&&readout.fillEnd()==.5f,"Zero gain has a half-full geometry");
        else require(readout.value*amount>0&&std::abs(readout.value)<=12,"Transient gain change lost its sign or bounds");
        transientRaster(p,*editor,1.f,"transient-"+juce::String(amount,0));
    }
}
void testFirEstimate(){
    TruePeakProcessor p;prepare(p,11025,256);parameter(p,"ceiling",-6);
    require(p.getLatencySamples()==56,"Limiter is not reporting ceil(rate*5ms) actual latency");
    auto readout=ui::pluginMeterReadout(p);require(readout.heading.contains("ESTIMATE")&&readout.heading.contains("8x, 32-TAP")&&readout.detail.contains("56 samples / 5.079 ms"),"Limiter uses an unqualified TP claim or hardcoded 5 ms latency");
    Reconstruction filter;std::array<std::array<float,Reconstruction::taps>,2> history{};int position=0;juce::AudioBuffer<float> b(2,256);
    float reference=0;for(int block=0;block<12;++block){for(int i=0;i<256;++i){const float sample=.95f*std::sin(float(2*pi*1000*(block*256+i)/11025));b.setSample(0,i,sample);b.setSample(1,i,-sample);}freshProcess(p,b);reference=0;
        for(int i=0;i<256;++i){for(int ch=0;ch<2;++ch){history[size_t(ch)][size_t(position)]=b.getSample(ch,i);for(const auto& phase:filter.h){double v=0;for(int k=0;k<Reconstruction::taps;++k)v+=phase[size_t(k)]*history[size_t(ch)][size_t((position-k+Reconstruction::taps)%Reconstruction::taps)];reference=std::max(reference,float(std::abs(v)));}}position=(position+1)%Reconstruction::taps;}
        near(p.meterSnapshot().value,reference,1.e-6f,"Limiter meter does not match the actual output's defined 8x FIR reconstruction");
    }
    readout=ui::pluginMeterReadout(p);require(readout.available&&readout.snapshot.auxiliary>0&&readout.valueText.contains("dBTP EST.")&&readout.valueText.contains("GR"),"Measured limiter estimate/reduction is missing");require(reference<=gain(-6)+1.e-5f,"Defined FIR estimate exceeds settled limiter ceiling");
    for(int scale:{1,2}){const auto image=raster(p,scale);const auto numeric="OUTPUT PEAK ESTIMATE / 8x, 32-TAP WINDOWED-SINC FIR / dBTP  /  "+juce::String(20.f*std::log10(reference),2)+" dBTP EST. / GR "+juce::String(p.meterSnapshot().auxiliary,2)+" dB";assertGenericReadout(image,numeric);save(image,"fir-estimate-"+juce::String(scale)+"x");}
}
void testAllActualLevelLayouts(){
 int checked=0;
 for(const auto& entry:PluginRegistry::entries()){
  auto p=entry.create();if(chassisMeterLayout(*p)||dynamic_cast<BandModuleProcessor*>(p.get())||nonlinearMeterLayout(*p)||restorationMeterLayout(*p)||!analysisMeterLanes(*p).empty())continue;
  prepare(*p);juce::AudioBuffer<float> b(2,256);
  PluginTelemetry::Levels expected;
  for(int block=0;block<40;++block){for(int i=0;i<256;++i){const float phase=float(2*pi*(block*256+i)/64);b.setSample(0,i,.5f*std::sin(phase));b.setSample(1,i,.25f*std::cos(phase));}
   const std::array<float,2> input{b.getMagnitude(0,0,256),b.getMagnitude(1,0,256)};freshProcess(*p,b);require(p->pluginTelemetry()->read(expected)&&expected.valid,"Live level capture unavailable");
   for(int ch=0;ch<2;++ch){near(expected.peak[ch],input[ch],1.e-6f,"Actual input aperture's published peak differs from known input");near(expected.peak[ch+2],b.getMagnitude(ch,0,256),1.e-6f,"Actual output aperture's published peak differs from processed samples");}}
  for(int scale:{1,2}){const auto image=raster(*p,scale);juce::String failure;require(verifyActualLevels(image,*p,expected,true,&failure),juce::String(entry.id)+": "+failure);assertActualLevelNegativeControls(image,*p,expected,true);
   if(dynamic_cast<SmartGateProcessor*>(p.get())||dynamic_cast<PlosiveProcessor*>(p.get())){auto* source=dynamic_cast<FoundationDisplaySource*>(p.get());FoundationDisplayFrame frame;require(source&&source->displayTelemetry().read(source->displayTelemetry().latestSerial(),frame),"Foundation measured frame absent");require(verifyFoundationReduction(image,*p,frame.values[10],true,&failure),failure);}
   save(image,juce::String(entry.id)+"-actual-levels-"+juce::String(scale)+"x");}
  ++checked;
 }
 require(checked==9,"Legacy-editor layout coverage changed; seven approved chassis and Tape have dedicated profiles");
}
void testRecoveredFamilyMeterCalibration(){
 int checked=0;
 for(const auto&entry:PluginRegistry::entries()){
  auto p=entry.create();const auto lanes=recoveredFamilyLanes(*p);if(lanes.empty())continue;prepare(*p);juce::AudioBuffer<float>b(2,256);
  // Publication-gated editors require a fresh callback after opening.
  std::unique_ptr<juce::AudioProcessorEditor> editor(p->createEditor());
  for(const auto amplitude:std::array<std::array<float,2>,3>{{{{.5f,.25f}},{{.1f,.01f}},{{1.2f,.005f}}}}){
   std::array<float,4> expected{};
   for(int block=0;block<40;++block){for(int i=0;i<256;++i){const float phase=float(2*pi*i/64);b.setSample(0,i,amplitude[0]*std::sin(phase));b.setSample(1,i,amplitude[1]*std::cos(phase));}
    expected[0]=b.getMagnitude(0,0,256);expected[1]=b.getMagnitude(1,0,256);freshProcess(*p,b);expected[2]=b.getMagnitude(0,0,256);expected[3]=b.getMagnitude(1,0,256);
    PluginTelemetry::Levels levels;require(p->pluginTelemetry()&&p->pluginTelemetry()->read(levels)&&levels.valid,"Recovered family actual telemetry missing");for(int c=0;c<4;++c)near(levels.peak[size_t(c)],expected[size_t(c)],1.e-6f,"Recovered family telemetry differs from real input/output");}
   std::array<juce::Image,2> images;
   for(int scale:{1,2}){const auto image=rasterEditor(*editor,scale);images[size_t(scale-1)]=image;require(verifyRecoveredFamilyLevels(image,*p,expected,true),juce::String(entry.id)+" actual24-segment levels miscalibrated");
    auto erased=image.createCopy();const auto lane=lanes[0];{juce::Graphics g(erased);g.setColour(juce::Colour(0xff10232b));g.fillRect(juce::Rectangle<float>(lane.getX()*image.getWidth()/1672.f,lane.getY()*image.getHeight()/941.f,lane.getWidth()*image.getWidth()/1672.f,lane.getHeight()*image.getHeight()/941.f));}
    require(!verifyRecoveredFamilyLevels(erased,*p,expected,true),"Recovered meter verifier accepted erased real signal");
    auto wrong=expected;wrong[0]*=.01f;require(!verifyRecoveredFamilyLevels(image,*p,wrong,true),"Recovered meter verifier accepted wrong amplitude");
    auto swapped=expected;std::swap(swapped[0],swapped[1]);require(!verifyRecoveredFamilyLevels(image,*p,swapped,true),"Recovered meter verifier accepted swapped stereo lanes");
   }
   // Encode only after both freshness-sensitive assertions. PNG compression
   // is not an audio callback and must not age the 2x publication fixture.
   for(int scale:{1,2})save(images[size_t(scale-1)],juce::String(entry.id)+"-calibrated-"+juce::String(amplitude[0],3)+"-"+juce::String(scale)+"x");}
  p->releaseResources();for(int scale:{1,2}){const auto image=rasterEditor(*editor,scale);require(verifyRecoveredFamilyLevels(image,*p,{},false),"Released family meter retained stale instantaneous signal");}
  ++checked;
 }
 require(checked==6,"Reverb/Modulation/Delay meter coverage changed; add an explicit independent profile");
}
void testTransientMixAndTrim(){
 for(float mix:{0.f,.5f,1.f}){TransientProcessor p;prepare(p);std::unique_ptr<juce::AudioProcessorEditor> editor(p.createEditor());parameter(p,"attack",12);parameter(p,"sustain",-12);parameter(p,"mix",mix*100);parameter(p,"output",-6);juce::AudioBuffer<float>b(2,256);
  // The independently specified mix is a settled control value. The DSP ramps
  // from unity mix at prepare; warm it with real silence before testing it.
  b.clear();for(int warm=0;warm<80;++warm)chassisFreshProcess(p,*editor,b);
  bool attack=false,sustain=false;
  for(int block=0;block<150;++block){for(int i=0;i<256;++i){const float value=(block%60<15?.4f:.002f)*std::sin(float(block*256+i)*.17f);b.setSample(0,i,value);b.setSample(1,i,value);}chassisFreshProcess(p,*editor,b);PluginTelemetry::Levels levels;require(p.pluginTelemetry()->read(levels),"Missing mixed transient levels");attack|=levels.gains[0]>1;sustain|=levels.gains[1]<-1;
   for(int branch=0;branch<2;++branch){const float expected=20*std::log10(1+mix*(std::pow(10.f,levels.gains[branch]/20)-1));near(levels.gains[branch+2],expected,.002f,"Transient applied branch includes output trim or wrong mix");}}
  require(attack&&sustain,"Mixed transient fixture did not activate both signed branches");transientRaster(p,*editor,mix,"transient-mix-"+juce::String(mix,1)+"-output-minus6");}
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
    testLifecycleAndSilence();testCorrelation();testSamplePeaks();testGainReduction();testSignedGain();testTransientMixAndTrim();testAllActualLevelLayouts();testChassisMeterCalibration();testRecoveredFamilyMeterCalibration();testNonlinearMeterCalibration();testRestorationMeterCalibration();testFirEstimate();testSnapshotCoherence();testReprepareMetadataCoherence();
    std::cout<<"PASS: "<<PluginRegistry::entries().size()<<" native processor readiness/reset/real-silence states; correlation +/-1,+/-.5,zero/undefined; actual four-lane segmented I/O meters and VCA GR history; independent approved 01/05/07/08/10/11/12 peak/300 ms RMS, all-lane mutants, nonfinite audio and publication lifecycle; independent approved 26/27/28 nonlinear stereo peak/300 ms RMS calibration, mutation controls and publication lifecycle; independent original 02/06/09 restoration stereo peak/300 ms RMS calibration, all-lane mutants and prepare/reset/release/publication lifecycle; numeric transient pre/post-mix and output-trim exclusion; defined 8x FIR output estimate and real latency; coherent atomic snapshots and concurrent reprepare rate/latency metadata; actual native editor 1x/2x calibrated raster\n";return 0;
}catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}}
