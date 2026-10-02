#pragma once
#include <JuceHeader.h>

namespace hem {
// A bounded descriptor and the VST3's own opaque state, never the empty HEM
// adapter APVTS. An empty opaque block is valid for a newly selected plug-in.
struct ExternalPluginState {
    static constexpr size_t maxDescriptionBytes = 64 * 1024;
    static constexpr size_t maxStateBytes = 16 * 1024 * 1024;
    juce::String descriptionXml;
    juce::MemoryBlock state;

    bool getDescription(juce::PluginDescription& description, juce::String& error) const {
        error.clear();
        if (descriptionXml.isEmpty() || descriptionXml.getNumBytesAsUTF8() > maxDescriptionBytes) {
            error = "The VST3 descriptor is empty or exceeds 64 KiB."; return false;
        }
        if (state.getSize() > maxStateBytes) {
            error = "The VST3 state exceeds the 16 MiB per-plug-in limit."; return false;
        }
        // No document types, entity declarations, or external XML resources.
        if (descriptionXml.containsIgnoreCase("<!DOCTYPE") || descriptionXml.containsIgnoreCase("<!ENTITY")) {
            error = "The VST3 descriptor contains unsupported XML declarations."; return false;
        }
        int markupStarts = 0;
        for (auto character : descriptionXml)
            if (character == '<' && ++markupStarts > 3) {
                // A descriptor has only a root, optional closing root, and an
                // optional XML declaration. Reject deep input before parsing.
                error = "The VST3 descriptor contains unsupported nested XML."; return false;
            }
        const auto xml = juce::XmlDocument::parse(descriptionXml);
        if (!xml || !xml->hasTagName("PLUGIN") || xml->getNumChildElements() != 0
            || xml->getNumAttributes() > 32 || !xml->hasAttribute("uniqueId")) {
            error = "The VST3 descriptor is malformed."; return false;
        }
        // JUCE's descriptor reader intentionally uses permissive integer
        // conversions. Persistence is stricter: never turn corrupted IDs or
        // channel counts into a different valid-looking plug-in descriptor.
        const auto validDigits = [] (const juce::String& value, const char* digits, int maxLength) {
            return value.isNotEmpty() && value.length() <= maxLength
                && value.containsOnly(digits);
        };
        for (const auto* attribute : {"uniqueId", "uid", "fileTime", "infoUpdateTime"}) {
            if (!xml->hasAttribute(attribute) && juce::String(attribute) != "uniqueId") continue;
            const int maximum = juce::String(attribute).endsWith("Time") ? 16 : 8;
            if (!validDigits(xml->getStringAttribute(attribute), "0123456789abcdefABCDEF", maximum)) {
                error = "The VST3 descriptor has an invalid hexadecimal identifier or timestamp."; return false;
            }
        }
        for (const auto* attribute : {"numInputs", "numOutputs"}) {
            const auto text = xml->getStringAttribute(attribute);
            if (!validDigits(text, "0123456789", 4) || text.getIntValue() > 4096) {
                error = "The VST3 descriptor has invalid channel counts."; return false;
            }
        }
        for (const auto* attribute : {"isInstrument", "isShell", "hasARAExtension"}) {
            if (!xml->hasAttribute(attribute) && juce::String(attribute) != "isInstrument") continue;
            const auto text = xml->getStringAttribute(attribute);
            if (text != "0" && text != "1") {
                error = "The VST3 descriptor has an invalid boolean flag."; return false;
            }
        }
        if (!description.loadFromXml(*xml)) {
            error = "The VST3 descriptor is malformed."; return false;
        }
        if (description.pluginFormatName != "VST3") {
            error = "Only VST3 effect descriptors are supported."; return false;
        }
        if (description.name.isEmpty() || description.name.length() > 4096
            || description.fileOrIdentifier.length() > 4096
            || !juce::File::isAbsolutePath(description.fileOrIdentifier)
            || !description.fileOrIdentifier.endsWithIgnoreCase(".vst3")) {
            error = "The VST3 descriptor needs a name and an absolute .vst3 file or bundle path."; return false;
        }
        if (description.isInstrument) {
            error = "VST3 instruments are unsupported; select a stereo audio effect."; return false;
        }
        if (description.numInputChannels < 0 || description.numOutputChannels < 0
            || description.numInputChannels > 4096 || description.numOutputChannels > 4096) {
            error = "The VST3 descriptor has invalid channel counts."; return false;
        }
        return true;
    }

    bool validate(juce::String& error) const {
        juce::PluginDescription description;
        return getDescription(description, error);
    }
};
} // namespace hem
