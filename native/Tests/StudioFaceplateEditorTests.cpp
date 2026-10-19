// Editor and wiring checks for the 2026-10 faceplates (Vocal De-Reverb, S.I.P.A.,
// Transient Shaper, Breath / Mouth Control): registry identity, every parameter has
// a visible control that drives its APVTS value, Undo / Redo / A-B, migration of
// sessions saved with the previous parameter sets, and a rendered PNG per editor.
#include "../Source/Plugins/PluginRegistry.h"
#include "../Source/Plugins/StudioFaceplateEditors.h"
#include "../Source/Plugins/Editors.h"
#include "DiscreteControlChecks.h"
#include <iostream>
#include <random>
#include <stdexcept>

namespace {
void require(bool ok, const juce::String& what) { if (!ok) throw std::runtime_error(what.toStdString()); }
juce::Component* find(juce::Component& root, const juce::String& id) {
    if (root.getComponentID() == id) return &root;
    for (auto* c : root.getChildren()) if (auto* f = find(*c, id)) return f;
    return nullptr;
}
juce::Button* buttonWithText(juce::Component& root, const juce::String& text) {
    if (auto* b = dynamic_cast<juce::Button*>(&root); b && b->getButtonText() == text) return b;
    for (auto* c : root.getChildren()) if (auto* f = buttonWithText(*c, text)) return f;
    return nullptr;
}
void feed(hem::ModuleProcessor& p, double seconds) {
    juce::AudioBuffer<float> b(2, 512); juce::MidiBuffer midi;
    std::mt19937 g(5); std::normal_distribution<float> d(0, 1);
    int t = 0;
    for (int block = 0; block < int(seconds * 48000 / 512); ++block) {
        for (int i = 0; i < 512; ++i, ++t) {
            const int ph = t % 24000;
            float v = ph < 14000 ? .25f * std::sin(float(t) * .0288f) + .1f * std::sin(float(t) * .0864f) : .01f * d(g);
            if (ph < 300) v += .4f * d(g) * std::exp(-float(ph) / 60.f);
            b.setSample(0, i, v); b.setSample(1, i, .8f * v + .02f * d(g));
        }
        p.processBlock(b, midi);
    }
}
}

