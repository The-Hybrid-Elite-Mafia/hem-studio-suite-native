#pragma once
// 2026-10 Studio faceplate processors: Vocal De-Reverb (new), and the replacement
// S.I.P.A., Transient Shaper and Breath / Mouth Control. They keep the registry IDs
// and state types of the modules they replace ("sipa"/SIPA, "transient"/TRANSIENT,
// "breath-mouth-control"/BREATH_MOUTH_CONTROL), and migrate sessions saved with the
// previous parameter sets. The previous classes stay in the source for their tests
// but are no longer registered.
#include "Processors.h"
#include "../DSP/VocalDeReverbCore.h"
#include "../DSP/SipaArchitectCore.h"
#include "../DSP/TransientShaperCore.h"
#include "../DSP/BreathMouthStudioCore.h"

namespace hem {

class StudioFaceplateProcessor : public ModuleProcessor {
public:
    StudioFaceplateProcessor(juce::String typeId, juce::String displayName, std::vector<ParameterSpec> parameterList, PluginMeterKind meterType = PluginMeterKind::samplePeak)
        : ModuleProcessor(std::move(typeId), std::move(displayName), std::move(parameterList), meterType) {}
    studio::ScopeTap& scopeTap() noexcept { return tap; }
    float meter() const noexcept override { return outputPeak.load(std::memory_order_relaxed); }
    juce::String meterLabel() const override { return "OUTPUT SAMPLE PEAK"; }
    int choice(size_t index) const noexcept { return int(std::lround(value(index))); }
    bool flag(size_t index) const noexcept { return value(index) > .5f; }
    int indexOf(const juce::String& parameterId) const noexcept {
        for (size_t i = 0; i < parameterSpecs().size(); ++i) if (parameterId == parameterSpecs()[i].id) return int(i);
        return -1;
    }
    // Message thread: per-editor view choices kept with the plugin state (not automatable).
    int viewSetting(const juce::Identifier& key, int fallback) const { return int(state.state.getProperty(key, fallback)); }
    void setViewSetting(const juce::Identifier& key, int v) { state.state.setProperty(key, v, nullptr); }
protected:
    // Fills a complete schema (all specs at defaults), then lets `map` overwrite.
    juce::ValueTree defaultsTree() const;
    static float oldValue(const juce::ValueTree& values, const char* id, float fallback);
    static void put(juce::ValueTree& tree, const char* id, float v);
    void publishPeak(const juce::AudioBuffer<float>& b) noexcept;
    studio::ScopeTap tap;
    std::atomic<float> outputPeak{0};
};

class VocalDeReverbProcessor final : public StudioFaceplateProcessor {
public:
    enum Index : size_t { reduction, early, tail, ambience, roomSize, damping, clarity, presence, mix, output, mode, algorithm, focus, sensitivity, stereo, learn, resetLearn, bypass };
    VocalDeReverbProcessor();
    void prepareToPlay(double, int) override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    juce::AudioProcessorEditor* createEditor() override;
    juce::String helpText() const override;
    double getTailLengthSeconds() const override { return .1; }
    studio::DeReverbSettings settings() const noexcept;
    const studio::VocalDeReverbCore& engine() const noexcept { return core; }
private:
    studio::VocalDeReverbCore core;
};

class SipaArchitectProcessor final : public StudioFaceplateProcessor {
public:
    enum Index : size_t { width, panLaw, centerFocus, depth, monoBass, output, phaseMode, phaseRotate, stereoMode, msBalance, harmonicWidth, air, hpfSide, lpfSide, dynamicWidth, trim, monoCheck, bypass };
    SipaArchitectProcessor();
    void prepareToPlay(double, int) override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    juce::AudioProcessorEditor* createEditor() override;
    juce::String helpText() const override;
    float meter() const noexcept override { return core.correlation.load(std::memory_order_relaxed); }
    juce::String meterLabel() const override { return "L/R CORRELATION"; }
    juce::ValueTree migrateParameterState(const juce::ValueTree&) const override;
    studio::SipaSettings settings() const noexcept;
private:
    studio::SipaArchitectCore core;
};

class TransientShaperProcessor final : public StudioFaceplateProcessor {
public:
    enum Index : size_t { attack, sustain, sensitivity, length, lowFocus, highFocus, clipGuard, mix, output, detection, focus, processMode, solo, delta, bypass };
    TransientShaperProcessor();
    void prepareToPlay(double, int) override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    juce::AudioProcessorEditor* createEditor() override;
    juce::String helpText() const override;
    juce::ValueTree migrateParameterState(const juce::ValueTree&) const override;
    studio::TransientShaperSettings settings() const noexcept;
    const studio::TransientShaperCore& engine() const noexcept { return core; }
private:
    studio::TransientShaperCore core;
};

class BreathMouthStudioProcessor final : public StudioFaceplateProcessor {
public:
    enum Index : size_t { sensitivity, threshold, attack, release, reduction, frequency, q, mix, output, detection, focus, mode, stageDetect, stageFilter, stageDynamic, stageOutput, listenReduction, solo, bypass };
    BreathMouthStudioProcessor();
    void prepareToPlay(double, int) override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    juce::AudioProcessorEditor* createEditor() override;
    juce::String helpText() const override;
    juce::ValueTree migrateParameterState(const juce::ValueTree&) const override;
    studio::BreathMouthStudioSettings settings() const noexcept;
    const studio::BreathMouthStudioCore& engine() const noexcept { return core; }
private:
    studio::BreathMouthStudioCore core;
};

} // namespace hem
