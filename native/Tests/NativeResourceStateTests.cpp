#include "../Source/Systems/NativeResourceState.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

namespace nr = hem::nativeResource;
namespace {
int assertions = 0;
void check(bool condition, const std::string& message)
{
    ++assertions;
    if (!condition) throw std::runtime_error(message);
}
void ok(const juce::Result& result, const std::string& message)
{
    check(result.wasOk(), message + ": " + result.getErrorMessage().toStdString());
}
const juce::String hash = juce::String::repeatedString("0123456789abcdef", 4);
nr::ExpectedLayout one() { return { "DRUM_REPLACER", { { "sample", 0 } } }; }
nr::ExpectedLayout four()
{
    return { "DRUM_REPLACER", { { "sample", 0 }, { "sample", 1 }, { "sample", 2 }, { "sample", 3 } } };
}
nr::ResourceDescriptor assigned(juce::String role = "sample", int index = 0)
{
    return { role, index, "/media/one.wav", hash, 123456, 48000, 2, 23456 };
}
nr::State stateFor(const nr::ExpectedLayout& layout)
{
    nr::State s;
    s.ownerType = layout.ownerType;
    for (const auto& key : layout.resources) s.resources.push_back(assigned(key.role, key.index));
    return s;
}
bool equal(const nr::State& a, const nr::State& b)
{
    if (a.ownerType != b.ownerType || a.resources.size() != b.resources.size()) return false;
    for (size_t i = 0; i < a.resources.size(); ++i) {
        const auto& x = a.resources[i]; const auto& y = b.resources[i];
        if (x.role != y.role || x.index != y.index || x.path != y.path || x.sha256 != y.sha256
            || x.byteSize != y.byteSize || x.sourceRate != y.sourceRate
            || x.channels != y.channels || x.frames != y.frames) return false;
    }
    return true;
}
juce::ValueTree treeFor(const nr::State& s, const nr::ExpectedLayout& layout)
{
    juce::ValueTree tree;
    ok(nr::encodeManifest(s, layout, tree), "fixture encode");
    return tree;
}
juce::ValueTree fixture() { return treeFor(stateFor(one()), one()); }
void rejectTree(const juce::ValueTree& tree, const nr::ExpectedLayout& layout, const std::string& label)
{
    const nr::ExpectedLayout sentinelLayout { "SENTINEL", { { "reference", 0 } } };
    auto output = stateFor(sentinelLayout);
    const auto before = output;
    check(nr::decodeManifest(tree, layout, output).failed(), "reject tree: " + label);
    check(equal(output, before), "decode output conserved: " + label);
}
void rejectState(const nr::State& state, const nr::ExpectedLayout& layout, const std::string& label)
{
    check(nr::validateManifest(state, layout).failed(), "reject state: " + label);
    juce::ValueTree output("SENTINEL");
    output.setProperty("retained", "unchanged", nullptr);
    output.addChild(juce::ValueTree("CHILD"), -1, nullptr);
    const auto alias = output;
    const auto before = output.createCopy();
    check(nr::encodeManifest(state, layout, output).failed(), "reject encode: " + label);
    check(output == alias && output.isEquivalentTo(before), "encode output conserved: " + label);
    auto xml = std::make_unique<juce::XmlElement>("SENTINEL");
    xml->setAttribute("retained", "unchanged");
    auto* identity = xml.get();
    const auto xmlBefore = xml->toString();
    check(nr::encodeManifestXml(state, layout, xml).failed(), "reject XML encode: " + label);
    check(xml.get() == identity && xml->toString() == xmlBefore, "XML output conserved: " + label);
}
void rejectProperty(const char* key, const juce::var& value, const std::string& label, bool root = false)
{
    auto tree = fixture();
    (root ? tree : tree.getChild(0)).setProperty(key, value, nullptr);
    rejectTree(tree, one(), label);
}
// Exercise JUCE's actual scalar JSON writer/parser without pulling the full
// host or PresetManager into this dependency-light codec target.
juce::ValueTree jsonScalarRoundTrip(const juce::ValueTree& tree)
{
    auto* fields = new juce::DynamicObject();
    juce::var value(fields);
    for (int i = 0; i < tree.getNumProperties(); ++i) {
        const auto key = tree.getPropertyName(i);
        fields->setProperty(key, tree[key]);
    }
    juce::var parsed;
    ok(juce::JSON::parse(juce::JSON::toString(value), parsed), "JSON scalar parsing");
    check(parsed.getDynamicObject() != nullptr, "JSON properties object");
    juce::ValueTree result(tree.getType());
    for (const auto& entry : parsed.getDynamicObject()->getProperties())
        result.setProperty(entry.name, entry.value, nullptr);
    for (const auto child : tree) result.addChild(jsonScalarRoundTrip(child), -1, nullptr);
    return result;
}
void roundTrip(const nr::State& state, const nr::ExpectedLayout& layout)
{
    auto tree = treeFor(state, layout);
    nr::State decoded;
    ok(nr::decodeManifest(tree, layout, decoded), "typed round trip");
    check(equal(state, decoded), "typed exact equality");
    std::unique_ptr<juce::XmlElement> xml;
    ok(nr::encodeManifestXml(state, layout, xml), "exact manifest XML encoding");
    const auto standardXml = tree.createXml();
    auto restoredFormatting = std::make_unique<juce::XmlElement>(*xml);
    for (int i = 0; i < tree.getNumChildren(); ++i)
        restoredFormatting->getChildElement(i)->setAttribute("sourceRate", standardXml->getChildElement(i)->getStringAttribute("sourceRate"));
    check(restoredFormatting->toString() == standardXml->toString(), "only XML sourceRate formatting changed");
    const auto parsedXml = juce::parseXML(xml->toString());
    check(parsedXml != nullptr, "XML parse");
    nr::State xmlState;
    ok(nr::decodeManifest(juce::ValueTree::fromXml(*parsedXml), layout, xmlState), "XML round trip");
    check(equal(state, xmlState), "XML exact metadata equality");
    juce::MemoryOutputStream stream;
    tree.writeToStream(stream);
    nr::State binaryState;
    ok(nr::decodeManifest(juce::ValueTree::readFromData(stream.getData(), stream.getDataSize()), layout, binaryState), "binary tree round trip");
    check(equal(state, binaryState), "binary exact metadata equality");
    nr::State jsonState;
    ok(nr::decodeManifest(jsonScalarRoundTrip(tree), layout, jsonState), "JSON scalar round trip");
    check(equal(state, jsonState), "JSON scalar exact metadata equality");
}
void shapeAndOwnership()
{
    auto layout = four();
    auto source = stateFor(layout);
    source.resources[1] = { "sample", 1, {}, {}, 0, 0, 0, 0 };
    roundTrip(source, layout);
    auto tree = treeFor(source, layout);
    check(tree.hasType("NATIVE_RESOURCE") && tree.getNumProperties() == 2 && tree.getNumChildren() == 4, "exact root shape");
    check(tree["version"].isInt() && int(tree["version"]) == 1 && tree["ownerType"].isString(), "exact root field types");
    const char* const keys[] { "role", "index", "path", "sha256", "byteSize", "sourceRate", "channels", "frames" };
    for (int i = 0; i < 4; ++i) {
        const auto child = tree.getChild(i);
        check(child.hasType("RESOURCE") && child.getNumChildren() == 0 && child.getNumProperties() == 8, "exact child shape");
        for (const auto* key : keys) check(child.hasProperty(key), std::string("required child field ") + key);
        check(child["role"].isString() && child["path"].isString() && child["sha256"].isString(), "string field types");
        check(child["index"].isInt() && child["channels"].isInt() && child["byteSize"].isInt64()
              && child["frames"].isInt64() && child["sourceRate"].isDouble(), "exact numeric field types");
        check(int(child["index"]) == i, "expected order");
    }
    const auto empty = tree.getChild(1);
    check(empty["path"].toString().isEmpty() && empty["sha256"].toString().isEmpty()
          && juce::int64(empty["byteSize"]) == 0 && double(empty["sourceRate"]) == 0
          && int(empty["channels"]) == 0 && juce::int64(empty["frames"]) == 0, "canonical empty encoding");
    auto reversed = source;
    std::reverse(reversed.resources.begin(), reversed.resources.end());
    check(treeFor(reversed, layout).isEquivalentTo(tree), "encoder canonical layout order");
    tree.moveChild(0, 3, nullptr);
    nr::State decoded;
    ok(nr::decodeManifest(tree, layout, decoded), "decode independent of child order");
    const auto decodedCopy = decoded;
    tree.getChild(0).setProperty("path", "/mutated/input", nullptr);
    check(equal(decoded, decodedCopy), "input tree has no mutable alias into decoded state");
    auto stateCopy = source;
    stateCopy.ownerType += "changed";
    stateCopy.resources[0].path += ".changed";
    stateCopy.resources.pop_back();
    check(source.ownerType == layout.ownerType && source.resources.size() == 4
          && source.resources[0].path == "/media/one.wav", "state copies have deep value ownership");
    auto encoded = treeFor(source, layout);
    source.resources[0].path = "/mutated/source";
    check(encoded.getChild(0)["path"].toString() == "/media/one.wav", "encoder retains no mutable state alias");
    auto encoded2 = treeFor(source, layout);
    encoded2.getChild(0).setProperty("path", "/mutated/output", nullptr);
    check(source.resources[0].path == "/mutated/source" && encoded.getChild(0)["path"].toString() == "/media/one.wav", "independent encodings");
}
void schemas()
{
    rejectTree({}, one(), "invalid tree");
    for (const auto* type : { "RESOURCE", "MATCH_PROFILE", "CLIP_EDIT", "NATIVE_RESOURCES" })
        rejectTree(juce::ValueTree(type), one(), std::string("wrong root ") + type);
    for (const auto* field : { "version", "ownerType" }) {
        auto tree = fixture(); tree.removeProperty(field, nullptr); rejectTree(tree, one(), "missing root field");
    }
    for (const auto* field : { "role", "index", "path", "sha256", "byteSize", "sourceRate", "channels", "frames" }) {
        auto tree = fixture(); tree.getChild(0).removeProperty(field, nullptr); rejectTree(tree, one(), std::string("missing ") + field);
    }
    auto tree = fixture(); tree.setProperty("profile", "opaque", nullptr); rejectTree(tree, one(), "unknown root property");
    tree = fixture(); tree.getChild(0).setProperty("extra", 1, nullptr); rejectTree(tree, one(), "unknown child property");
    tree = fixture(); tree.getChild(0).removeProperty("frames", nullptr); tree.getChild(0).setProperty("extra", 1, nullptr); rejectTree(tree, one(), "same-count field replacement");
    tree = fixture(); tree.removeAllChildren(nullptr); rejectTree(tree, one(), "zero children");
    tree = fixture(); tree.addChild(tree.getChild(0).createCopy(), -1, nullptr); rejectTree(tree, one(), "extra child");
    tree = fixture(); tree.getChild(0).addChild(juce::ValueTree("RESOURCE"), -1, nullptr); rejectTree(tree, one(), "nested child");
    for (const auto* type : { "MATCH_PROFILE", "CLIP_EDIT", "PARAM", "OPAQUE" }) {
        tree = fixture(); tree.removeAllChildren(nullptr); tree.addChild(juce::ValueTree(type), -1, nullptr); rejectTree(tree, one(), std::string("unknown child ") + type);
    }
    for (const auto& value : { juce::var(0), juce::var(2), juce::var(1.1), juce::var(true), juce::var("1suffix") })
        rejectProperty("version", value, "invalid version", true);
    rejectProperty("ownerType", "UNKNOWN", "unknown owner", true);
    rejectProperty("ownerType", 1, "numeric owner", true);
    rejectProperty("ownerType", "", "empty owner", true);
    auto fourTree = treeFor(stateFor(four()), four());
    fourTree.getChild(3).setProperty("index", 0, nullptr); rejectTree(fourTree, four(), "duplicate/missing sample index");
    for (const auto* role : { "Sample", "unknown", "reference ", "", "MATCH_PROFILE", "cabinet" })
        rejectProperty("role", role, std::string("unknown role ") + role);
    rejectProperty("role", "reference", "known role absent from expected layout");
    auto state = stateFor(one());
    state.resources.push_back(assigned()); rejectState(state, one(), "duplicate descriptor");
    state = stateFor(four()); state.resources.pop_back(); rejectState(state, four(), "partial four-slot state");
    auto layout = one(); layout.resources.clear(); rejectState(stateFor(one()), layout, "empty expected layout");
    layout = four(); layout.resources.push_back({ "reference", 0 }); rejectState(stateFor(layout), layout, "five resources exceeds bounds");
    layout = one(); layout.resources.push_back({ "sample", 0 }); rejectState(stateFor(layout), layout, "duplicate expected key");
    for (auto key : { nr::RoleIndex { "sample", -1 }, nr::RoleIndex { "sample", 4 }, nr::RoleIndex { "reference", 1 }, nr::RoleIndex { "cabinet-ir", 1 }, nr::RoleIndex { "other", 0 } }) {
        layout = { "OWNER", { key } }; rejectState(stateFor(layout), layout, "invalid expected key");
    }
    for (const auto* role : { "reference", "cabinet-ir" }) {
        layout = { "OWNER", { { role, 0 } } }; roundTrip(stateFor(layout), layout);
        auto emptyState = stateFor(layout); emptyState.resources[0] = { role, 0, {}, {}, 0, 0, 0, 0 }; roundTrip(emptyState, layout);
    }
    layout = { "OWNER", { { "sample", 3 }, { "reference", 0 }, { "cabinet-ir", 0 } } };
    roundTrip(stateFor(layout), layout);
}
void numbers()
{
    auto s = stateFor(one());
    for (auto n : { juce::int64(1), nr::maxExactInteger }) {
        s.resources[0].byteSize = n; s.resources[0].frames = n; roundTrip(s, one());
    }
    for (auto n : { juce::int64(-1), juce::int64(0), nr::maxExactInteger + 1, std::numeric_limits<juce::int64>::max() }) {
        s = stateFor(one()); s.resources[0].byteSize = n; rejectState(s, one(), "invalid byte count");
        s = stateFor(one()); s.resources[0].frames = n; rejectState(s, one(), "invalid frame count");
    }
    for (double rate : { 8000.0, 384000.0, 44100.25, std::nextafter(8000.0, 9000.0), 44100.123456789123 }) {
        s = stateFor(one()); s.resources[0].sourceRate = rate; roundTrip(s, one());
    }
    for (double rate : { 0.0, std::nextafter(8000.0, 0.0), std::nextafter(384000.0, 400000.0), std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN() }) {
        s = stateFor(one()); s.resources[0].sourceRate = rate; rejectState(s, one(), "invalid rate");
    }
    for (int channels : { -1, 0, 3 }) { s = stateFor(one()); s.resources[0].channels = channels; rejectState(s, one(), "invalid channel count"); }
    s = stateFor(one()); s.resources[0].channels = 1; roundTrip(s, one());
    for (const auto* key : { "index", "byteSize", "channels", "frames" }) {
        for (const auto& value : { juce::var(true), juce::var(false), juce::var(0.5), juce::var(-1), juce::var(std::numeric_limits<double>::infinity()), juce::var(std::numeric_limits<double>::quiet_NaN()), juce::var("1x"), juce::var("1.1"), juce::var("0.00000000000000001"), juce::var("9007199254740991.1"), juce::var("9007199254740992"), juce::var("1e100000"), juce::var("1e-100000"), juce::var("1.0000000000000000001") })
            rejectProperty(key, value, std::string("strict integer ") + key);
    }
    for (const auto* key : { "index", "byteSize", "channels", "frames", "sourceRate" }) {
        for (const auto* text : { "", " ", " 1", "1 ", "+1", "01", ".1", "1.", "1e", "1e+", "--1", "nan", "inf", "0x1", "1,0", "1\n", "1junk" })
            rejectProperty(key, text, std::string("bad numeric text ") + text);
        rejectProperty(key, juce::var(), "void numeric");
        rejectProperty(key, juce::var(new juce::DynamicObject()), "object numeric");
        rejectProperty(key, juce::Array<juce::var> { 1 }, "array numeric");
    }
    for (const auto* text : { "9007199254740991", "9007199254740991.0", "9.007199254740991e15", "900719925474099100e-2" }) {
        auto tree = fixture(); tree.getChild(0).setProperty("frames", text, nullptr); nr::State decoded;
        ok(nr::decodeManifest(tree, one(), decoded), "exact decimal maximum"); check(decoded.resources[0].frames == nr::maxExactInteger, "exact maximum preserved");
    }
    auto tree = fixture(); tree.setProperty("version", "1.00e0", nullptr); tree.getChild(0).setProperty("index", "-0e0", nullptr);
    tree.getChild(0).setProperty("channels", "20e-1", nullptr); tree.getChild(0).setProperty("sourceRate", "4.410025e4", nullptr);
    nr::State decoded; ok(nr::decodeManifest(tree, one(), decoded), "strict complete XML decimals");
    check(decoded.resources[0].sourceRate == 44100.25, "fractional source rate preserved");
    rejectProperty("sourceRate", true, "bool sample rate");
    rejectProperty("sourceRate", "8000suffix", "rate suffix");
    rejectProperty("sourceRate", "1e309", "rate overflow");
    rejectProperty("sourceRate", "1e-999", "rate underflow");
}
void fractionalRateSerializationBoundary()
{
    auto source = stateFor(one());
    source.resources[0].sourceRate = std::nextafter(384000.0, 300000.0);
    auto tree = treeFor(source, one());
    nr::State typed;
    ok(nr::decodeManifest(tree, one(), typed), "edge rate typed decode");
    check(equal(source, typed), "edge rate retained exactly in encoded ValueTree");
    auto xml = tree.createXml();
    nr::State usualXml;
    ok(nr::decodeManifest(juce::ValueTree::fromXml(*xml), one(), usualXml), "JUCE XML edge rate decode");
    // This is a characterized upstream formatting limitation, not permission
    // for codec rounding. Session XML serializers can retain exact metadata by
    // replacing only RESOURCE.sourceRate attributes with max_digits10 text.
    check(usualXml.resources[0].sourceRate != source.resources[0].sourceRate,
          "known pinned JUCE XML rate quantization is visible");
    check(std::abs(usualXml.resources[0].sourceRate - source.resources[0].sourceRate)
          <= 1e-10, "pinned JUCE XML rate quantization bound for this fixture");
    ok(nr::encodeManifestXml(source, one(), xml), "exact edge-rate XML encoding");
    const auto reparsed = juce::parseXML(xml->toString());
    check(reparsed != nullptr, "precise XML fixture parse");
    nr::State precise;
    ok(nr::decodeManifest(juce::ValueTree::fromXml(*reparsed), one(), precise), "exact-rate XML decode");
    check(equal(source, precise), "max_digits10 sourceRate has exact XML restoration");
    nr::State json;
    ok(nr::decodeManifest(jsonScalarRoundTrip(tree), one(), json), "edge rate JSON decode");
    check(equal(source, json), "edge rate JSON exact restoration");
    for (const auto rate : { 8000.0, std::nextafter(8000.0, 9000.0),
                            std::nextafter(10000.0, 9000.0), 10000.0, std::nextafter(10000.0, 11000.0),
                            std::nextafter(100000.0, 90000.0), 100000.0, std::nextafter(100000.0, 110000.0),
                            std::nextafter(384000.0, 300000.0), 384000.0 }) {
        source.resources[0].sourceRate = rate;
        roundTrip(source, one());
    }
    uint64_t random = 0xdaf02410b17ULL;
    for (int i = 0; i < 256; ++i) {
        random ^= random << 13; random ^= random >> 7; random ^= random << 17;
        source.resources[0].sourceRate = 8000.0 + (384000.0 - 8000.0)
            * (double(random >> 11) / double(uint64_t(1) << 53));
        roundTrip(source, one());
    }
}
void pathsAndText()
{
    for (const auto* path : { "/", "/tmp/audio.wav", "/media/a b & <c> \"d\".wav", "C:\\Audio\\one.wav", "z:/音楽/one.wav", "C:/", "\\\\server\\share\\one.wav", "\\\\server\\share", "//server/share/one.wav", "//server/share", "/missing/never-opened.wav" }) {
        auto s = stateFor(one()); s.resources[0].path = juce::String::fromUTF8(path); roundTrip(s, one());
    }
    for (const auto* path : { "relative.wav", "../one.wav", "./one.wav", "~/.audio.wav", "C:one.wav", "C:", "\\one.wav", "https://example.com/one.wav", "file:///tmp/one.wav", "c://one.wav", "//", "//server", "//server/", "///share/one.wav", "\\\\server", "\\\\server\\", "\\\\?\\C:\\one.wav", "\\\\.\\share\\one.wav", "//./share/one.wav", "//server/../one.wav", "//server//one.wav", "//server/share/one:stream", "//server/share\\one.wav", "C:/one.wav:stream", "C:/wild*.wav", "/tmp/line\nbreak.wav", "/tmp/tab\t.wav", " /tmp/one.wav" }) {
        auto s = stateFor(one()); s.resources[0].path = path; rejectState(s, one(), std::string("invalid path ") + path);
        rejectProperty("path", path, std::string("invalid tree path ") + path);
    }
    auto s = stateFor(one());
    s.resources[0].path = "/" + juce::String::repeatedString("a", 4095); roundTrip(s, one());
    s.resources[0].path += "a"; rejectState(s, one(), "4097-byte path");
    s.resources[0].path = "/" + juce::String::repeatedString(juce::String::fromUTF8("é"), 2047) + "a"; roundTrip(s, one());
    s.resources[0].path += "a"; rejectState(s, one(), "UTF-8 byte rather than character bound");
    for (int c : { 1, 31, 127, 128, 159 }) { s = stateFor(one()); s.resources[0].path += juce::String::charToString(juce::juce_wchar(c)); rejectState(s, one(), "C0/C1 control path"); }
    for (const auto& invalidHash : { hash.substring(1), hash + "0", hash.toUpperCase(), juce::String::repeatedString("g", 64), " " + hash.substring(1), juce::String() }) {
        s = stateFor(one()); s.resources[0].sha256 = invalidHash; rejectState(s, one(), "malformed hash");
    }
    for (const auto* key : { "role", "path", "sha256" }) rejectProperty(key, 123, "numeric text field");
    auto layout = one(); layout.ownerType = juce::String::repeatedString("X", 64); roundTrip(stateFor(layout), layout);
    layout.ownerType += "X"; rejectState(stateFor(layout), layout, "owner 65 bytes");
    layout.ownerType = juce::String::repeatedString(juce::String::fromUTF8("é"), 32); roundTrip(stateFor(layout), layout);
    layout.ownerType += "x"; rejectState(stateFor(layout), layout, "UTF-8 owner 65 bytes");
    layout.ownerType = ""; rejectState(stateFor(layout), layout, "empty expected owner");
}
void emptiesAndBudgets()
{
    auto s = stateFor(one()); s.resources[0] = { "sample", 0, {}, {}, 0, 0, 0, 0 }; roundTrip(s, one());
    for (int which = 0; which < 6; ++which) {
        auto bad = s;
        if (which == 0) bad.resources[0].sha256 = hash;
        if (which == 1) bad.resources[0].byteSize = 1;
        if (which == 2) bad.resources[0].sourceRate = 8000;
        if (which == 3) bad.resources[0].channels = 1;
        if (which == 4) bad.resources[0].frames = 1;
        if (which == 5) bad.resources[0].sourceRate = std::numeric_limits<double>::quiet_NaN();
        rejectState(bad, one(), "partially populated empty descriptor");
    }
    auto tree = treeFor(s, one());
    tree.getChild(0).setProperty("path", "/media/assigned.wav", nullptr); rejectTree(tree, one(), "path alone is not assigned");
    auto large = stateFor(four());
    for (auto& resource : large.resources) resource.path = "/" + juce::String::repeatedString("\"", 4095);
    roundTrip(large, four());
    tree = fixture(); tree.setProperty("version", "1.", nullptr);
    const auto overhead = tree.createXml()->toString().getNumBytesAsUTF8();
    tree.setProperty("version", "1." + juce::String::repeatedString("0", int(nr::maxSerializedBytes - overhead)), nullptr);
    // JUCE wraps attributes differently after the version string becomes long.
    const auto actual = tree.createXml()->toString().getNumBytesAsUTF8();
    tree.setProperty("version", tree["version"].toString().dropLastCharacters(int(actual - nr::maxSerializedBytes)), nullptr);
    check(tree.createXml()->toString().getNumBytesAsUTF8() == nr::maxSerializedBytes, "exact 128KiB fixture");
    nr::State decoded;
    ok(nr::decodeManifest(tree, one(), decoded), "128KiB inclusive serialization bound");
    tree.setProperty("version", tree["version"].toString() + "0", nullptr);
    rejectTree(tree, one(), "128KiB plus one");
    auto tooMany = treeFor(stateFor(four()), four());
    tooMany.addChild(tooMany.getChild(0).createCopy(), -1, nullptr); rejectTree(tooMany, four(), "six nodes");
    auto deep = fixture(); auto child = deep.getChild(0);
    for (int i = 0; i < 100; ++i) { juce::ValueTree nested("RESOURCE"); child.addChild(nested, -1, nullptr); child = nested; }
    rejectTree(deep, one(), "deep nested payload");
}
}
int main()
{
    try {
        shapeAndOwnership(); schemas(); numbers(); fractionalRateSerializationBoundary(); pathsAndText(); emptiesAndBudgets();
        std::cout << "NativeResourceStateTests PASS: " << assertions << " assertions\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "NativeResourceStateTests FAIL after " << assertions << " assertions: " << error.what() << '\n';
        return 1;
    }
}
