#pragma once
#include "BandParameters.h"
namespace hem::bands {
template<size_t A,size_t B> constexpr auto appendSpecs(const std::array<Spec,A>& original,const std::array<Spec,B>& extra){
    std::array<Spec,A+B> result{};for(size_t i=0;i<A;++i)result[i]=original[i];for(size_t i=0;i<B;++i)result[A+i]=extra[i];return result;
}
inline constexpr std::array<Spec,24> DynamicExtras{{
    {"b1Type","TYPE",0.f,2.f,0.f,1.f,""},
    {"b1DynamicOn","DYNAMIC ON",0.f,1.f,0.f,1.f,"",true},
    {"b1Up","UP / DOWN",0.f,1.f,0.f,1.f,"",true},
    {"b2Type","TYPE",0.f,2.f,0.f,1.f,""},
    {"b2DynamicOn","DYNAMIC ON",0.f,1.f,0.f,1.f,"",true},
    {"b2Up","UP / DOWN",0.f,1.f,0.f,1.f,"",true},
    {"b3Type","TYPE",0.f,2.f,0.f,1.f,""},
    {"b3DynamicOn","DYNAMIC ON",0.f,1.f,0.f,1.f,"",true},
    {"b3Up","UP / DOWN",0.f,1.f,0.f,1.f,"",true},
    {"b4Type","TYPE",0.f,2.f,0.f,1.f,""},
    {"b4DynamicOn","DYNAMIC ON",0.f,1.f,0.f,1.f,"",true},
    {"b4Up","UP / DOWN",0.f,1.f,0.f,1.f,"",true},
    {"b5Type","TYPE",0.f,2.f,0.f,1.f,""},
    {"b5DynamicOn","DYNAMIC ON",0.f,1.f,0.f,1.f,"",true},
    {"b5Up","UP / DOWN",0.f,1.f,0.f,1.f,"",true},
    {"b6Type","TYPE",0.f,2.f,0.f,1.f,""},
    {"b6DynamicOn","DYNAMIC ON",0.f,1.f,0.f,1.f,"",true},
    {"b6Up","UP / DOWN",0.f,1.f,0.f,1.f,"",true},
    {"b7Type","TYPE",0.f,2.f,0.f,1.f,""},
    {"b7DynamicOn","DYNAMIC ON",0.f,1.f,0.f,1.f,"",true},
    {"b7Up","UP / DOWN",0.f,1.f,0.f,1.f,"",true},
    {"b8Type","TYPE",0.f,2.f,0.f,1.f,""},
    {"b8DynamicOn","DYNAMIC ON",0.f,1.f,0.f,1.f,"",true},
    {"b8Up","UP / DOWN",0.f,1.f,0.f,1.f,"",true}
}};
inline constexpr auto ExpandedDynamicSpecs=appendSpecs(DynamicSpecs,DynamicExtras);
inline constexpr std::array<Spec,21> MidSideExtras{{
    {"m5On","BAND ON",0.f,1.f,1.f,1.f,"",true},
    {"m5Freq","FREQ",20.f,18000.f,16000.f,1.f,"Hz"},
    {"m5Gain","GAIN",-12.f,12.f,0.f,0.1f,"dB"},
    {"m5Q","Q",0.25f,8.f,1.f,0.01f,"Q"},
    {"s5On","BAND ON",0.f,1.f,1.f,1.f,"",true},
    {"s5Freq","FREQ",20.f,18000.f,16000.f,1.f,"Hz"},
    {"s5Gain","GAIN",-12.f,12.f,0.f,0.1f,"dB"},
    {"s5Q","Q",0.25f,8.f,1.f,0.01f,"Q"},
    {"m1Type","TYPE",0.f,2.f,0.f,1.f,""},
    {"m2Type","TYPE",0.f,2.f,0.f,1.f,""},
    {"m3Type","TYPE",0.f,2.f,0.f,1.f,""},
    {"m4Type","TYPE",0.f,2.f,0.f,1.f,""},
    {"m5Type","TYPE",0.f,2.f,0.f,1.f,""},
    {"s1Type","TYPE",0.f,2.f,0.f,1.f,""},
    {"s2Type","TYPE",0.f,2.f,0.f,1.f,""},
    {"s3Type","TYPE",0.f,2.f,0.f,1.f,""},
    {"s4Type","TYPE",0.f,2.f,0.f,1.f,""},
    {"s5Type","TYPE",0.f,2.f,0.f,1.f,""},
    {"width","WIDTH",0.f,200.f,100.f,0.1f,"%"},
    {"midSolo","MID SOLO",0.f,1.f,0.f,1.f,"",true},
    {"sideSolo","SIDE SOLO",0.f,1.f,0.f,1.f,"",true}
}};
inline constexpr auto ExpandedMidSideSpecs=appendSpecs(MidSideSpecs,MidSideExtras);
inline constexpr std::array<Spec,24> DynamicsExtras{{
    {"xMid","MIDDLE CROSSOVER",20.f,18000.f,1000.f,1.f,"Hz"},
    {"lowMidThreshold","THRESHOLD",-60.f,0.f,0.f,0.1f,"dBFS peak"},
    {"lowMidRatio","RATIO",1.f,10.f,1.f,0.01f,":1"},
    {"lowMidAttack","ATTACK",0.1f,100.f,10.f,0.1f,"ms"},
    {"lowMidRelease","RELEASE",10.f,1000.f,100.f,1.f,"ms"},
    {"lowMidKnee","KNEE",0.f,24.f,0.f,0.1f,"dB"},
    {"lowMidRange","MAX REDUCTION",0.f,24.f,24.f,0.1f,"dB"},
    {"lowMidMakeup","MAKEUP",-12.f,12.f,0.f,0.1f,"dB"},
    {"lowMidSolo","SOLO",0.f,1.f,0.f,1.f,"",true},
    {"highMidThreshold","THRESHOLD",-60.f,0.f,0.f,0.1f,"dBFS peak"},
    {"highMidRatio","RATIO",1.f,10.f,1.f,0.01f,":1"},
    {"highMidAttack","ATTACK",0.1f,100.f,10.f,0.1f,"ms"},
    {"highMidRelease","RELEASE",10.f,1000.f,100.f,1.f,"ms"},
    {"highMidKnee","KNEE",0.f,24.f,0.f,0.1f,"dB"},
    {"highMidRange","MAX REDUCTION",0.f,24.f,24.f,0.1f,"dB"},
    {"highMidMakeup","MAKEUP",-12.f,12.f,0.f,0.1f,"dB"},
    {"highMidSolo","SOLO",0.f,1.f,0.f,1.f,"",true},
    {"lowOn","BAND ON",0.f,1.f,1.f,1.f,"",true},
    {"lowMidOn","BAND ON",0.f,1.f,1.f,1.f,"",true},
    {"highMidOn","BAND ON",0.f,1.f,1.f,1.f,"",true},
    {"highOn","BAND ON",0.f,1.f,1.f,1.f,"",true},
    {"legacyMidOn","BAND ON",0.f,1.f,1.f,1.f,"",true},
    {"mix","MIX",0.f,100.f,100.f,0.1f,"%"},
    {"topology","TOPOLOGY",0.f,1.f,1.f,1.f,""}
}};
inline constexpr auto ExpandedDynamicsSpecs=appendSpecs(DynamicsSpecs,DynamicsExtras);
inline constexpr std::array<Spec,13> ImagerExtras{{
    {"xMid","MIDDLE CROSSOVER",20.f,18000.f,1000.f,1.f,"Hz"},
    {"lowMidWidth","WIDTH",0.f,200.f,100.f,0.1f,"%"},
    {"lowMidGain","OUTPUT",-12.f,12.f,0.f,0.1f,"dB"},
    {"lowMidSolo","SOLO",0.f,1.f,0.f,1.f,"",true},
    {"highMidWidth","WIDTH",0.f,200.f,100.f,0.1f,"%"},
    {"highMidGain","OUTPUT",-12.f,12.f,0.f,0.1f,"dB"},
    {"highMidSolo","SOLO",0.f,1.f,0.f,1.f,"",true},
    {"lowOn","BAND ON",0.f,1.f,1.f,1.f,"",true},
    {"lowMidOn","BAND ON",0.f,1.f,1.f,1.f,"",true},
    {"highMidOn","BAND ON",0.f,1.f,1.f,1.f,"",true},
    {"highOn","BAND ON",0.f,1.f,1.f,1.f,"",true},
    {"legacyMidOn","BAND ON",0.f,1.f,1.f,1.f,"",true},
    {"topology","TOPOLOGY",0.f,1.f,1.f,1.f,""}
}};
inline constexpr auto ExpandedImagerSpecs=appendSpecs(ImagerSpecs,ImagerExtras);
template<Kind K> constexpr auto& expandedSpecs(){if constexpr(K==Kind::Dynamic)return ExpandedDynamicSpecs;else if constexpr(K==Kind::MidSide)return ExpandedMidSideSpecs;else if constexpr(K==Kind::Dynamics)return ExpandedDynamicsSpecs;else return ExpandedImagerSpecs;}
template<Kind K> using ExpandedSettings=std::array<float,expandedSpecs<K>().size()>;
template<Kind K> constexpr ExpandedSettings<K> expandedDefaults(){ExpandedSettings<K> p{};for(size_t i=0;i<p.size();++i)p[i]=expandedSpecs<K>()[i].initial;return p;}
template<Kind K> constexpr bool choiceParameter(size_t index){if constexpr(K==Kind::Dynamic)return index>=66&&(index-66)%3==0;else if constexpr(K==Kind::MidSide)return index>=42&&index<52;else return index==expandedSpecs<K>().size()-1;}
static_assert(ExpandedDynamicSpecs.size()==90&&ExpandedMidSideSpecs.size()==55&&ExpandedDynamicsSpecs.size()==52&&ExpandedImagerSpecs.size()==27);
}