int main(int argc, char** argv) {
    juce::ScopedJuceInitialiser_GUI gui;
    try {
        const auto renderDir = juce::SystemStats::getEnvironmentVariable("HEM_FACEPLATE_RENDER_DIR", {});
        const juce::File out = argc > 1 ? juce::File(juce::String(argv[1])) : renderDir.isNotEmpty() ? juce::File(renderDir) : juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("hem-faceplates");
        out.createDirectory();
        struct Case { const char* id; const char* type; };
        for (const auto& c : std::array<Case, 4>{{{"vocal-de-reverb", "VOCAL_DE_REVERB"}, {"sipa", "SIPA"}, {"transient", "TRANSIENT"}, {"breath-mouth-control", "BREATH_MOUTH_CONTROL"}}}) {
            auto processor = hem::PluginRegistry::create(c.id);
            require(processor != nullptr, juce::String(c.id) + " not registered");
            require(dynamic_cast<hem::StudioFaceplateProcessor*>(processor.get()) != nullptr, juce::String(c.id) + " is not the new faceplate processor");
            require(processor->state.state.hasType(c.type) && hem::PluginRegistry::idFor(*processor) == c.id, juce::String(c.id) + " state identity changed");
            processor->setPlayConfigDetails(2, 2, 48000, 512);
            processor->prepareToPlay(48000, 512);
            std::unique_ptr<juce::AudioProcessorEditor> editor(processor->createEditor());
            auto* faceplate = dynamic_cast<hem::StudioFaceplateEditor*>(editor.get());
            require(faceplate != nullptr, juce::String(c.id) + " editor is not a faceplate editor");
            // Every parameter: a visible control inside the editor that reaches the DSP value.
            int knobs = 0, toggles = 0, choices = 0;
            for (const auto& spec : processor->parameterSpecs()) {
                auto* component = find(*editor, spec.id);
                require(component != nullptr, juce::String(c.id) + " has no control for " + spec.id);
                require(component->isVisible() && !component->getBounds().isEmpty() && editor->getLocalBounds().contains(component->getBounds()), juce::String(c.id) + " control outside editor: " + spec.id);
                require(component->getName() == spec.name, juce::String(c.id) + " control name differs for " + spec.id);
                auto* raw = processor->state.getRawParameterValue(spec.id);
                if (hem::controlChecks::exerciseDiscrete(*processor, *editor, spec, *component)) ++choices;
                else if (spec.toggle) {
                    auto* button = dynamic_cast<juce::ToggleButton*>(component);
                    require(button != nullptr, juce::String(c.id) + " toggle is not a button: " + spec.id);
                    button->setToggleState(true, juce::sendNotificationSync); require(raw->load() == 1.f, juce::String(c.id) + " toggle ON lost: " + spec.id);
                    button->setToggleState(false, juce::sendNotificationSync); require(raw->load() == 0.f, juce::String(c.id) + " toggle OFF lost: " + spec.id);
                    ++toggles;
                } else {
                    auto* slider = dynamic_cast<juce::Slider*>(component);
                    require(slider != nullptr, juce::String(c.id) + " numeric control is not a slider: " + spec.id);
                    require(std::abs(slider->getMinimum() - spec.minimum) < 1.e-3 && std::abs(slider->getMaximum() - spec.maximum) < 1.e-3, juce::String(c.id) + " range differs: " + spec.id);
                    for (float v : {spec.minimum, spec.maximum, spec.initial}) {
                        slider->setValue(v, juce::sendNotificationSync);
                        require(std::abs(raw->load() - v) <= std::max(1.e-3f, spec.step * .51f) + std::abs(v) * 1.e-4f, juce::String(c.id) + " knob did not reach DSP: " + spec.id);
                    }
                    require(std::abs(slider->getDoubleClickReturnValue() - spec.initial) < 1.e-3, juce::String(c.id) + " double-click reset differs: " + spec.id);
                    ++knobs;
                }
                auto* parameter = processor->state.getParameter(spec.id);
                parameter->setValueNotifyingHost(parameter->convertTo0to1(spec.initial));
            }
            // Undo / Redo / A-B (where the faceplate has them).
            const auto& first = processor->parameterSpecs().front();
            if (auto* undo = buttonWithText(*editor, "UNDO")) {
                auto* redo = buttonWithText(*editor, "REDO"); auto* ab = buttonWithText(*editor, "A / B");
                require(redo && ab, juce::String(c.id) + " history buttons incomplete");
                auto* slider = dynamic_cast<juce::Slider*>(find(*editor, first.id));
                faceplate->pumpForTest();
                const float before = processor->state.getRawParameterValue(first.id)->load();
                const float changed = before + (first.maximum - first.minimum) * .25f;
                slider->setValue(changed, juce::sendNotificationSync); faceplate->pumpForTest();
                undo->onClick(); require(std::abs(processor->state.getRawParameterValue(first.id)->load() - before) < .01f, juce::String(c.id) + " UNDO did not restore");
                redo->onClick(); require(std::abs(processor->state.getRawParameterValue(first.id)->load() - changed) < .01f, juce::String(c.id) + " REDO did not reapply");
                ab->onClick(); slider->setValue(first.minimum, juce::sendNotificationSync); faceplate->pumpForTest();
                ab->onClick(); require(std::abs(processor->state.getRawParameterValue(first.id)->load() - changed) < .01f, juce::String(c.id) + " A/B did not recall A");
                ab->onClick(); require(std::abs(processor->state.getRawParameterValue(first.id)->load() - first.minimum) < .01f, juce::String(c.id) + " A/B did not recall B");
                ab->onClick();
                for (const auto& spec : processor->parameterSpecs()) { auto* p = processor->state.getParameter(spec.id); p->setValueNotifyingHost(p->convertTo0to1(spec.initial)); }
            }
            // Render with live audio.
            feed(*processor, 1.5);
            for (int i = 0; i < 8; ++i) { faceplate->pumpForTest(); feed(*processor, .05); }
            faceplate->pumpForTest();
            juce::Image image(juce::Image::RGB, editor->getWidth(), editor->getHeight(), true);
            { juce::Graphics g(image); editor->paintEntireComponent(g, true); }
            juce::PNGImageFormat png; auto file = out.getChildFile(juce::String(c.id) + ".png"); file.deleteFile();
            { juce::FileOutputStream stream(file); require(stream.openedOk() && png.writeImageToStream(image, stream), "could not write " + file.getFullPathName()); }
            std::cout << "PASS " << c.id << ": " << knobs << " knobs/sliders, " << toggles << " toggles, " << choices << " selectors wired; history checked; rendered " << file.getFullPathName() << "\n";
            editor.reset(); processor->releaseResources();
        }
        // Sessions saved with the previous parameter sets still load.
        {
            hem::SipaArchitectProcessor sipa;
            juce::ValueTree old("PARAMETERS");
            for (auto [id, v] : std::initializer_list<std::pair<const char*, double>>{{"width", 140}, {"bass", 90}, {"balance", 20}, {"output", -3}, {"bassOn", 1}, {"mono", 0}, {"invertL", 0}, {"invertR", 1}, {"swap", 0}}) { juce::ValueTree p("PARAM"); p.setProperty("id", id, nullptr); p.setProperty("value", v, nullptr); old.addChild(p, -1, nullptr); }
            auto migrated = sipa.migrateParameterState(old);
            require(migrated.getNumChildren() == int(sipa.parameterSpecs().size()), "S.I.P.A. migration incomplete");
            auto value = [&](const char* id) { for (auto ch : migrated) if (ch["id"].toString() == id) return double(ch["value"]); return -999.0; };
            require(value("width") == 140 && value("bass") == 90 && value("output") == -3 && value("phaseMode") == 2, "S.I.P.A. old session mapped wrongly");
        }
        {
            hem::TransientShaperProcessor transient;
            juce::ValueTree old("PARAMETERS");
            for (auto [id, v] : std::initializer_list<std::pair<const char*, double>>{{"attack", 9}, {"sustain", -6}, {"speed", 2}, {"recovery", 100}, {"mix", 80}, {"output", -2}}) { juce::ValueTree p("PARAM"); p.setProperty("id", id, nullptr); p.setProperty("value", v, nullptr); old.addChild(p, -1, nullptr); }
            auto migrated = transient.migrateParameterState(old);
            auto value = [&](const char* id) { for (auto ch : migrated) if (ch["id"].toString() == id) return double(ch["value"]); return -999.0; };
            require(std::abs(value("attackAmount") - 50) < .01 && std::abs(value("sustainAmount") + 33.33) < .01 && value("mix") == 80, "Transient old session mapped wrongly");
        }
        {
            hem::BreathMouthStudioProcessor breath;
            juce::ValueTree old("PARAMETERS");
            for (auto [id, v] : std::initializer_list<std::pair<const char*, double>>{{"breathDetection", 70}, {"breathReduction", 10}, {"breathLength", 100}, {"mouthDetection", 50}, {"mouthReduction", 0}, {"mouthLength", .5}, {"low", 1500}, {"high", 12000}, {"listenBreath", 0}, {"listenMouth", 0}, {"bypass", 0}}) { juce::ValueTree p("PARAM"); p.setProperty("id", id, nullptr); p.setProperty("value", v, nullptr); old.addChild(p, -1, nullptr); }
            auto migrated = breath.migrateParameterState(old);
            auto value = [&](const char* id) { for (auto ch : migrated) if (ch["id"].toString() == id) return double(ch["value"]); return -999.0; };
            require(value("detection") == 0 && value("reduction") == 10 && value("sensitivity") == 70, "Breath old session mapped wrongly");
        }
        std::cout << "PASS old S.I.P.A., Transient and Breath sessions migrate to the new controls\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << "FAIL: " << e.what() << "\n"; return 1; }
}
