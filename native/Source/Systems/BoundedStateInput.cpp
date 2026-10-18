#include "BoundedStateInput.h"

#include <cstdint>
#include <limits>
#include <set>
#include <string>
#include <vector>

namespace hem::stateinput {
namespace {

juce::Result failure(const char* reason) { return juce::Result::fail(reason); }

// Decode one bounded, canonical UTF-8 scalar. Reject NUL too: JUCE's text
// parsers stop at NUL, which must not hide any part of the checked byte view.
bool scalar(std::string_view text, std::size_t& pos, std::uint32_t& value) {
    if (pos == text.size()) return false;
    const auto first = static_cast<unsigned char>(text[pos++]);
    if (first < 0x80) { value = first; return value != 0; }
    unsigned extra = 0;
    std::uint32_t minimum = 0;
    if (first >= 0xc2 && first <= 0xdf) { extra = 1; value = first & 0x1f; minimum = 0x80; }
    else if (first >= 0xe0 && first <= 0xef) { extra = 2; value = first & 0x0f; minimum = 0x800; }
    else if (first >= 0xf0 && first <= 0xf4) { extra = 3; value = first & 0x07; minimum = 0x10000; }
    else return false;
    if (text.size() - pos < extra) return false;
    while (extra-- != 0) {
        const auto next = static_cast<unsigned char>(text[pos++]);
        if ((next & 0xc0) != 0x80) return false;
        value = (value << 6) | (next & 0x3f);
    }
    return value >= minimum && value <= 0x10ffff && !(value >= 0xd800 && value <= 0xdfff);
}

juce::Result checkText(std::string_view text, Limits limits) {
    if (text.empty()) return failure("State input is empty");
    if (text.size() > limits.maxBytes) return failure("State input exceeds byte limit");
    std::size_t pos = 0;
    std::uint32_t value = 0;
    while (pos != text.size())
        if (!scalar(text, pos, value)) return failure("State input is not NUL-free canonical UTF-8");
    return juce::Result::ok();
}

void skipWhitespace(std::string_view text, std::size_t& pos) {
    while (pos < text.size()) {
        auto next = pos;
        std::uint32_t value = 0;
        if (!scalar(text, next, value) || !juce::CharacterFunctions::isWhitespace(static_cast<juce::juce_wchar>(value))) break;
        pos = next;
    }
}

bool starts(std::string_view text, std::size_t pos, std::string_view token) {
    return text.substr(pos, token.size()) == token;
}

bool skipThrough(std::string_view text, std::size_t& pos, std::string_view terminator) {
    const auto end = text.find(terminator, pos);
    if (end == std::string_view::npos) return false;
    pos = end + terminator.size();
    return true;
}

int hex(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

bool codeUnit(std::string_view text, std::size_t& pos, std::uint32_t& value) {
    if (text.size() - pos < 4) return false;
    value = 0;
    for (unsigned i = 0; i < 4; ++i) {
        const auto digit = hex(text[pos++]);
        if (digit < 0) return false;
        value = (value << 4) | static_cast<std::uint32_t>(digit);
    }
    return true;
}

void appendScalar(std::string& output, std::uint32_t value) {
    if (value < 0x80) output.push_back(static_cast<char>(value));
    else {
        if (value < 0x800) output.push_back(static_cast<char>(0xc0 | (value >> 6)));
        else {
            if (value < 0x10000) output.push_back(static_cast<char>(0xe0 | (value >> 12)));
            else {
                output.push_back(static_cast<char>(0xf0 | (value >> 18)));
                output.push_back(static_cast<char>(0x80 | ((value >> 12) & 0x3f)));
            }
            output.push_back(static_cast<char>(0x80 | ((value >> 6) & 0x3f)));
        }
        output.push_back(static_cast<char>(0x80 | (value & 0x3f)));
    }
}

// Deliberately match pinned JUCE 8.0.10 parseString, including single-quoted
// values, \a and its permissive unknown escapes. Strict RFC-only decoding here
// could miss duplicate spellings that JUCE maps to the same Identifier.
bool jsonString(std::string_view text, std::size_t& pos, std::string& decoded) {
    const auto quote = text[pos++];
    while (pos < text.size()) {
        const auto c = text[pos++];
        if (c == quote) return true;
        if (c != '\\') { decoded.push_back(c); continue; }
        if (pos == text.size()) return false;
        const auto escaped = text[pos++];
        switch (escaped) {
            case 'a': decoded.push_back('\a'); break;
            case 'b': decoded.push_back('\b'); break;
            case 'f': decoded.push_back('\f'); break;
            case 'n': decoded.push_back('\n'); break;
            case 'r': decoded.push_back('\r'); break;
            case 't': decoded.push_back('\t'); break;
            case 'u': {
                std::uint32_t value = 0;
                if (!codeUnit(text, pos, value) || value == 0) return false;
                if (value >= 0xd800 && value <= 0xdbff) {
                    if (!starts(text, pos, "\\u")) return false;
                    pos += 2;
                    std::uint32_t low = 0;
                    if (!codeUnit(text, pos, low) || low < 0xdc00 || low > 0xdfff) return false;
                    value = 0x10000 + ((value - 0xd800) << 10) + (low - 0xdc00);
                } else if (value >= 0xdc00 && value <= 0xdfff) return false;
                appendScalar(decoded, value);
                break;
            }
            default: decoded.push_back(escaped); break;
        }
    }
    return false;
}

// JUCE's skipNextWhiteSpace also skips comments/PIs between attributes and
// around '='. Matching those boundaries prevents duplicate-attribute bypasses.
bool xmlTrivia(std::string_view text, std::size_t& pos) {
    for (;;) {
        skipWhitespace(text, pos);
        if (starts(text, pos, "<!--")) {
            pos += 4;
            if (!skipThrough(text, pos, "-->")) return false;
        } else if (starts(text, pos, "<?")) {
            pos += 2;
            if (!skipThrough(text, pos, "?>")) return false;
        } else return true;
    }
}

bool xmlName(std::string_view text, std::size_t& pos) {
    const auto begin = pos;
    while (pos < text.size()) {
        auto next = pos;
        std::uint32_t value = 0;
        if (!scalar(text, next, value)) return false;
        if (!(juce::CharacterFunctions::isLetterOrDigit(static_cast<juce::juce_wchar>(value))
            || value == '_' || value == '-' || value == ':' || value == '.')) break;
        pos = next;
    }
    return pos != begin;
}

bool xmlEntity(std::string_view text, std::size_t& pos) {
    ++pos; // '&'
    const auto begin = pos;
    if (pos < text.size() && text[pos] == '#') {
        ++pos;
        const bool hexadecimal = pos < text.size() && (text[pos] == 'x' || text[pos] == 'X');
        if (hexadecimal) ++pos;
        const auto digitsBegin = pos;
        std::uint32_t value = 0;
        while (pos < text.size() && text[pos] != ';') {
            const auto digit = hexadecimal ? hex(text[pos]) : (text[pos] >= '0' && text[pos] <= '9' ? text[pos] - '0' : -1);
            if (digit < 0 || pos - digitsBegin >= (hexadecimal ? 8u : 12u)) return false;
            const auto radix = hexadecimal ? 16u : 10u;
            if (value > (0x10ffffu - static_cast<unsigned>(digit)) / radix) return false;
            value = value * radix + static_cast<unsigned>(digit);
            ++pos;
        }
        if (pos == digitsBegin || pos == text.size() || value == 0 || (value >= 0xd800 && value <= 0xdfff)) return false;
        ++pos;
        return true;
    }
    // No unbounded search or external expansion: predefined names have <=4
    // letters. In particular even an undeclared "&<element/>;" is unsafe: JUCE
    // can reparse unknown entity replacement text as markup.
    while (pos < text.size() && text[pos] != ';' && pos - begin <= 4) ++pos;
    if (pos == text.size() || text[pos] != ';' || pos - begin > 4) return false;
    std::string name(text.substr(begin, pos - begin));
    for (auto& c : name) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    ++pos;
    return name == "amp" || name == "quot" || name == "apos" || name == "lt" || name == "gt";
}

struct XmlFrame {
    std::size_t children = 0;
    bool countedTextRun = false;
};

bool xmlChild(std::vector<XmlFrame>& frames, Limits limits, bool textRun = false) {
    if (frames.empty()) return true;
    auto& parent = frames.back();
    if (textRun && parent.countedTextRun) return true;
    if (parent.children >= limits.maxChildren) return false;
    ++parent.children;
    parent.countedTextRun = textRun;
    return true;
}

juce::Result xmlStartTag(std::string_view text, std::size_t& pos, std::vector<XmlFrame>& children, Limits limits) {
    ++pos; // '<'
    if (children.size() >= limits.maxDepth) return failure("XML state exceeds raw depth limit");
    if (!xmlChild(children, limits)) return failure("XML element exceeds child limit");
    // JUCE permits whitespace/comments/PIs immediately after '<'.
    if (!xmlTrivia(text, pos) || !xmlName(text, pos)) return failure("Malformed XML start tag");
    std::set<std::string_view> names;
    for (;;) {
        if (!xmlTrivia(text, pos) || pos == text.size()) return failure("Truncated XML start tag");
        if (starts(text, pos, "/>")) { pos += 2; return juce::Result::ok(); }
        if (text[pos] == '>') { ++pos; children.push_back({}); return juce::Result::ok(); }
        const auto nameBegin = pos;
        if (!xmlName(text, pos)) return failure("Malformed XML attribute name");
        if (names.size() >= limits.maxMembers) return failure("XML element exceeds attribute limit");
        if (!names.insert(text.substr(nameBegin, pos - nameBegin)).second) return failure("Duplicate XML attribute name");
        if (!xmlTrivia(text, pos) || pos == text.size() || text[pos++] != '=') return failure("Malformed XML attribute assignment");
        if (!xmlTrivia(text, pos) || pos == text.size() || (text[pos] != '\'' && text[pos] != '"')) return failure("Unquoted XML attribute");
        const auto quote = text[pos++];
        while (pos < text.size() && text[pos] != quote) {
            if (text[pos] == '&') {
                if (!xmlEntity(text, pos)) return failure("Unsupported or malformed XML entity reference");
            } else ++pos;
        }
        if (pos == text.size()) return failure("Truncated XML attribute value");
        ++pos;
    }
}

} // namespace

juce::Result decodeFileText(std::string_view input, std::size_t maxBytes, juce::String& output) {
    if (input.empty()) return failure("State file is empty");
    if (input.size() > maxBytes) return failure("State file exceeds raw byte limit");
    const auto byte = [&](std::size_t pos) { return static_cast<unsigned char>(input[pos]); };
    const bool bigEndian = input.size() >= 2 && byte(0) == 0xfe && byte(1) == 0xff;
    const bool littleEndian = input.size() >= 2 && byte(0) == 0xff && byte(1) == 0xfe;
    std::string decoded;
    std::string_view utf8 = input;
    if (bigEndian || littleEndian) {
        if ((input.size() & 1u) != 0) return failure("Truncated UTF-16 state file code unit");
        decoded.reserve(input.size());
        const auto unit = [&](std::size_t pos) -> std::uint32_t {
            return bigEndian ? (std::uint32_t(byte(pos)) << 8) | byte(pos + 1)
                             : (std::uint32_t(byte(pos + 1)) << 8) | byte(pos);
        };
        for (std::size_t pos = 2; pos < input.size();) {
            auto value = unit(pos);
            pos += 2;
            if (value == 0) return failure("NUL code unit in UTF-16 state file");
            if (value >= 0xd800 && value <= 0xdbff) {
                if (input.size() - pos < 2) return failure("Truncated UTF-16 state file surrogate pair");
                const auto low = unit(pos);
                pos += 2;
                if (low < 0xdc00 || low > 0xdfff) return failure("Invalid UTF-16 state file surrogate pair");
                value = 0x10000 + ((value - 0xd800) << 10) + (low - 0xdc00);
            } else if (value >= 0xdc00 && value <= 0xdfff) {
                return failure("Unpaired low surrogate in UTF-16 state file");
            }
            const std::size_t needed = value < 0x80 ? 1 : value < 0x800 ? 2 : value < 0x10000 ? 3 : 4;
            if (needed > maxBytes - decoded.size()) return failure("State file exceeds decoded UTF-8 byte limit");
            appendScalar(decoded, value);
        }
        utf8 = decoded;
    } else if (input.size() >= 3 && byte(0) == 0xef && byte(1) == 0xbb && byte(2) == 0xbf) {
        // An explicit UTF-8 BOM declares the encoding: no codepage fallback.
        utf8.remove_prefix(3);
    } else if (checkText(input, {maxBytes, recommendedMaxDepth}).failed()) {
        // Match old file.loadFileAsString() for unmarked legacy text, without
        // allowing NUL truncation or ambiguous bytes to skip canonical checks.
        if (input.find('\0') != std::string_view::npos) return failure("NUL byte in state file");
        decoded.reserve(input.size());
        for (std::size_t pos = 0; pos < input.size(); ++pos) {
            // createStringFromData has a historical single-byte special case.
            const auto value = input.size() == 1 ? std::uint32_t(byte(pos))
                : static_cast<std::uint32_t>(juce::CharacterFunctions::getUnicodeCharFromWindows1252Codepage(byte(pos)));
            const std::size_t needed = value < 0x80 ? 1 : value < 0x800 ? 2 : 3;
            if (needed > maxBytes - decoded.size()) return failure("State file exceeds decoded UTF-8 byte limit");
            appendScalar(decoded, value);
        }
        utf8 = decoded;
    }
    const auto checked = checkText(utf8, {maxBytes, recommendedMaxDepth});
    if (checked.failed()) return checked;
    if (utf8.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
        return failure("State file exceeds JUCE text length limit");
    auto result = juce::String::fromUTF8(utf8.data(), static_cast<int>(utf8.size()));
    output = std::move(result);
    return juce::Result::ok();
}

juce::Result preflightJson(std::string_view text, Limits limits) {
    const auto checked = checkText(text, limits);
    if (checked.failed()) return checked;
    struct Frame {
        char opening;
        std::set<std::string> names;
        std::size_t entries = 0;
        bool expectingEntry = true;
    };
    std::vector<Frame> stack;
    for (std::size_t pos = 0; pos < text.size();) {
        skipWhitespace(text, pos);
        if (pos == text.size()) break;
        const auto c = text[pos];
        // Only recognize entry starts; JUCE still owns scalar grammar and
        // separators. Missing values/commas cannot make JUCE construct extra
        // entries: it rejects them before progressing to another value.
        if (!stack.empty() && stack.back().opening == '[' && stack.back().expectingEntry
            && c != ']' && c != ',') {
            if (stack.back().entries >= limits.maxChildren) return failure("JSON array exceeds entry limit");
            ++stack.back().entries;
            stack.back().expectingEntry = false;
        }
        if (c == '"' || c == '\'') {
            std::string decoded;
            if (!jsonString(text, pos, decoded)) return failure("Malformed or truncated JSON string");
            auto after = pos;
            skipWhitespace(text, after);
            if (c == '"' && !stack.empty() && stack.back().opening == '{' && after < text.size() && text[after] == ':') {
                if (stack.back().names.size() >= limits.maxMembers) return failure("JSON object exceeds member limit");
                if (!stack.back().names.insert(std::move(decoded)).second) return failure("Duplicate JSON property name");
            }
        } else if (c == '{' || c == '[') {
            if (stack.size() >= limits.maxDepth) return failure("JSON state exceeds raw depth limit");
            stack.push_back({c, {}});
            ++pos;
        } else if (c == '}' || c == ']') {
            if (stack.empty() || stack.back().opening != (c == '}' ? '{' : '[')) return failure("Unbalanced JSON containers");
            stack.pop_back();
            ++pos;
        } else if (c == ',') {
            if (!stack.empty() && stack.back().opening == '[') stack.back().expectingEntry = true;
            ++pos;
        } else ++pos;
    }
    return stack.empty() ? juce::Result::ok() : failure("Truncated JSON container");
}

juce::Result preflightXml(std::string_view text, Limits limits) {
    const auto checked = checkText(text, limits);
    if (checked.failed()) return checked;
    std::vector<XmlFrame> children;
    for (std::size_t pos = 0; pos < text.size();) {
        if (starts(text, pos, "<!--")) {
            pos += 4;
            if (!skipThrough(text, pos, "-->")) return failure("Truncated XML comment");
        } else if (starts(text, pos, "<![CDATA[")) {
            if (!xmlChild(children, limits)) return failure("XML element exceeds child limit");
            pos += 9;
            if (!skipThrough(text, pos, "]]>")) return failure("Truncated XML CDATA");
        } else if (starts(text, pos, "<?")) {
            pos += 2;
            if (!skipThrough(text, pos, "?>")) return failure("Truncated XML processing instruction");
            // In pinned JUCE, a content PI followed by character data makes
            // readChildElements restore its position to before the PI forever.
            // Skip the whole trivia run once (not repeated suffix lookaheads)
            // and reject only this non-progress combination before parsing.
            if (!children.empty()) {
                if (!xmlTrivia(text, pos)) return failure("Truncated XML processing-instruction trivia");
                if (pos < text.size() && text[pos] != '<')
                    return failure("XML content processing instruction before text is unsupported");
            }
        } else if (starts(text, pos, "<!")) {
            return failure("XML DTDs and entity declarations are unsupported in state input");
        } else if (starts(text, pos, "</")) {
            pos += 2;
            if (children.empty() || !skipThrough(text, pos, ">")) return failure("Unbalanced XML end tag");
            children.pop_back();
        } else if (text[pos] == '<') {
            const auto result = xmlStartTag(text, pos, children, limits);
            if (result.failed()) return result;
        } else if (text[pos] == '&') {
            if (!xmlChild(children, limits, true)) return failure("XML element exceeds child limit");
            if (!xmlEntity(text, pos)) return failure("Unsupported or malformed XML entity reference");
        } else {
            std::uint32_t value = 0;
            if (!scalar(text, pos, value)) return failure("Malformed XML text");
            if (!juce::CharacterFunctions::isWhitespace(static_cast<juce::juce_wchar>(value)))
                if (!xmlChild(children, limits, true)) return failure("XML element exceeds child limit");
        }
    }
    return children.empty() ? juce::Result::ok() : failure("Truncated XML element");
}

} // namespace hem::stateinput
