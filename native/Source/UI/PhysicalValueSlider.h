#pragma once
#include <JuceHeader.h>
#include "NeutralMapping.h"
namespace hem::ui {
// Presentation mapping only: APVTS parameter normalization and physical units stay unchanged.
class PhysicalValueSlider : public juce::Slider {
public:
 double valueToProportionOfLength(double v) override {return isBipolar(getMinimum(),getMaximum())?bipolarToPosition(v,getMinimum(),getMaximum()):juce::Slider::valueToProportionOfLength(v);}
 double proportionOfLengthToValue(double p) override {return isBipolar(getMinimum(),getMaximum())?positionToBipolar(p,getMinimum(),getMaximum()):juce::Slider::proportionOfLengthToValue(p);}
};
}
