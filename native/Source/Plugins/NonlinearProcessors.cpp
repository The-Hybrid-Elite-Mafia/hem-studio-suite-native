#include "NonlinearProcessors.h"
#include "NonlinearEditor.h"
#include <cstdlib>
#include <cerrno>
namespace hem {
namespace {
void restoreNeutral(ModuleProcessor& p,const void* data,int bytes,const char* tag){
    // Message-thread transaction: reject malformed/unknown/duplicate fields,
    // non-finite, out-of-range or off-grid values; never partially apply.
    if(!data||bytes<=0||bytes>1024*1024)return;
    const auto xml=juce::AudioProcessor::getXmlFromBinary(data,bytes);if(!xml||!xml->hasTagName(tag))return;
    const auto incoming=juce::ValueTree::fromXml(*xml);const auto& specs=p.parameterSpecs();
    if(incoming.getNumProperties()!=0||incoming.getNumChildren()>int(specs.size()))return;
    std::vector<float> values;std::vector<bool> seen(specs.size(),false);for(const auto& spec:specs)values.push_back(spec.initial);
    for(const auto& child:incoming){
        if(!child.hasType("PARAM")||child.getNumChildren()!=0||child.getNumProperties()!=2||!child.hasProperty("id")||!child.hasProperty("value"))return;
        size_t index=0;while(index<specs.size()&&child["id"].toString()!=specs[index].id)++index;
        if(index==specs.size()||seen[index])return;seen[index]=true;
        const auto text=child["value"].toString().trim();if(text.isEmpty()||!text.containsOnly("0123456789+-.eE"))return;char* end=nullptr;errno=0;
        const double candidate=std::strtod(text.toRawUTF8(),&end);const auto& spec=specs[index];
        if(errno==ERANGE||!end||*end!=0||!std::isfinite(candidate)||candidate<spec.minimum||candidate>spec.maximum)return;
        const double snapped=spec.minimum+std::round((candidate-spec.minimum)/spec.step)*spec.step;
        if(std::abs(snapped-candidate)>std::max(1.e-5,double(spec.step)*1.e-3))return;
        values[index]=float(candidate);
    }
    auto merged=p.state.copyState();for(size_t i=0;i<specs.size();++i){auto child=merged.getChildWithProperty("id",specs[i].id);if(!child.isValid())return;child.setProperty("value",values[i],nullptr);}
    p.state.replaceState(merged);
}
}
ParallelProcessor::ParallelProcessor():ModuleProcessor("PARALLEL_DISTORTION","Parallel Distortion",{
    {"drive","DRIVE",0,24,0,.1f,"dB"},{"blend","BLEND",0,100,0,.1f,"%"},{"output","OUTPUT",-24,6,0,.1f,"dB"},
    {"type","DISTORTION TYPE",0,1,0,1,""},{"quality","OVERSAMPLING",0,1,1,1,""},
    {"soloWet","SOLO WET",0,1,0,1,"",true},{"bypass","BYPASS",0,1,0,1,"",true}}){}
void ParallelProcessor::prepareToPlay(double sr,int){ready.store(false);actualRate.store(0);resetMeterTelemetry();telemetry.reset();if(!std::isfinite(sr)||sr<=0)throw std::invalid_argument("Invalid nonlinear sample rate");actualRate.store(sr);core.prepare(sr);setLatencySamples(16);publishMeterPreparation();ready.store(true);}
void ParallelProcessor::processBlock(juce::AudioBuffer<float>& b,juce::MidiBuffer&){juce::ScopedNoDenormals guard;if(!processingReady()){b.clear();return;}if(b.getNumChannels()<2||b.getNumSamples()<=0)return;captureInput(b);
    auto* l=b.getWritePointer(0);auto* r=b.getWritePointer(1);telemetry.begin(l,r,b.getNumSamples());
    ParallelSettings p;p.drive=value(0);p.blend=value(1);p.output=value(2);p.type=int(value(3));p.quality=int(value(4));p.soloWet=value(5)>.5f;p.bypass=value(6)>.5f;
    core.process(l,r,b.getNumSamples(),p);telemetry.end(l,r,b.getNumSamples());publishMeterBlock(b);}
juce::AudioProcessorEditor* ParallelProcessor::createEditor(){return new OriginalNonlinearEditor(*this,0,telemetry);}
void ParallelProcessor::setStateInformation(const void* p,int n){restoreNeutral(*this,p,n,"PARALLEL_DISTORTION");}
ExciterProcessor::ExciterProcessor():ModuleProcessor("EXCITER","Exciter",{
    {"lowAmount","AMOUNT",0,100,0,.1f,"%"},{"lowMix","MIX",0,100,0,.1f,"%"},{"lowOn","BAND ON",0,1,0,1,"",true},{"lowSolo","SOLO",0,1,0,1,"",true},
    {"midAmount","AMOUNT",0,100,0,.1f,"%"},{"midMix","MIX",0,100,0,.1f,"%"},{"midOn","BAND ON",0,1,0,1,"",true},{"midSolo","SOLO",0,1,0,1,"",true},
    {"highAmount","AMOUNT",0,100,0,.1f,"%"},{"highMix","MIX",0,100,0,.1f,"%"},{"highOn","BAND ON",0,1,0,1,"",true},{"highSolo","SOLO",0,1,0,1,"",true},
    {"lowMid","LOW / MID",80,1500,250,1,"Hz"},{"midHigh","MID / HIGH",2000,12000,4000,1,"Hz"},{"output","OUTPUT",-24,6,0,.1f,"dB"},{"bypass","BYPASS",0,1,0,1,"",true}}){}
void ExciterProcessor::prepareToPlay(double sr,int){ready.store(false);actualRate.store(0);resetMeterTelemetry();telemetry.reset();if(!std::isfinite(sr)||sr<=0)throw std::invalid_argument("Invalid nonlinear sample rate");actualRate.store(sr);core.prepare(sr);setLatencySamples(16);publishMeterPreparation();ready.store(true);}
void ExciterProcessor::processBlock(juce::AudioBuffer<float>& b,juce::MidiBuffer&){juce::ScopedNoDenormals guard;if(!processingReady()){b.clear();return;}if(b.getNumChannels()<2||b.getNumSamples()<=0)return;captureInput(b);
    auto* l=b.getWritePointer(0);auto* r=b.getWritePointer(1);telemetry.begin(l,r,b.getNumSamples());ExciterSettings p;
    for(size_t j=0;j<3;++j){p.amount[j]=value(4*j);p.mix[j]=value(4*j+1);p.on[j]=value(4*j+2)>.5f;p.solo[j]=value(4*j+3)>.5f;}
    p.lowMid=value(12);p.midHigh=value(13);p.output=value(14);p.bypass=value(15)>.5f;core.process(l,r,b.getNumSamples(),p);telemetry.end(l,r,b.getNumSamples());publishMeterBlock(b);}
juce::AudioProcessorEditor* ExciterProcessor::createEditor(){return new OriginalNonlinearEditor(*this,1,telemetry);}
void ExciterProcessor::setStateInformation(const void* p,int n){restoreNeutral(*this,p,n,"EXCITER");}
}
