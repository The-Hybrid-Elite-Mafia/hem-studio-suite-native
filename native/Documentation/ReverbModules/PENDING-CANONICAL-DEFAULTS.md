# Pending constructor-default correction

Status: proposed correction recovered from the diagnosed failure; not applied to the byte-exact recovered baseline. The pre-loss build worker independently verified that normal parameter notifications eliminate the startup and exact SessionState differences. Fresh implementation/regression testing is still required after applying it.

Cause: JUCE APVTS ParameterAdapter intentionally initializes its raw value using an unsnapped NormalisableRange::convertFrom0to1(parameter.getDefaultValue()). Later normalized recall uses RangedAudioParameter::convertFrom0to1, which snaps to the interval. Algorithmic damping therefore starts at 7999.99951171875 and later becomes 8000. Source/Plugins/ReverbProcessors.cpp should canonicalize only its new processors, during construction, before workers/editors/audio.

Minimal recipe:

1. For every declared parameter, call setValueNotifyingHost(convertTo0to1(spec.initial)) through its existing RangedAudioParameter
2. Copy APVTS state, set each PARAM child's value from that parameter's now-canonical raw value, then replace the owned state once. This avoids approximatelyEqual leaving the old unsnapped cached property behind
3. Call the helper before startThread() in ConvolutionReverbProcessor's constructor and in AlgorithmicReverbProcessor's constructor body
4. Do not change ranges, steps, defaults, skew, shared ModuleProcessor normalization or DSP kernels. Do not write raw atomics directly
5. Strengthen the reverb startup regression to the existing shared threshold abs(raw - initial) < 1e-4, with an additional exact damping == 8000.f assertion
6. Verify constructor, prepare, editor and repeated normalized recall. Compare raw == underlying AudioParameterFloat::get() == cached tree; require exact SessionState round-trip equality for defaults and representative legal-grid values. Preserve transactional malformed-state tests

The integration owner reported the same issue in the new modulation constructors and was applying the same narrow recipe there. That change is outside this recovered reverb baseline and must be tracked separately.
