#include "StudioFaceplateProcessors.h"
#include "StudioFaceplateEditors.h"

namespace hem {

juce::ValueTree StudioFaceplateProcessor::defaultsTree() const {
    juce::ValueTree result("PARAMETERS");
    for (const auto& spec : parameterSpecs()) {
        juce::ValueTree v("PARAM");
        v.setProperty("id", spec.id, nullptr);
        v.setProperty("value", double(spec.initial), nullptr);
        result.addChild(v, -1, nullptr);
    }
    return result;
}
float StudioFaceplateProcessor::oldValue(const juce::ValueTree& values, const char* id, float fallback) {
    for (auto child : values) if (child["id"].toString() == id) { const double v = child["value"]; return std::isfinite(v) ? float(v) : fallback; }
    return fallback;
}
void StudioFaceplateProcessor::put(juce::ValueTree& tree, const char* id, float v) {
    for (auto child : tree) if (child["id"].toString() == id) { child.setProperty("value", double(v), nullptr); return; }
}
void StudioFaceplateProcessor::publishPeak(const juce::AudioBuffer<float>& b) noexcept {
    const int n = b.getNumSamples();
    float peak = 0;
    for (int c = 0; c < 2; ++c) { const auto range = juce::FloatVectorOperations::findMinAndMax(b.getReadPointer(c), n); peak = std::max({peak, std::abs(range.getStart()), std::abs(range.getEnd())}); }
    outputPeak.store(std::isfinite(peak) ? peak : 0.f, std::memory_order_relaxed);
}

// ---------------------------------------------------------------- Vocal De-Reverb
VocalDeReverbProcessor::VocalDeReverbProcessor() : StudioFaceplateProcessor("VOCAL_DE_REVERB", "Vocal De-Reverb", {
    {"reduction", "REVERB REDUCTION", 0, 100, 50, .1f, "%"},
    {"early", "EARLY REFLECTIONS", 0, 100, 45, .1f, "%"},
    {"tail", "TAIL REDUCTION", 0, 100, 60, .1f, "%"},
    {"ambience", "AMBIENCE", 0, 100, 38, .1f, "%"},
    {"roomSize", "ROOM SIZE", 0, 100, 25, .1f, "%"},
    {"damping", "DAMPING", 0, 100, 50, .1f, "%"},
    {"clarity", "CLARITY", 0, 100, 62, .1f, "%"},
    {"presence", "PRESENCE", 0, 100, 0, .1f, "%"},
    {"mix", "DRY / WET", 0, 100, 100, .1f, "%"},
    {"output", "OUTPUT", -24, 24, 0, .1f, "dB"},
    {"mode", "MODE", 0, 2, 0, 1, ""},
    {"algorithm", "ALGORITHM", 0, 2, 0, 1, ""},
    {"focus", "FREQUENCY FOCUS", 20, 20000, 200, 1, "Hz", false, -1},
    {"sensitivity", "DETECTION SENSITIVITY", 0, 100, 50, .1f, "%"},
    {"stereo", "STEREO PROCESSING", 0, 2, 2, 1, ""},
    {"learn", "LEARN", 0, 1, 0, 1, "", true},
    {"resetLearn", "RESET LEARNED ROOM", 0, 1, 0, 1, "", true},
    {"bypass", "BYPASS", 0, 1, 0, 1, "", true}}) {}
studio::DeReverbSettings VocalDeReverbProcessor::settings() const noexcept {
    studio::DeReverbSettings p;
    p.reduction = value(reduction); p.early = value(early); p.tail = value(tail); p.ambience = value(ambience); p.roomSize = value(roomSize);
    p.damping = value(damping); p.clarity = value(clarity); p.presence = value(presence); p.mix = value(mix); p.output = value(output);
    p.mode = choice(mode); p.algorithm = choice(algorithm); p.focusHz = value(focus); p.sensitivity = value(sensitivity); p.stereo = choice(stereo);
    p.learn = flag(learn); p.resetLearn = flag(resetLearn); p.bypass = flag(bypass);
    return p;
}
void VocalDeReverbProcessor::prepareToPlay(double rate, int) {
    resetMeterTelemetry(); actualRate.store(rate);
    core.prepare(rate);
    setLatencySamples(core.latency());
    publishMeterPreparation();
}
void VocalDeReverbProcessor::processBlock(juce::AudioBuffer<float>& b, juce::MidiBuffer&) {
    juce::ScopedNoDenormals guard;
    if (b.getNumChannels() < 2 || b.getNumSamples() <= 0) return;
    captureInput(b);
    core.process(b.getWritePointer(0), b.getWritePointer(1), b.getNumSamples(), settings(), &tap);
    publishPeak(b);
    publishMeterBlock(b);
}
juce::AudioProcessorEditor* VocalDeReverbProcessor::createEditor() { return new VocalDeReverbEditor(*this); }
juce::String VocalDeReverbProcessor::helpText() const {
    return "STFT reverb suppression. Late reverb is predicted from the signal 50 ms earlier using the decay time (T60); early reflections from the previous frame. "
           "SPECTRAL uses Room Size as T60. ADAPTIVE measures T60 from the material's own decays. LEARNED uses the room captured while LEARN is on (Room Size then scales it). "
           "There is no neural network or AI model in this plugin. Damping shortens high-frequency decay; Ambience sets how much room is always left; Clarity protects consonant onsets; "
           "Frequency Focus leaves everything below it untouched; Stereo Processing picks Mid, Side or both. Latency: one FFT frame (about 21 ms at 48 kHz).";
}

// ---------------------------------------------------------------- S.I.P.A.
SipaArchitectProcessor::SipaArchitectProcessor() : StudioFaceplateProcessor("SIPA", "S.I.P.A.  -  Stereo Image & Phase Architect", {
    {"width", "WIDTH", 0, 200, 100, .1f, "%"},
    {"panLaw", "PAN LAW (CENTER LEVEL)", -6, 6, 0, .1f, "dB"},
    {"centerFocus", "CENTER FOCUS", 0, 100, 0, .1f, "%"},
    {"depth", "DEPTH", 0, 100, 0, .1f, "%"},
    {"bass", "MONO BASS", 0, 200, 0, 1, "Hz"},
    {"output", "OUTPUT", -24, 24, 0, .1f, "dB"},
    {"phaseMode", "PHASE CONTROL", 0, 3, 0, 1, ""},
    {"phaseRotate", "PHASE ROTATE", -180, 180, 0, .1f, "deg"},
    {"stereoMode", "STEREO MODE", 0, 3, 0, 1, ""},
    {"msBalance", "MID / SIDE BALANCE", -100, 100, 0, .1f, ""},
    {"harmonicWidth", "HARMONIC WIDTH", 0, 100, 0, .1f, "%"},
    {"air", "AIR ENHANCE", 0, 100, 0, .1f, "%"},
    {"hpfSide", "HPF SIDE", 20, 1000, 20, 1, "Hz", false, -1},
    {"lpfSide", "LPF SIDE", 1000, 20000, 20000, 1, "Hz", false, -1},
    {"dynamicWidth", "DYNAMIC WIDTH", 0, 100, 0, .1f, "%"},
    {"trim", "OUTPUT TRIM", -24, 24, 0, .1f, "dB"},
    {"monoCheck", "MONO CHECK", 0, 1, 0, 1, "", true},
    {"bypass", "BYPASS", 0, 1, 0, 1, "", true}}, PluginMeterKind::correlation) {}
studio::SipaSettings SipaArchitectProcessor::settings() const noexcept {
    studio::SipaSettings p;
    p.width = value(width); p.panLaw = value(panLaw); p.centerFocus = value(centerFocus); p.depth = value(depth); p.monoBass = value(monoBass); p.output = value(output);
    p.phaseMode = choice(phaseMode); p.phaseRotate = value(phaseRotate); p.stereoMode = choice(stereoMode); p.msBalance = value(msBalance);
    p.harmonicWidth = value(harmonicWidth); p.air = value(air); p.hpfSide = value(hpfSide); p.lpfSide = value(lpfSide); p.dynamicWidth = value(dynamicWidth); p.trim = value(trim);
    p.monoCheck = flag(monoCheck); p.bypass = flag(bypass);
    return p;
}
void SipaArchitectProcessor::prepareToPlay(double rate, int) {
    resetMeterTelemetry(); actualRate.store(rate); setLatencySamples(0);
    core.prepare(rate);
    publishMeterPreparation();
}
void SipaArchitectProcessor::processBlock(juce::AudioBuffer<float>& b, juce::MidiBuffer&) {
    juce::ScopedNoDenormals guard;
    if (b.getNumChannels() < 2 || b.getNumSamples() <= 0) return;
    captureInput(b);
    core.process(b.getWritePointer(0), b.getWritePointer(1), b.getNumSamples(), settings(), &tap);
    publishPeak(b);
    publishMeterBlock(b);
}
juce::ValueTree SipaArchitectProcessor::migrateParameterState(const juce::ValueTree& values) const {
    // Previous S.I.P.A. schema: width, bass, balance, output, bassOn, mono, invertL, invertR, swap.
    bool old = false;
    for (auto child : values) if (child["id"].toString() == "bassOn") old = true;
    if (!old) return values;
    auto tree = defaultsTree();
    put(tree, "width", juce::jlimit(0.f, 200.f, oldValue(values, "width", 100)));
    put(tree, "bass", oldValue(values, "bassOn", 0) > .5f ? juce::jlimit(20.f, 200.f, oldValue(values, "bass", 120)) : 0.f);
    put(tree, "output", juce::jlimit(-24.f, 24.f, oldValue(values, "output", 0)));
    put(tree, "stereoMode", oldValue(values, "mono", 0) > .5f ? 3.f : 0.f);
    const bool swap = oldValue(values, "swap", 0) > .5f, invL = oldValue(values, "invertL", 0) > .5f, invR = oldValue(values, "invertR", 0) > .5f;
    put(tree, "phaseMode", swap ? 3.f : invL ? 1.f : invR ? 2.f : 0.f); // old L/R balance has no equivalent and is dropped
    return tree;
}
juce::AudioProcessorEditor* SipaArchitectProcessor::createEditor() { return new SipaArchitectEditor(*this); }
juce::String SipaArchitectProcessor::helpText() const {
    return "Zero-latency M/S image and phase tool. WIDTH scales the side; PAN LAW sets the center (mid) level; CENTER FOCUS removes side energy around 1 kHz; DEPTH adds a 12 ms delayed, darkened copy of the mid to the side; "
           "MONO BASS makes everything below it mono (0 = off); HARMONIC WIDTH adds harmonics of the upper mid to the side; AIR lifts the side above 9 kHz; FILTER SIDE band-limits the side; "
           "DYNAMIC WIDTH narrows transients and widens sustain. PHASE ROTATE turns R against L by a constant angle (Hilbert pair). STEREO MODES: Stereo, Mid/Side (M on L, S on R), Side Only, Mono. "
           "MONO CHECK sums the result for listening. OUTPUT TRIM is applied after MONO CHECK.";
}

// ---------------------------------------------------------------- Transient Shaper
TransientShaperProcessor::TransientShaperProcessor() : StudioFaceplateProcessor("TRANSIENT", "Transient Shaper", {
    {"attackAmount", "ATTACK", -100, 100, 0, .1f, "%"},
    {"sustainAmount", "SUSTAIN", -100, 100, 0, .1f, "%"},
    {"sensitivity", "SENSITIVITY", -100, 100, 0, .1f, "%"},
    {"length", "TRANSIENT LENGTH", 0, 500, 0, 1, "ms", false, 80},
    {"lowFocus", "LOW FOCUS", -100, 100, 0, .1f, "%"},
    {"highFocus", "HIGH FOCUS", -100, 100, 0, .1f, "%"},
    {"clipGuard", "CLIP GUARD", 0, 100, 0, .1f, "%"},
    {"mix", "MIX", 0, 100, 100, .1f, "%"},
    {"output", "OUTPUT", -24, 24, 0, .1f, "dB"},
    {"detection", "DETECTION MODE", 0, 2, 0, 1, ""},
    {"focusRange", "FOCUS RANGE", 0, 2, 1, 1, ""},
    {"processMode", "PROCESS MODE", 0, 3, 0, 1, ""},
    {"solo", "SOLO DETECTOR BAND", 0, 1, 0, 1, "", true},
    {"delta", "DELTA", 0, 1, 0, 1, "", true},
    {"bypass", "BYPASS", 0, 1, 0, 1, "", true}}) {}
studio::TransientShaperSettings TransientShaperProcessor::settings() const noexcept {
    studio::TransientShaperSettings p;
    p.attack = value(attack); p.sustain = value(sustain); p.sensitivity = value(sensitivity); p.length = value(length);
    p.lowFocus = value(lowFocus); p.highFocus = value(highFocus); p.clipGuard = value(clipGuard); p.mix = value(mix); p.output = value(output);
    p.detection = choice(detection); p.focus = choice(focus); p.processMode = choice(processMode);
    p.solo = flag(solo); p.delta = flag(delta); p.bypass = flag(bypass);
    return p;
}
void TransientShaperProcessor::prepareToPlay(double rate, int) {
    resetMeterTelemetry(); actualRate.store(rate); setLatencySamples(0);
    core.prepare(rate);
    publishMeterPreparation();
}
void TransientShaperProcessor::processBlock(juce::AudioBuffer<float>& b, juce::MidiBuffer&) {
    juce::ScopedNoDenormals guard;
    if (b.getNumChannels() < 2 || b.getNumSamples() <= 0) return;
    captureInput(b);
    core.process(b.getWritePointer(0), b.getWritePointer(1), b.getNumSamples(), settings(), &tap);
    publishPeak(b);
    publishMeterBlock(b);
}
juce::ValueTree TransientShaperProcessor::migrateParameterState(const juce::ValueTree& values) const {
    // Previous schema: attack (dB), sustain (dB), speed, recovery, mix, output.
    bool old = false;
    for (auto child : values) if (child["id"].toString() == "recovery") old = true;
    if (!old) return values;
    auto tree = defaultsTree();
    put(tree, "attackAmount", juce::jlimit(-100.f, 100.f, oldValue(values, "attack", 0) / studio::TransientShaperCore::maxDb * 100.f));
    put(tree, "sustainAmount", juce::jlimit(-100.f, 100.f, oldValue(values, "sustain", 0) / studio::TransientShaperCore::maxDb * 100.f));
    put(tree, "mix", juce::jlimit(0.f, 100.f, oldValue(values, "mix", 100)));
    put(tree, "output", juce::jlimit(-24.f, 24.f, oldValue(values, "output", 0)));
    return tree;
}
juce::AudioProcessorEditor* TransientShaperProcessor::createEditor() { return new TransientShaperEditor(*this); }
juce::String TransientShaperProcessor::helpText() const {
    return "Zero latency. ATTACK and SUSTAIN: +/-100% = +/-18 dB on onsets and decays. FOCUS RANGE picks the band that triggers detection (Low 40-250 Hz, Mid 250 Hz-4 kHz, High 4-16 kHz); "
           "DETECTION MODE sets the detector speed (Adaptive follows the crest factor). TRANSIENT LENGTH holds the attack gain (0 = automatic). LOW/HIGH FOCUS scale the effect below 200 Hz / above 4 kHz. "
           "PROCESS MODE: Clean, Punch (adds harmonics on onsets), Smooth (5 ms gain smoothing), Aggressive. CLIP GUARD soft-clips below -0.1 dBFS after Output. SOLO plays the detector band; DELTA plays only what changed.";
}

// ---------------------------------------------------------------- Breath / Mouth Control
BreathMouthStudioProcessor::BreathMouthStudioProcessor() : StudioFaceplateProcessor("BREATH_MOUTH_CONTROL", "Breath / Mouth Control", {
    {"sensitivity", "SENSITIVITY", 0, 100, 50, .1f, "%"},
    {"threshold", "THRESHOLD", -60, 0, -24, .1f, "dB"},
    {"attack", "ATTACK", .1f, 100, 5, .1f, "ms", false, 5},
    {"release", "RELEASE", 10, 2000, 250, 1, "ms", false, 250},
    {"reduction", "REDUCTION", 0, 30, 12, .1f, "dB"},
    {"frequency", "FREQUENCY", 20, 20000, 3500, 1, "Hz", false, -1},
    {"q", "Q", .1f, 10, 1.2f, .01f, "", false, 1.2f},
    {"mix", "MIX", 0, 100, 100, .1f, "%"},
    {"output", "OUTPUT", -24, 24, 0, .1f, "dB"},
    {"detection", "DETECTION", 0, 3, 0, 1, ""},
    {"focusRange", "FREQUENCY FOCUS", 0, 2, 1, 1, ""},
    {"mode", "MODE", 0, 2, 0, 1, ""},
    {"stageDetect", "PROCESS: DETECT", 0, 1, 1, 1, "", true},
    {"stageFilter", "PROCESS: FILTER", 0, 1, 1, 1, "", true},
    {"stageDynamic", "PROCESS: DYNAMIC", 0, 1, 1, 1, "", true},
    {"stageOutput", "PROCESS: OUTPUT", 0, 1, 1, 1, "", true},
    {"listenReduction", "LISTEN REDUCTION", 0, 1, 0, 1, "", true},
    {"solo", "SOLO BAND", 0, 1, 0, 1, "", true},
    {"bypass", "BYPASS", 0, 1, 0, 1, "", true}}) {}
studio::BreathMouthStudioSettings BreathMouthStudioProcessor::settings() const noexcept {
    studio::BreathMouthStudioSettings p;
    p.sensitivity = value(sensitivity); p.threshold = value(threshold); p.attack = value(attack); p.release = value(release); p.reduction = value(reduction);
    p.frequencyHz = value(frequency); p.q = value(q); p.mix = value(mix); p.output = value(output);
    p.detection = choice(detection); p.focus = choice(focus); p.mode = choice(mode);
    p.stageDetect = flag(stageDetect); p.stageFilter = flag(stageFilter); p.stageDynamic = flag(stageDynamic); p.stageOutput = flag(stageOutput);
    p.listenReduction = flag(listenReduction); p.solo = flag(solo); p.bypass = flag(bypass);
    return p;
}
void BreathMouthStudioProcessor::prepareToPlay(double rate, int) {
    resetMeterTelemetry(); actualRate.store(rate);
    core.prepare(rate);
    setLatencySamples(core.latency());
    publishMeterPreparation();
}
void BreathMouthStudioProcessor::processBlock(juce::AudioBuffer<float>& b, juce::MidiBuffer&) {
    juce::ScopedNoDenormals guard;
    if (b.getNumChannels() < 2 || b.getNumSamples() <= 0) return;
    captureInput(b);
    core.process(b.getWritePointer(0), b.getWritePointer(1), b.getNumSamples(), settings(), &tap);
    publishPeak(b);
    publishMeterBlock(b);
}
juce::ValueTree BreathMouthStudioProcessor::migrateParameterState(const juce::ValueTree& values) const {
    // Previous schema: breath/mouth detection, reduction and length, low/high focus, listen buttons, bypass.
    bool old = false;
    for (auto child : values) if (child["id"].toString() == "breathReduction") old = true;
    if (!old) return values;
    auto tree = defaultsTree();
    const float breath = oldValue(values, "breathReduction", 0), mouth = oldValue(values, "mouthReduction", 0);
    const bool mouthFirst = mouth > breath;
    put(tree, "detection", breath > 0 && mouth > 0 ? 3.f : mouthFirst ? 1.f : 0.f);
    put(tree, "reduction", juce::jlimit(0.f, 30.f, std::max(breath, mouth)));
    put(tree, "sensitivity", juce::jlimit(0.f, 100.f, oldValue(values, mouthFirst ? "mouthDetection" : "breathDetection", 50)));
    put(tree, "listenReduction", oldValue(values, "listenBreath", 0) > .5f || oldValue(values, "listenMouth", 0) > .5f ? 1.f : 0.f);
    put(tree, "bypass", oldValue(values, "bypass", 0) > .5f ? 1.f : 0.f);
    return tree;
}
juce::AudioProcessorEditor* BreathMouthStudioProcessor::createEditor() { return new BreathMouthStudioEditor(*this); }
juce::String BreathMouthStudioProcessor::helpText() const {
    return "Heuristic detectors, not a trained classifier: they can also react to hiss, fricatives or clicks, so tune with LISTEN > REDUCTION. "
           "Breath = noise-like sound 12-50 dB under the recent vocal peak; Mouth Noise = impulses above 5.5 kHz; Sibilance = high share of 4.5-10 kHz energy. "
           "Frequency Focus sets where the breath detector listens. The reduction is a dynamic bell cut at FREQUENCY / Q (broadband when FILTER is off); with DYNAMIC on it only cuts "
           "the part above THRESHOLD, up to REDUCTION. Turning DETECT off applies the cut all the time. SOLO plays the filter band. 2 ms lookahead latency. "
           "External sidechain needs host sidechain routing, which Studio Suite does not have yet.";
}

} // namespace hem
