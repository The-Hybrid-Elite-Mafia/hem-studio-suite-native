#pragma once
#include <juce_data_structures/juce_data_structures.h>
#include <vector>

namespace hem::nativeResource {
// Descriptor limits are persistence limits, not permission to allocate/decode
// this many bytes or frames. Actual providers impose their own engine limits.
inline constexpr int manifestVersion = 1;
inline constexpr size_t maxSerializedBytes = 128 * 1024;
inline constexpr size_t maxResources = 4;
inline constexpr size_t maxNodes = 5;
inline constexpr size_t maxOwnerBytes = 64;
inline constexpr size_t maxPathBytes = 4096;
inline constexpr juce::int64 maxExactInteger = 9007199254740991LL;

struct RoleIndex {
    juce::String role;
    int index = 0;
};

// Supplied by an actual resource provider, never inferred from untrusted state.
// Closed roles: sample (index 0..3), cabinet-ir/reference (index 0).
struct ExpectedLayout {
    juce::String ownerType;
    std::vector<RoleIndex> resources;
};

struct ResourceDescriptor {
    juce::String role;
    int index = 0;
    juce::String path, sha256;
    juce::int64 byteSize = 0;
    double sourceRate = 0;
    int channels = 0;
    juce::int64 frames = 0;
};

// Value-owned state: no ValueTree, var object, resource handle, or borrowed data.
// String copies use JUCE's value-semantic copy-on-write ownership.
struct State {
    juce::String ownerType;
    std::vector<ResourceDescriptor> resources;
};

// Non-audio-thread descriptor operations only. No filesystem access, hashing,
// audio preparation, threads, DSP, publication, or registration occurs here.
// Exactly one RESOURCE per expected (role,index) is required, in any order.
// A canonical empty descriptor retains role/index and zeros all other fields.
// Success replaces out; any validation failure leaves out unchanged. Encoded
// children use expected-layout order. Decode accepts numeric vars and strict
// decimal XML strings; JSON ingress must enforce numeric types on these nodes.
// Integer strings are checked exactly before conversion (no floating rounding).
// ValueTree cannot expose duplicate textual properties already lost in parsing;
// raw XML/JSON preflight is required at that separate trust boundary.
juce::Result validateManifest(const State&, const ExpectedLayout&);
juce::Result decodeManifest(const juce::ValueTree&, const ExpectedLayout&, State& out);
juce::Result encodeManifest(const State&, const ExpectedLayout&, juce::ValueTree& out);

// Exact XML serialization for the new manifest only. JUCE's ordinary XML
// double formatter can round fractional rates. This preserves every other
// attribute's standard formatting, and writes RESOURCE.sourceRate with
// locale-independent max_digits10 precision. Does not alter ValueTree/JSON
// numeric types or any legacy record. Failure leaves the unique_ptr unchanged.
juce::Result encodeManifestXml(const State&, const ExpectedLayout&,
                               std::unique_ptr<juce::XmlElement>& out);
} // namespace hem::nativeResource
