#pragma once
#include "Processors.h"
#include "../DSP/ExpandedBandCore.h"
#include "BandStateMigration.h"
#include <cstdlib>
#include <stdexcept>
namespace hem {
class BandModuleProcessor:public ModuleProcessor {
public:
    BandModuleProcessor(const char* root,const char* title,std::vector<ParameterSpec> p,PluginMeterKind meter,bands::Kind k)
        :ModuleProcessor(root,title,std::move(p),meter),bandKind(k){actualRate.store(0);}
    const bands::Kind bandKind;
    virtual bands::Snapshot bandSnapshot() const noexcept=0;
    virtual bool visualSnapshot(bands::VisualFrame&) const noexcept=0;
    virtual void subscribeVisuals(bool) noexcept=0;
    virtual bool migrateParameters(const juce::ValueTree&,juce::ValueTree&,juce::String&) const=0;
    juce::AudioProcessorEditor* createEditor() override;
    bool usesWetScaledReductionLabel() const noexcept override {return false;}
    juce::String meterLabel() const override {
        if(bandKind==bands::Kind::Dynamic)return "MAX ABS BAND DYNAMIC CHANGE / NOT BROADBAND GR / dB";
        if(bandKind==bands::Kind::Dynamics)return "MAX BAND COMPRESSOR GR / BEFORE MIX & MAKEUP / dB";
        return bandKind==bands::Kind::Imager?"OUTPUT L/R CORRELATION":"OUTPUT SAMPLE PEAK / dBFS";
    }
    juce::String helpText() const override {
        if(bandKind==bands::Kind::Dynamic)return "8 independent bell/shelf bands. UP boosts above threshold; DOWN cuts. Detectors read original input. Times = 63.2% error removed.";
        if(bandKind==bands::Kind::MidSide)return "Five bands per lane. M=(L+R)/2, S=(L-R)/2. WIDTH scales filtered SIDE. Lane SOLO is saved and rendered.";
        return "LR4 all-pass sum: magnitude-flat, nonlinear phase. MIX uses phase-matched crossover bands, not raw bypass. SOLO is saved/rendered. Legacy mode retains three bands.";
    }
    bool parameterActive(const juce::String& id) const override {
        auto raw=[this](const juce::String& key){const auto* p=state.getRawParameterValue(key);return p?p->load():0.f;};
        if(bandKind==bands::Kind::Dynamics||bandKind==bands::Kind::Imager){const bool modern=raw("topology")>.5f;
            if(id=="xMid")return modern;
            if(id=="legacyMidOn")return !modern;
            if(id.startsWith("lowMid")||id.startsWith("highMid")){if(!modern)return false;}
            else if(id.startsWith("mid")){if(modern)return false;}
            for(const auto* prefix:{"lowMid","highMid","low","mid","high"}){const juce::String start(prefix);
                if(!id.startsWith(start))continue;
                if(id==start+"On"||id==start+"Solo")return true;
                const auto on=start=="mid"?juce::String("legacyMidOn"):start+"On";
                if(raw(on)<.5f)return false;
                if(bandKind==bands::Kind::Imager)return id!="lowWidth"||raw("lowMono")<.5f;
                if(id==start+"Ratio"||id==start+"Range"||id==start+"Makeup")return true;
                return raw(start+"Ratio")>1&&raw(start+"Range")>0;
            }return true;
        }else if(id!="enabled"&&id!="output"&&id!="width"&&id!="midSolo"&&id!="sideSolo"){
            const auto prefix=id.substring(0,2);if(id!=prefix+"On"&&raw(prefix+"On")<.5f)return false;
            if(bandKind==bands::Kind::Dynamic){if(id.endsWith("Threshold")||id.endsWith("Attack")||id.endsWith("Release"))return raw(prefix+"Range")>0&&raw(prefix+"DynamicOn")>.5f;
                if(id.endsWith("Range")||id.endsWith("Up"))return raw(prefix+"DynamicOn")>.5f;}
        }
        return true;
    }
};
template<bands::Kind K> class BandProcessor:public BandModuleProcessor {
public:
    BandProcessor(const char* root,const char* title):BandModuleProcessor(root,title,definitions(),
        K==bands::Kind::Imager?PluginMeterKind::correlation:K==bands::Kind::MidSide?PluginMeterKind::samplePeak:PluginMeterKind::gainReduction,K){
        // Use the same physical/normalised conversion at construction as at
        // host edits and recall, including JUCE's skewed-frequency rounding.
        for(const auto& spec:parameterSpecs()){auto* parameter=state.getParameter(spec.id);
            parameter->setValueNotifyingHost(parameter->convertTo0to1(spec.initial));}
        state.state.setProperty("bandSchemaVersion",2,nullptr);
    }
    void prepareToPlay(double rate,int) override {
        ready.store(false);actualRate.store(0);resetMeterTelemetry();
        if(!core.prepare(rate,settings()))throw std::invalid_argument("Band modules require finite sample rates from 8000 to 384000 Hz");
        actualRate.store(rate);setLatencySamples(0);publishMeterPreparation();ready.store(true);
    }
    bool processingReady() const noexcept override {return ready.load();}
    void releaseResources() override {ready.store(false);actualRate.store(0);resetMeterTelemetry();ModuleProcessor::releaseResources();}
    void processBlock(juce::AudioBuffer<float>& b,juce::MidiBuffer&) override {
        juce::ScopedNoDenormals noDenormals;
        if(!processingReady()){b.clear();return;}
        if(b.getNumChannels()<2||b.getNumSamples()<=0)return;
        captureInput(b);
        core.process(b.getWritePointer(0),b.getWritePointer(1),b.getNumSamples(),settings());
        publishMeterBlock(b,core.telemetry.read().outputPeak);
    }
    bands::Snapshot bandSnapshot() const noexcept override {return core.telemetry.read();}
    bool visualSnapshot(bands::VisualFrame& frame) const noexcept override {return core.visuals.read(frame);}
    void subscribeVisuals(bool enabled) noexcept override {core.visuals.subscribed.store(enabled);}
    bool migrateParameters(const juce::ValueTree& input,juce::ValueTree& output,juce::String& error) const override {return bands::migrateState<K>(input,state,output,error);}
    float meter() const noexcept override {
        const auto s=bandSnapshot();
        if constexpr(K==bands::Kind::Imager)return s.correlation;
        else if constexpr(K==bands::Kind::MidSide)return s.outputPeak;
        else return *std::max_element(s.cuts.begin(),s.cuts.end());
    }
    double getTailLengthSeconds() const override {
        if(value(0)<.5f)return 0;
        if constexpr(K==bands::Kind::Dynamic){for(size_t b=0;b<8;++b)if(value(66+b*3)!=0||value(68+b*3)>.5f)return 60;return 12;}
        else if constexpr(K==bands::Kind::MidSide){if(value(34)>.5f||value(38)>.5f)return 24;for(size_t b=0;b<10;++b)if(value(42+b)!=0)return 24;return 12;}
        else return value(bands::ExpandedCore<K>::count-1)>.5f?1.:.5;
    }
    void setStateInformation(const void* data,int bytes) override {
        // Entire validation and snapping happen off callback before one commit.
        if(!data||bytes<=0||bytes>8*1024*1024)return;
        const auto xml=getXmlFromBinary(data,bytes);if(!xml)return;
        const auto tree=juce::ValueTree::fromXml(*xml);if(!tree.hasType(state.state.getType()))return;
        juce::ValueTree parameters("PARAMETERS");for(int i=0;i<tree.getNumProperties();++i){const auto key=tree.getPropertyName(i);parameters.setProperty(key,tree[key],nullptr);}
        for(const auto child:tree)parameters.addChild(child.createCopy(),-1,nullptr);
        juce::ValueTree migrated;juce::String error;if(!migrateParameters(parameters,migrated,error))return;
        juce::ValueTree result(state.state.getType());result.setProperty("bandSchemaVersion",2,nullptr);
        for(const auto child:migrated)result.addChild(child.createCopy(),-1,nullptr);state.replaceState(result);
    }
private:
    static std::vector<ParameterSpec> definitions(){std::vector<ParameterSpec> p;
        for(const auto& s:bands::expandedSpecs<K>())p.push_back({s.id,s.name,s.minimum,s.maximum,s.initial,s.step,s.unit,s.toggle});return p;}
    bands::ExpandedSettings<K> settings() const noexcept {bands::ExpandedSettings<K> p{};for(size_t i=0;i<p.size();++i)p[i]=value(i);return p;}
    bands::ExpandedCore<K> core;std::atomic<bool> ready{false};
};
class DynamicEq8Processor final:public BandProcessor<bands::Kind::Dynamic>{public:DynamicEq8Processor():BandProcessor("DYNAMIC_EQ_8","Dynamic EQ 8") {}};
class MidSideEqProcessor final:public BandProcessor<bands::Kind::MidSide>{public:MidSideEqProcessor():BandProcessor("MID_SIDE_EQ","Mid/Side EQ") {}};
class MultibandDynamicsProcessor final:public BandProcessor<bands::Kind::Dynamics>{public:MultibandDynamicsProcessor():BandProcessor("MULTIBAND_DYNAMICS","Multiband Dynamics") {}};
class MultibandImagerProcessor final:public BandProcessor<bands::Kind::Imager>{public:MultibandImagerProcessor():BandProcessor("MULTIBAND_IMAGER","Multiband Imager") {}};
}
