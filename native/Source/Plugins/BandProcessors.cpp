#include "BandProcessors.h"
#include "BandEditor.h"
#include "OriginalDynamicEqEditor.h"
#include "OriginalMidSideEqEditor.h"
#include "OriginalMultibandDynamicsEditor.h"
#include "OriginalMultibandImagerEditor.h"
namespace hem {juce::AudioProcessorEditor* BandModuleProcessor::createEditor(){if(bandKind==bands::Kind::Dynamic)return new OriginalDynamicEqEditor(*this);if(bandKind==bands::Kind::MidSide)return new OriginalMidSideEqEditor(*this);if(bandKind==bands::Kind::Dynamics)return new OriginalMultibandDynamicsEditor(*this);return new OriginalMultibandImagerEditor(*this);}}
