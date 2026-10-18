#pragma once
#include "../DSP/ExpandedBandParameters.h"
#include <cstdlib>
namespace hem::bands {
template<Kind K> bool migrateState(const juce::ValueTree& input,const juce::AudioProcessorValueTreeState& state,
                                  juce::ValueTree& output,juce::String& error){
    const auto reject=[&](const char* reason){error=reason;return false;};
    if(!input.hasType("PARAMETERS"))return reject("Band parameter root mismatch");
    const bool versioned=input.hasProperty("bandSchemaVersion");
    if(input.getNumProperties()!=(versioned?1:0))return reject("Unknown band state property");
    const auto version=versioned?input["bandSchemaVersion"].toString():juce::String("1");
    if(version!="1"&&version!="2")return reject("Unsupported band state version");
    const bool legacy=version=="1";constexpr size_t oldCount=specs<K>().size(),newCount=expandedSpecs<K>().size();
    const size_t expected=legacy?oldCount:newCount;
    if(input.getNumChildren()!=int(expected))return reject("Incomplete band parameter inventory");
    auto values=expandedDefaults<K>();std::array<bool,newCount> seen{};
    for(const auto child:input){
        if(!child.hasType("PARAM")||child.getNumChildren()!=0||child.getNumProperties()!=2||!child.hasProperty("id")||!child.hasProperty("value"))return reject("Malformed band parameter");
        const auto key=child["id"].toString();size_t index=0;while(index<expected&&key!=expandedSpecs<K>()[index].id)++index;
        if(index==expected||seen[index])return reject("Unknown or duplicate band parameter");seen[index]=true;
        const auto text=child["value"].toString().trim().toStdString();char* end=nullptr;const double number=std::strtod(text.c_str(),&end);
        const float value=float(number);const auto& spec=expandedSpecs<K>()[index];
        if(end==text.c_str()||*end!='\0'||!std::isfinite(number)||!std::isfinite(value)||value<spec.minimum||value>spec.maximum)return reject("Invalid band parameter number");
        if(spec.toggle&&number!=0&&number!=1)return reject("Invalid band boolean");
        if(choiceParameter<K>(index)&&number!=std::floor(number))return reject("Invalid band choice");
        values[index]=state.getParameter(key)->getNormalisableRange().snapToLegalValue(value);
    }
    if(legacy){
        if constexpr(K==Kind::Dynamic)for(size_t b=0;b<8;++b)values[67+b*3]=1;
        if constexpr(K==Kind::MidSide)values[34]=values[38]=0;
        if constexpr(K==Kind::Dynamics){values[51]=0;for(size_t i=0;i<8;++i)values[29+i]=values[37+i]=values[12+i];}
        if constexpr(K==Kind::Imager){values[26]=0;for(size_t i=0;i<3;++i)values[15+i]=values[18+i]=values[8+i];}
    }
    juce::ValueTree canonical("PARAMETERS");canonical.setProperty("bandSchemaVersion",2,nullptr);
    for(size_t i=0;i<newCount;++i){juce::ValueTree child("PARAM");child.setProperty("id",expandedSpecs<K>()[i].id,nullptr);child.setProperty("value",values[i],nullptr);canonical.addChild(child,-1,nullptr);}
    output=canonical;error.clear();return true;
}
}
