#include "../Source/Systems/BoundedStateInput.h"

#include <chrono>
#include <fstream>
#include <iostream>
#include <iterator>
#include <random>
#include <stdexcept>
#include <string>

namespace {
using hem::stateinput::Limits;
using hem::stateinput::decodeFileText;
using hem::stateinput::preflightJson;
using hem::stateinput::preflightXml;
constexpr std::size_t MiB = 1024 * 1024;
constexpr Limits stateLimits{8 * MiB, hem::stateinput::recommendedMaxDepth};
unsigned checks = 0;

void require(bool condition, const char* message) {
    ++checks;
    if (!condition) throw std::runtime_error(message);
}

std::string bytes(const juce::String& text) { return {text.toRawUTF8(), text.getNumBytesAsUTF8()}; }
juce::String string(std::string_view text) { return juce::String::fromUTF8(text.data(), static_cast<int>(text.size())); }
void jsonGood(const std::string& text, Limits limits = stateLimits) {
    const auto original = text;
    const auto result = preflightJson(text, limits);
    require(result.wasOk(), result.getErrorMessage().toRawUTF8());
    require(text == original, "JSON preflight mutated input");
    juce::var parsed;
    require(juce::JSON::parse(string(text), parsed).wasOk(), "JUCE rejected good JSON fixture");
}
void xmlGood(const std::string& text, Limits limits = stateLimits) {
    const auto original = text;
    const auto result = preflightXml(text, limits);
    require(result.wasOk(), result.getErrorMessage().toRawUTF8());
    require(text == original, "XML preflight mutated input");
    require(juce::XmlDocument::parse(string(text)) != nullptr, "JUCE rejected good XML fixture");
}
void jsonBad(const std::string& text, Limits limits = stateLimits) {
    const auto original = text;
    require(preflightJson(text, limits).failed(), "Unsafe JSON passed preflight");
    require(text == original, "Failing JSON preflight mutated input");
}
void xmlBad(const std::string& text, Limits limits = stateLimits) {
    const auto original = text;
    require(preflightXml(text, limits).failed(), "Unsafe XML passed preflight");
    require(text == original, "Failing XML preflight mutated input");
}

void duplicates() {
    jsonBad(R"({"a":1,"a":2})");
    jsonBad(R"({"x":[{"n":0,"n":1}]})");
    jsonBad(R"({"a":1,"\u0061":2})");
    jsonBad(R"({"nested":{"\u006E\u0061me":1,"name":2}})");
    jsonBad(R"({"😀":1,"\uD83D\uDE00":2})");
    jsonBad(R"({"\ud83d\ude00":1,"\uD83D\uDE00":2})");
    jsonBad(R"({"é":1,"\u00e9":2})");
    jsonBad(R"({"𐀀":1,"\uD800\uDC00":2})");
    jsonBad(std::string("{\"\xf4\x8f\xbf\xbf\":1,\"\\uDBFF\\uDFFF\":2}"));
    jsonBad(std::string("{\"a\"\v:1,\"\\u0061\"\f:2}"));
    jsonBad(R"({"a\/b":1,"a/b":2})");
    jsonBad(R"({"a\\b":1,"a\u005cb":2})");
    jsonBad(R"({"a\"b":1,"a\u0022b":2})");
    jsonBad(R"({"a\nb":1,"a\u000Ab":2})");
    jsonBad(R"({"\a":1,"\u0007":2})");
    jsonBad(R"({"value":"a\u0000b"})");
    jsonBad(R"({"value":"\uD800"})");
    jsonBad(R"({"q":1,"\q":2})"); // Pinned JUCE permissive escape.
    jsonBad(R"({"'":1,"\'":2})");
    jsonGood(R"({"a":{"name":1},"b":{"name":2},"name":3})");
    jsonGood(R"([{"a":1},{"a":2}])");
    jsonGood(R"({"😀":1,"\uD83D\uDE01":2})");
    jsonGood(R"({"é":1,"e\u0301":2})"); // No invented Unicode normalization.
    jsonGood(R"({"A":1,"a":2})");
    jsonGood(R"({"quoted":"{ [ } ] \\\" : ","single":'[{"a":1,"a":2}]'})");
    jsonGood(R"({"q": "\q", "alarm":"\a", "quote": "\'"})");

    juce::var parsed;
    require(juce::JSON::parse(R"({"x":1,"\u0078":2})", parsed).wasOk(), "JUCE duplicate baseline changed");
    require(parsed.getDynamicObject()->getProperties().size() == 1, "JUCE no longer overwrites duplicate evidence");

    xmlBad("<R a='1' a=\"2\"/>");
    xmlBad("<R><N a='1' a='2'/></R>");
    xmlBad("<R a='1' <!-- duplicate hidden --> a='2'/>");
    xmlBad("<R a='1' <?hidden?> a='2'/>");
    xmlBad("<R é='1' é='2'/>");
    xmlGood("<R a='1'><N a='2'/><N a='3'/></R>");
    xmlGood("<R A='1' a='2'/>");
    xmlGood("<R a <!--comment--> = <?value?> '1'/>");
    const auto xml = juce::XmlDocument::parse("<R a='1' a='2'/>");
    require(xml != nullptr && xml->getNumAttributes() == 2, "JUCE XML duplicate evidence fixture changed");
    std::cout << "PASS decoded JSON key and per-tag XML attribute duplicates, JUCE overwrite baseline\n";
}

void lexicalAndEntities() {
    xmlGood("<?xml version=\"1.0\" encoding=\"UTF-8\"?><R a='&lt;&gt;&amp;&quot;&apos;&#65;&#x1F600;'>text &AMP; &#x3C; &#62;</R>");
    xmlGood("<R a='> <N x=\"y\"> \\ &#39;'><![CDATA[<N a='1' a='2'><!DOCTYPE evil>&custom;]]><!-- <X><X> </R> --></R>");
    xmlGood("<!-- <!DOCTYPE R> --><R><?pi x='quoted >'?><N/><!-- -->text</R>");
    xmlBad("<R><?pi?>text</R>");
    xmlBad("<R><?pi x='?>' <N/></R>");
    xmlBad("<R><!--comment--><?pi?> <!--comment--> text</R>");
    xmlBad("<R>text<?pi?>more</R>");
    xmlBad("<R><?pi?>&amp;</R>");
    xmlGood("<R><?pi?> <!--comment--><?other?><![CDATA[text]]></R>");
    xmlGood("< R a='v'/>"); // Pinned JUCE's whitespace after '<'.
    xmlGood("<R a='literal <!DOCTYPE is text'/> ");
    xmlBad("<!DOCTYPE R><R/>");
    xmlBad("<!DOCTYPE R SYSTEM 'file:///no-read'><R/>");
    xmlBad("<!DOCTYPE R [<!ENTITY a '&a;'>]><R>&a;</R>");
    xmlBad("<R><!ENTITY a 'value'></R>");
    xmlBad("<?xml version='1.0'?><!--x--><!DOCTYPE R><R/>");
    xmlBad("<R>&custom;</R>");
    xmlBad("<R a='&custom;'/>");
    xmlBad("<R>&<R><R><R/>;</R>"); // Unknown entities can become markup in JUCE.
    xmlBad("<R>&#x;</R>");
    xmlBad("<R>&#;</R>");
    xmlBad("<R>&#0;</R>");
    xmlBad("<R>&#xD800;</R>");
    xmlBad("<R>&#x110000;</R>");
    xmlBad("<R>&#12a;</R>");
    xmlBad("<R>&#111111111111111;</R>");
    xmlBad("<R>&amp</R>");
    for (const auto* input : {"", "{", "[}", "{\"a\":\"", "{\"a\":\"\\", "{\"a\":\"\\u12\"}",
             "{\"\\uD800\":1}", "{\"\\uDC00\":1}", "{\"\\uD800\\u0041\":1}", "{\"\\u0000\":1}", "{\"a\":[]"}) jsonBad(input);
    for (const auto* input : {"", "<", "<R", "<R/", "<R a", "<R a=", "<R a='", "<R>", "<R></", "</R>",
             "<R><!--", "<R><![CDATA[", "<R><?pi", "<R a=x/>", "<R a='x'", "<R>&"}) xmlBad(input);
    for (auto invalid : {std::string("\xc0\xaf", 2), std::string("\xed\xa0\x80", 3), std::string("\xf4\x90\x80\x80", 4),
                        std::string("\xe2\x28\xa1", 3), std::string("\xc2", 1), std::string("\0", 1)}) {
        jsonBad("{\"" + invalid + "\":1}");
        xmlBad("<R a='" + invalid + "'/>");
    }
    // The preflight intentionally does NOT replace JUCE syntax/semantic checks.
    const std::string scalarSyntax = R"({"a":not_a_json_value})";
    require(preflightJson(scalarSyntax, stateLimits).wasOk(), "Preflight overreached into scalar semantics");
    juce::var parsed;
    require(juce::JSON::parse(string(scalarSyntax), parsed).failed(), "JUCE must reject malformed scalar");
    require(preflightJson(R"({"type":"NOT_A_STATE_NODE"})", stateLimits).wasOk(), "Preflight overreached into state schema");
    std::cout << "PASS strings/trivia/CDATA, bounded entity subset, truncation, UTF-8/NUL, JUCE-owned semantics\n";
}

juce::var wrappedTree(unsigned remaining) {
    auto* object = new juce::DynamicObject;
    object->setProperty("type", "PARAMETERS");
    auto* properties = new juce::DynamicObject;
    properties->setProperty("name", "six-level legacy shape");
    object->setProperty("properties", juce::var(properties));
    juce::Array<juce::var> children;
    if (remaining != 0) children.add(wrappedTree(remaining - 1));
    object->setProperty("children", children);
    return juce::var(object);
}

void boundaries() {
    for (const auto limit : {std::size_t{1}, std::size_t{2}, std::size_t{14}, std::size_t{32}}) {
        const auto json = std::string(limit, '[') + "0" + std::string(limit, ']');
        jsonGood(json, {json.size(), limit});
        jsonBad(json, {json.size(), limit - 1});
        jsonBad(json, {json.size() - 1, limit});
        std::string xml;
        for (std::size_t i = 0; i + 1 < limit; ++i) xml += "<R>";
        xml += "<R/>";
        for (std::size_t i = 0; i + 1 < limit; ++i) xml += "</R>";
        xmlGood(xml, {xml.size(), limit});
        xmlBad(xml, {xml.size(), limit - 1});
        xmlBad(xml, {xml.size() - 1, limit});
    }
    jsonBad("{}", {0, 32});
    xmlBad("<R/>", {0, 32});
    const auto wrapped = bytes(juce::JSON::toString(wrappedTree(6)));
    jsonGood(wrapped, {wrapped.size(), 14});
    jsonBad(wrapped, {wrapped.size(), 13});
    jsonGood(wrapped);
    for (const auto cap : {MiB, 8 * MiB}) {
        const auto json = std::string("{\"v\":\"") + std::string(cap - 8, 'x') + "\"}";
        require(json.size() == cap, "JSON exact-cap fixture error");
        jsonGood(json, {cap, 32});
        jsonBad(json + " ", {cap, 32});
        const auto xml = std::string("<R a='") + std::string(cap - 9, 'x') + "'/>";
        require(xml.size() == cap, "XML exact-cap fixture error");
        xmlGood(xml, {cap, 32});
        xmlBad(xml + " ", {cap, 32});
    }
    jsonGood(R"({"ignored":"[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[["})", {1024, 1});
    xmlGood("<R a='<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<'><![CDATA[<<<<<<<<<<<<<<]]><!-- <<<< --></R>", {1024, 1});
    std::cout << "PASS inclusive 1MiB/8MiB byte and 1/2/14/32 raw-depth boundaries, legacy wrapper depth14\n";
}

std::string readFixture(const std::string& filename) {
    std::ifstream input(filename, std::ios::binary);
    require(input.good(), "Required legacy fixture cannot be opened");
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

std::string utf16File(const std::string& utf8, bool bigEndian) {
    std::string result = bigEndian ? std::string("\xfe\xff", 2) : std::string("\xff\xfe", 2);
    const auto text = string(utf8);
    const auto* units = text.toUTF16().getAddress();
    for (std::size_t i = 0; units[i] != 0; ++i) {
        const auto high = static_cast<char>(units[i] >> 8);
        const auto low = static_cast<char>(units[i] & 0xff);
        result += bigEndian ? high : low;
        result += bigEndian ? low : high;
    }
    return result;
}

void decodedGood(const std::string& input, const std::string& expected, std::size_t cap = stateLimits.maxBytes) {
    const auto original = input;
    juce::String output("unchanged until success");
    const auto result = decodeFileText(input, cap, output);
    require(result.wasOk(), result.getErrorMessage().toRawUTF8());
    require(bytes(output) == expected, "Decoded file text changed Unicode or legacy bytes");
    require(input == original, "File decoder mutated input bytes");
}
void decodedBad(const std::string& input, std::size_t cap = stateLimits.maxBytes) {
    const auto original = input;
    const juce::String sentinel("preserve caller output 😀");
    auto output = sentinel;
    require(decodeFileText(input, cap, output).failed(), "Invalid file encoding passed decoding");
    require(output == sentinel, "Failing decoder mutated caller output");
    require(input == original, "Failing decoder mutated input bytes");
}
void fileEncodings() {
    const std::string text = R"({"😀":"é 水", "boundary":"𐀀"})";
    decodedGood(text, text, text.size());
    decodedBad(text, text.size() - 1);
    const auto bomText = std::string("\xef\xbb\xbf", 3) + text;
    decodedGood(bomText, text, bomText.size());
    decodedBad(bomText, bomText.size() - 1);
    for (const auto bigEndian : {false, true}) {
        const auto encoded = utf16File(text, bigEndian);
        decodedGood(encoded, text, encoded.size());
        decodedBad(encoded, encoded.size() - 1);
        decodedBad(encoded.substr(0, encoded.size() - 1));
        const auto encodeUnits = [bigEndian](std::initializer_list<unsigned> units) {
            auto result = utf16File("", bigEndian);
            for (const auto unit : units) {
                result += static_cast<char>(bigEndian ? unit >> 8 : unit & 0xff);
                result += static_cast<char>(bigEndian ? unit & 0xff : unit >> 8);
            }
            return result;
        };
        decodedBad(encodeUnits({0}));
        decodedBad(encodeUnits({'x', 0, 'y'}));
        decodedBad(encodeUnits({0xd800}));
        decodedBad(encodeUnits({0xdc00}));
        decodedBad(encodeUnits({0xd800, 'x'}));
        decodedBad(encodeUnits({0xd800, 0}));
        decodedBad(encodeUnits({0xd800, 0xd800}));
        decodedBad(encodeUnits({0xdc00, 0xd800}));
        decodedGood(encodeUnits({0xd800, 0xdc00}), "𐀀");
        decodedGood(encodeUnits({0xdbff, 0xdfff}), std::string("\xf4\x8f\xbf\xbf", 4));
        const auto expanded = utf16File("水水水水水水", bigEndian);
        require(expanded.size() == 14, "UTF-16 expansion fixture length changed");
        decodedGood(expanded, "水水水水水水", 18);
        decodedBad(expanded, 17); // Raw fits, decoded UTF-8 does not.
        decodedBad(utf16File("", bigEndian));
    }
    for (const auto& invalid : {std::string(), std::string("\xef\xbb\xbf", 3), std::string("x\0y", 3)}) decodedBad(invalid);
    const std::string utf8Bom("\xef\xbb\xbf", 3);
    for (const auto& malformed : {std::string("\xff", 1), std::string("\x80", 1), std::string("\xed\xa0\x80", 3),
                                  std::string("\xc0\xaf", 2), std::string("\xe2\x28\xa1", 3)}) decodedBad(utf8Bom + malformed);
    for (unsigned high = 0x80; high <= 0xff; ++high) {
        const auto legacy = std::string("x") + static_cast<char>(high);
        const auto old = juce::String::createStringFromData(legacy.data(), static_cast<int>(legacy.size()));
        const auto mapped = juce::String("x") + juce::String::charToString(juce::CharacterFunctions::getUnicodeCharFromWindows1252Codepage(static_cast<juce::uint8>(high)));
        require(old == mapped, "Pinned JUCE Windows-1252 mapping changed");
        decodedGood(legacy, bytes(old));
        const auto single = std::string(1, static_cast<char>(high));
        decodedGood(single, bytes(juce::String::createStringFromData(single.data(), 1)));
    }
    const std::string euroLegacy("x\x80", 2);
    decodedGood(euroLegacy, "x€", 4);
    decodedBad(euroLegacy, 3);
    // Duplicate checks occur AFTER decoding, against exactly JUCE's canonical
    // input, so alternate raw spellings cannot hide duplicate decoded keys.
    const auto legacyDuplicate = std::string("{\"\x80\":1,\"\\u20AC\":2}");
    juce::String duplicateText;
    require(decodeFileText(legacyDuplicate, stateLimits.maxBytes, duplicateText).wasOk(), "Legacy duplicate fixture decode failed");
    jsonBad(bytes(duplicateText));
    decodedBad(std::string("\xef\xbb\xbf", 3) + std::string("x\0y", 3));
    decodedBad("x", 0);
    std::cout << "PASS bounded UTF-8/BOM, UTF-16 LE/BE and all Windows-1252 high-byte mappings; transactional output\n";
}

void fixtures(const std::string& directory) {
    const auto json = readFixture(directory + "/full-13x8.json");
    const auto xml = readFixture(directory + "/full-13x8.xml");
    jsonGood(json);
    xmlGood(xml);
    for (const auto& fixture : {json, xml}) {
        decodedGood(fixture, fixture);
        // These exact historical fixtures are ASCII, thus also unchanged
        // Windows-1252; high-byte equivalence is exhaustively checked above.
        require(std::all_of(fixture.begin(), fixture.end(), [](unsigned char c) { return c < 0x80; }), "Legacy fixture is no longer ASCII/Windows-1252 representable");
        decodedGood(fixture, bytes(juce::String::createStringFromData(fixture.data(), static_cast<int>(fixture.size()))));
        decodedGood(std::string("\xef\xbb\xbf", 3) + fixture, fixture);
        for (const auto bigEndian : {false, true}) {
            const auto encoded = utf16File(fixture, bigEndian);
            decodedGood(encoded, fixture);
            juce::String decoded;
            require(decodeFileText(encoded, stateLimits.maxBytes, decoded).wasOk(), "Legacy UTF-16 decode failed");
            if (fixture == json) jsonGood(bytes(decoded));
            else xmlGood(bytes(decoded));
        }
    }
    // Exercise current v2 emitted XML/JSON spellings using actual pinned JUCE
    // serializers. These are representative emitted shapes, not another
    // checked-in historical fixture or a substitute for SessionState tests.
    auto v2xml = juce::XmlDocument::parse(string(xml));
    v2xml->setAttribute("version", 2);
    auto* external = v2xml->createNewChildElement("EXTERNAL");
    external->setAttribute("description", "<PLUGIN name=\"Example & fixture\"/>");
    external->setAttribute("state", "AQIDBA==");
    xmlGood(bytes(v2xml->toString()));
    juce::var v2json;
    require(juce::JSON::parse(string(json), v2json).wasOk(), "Legacy JSON no longer parses");
    v2json.getDynamicObject()->getProperty("properties").getDynamicObject()->setProperty("version", 2);
    jsonGood(bytes(juce::JSON::toString(v2json)));
    require(json == readFixture(directory + "/full-13x8.json"), "Legacy JSON fixture bytes changed");
    require(xml == readFixture(directory + "/full-13x8.xml"), "Legacy XML fixture bytes changed");
    std::cout << "PASS actual BandV1 full-13x8 XML/JSON immutable bytes; JUCE-emitted representative v2 shapes\n";
}

void cardinalities() {
    std::string object = "{";
    std::string element = "<R";
    for (unsigned i = 0; i < 32; ++i) {
        if (i != 0) object += ',';
        object += "\"key" + std::to_string(i) + "\":0";
        element += " key" + std::to_string(i) + "='0'";
    }
    jsonGood(object + "}");
    xmlGood(element + "/>");
    jsonBad(object + ",\"key32\":0}");
    xmlBad(element + " key32='0'/>");
    require(preflightJson(object + ",\"key32\":0}", stateLimits).getErrorMessage().contains("member limit"), "JSON member cap not enforced structurally");
    require(preflightXml(element + " key32='0'/>", stateLimits).getErrorMessage().contains("attribute limit"), "XML attribute cap not enforced structurally");
    std::string array = "[";
    std::string children = "<R>";
    for (unsigned i = 0; i < 128; ++i) {
        if (i != 0) array += ',';
        array += "{\"sameKey\":\"quoted ,[{}], : text\"}";
        children += "<N sameKey='quoted > <N/>'/>";
    }
    jsonGood(array + "]");
    xmlGood(children + "</R>");
    jsonBad(array + ",{}]");
    xmlBad(children + "<N/></R>");
    require(preflightJson(array + ",{}]", stateLimits).getErrorMessage().contains("entry limit"), "JSON array cap not enforced structurally");
    require(preflightXml(children + "<N/></R>", stateLimits).getErrorMessage().contains("child limit"), "XML child cap not enforced structurally");

    constexpr Limits two{4096, 32, 2, 2};
    jsonGood(R"({"a":{"same":1,"other":2},"b":{"same":3,"other":4}})", two);
    jsonBad(R"({"a":{"same":1,"other":2,"third":3}})", two);
    jsonGood(R"([[0,1],[2,3]])", two);
    jsonBad(R"([[0,1,2],[2,3]])", two);
    jsonGood(R"(["commas,inside,strings",'and,quoted,values'])", two);
    jsonGood("[\vtrue\f,\tfalse\n]", two);
    jsonBad("[\vtrue\f,\tfalse\n,null]", two);
    xmlGood("<R><N a='1' b='2'><C/><C/></N><N a='3' b='4'/></R>", two);
    xmlBad("<R><N a='1' b='2' c='3'/></R>", two);
    xmlBad("<R><N><C/><C/><C/></N></R>", two);
    xmlGood("<R><!--<N/><N/>--><![CDATA[<N/><N/>]]><N/></R>", two);
    xmlBad("<R><![CDATA[one]]><![CDATA[two]]><![CDATA[three]]></R>", two);
    xmlBad("<R><![CDATA[one]]><N/><N/></R>", two);
    xmlGood("<R>before<!--inside text-->&amp;after<N/></R>", two);
    xmlBad("<R>before<N/>after</R>", two);
    xmlGood("<R> \n <N/> \t <N/> \r\n </R>", two);
    jsonGood("{}", {4096, 32, 0, 0});
    jsonGood("[]", {4096, 32, 0, 0});
    xmlGood("<R/>", {4096, 32, 0, 0});
    jsonBad(R"({"a":0})", {4096, 32, 0, 0});
    jsonBad("[0]", {4096, 32, 0, 0});
    xmlBad("<R a='0'/>", {4096, 32, 0, 0});
    xmlBad("<R><N/></R>", {4096, 32, 0, 0});
    std::cout << "PASS inclusive 32-member/attribute and 128-entry/child caps, explicit zero/two caps and per-container counting\n";
}

void boundedWork() {
    // Huge raw nesting is scanned iteratively and rejected before JUCE runs.
    jsonBad(std::string(2 * MiB, '['), {8 * MiB, 32});
    std::string xml;
    for (unsigned i = 0; i < 200000; ++i) xml += "<R>";
    xmlBad(xml);
    std::string manyJson = "{";
    std::string manyXml = "<R";
    for (unsigned i = 0; i < 10000; ++i) {
        if (i != 0) manyJson += ',';
        manyJson += "\"key" + std::to_string(i) + "\":0";
        manyXml += " key" + std::to_string(i) + "='0'";
    }
    // Oversized unique inputs stop at preflight. Never pass them to JUCE
    // NamedValueSet/XmlElement construction, where unique insertion is costly.
    jsonBad(manyJson + "}");
    xmlBad(manyXml + "/>");
    jsonBad(manyJson + ",\"key0\":1}");
    xmlBad(manyXml + " key0='1'/>");
    std::mt19937 rng(0x010b0u);
    const std::string alphabet = "<>!?[]{}=:'\"\\u&;/ abcdef0123456789\n\t";
    for (unsigned sample = 0; sample < 6000; ++sample) {
        std::string input;
        const auto count = rng() % 257;
        for (unsigned i = 0; i < count; ++i) input += alphabet[rng() % alphabet.size()];
        const auto original = input;
        const auto j1 = preflightJson(input, {256, 8});
        const auto x1 = preflightXml(input, {256, 8});
        const auto j2 = preflightJson(input, {256, 8});
        const auto x2 = preflightXml(input, {256, 8});
        require(j1 == j2 && x1 == x2, "Preflight is nondeterministic");
        require(input == original, "Fuzz input was mutated");
    }
    std::cout << "PASS bounded nesting/name volume and 6000 deterministic malformed/truncated fuzz cases\n";
}
} // namespace

int main(int argc, char** argv) {
    try {
        const auto start = std::chrono::steady_clock::now();
        duplicates();
        lexicalAndEntities();
        boundaries();
        fileEncodings();
        fixtures(argc > 1 ? argv[1] : std::string(__FILE__).substr(0, std::string(__FILE__).find_last_of("/\\")) + "/Fixtures/BandV1");
        cardinalities();
        boundedWork();
        const auto seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        std::cout << "PASS " << checks << " checks in " << seconds << " seconds; JUCE " << JUCE_MAJOR_VERSION << '.' << JUCE_MINOR_VERSION << '.' << JUCE_BUILDNUMBER << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL after " << checks << " checks: " << error.what() << '\n';
        return 1;
    }
}
