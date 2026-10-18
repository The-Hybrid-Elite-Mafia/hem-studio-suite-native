#include "NativeResourceState.h"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <limits>
#include <string>
#include <system_error>

namespace hem::nativeResource {
namespace {
juce::Result bad(const char* message) { return juce::Result::fail(message); }

bool cleanText(const juce::String& text, size_t limit, bool allowEmpty)
{
    const auto bytes = text.getNumBytesAsUTF8();
    if ((!allowEmpty && bytes == 0) || bytes > limit) return false;
    for (const auto character : text)
        if (character < 0x20 || (character >= 0x7f && character <= 0x9f)
            || (character >= 0xd800 && character <= 0xdfff) || character > 0x10ffff)
            return false;
    return true;
}

bool validRole(const juce::String& role, int index)
{
    return (role == "sample" && index >= 0 && index <= 3)
        || ((role == "cabinet-ir" || role == "reference") && index == 0);
}

bool sameKey(const ResourceDescriptor& resource, const RoleIndex& expected)
{
    return resource.role == expected.role && resource.index == expected.index;
}

bool validLayout(const ExpectedLayout& layout)
{
    if (!cleanText(layout.ownerType, maxOwnerBytes, false)
        || layout.resources.empty() || layout.resources.size() > maxResources) return false;
    for (size_t i = 0; i < layout.resources.size(); ++i) {
        const auto& key = layout.resources[i];
        if (!validRole(key.role, key.index)) return false;
        for (size_t j = 0; j < i; ++j)
            if (key.role == layout.resources[j].role && key.index == layout.resources[j].index)
                return false;
    }
    return true;
}

bool windowsComponent(const juce::String& component)
{
    return component.isNotEmpty() && component != "." && component != ".."
        && !component.containsAnyOf(":*?\"<>|/\\")
        && !component.endsWithChar('.') && !component.endsWithChar(' ');
}

bool absoluteDescriptorPath(const juce::String& path)
{
    if (!cleanText(path, maxPathBytes, false)) return false;
    // UNC syntax is recognized independently of the machine doing the recall.
    // Both common separator forms are allowed, but authority/share must exist.
    if (path.startsWith("\\\\") || path.startsWith("//")) {
        const auto separator = path[0];
        const auto other = separator == '/' ? '\\' : '/';
        if (path.containsChar(other)) return false;
        const auto tail = path.substring(2);
        const auto serverEnd = tail.indexOfChar(separator);
        if (serverEnd <= 0 || !windowsComponent(tail.substring(0, serverEnd))) return false;
        const auto afterServer = tail.substring(serverEnd + 1);
        const auto shareEnd = afterServer.indexOfChar(separator);
        const auto share = shareEnd < 0 ? afterServer : afterServer.substring(0, shareEnd);
        if (!windowsComponent(share)) return false;
        // Forbid device/URL/alternate-stream/wildcard interpretation throughout.
        return !afterServer.containsAnyOf(":*?\"<>|");
    }
    // A single initial '/' is POSIX absolute, regardless of local OS.
    if (path.startsWithChar('/')) return true;
    if (path.length() < 3) return false;
    const auto drive = path[0];
    if (!((drive >= 'A' && drive <= 'Z') || (drive >= 'a' && drive <= 'z'))
        || path[1] != ':' || (path[2] != '/' && path[2] != '\\')) return false;
    const auto tail = path.substring(3);
    // Reject C://host URL-like roots, device namespaces and alternate streams.
    return !tail.startsWithChar('/') && !tail.startsWithChar('\\')
        && !tail.containsAnyOf(":*?\"<>|");
}

bool validHash(const juce::String& hash)
{
    if (hash.length() != 64 || hash.getNumBytesAsUTF8() != 64) return false;
    for (const auto c : hash)
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
    return true;
}

// JSON decimal grammar also describes the strict subset accepted from XML.
// Retain coefficient and exponent to reject fractional integers exactly, rather
// than first rounding a decimal such as 9007199254740991.1 to a double.
struct Decimal {
    std::string digits;
    int scale = 0;
    bool negative = false;
};

bool decimal(const juce::String& text, Decimal& out)
{
    if (text.isEmpty() || text.getNumBytesAsUTF8() > maxSerializedBytes) return false;
    const auto s = text.toStdString();
    size_t p = 0;
    Decimal result;
    if (s[p] == '-') { result.negative = true; if (++p == s.size()) return false; }
    const auto start = p;
    if (s[p] == '0') ++p;
    else {
        if (s[p] < '1' || s[p] > '9') return false;
        while (p < s.size() && s[p] >= '0' && s[p] <= '9') ++p;
    }
    result.digits = s.substr(start, p - start);
    int fraction = 0;
    if (p < s.size() && s[p] == '.') {
        const auto begin = ++p;
        while (p < s.size() && s[p] >= '0' && s[p] <= '9') ++p;
        if (p == begin) return false;
        fraction = static_cast<int>(p - begin);
        result.digits.append(s, begin, p - begin);
    }
    int exponent = 0;
    bool negativeExponent = false;
    if (p < s.size() && (s[p] == 'e' || s[p] == 'E')) {
        if (++p == s.size()) return false;
        if (s[p] == '+' || s[p] == '-') {
            negativeExponent = s[p] == '-';
            if (++p == s.size()) return false;
        }
        const auto begin = p;
        while (p < s.size() && s[p] >= '0' && s[p] <= '9') {
            // Saturating beyond any possible coefficient length is sufficient.
            exponent = std::min(static_cast<int>(maxSerializedBytes) + 32,
                                exponent * 10 + int(s[p++] - '0'));
        }
        if (p == begin) return false;
    }
    if (p != s.size()) return false;
    result.scale = (negativeExponent ? -exponent : exponent) - fraction;
    out = std::move(result);
    return true;
}

bool exactInteger(const juce::var& value, juce::int64& out)
{
    if (value.isInt() || value.isInt64()) {
        const auto n = static_cast<juce::int64>(value);
        if (n < 0 || n > maxExactInteger) return false;
        out = n;
        return true;
    }
    if (value.isDouble()) {
        const auto n = static_cast<double>(value);
        if (!std::isfinite(n) || n < 0 || n > double(maxExactInteger) || std::floor(n) != n)
            return false;
        out = static_cast<juce::int64>(n);
        return true;
    }
    if (!value.isString()) return false; // In particular, bool is never numeric.
    Decimal d;
    if (!decimal(value.toString(), d)) return false;
    const auto first = d.digits.find_first_not_of('0');
    if (first == std::string::npos) { out = 0; return true; }
    if (d.negative) return false;
    d.digits.erase(0, first);
    while (d.scale < 0 && d.digits.back() == '0') { d.digits.pop_back(); ++d.scale; }
    if (d.scale < 0 || d.digits.size() + size_t(d.scale) > 16) return false;
    juce::int64 n = 0;
    for (const auto c : d.digits) n = n * 10 + (c - '0');
    for (int i = 0; i < d.scale; ++i) n *= 10;
    if (n > maxExactInteger) return false;
    out = n;
    return true;
}

bool finiteNumber(const juce::var& value, double& out)
{
    double n = 0;
    if (value.isInt() || value.isInt64() || value.isDouble()) n = static_cast<double>(value);
    else if (value.isString()) {
        Decimal d;
        if (!decimal(value.toString(), d)) return false;
        const auto s = value.toString().toStdString();
        const auto parsed = std::from_chars(s.data(), s.data() + s.size(), n,
                                            std::chars_format::general);
        if (parsed.ec != std::errc{} || parsed.ptr != s.data() + s.size()) return false;
    } else return false;
    if (!std::isfinite(n)) return false;
    out = n;
    return true;
}

template <size_t N>
bool properties(const juce::ValueTree& tree, const char* const (&names)[N])
{
    if (tree.getNumProperties() != int(N)) return false;
    for (const auto* name : names) if (!tree.hasProperty(name)) return false;
    for (int i = 0; i < tree.getNumProperties(); ++i) {
        const auto& value = tree[tree.getPropertyName(i)];
        if (!(value.isString() || value.isInt() || value.isInt64() || value.isDouble())) return false;
        if (value.isString() && value.toString().getNumBytesAsUTF8() > maxSerializedBytes) return false;
    }
    return true;
}

bool boundedSerialization(const juce::ValueTree& tree)
{
    const auto xml = tree.createXml();
    return xml != nullptr && xml->toString().getNumBytesAsUTF8() <= maxSerializedBytes;
}

juce::ValueTree uncheckedEncode(const State& state, const ExpectedLayout& layout)
{
    juce::ValueTree root("NATIVE_RESOURCE");
    root.setProperty("version", manifestVersion, nullptr);
    root.setProperty("ownerType", state.ownerType, nullptr);
    for (const auto& key : layout.resources) {
        const auto& resource = *std::find_if(state.resources.begin(), state.resources.end(),
                                           [&](const auto& r) { return sameKey(r, key); });
        juce::ValueTree child("RESOURCE");
        child.setProperty("role", resource.role, nullptr);
        child.setProperty("index", resource.index, nullptr);
        child.setProperty("path", resource.path, nullptr);
        child.setProperty("sha256", resource.sha256, nullptr);
        child.setProperty("byteSize", resource.byteSize, nullptr);
        child.setProperty("sourceRate", resource.sourceRate, nullptr);
        child.setProperty("channels", resource.channels, nullptr);
        child.setProperty("frames", resource.frames, nullptr);
        root.addChild(child, -1, nullptr);
    }
    return root;
}
} // namespace

juce::Result validateManifest(const State& state, const ExpectedLayout& layout)
{
    if (!validLayout(layout)) return bad("Invalid native resource expected layout");
    if (state.ownerType != layout.ownerType) return bad("Native resource owner mismatch");
    if (state.resources.size() != layout.resources.size()) return bad("Incomplete native resource coverage");
    for (const auto& key : layout.resources) {
        if (std::count_if(state.resources.begin(), state.resources.end(),
                         [&](const auto& r) { return sameKey(r, key); }) != 1)
            return bad("Unknown or duplicate native resource role/index");
    }
    for (const auto& resource : state.resources) {
        if (resource.path.isEmpty()) {
            if (resource.sha256.isNotEmpty() || resource.byteSize != 0 || resource.sourceRate != 0
                || resource.channels != 0 || resource.frames != 0)
                return bad("Non-canonical empty native resource");
        } else {
            if (!absoluteDescriptorPath(resource.path)) return bad("Invalid native resource absolute path");
            if (!validHash(resource.sha256)) return bad("Invalid native resource SHA-256");
            if (resource.byteSize < 1 || resource.byteSize > maxExactInteger
                || resource.frames < 1 || resource.frames > maxExactInteger
                || !std::isfinite(resource.sourceRate) || resource.sourceRate < 8000
                || resource.sourceRate > 384000 || (resource.channels != 1 && resource.channels != 2))
                return bad("Invalid native resource metadata");
        }
    }
    if (!boundedSerialization(uncheckedEncode(state, layout))) return bad("Native resource exceeds 128 KiB");
    return juce::Result::ok();
}

juce::Result decodeManifest(const juce::ValueTree& root, const ExpectedLayout& layout, State& out)
{
    static const char* const rootKeys[] = { "version", "ownerType" };
    static const char* const resourceKeys[] = {
        "role", "index", "path", "sha256", "byteSize", "sourceRate", "channels", "frames"
    };
    if (!validLayout(layout)) return bad("Invalid native resource expected layout");
    if (!root.hasType("NATIVE_RESOURCE") || !properties(root, rootKeys)
        || root.getNumChildren() != int(layout.resources.size())) return bad("Invalid NATIVE_RESOURCE shape");
    if (!root["ownerType"].isString() || root["ownerType"].toString() != layout.ownerType)
        return bad("Native resource owner mismatch");
    for (const auto child : root) {
        if (!child.hasType("RESOURCE") || child.getNumChildren() != 0 || !properties(child, resourceKeys))
            return bad("Invalid RESOURCE shape");
        for (const auto* name : { "role", "path", "sha256" })
            if (!child[name].isString()) return bad("Invalid native resource string type");
        if (!cleanText(child["role"].toString(), 10, false)
            || !cleanText(child["path"].toString(), maxPathBytes, true)
            || !cleanText(child["sha256"].toString(), 64, true)) return bad("Invalid native resource text");
    }
    // Shape and scalar bounds have already rejected arbitrary recursive data.
    if (!boundedSerialization(root)) return bad("Native resource exceeds 128 KiB");
    juce::int64 version = 0;
    if (!exactInteger(root["version"], version) || version != manifestVersion)
        return bad("Unsupported native resource version");
    State candidate;
    candidate.ownerType = root["ownerType"].toString();
    candidate.resources.reserve(layout.resources.size());
    for (const auto child : root) {
        ResourceDescriptor resource;
        resource.role = child["role"].toString();
        resource.path = child["path"].toString();
        resource.sha256 = child["sha256"].toString();
        juce::int64 index = 0, channels = 0;
        if (!exactInteger(child["index"], index) || index > 3
            || !exactInteger(child["channels"], channels) || channels > 2
            || !exactInteger(child["byteSize"], resource.byteSize)
            || !exactInteger(child["frames"], resource.frames)
            || !finiteNumber(child["sourceRate"], resource.sourceRate)) return bad("Invalid native resource number");
        resource.index = static_cast<int>(index);
        resource.channels = static_cast<int>(channels);
        candidate.resources.push_back(std::move(resource));
    }
    const auto result = validateManifest(candidate, layout);
    if (result.failed()) return result;
    out = std::move(candidate);
    return juce::Result::ok();
}

juce::Result encodeManifest(const State& state, const ExpectedLayout& layout, juce::ValueTree& out)
{
    const auto result = validateManifest(state, layout);
    if (result.failed()) return result;
    auto candidate = uncheckedEncode(state, layout);
    out = std::move(candidate);
    return juce::Result::ok();
}
juce::Result encodeManifestXml(const State& state, const ExpectedLayout& layout,
                               std::unique_ptr<juce::XmlElement>& out)
{
    juce::ValueTree encoded;
    const auto result = encodeManifest(state, layout, encoded);
    if (result.failed()) return result;
    auto candidate = encoded.createXml();
    if (candidate == nullptr) return bad("Cannot encode native resource XML");
    for (int i = 0; i < encoded.getNumChildren(); ++i) {
        const auto rate = static_cast<double>(encoded.getChild(i)["sourceRate"]);
        char buffer[64];
        const auto formatted = std::to_chars(buffer, buffer + sizeof(buffer), rate == 0 ? 0.0 : rate,
                                              std::chars_format::general,
                                              std::numeric_limits<double>::max_digits10);
        if (formatted.ec != std::errc{}) return bad("Cannot encode exact native resource rate");
        candidate->getChildElement(i)->setAttribute("sourceRate",
            juce::String::fromUTF8(buffer, static_cast<int>(formatted.ptr - buffer)));
    }
    if (candidate->toString().getNumBytesAsUTF8() > maxSerializedBytes)
        return bad("Native resource XML exceeds 128 KiB");
    out = std::move(candidate);
    return juce::Result::ok();
}
} // namespace hem::nativeResource
