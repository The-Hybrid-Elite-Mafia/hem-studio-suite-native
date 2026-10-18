#pragma once
#include "Processors.h"
#include "../DSP/FoundationCore.h"
#include "../DSP/FoundationTelemetry.h"
#include "../DSP/FoundationModes.h"
#include "HumAnalysisWorker.h"
#include <cstdlib>
#include <stdexcept>
namespace hem {
template<FoundationKind Kind> class FoundationProcessor : public ModuleProcessor, public FoundationDisplaySource, public HumAnalysisSource {
public:
    FoundationProcessor(const char* parameterId,const char* moduleTitle,std::vector<ParameterSpec> definitions,PluginMeterKind meterType)
        :ModuleProcessor(parameterId,moduleTitle,std::move(definitions),meterType) {actualRate.store(0);if constexpr(Kind==FoundationKind::hum)analyzer=std::make_unique<HumAnalysisWorker>();}
    void setStateInformation(const void* data,int bytes) override {
        // Message/staging thread only. Validate the complete tree before commit.
        if(!data||bytes<=0||bytes>8*1024*1024)return;
        const auto xml=getXmlFromBinary(data,bytes);if(!xml)return;
        const auto original=juce::ValueTree::fromXml(*xml);
        if(!original.hasType(state.state.getType()))return;
        auto tree=migrateParameterState(original);if(tree.isValid())state.replaceState(tree);
    }
    juce::ValueTree migrateParameterState(const juce::ValueTree& tree) const override {
        const auto& definitions=parameterSpecs();
        const int legacy=FoundationCore<Kind>::count;
        const bool old=tree.getNumChildren()==legacy;
        if(!old&&tree.getNumChildren()!=int(definitions.size()))return {};
        std::vector<bool> seen(definitions.size(),false);
        for(const auto child:tree){
            if(!child.hasType("PARAM")||child.getNumChildren()!=0||!child.hasProperty("id")||!child.hasProperty("value"))return {};
            const auto parameterId=child["id"].toString();size_t index=0;
            while(index<definitions.size()&&parameterId!=definitions[index].id)++index;
            if(index==definitions.size()||seen[index]||(old&&index>=size_t(legacy)))return {};seen[index]=true;
            const auto text=child["value"].toString().toStdString();char* end=nullptr;const double number=std::strtod(text.c_str(),&end);
            if(!std::isfinite(number)||std::abs(number)>std::numeric_limits<float>::max())return {};
            const auto& spec=definitions[index];
            // APVTS values are float. Compare in that domain so a decimal XML
            // round-trip of e.g. the 0.1f endpoint is not rejected by one double ULP.
            const auto parameterValue=static_cast<float>(number);
            if(end==text.c_str()||*end!='\0'||!std::isfinite(number)||parameterValue<spec.minimum||parameterValue>spec.maximum
                ||!std::isfinite(parameterValue)||(spec.toggle&&number!=0&&number!=1))return {};
        }
        auto result=tree.createCopy();
        if(old)for(size_t i=size_t(legacy);i<definitions.size();++i){juce::ValueTree child("PARAM");child.setProperty("id",definitions[i].id,nullptr);child.setProperty("value",definitions[i].initial,nullptr);result.addChild(child,-1,nullptr);}
        return result;
    }
    void prepareToPlay(double rate,int) override {
        ready.store(false);resetMeterTelemetry();actualRate.store(0);gateReduction.store(0);
        if constexpr(Kind==FoundationKind::hum)analyzer->stop();
        if(!core.prepare(rate,settings()))throw std::invalid_argument("Foundation modules require a finite sample rate from 8000 to 384000 Hz");
        modes.prepare(rate,lookahead(),listen());
        if constexpr(Kind==FoundationKind::hum)analyzer->prepare(rate);
        telemetry.prepare(core.sampleRate());actualRate.store(core.sampleRate());setLatencySamples(modes.latency());publishMeterPreparation();ready.store(true);
    }
    bool processingReady() const noexcept override {return ready.load();}
    void processBlock(juce::AudioBuffer<float>& buffer,juce::MidiBuffer&) override {
        juce::ScopedNoDenormals guard;
        if(!processingReady()){buffer.clear();return;}
        if(buffer.getNumChannels()<2||buffer.getNumSamples()<=0)return;
        captureInput(buffer);
        const auto p=settings();
        modes.targets(lookahead(),listen());
        if constexpr(Kind==FoundationKind::hum)analyzer->analysis.settings(p[0],int(std::lround(p[2])),value(7));
        float blockReduction=0;
        core.processTransformed(buffer.getWritePointer(0),buffer.getWritePointer(1),buffer.getNumSamples(),p,
            [this,&blockReduction](const auto& input,const auto& output,float gain,bool active) noexcept {if constexpr(Kind==FoundationKind::gate){gain=modes.reduction();blockReduction=std::max(blockReduction,gain);}telemetry.observe(input,output,gain,active,Kind==FoundationKind::hum?float(core.notchFrequency()):0.f);if constexpr(Kind==FoundationKind::hum)analyzer->analysis.observe(input);},
            [this](const auto& input,const auto& normal,const auto& controls,double gain) noexcept {return modes.process(input,normal,controls,gain);});
        if constexpr(Kind==FoundationKind::gate)gateReduction.store(blockReduction,std::memory_order_relaxed);
        publishMeterBlock(buffer);
    }
    const FoundationTelemetry& displayTelemetry() const noexcept override {return telemetry;}
    bool readHumAnalysis(HumAnalysisFrame& frame) const noexcept override {if constexpr(Kind==FoundationKind::hum)return ready.load()&&analyzer->analysis.read(frame)&&frame.fundamental==value(0)&&frame.harmonics==int(std::lround(value(2)))&&frame.thresholdDb==(-30.f-.6f*value(7));else return false;}
    float meter() const noexcept override {if constexpr(Kind==FoundationKind::gate)return gateReduction.load(std::memory_order_relaxed);else return core.measurement.load(std::memory_order_relaxed);}
    double getTailLengthSeconds() const override {
        return Kind==FoundationKind::cleaner?1.0:Kind==FoundationKind::plosive?.5:Kind==FoundationKind::hum?8.:0.;
    }
    juce::String meterLabel() const override {
        if constexpr(Kind==FoundationKind::gate)return "GATE PATH REDUCTION / PRE-OUTPUT / dB";
        else if constexpr(Kind==FoundationKind::rider)return "RIDING GAIN / PRE-MIX + PRE-OUTPUT / dB";
        else if constexpr(Kind==FoundationKind::plosive)return "LOW-BAND REDUCTION / dB";
        else return "OUTPUT SAMPLE PEAK / dBFS";
    }
    bool usesWetScaledReductionLabel() const noexcept override {return Kind==FoundationKind::plosive;}
    bool parameterActive(const juce::String& parameterId) const override {
        if constexpr(Kind==FoundationKind::cleaner)if(parameterId=="cutoff"||parameterId=="steep")return value(1)>.5f;
        if constexpr(Kind==FoundationKind::gate)if(parameterId=="ratio")return value(7)<.5f;
        return true;
    }
    juce::String helpText() const override {
        if constexpr(Kind==FoundationKind::cleaner)return "DC: fixed 5 Hz blocker. 24 dB/oct: two identical 12 dB sections. Subsonic controls inactive when off.";
        else if constexpr(Kind==FoundationKind::gate)return "Fixed 6 ms latency. Lookahead advances gain detection; Key Listen is delayed unity stereo input, excludes Output trim. 10 ms transitions; host bypass wins. Saved audition is printed/exported.";
        else if constexpr(Kind==FoundationKind::rider)return "Linked RMS riding, not voice recognition or LUFS. Both limits 0 = dry. 100 ms detector warm-up; no lookahead.";
        else if constexpr(Kind==FoundationKind::plosive)return "Listen Removed = aligned input minus normal output INCLUDING Mix and Output trim; Mix 0 with non-unity trim can produce signal. 10 ms transition; host bypass wins. Saved audition is printed/exported.";
        else return "Manual frequency, never auto-tuned. Continuous ~2 s measured windows; sensitivity sets -30 to -90 dBFS RMS plus 12 dB prominence. Removed = input minus full Mix/Output result; saved audition is printed/exported. 8 s tail.";
    }
protected:
    FoundationSettings settings() const noexcept {
        FoundationSettings p{};for(int i=0;i<FoundationCore<Kind>::count;++i)p[size_t(i)]=value(size_t(i));return p;
    }
private:
    float lookahead() const noexcept {if constexpr(Kind==FoundationKind::gate)return value(10);else return 0;}
    bool listen() const noexcept {if constexpr(Kind==FoundationKind::gate)return value(11)>.5f;else if constexpr(Kind==FoundationKind::plosive)return value(8)>.5f;else if constexpr(Kind==FoundationKind::hum)return value(6)>.5f;else return false;}
    FoundationTelemetry telemetry;FoundationCore<Kind> core;FoundationModes<Kind> modes;std::unique_ptr<HumAnalysisWorker> analyzer;std::atomic<bool> ready{false};std::atomic<float> gateReduction{0};
};
#define HEM_FOUNDATION_PROCESSOR(Name,Kind) class Name final:public FoundationProcessor<FoundationKind::Kind> { public: Name(); juce::AudioProcessorEditor* createEditor() override; };
HEM_FOUNDATION_PROCESSOR(DcSubsonicProcessor,cleaner)
HEM_FOUNDATION_PROCESSOR(SmartGateProcessor,gate)
HEM_FOUNDATION_PROCESSOR(VocalRiderProcessor,rider)
HEM_FOUNDATION_PROCESSOR(PlosiveProcessor,plosive)
HEM_FOUNDATION_PROCESSOR(HumBuzzProcessor,hum)
#undef HEM_FOUNDATION_PROCESSOR
}
