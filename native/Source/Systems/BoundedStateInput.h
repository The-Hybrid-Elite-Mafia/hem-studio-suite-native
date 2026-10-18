#pragma once

#include <juce_core/juce_core.h>
#include <cstddef>
#include <string_view>

namespace hem::stateinput {

struct Limits {
    std::size_t maxBytes;
    std::size_t maxDepth;
    // Existing state cardinalities, enforced before JUCE's NamedValueSet
    // insertion/recursive construction as well as by later schema checks.
    std::size_t maxMembers = 32;   // JSON object members / XML attributes.
    std::size_t maxChildren = 128; // JSON array entries / XML child nodes.
};

// Raw JSON objects/arrays add levels independently of ValueTree nodes. With
// fromJson's maximum node depth of 6 (root = 0), the emitted properties/children
// representation needs at most 14 raw levels. 32 deliberately leaves headroom
// for the resource envelope while still bounding JUCE's recursive parsers.
inline constexpr std::size_t recommendedMaxDepth = 32;

// Pure preflights over the exact immutable UTF-8 text subsequently given to
// JUCE. Limits are inclusive; the root object/array/element has depth 1 and an
// empty element also consumes one level. Neither function parses a state tree,
// admits node types, changes input/output state, performs I/O, or calls a JUCE
// JSON/XML parser. Callers MUST still use JUCE and existing schema validation.
//
// Rejects byte/depth/cardinality limits, noncanonical UTF-8/NUL,
// unbalanced lexical containers,
// and duplicate JSON names (after JUCE-compatible escape decoding) or XML
// attributes (within each individual object/start tag). Ordinary JSON scalar
// grammar, XML tag matching, state schemas and value semantics remain JUCE's
// and the caller's responsibility; success alone does not establish validity.
//
// XML DTD/declaration markup and custom entity references are unsupported:
// they can expand into markup or recurse outside the raw byte/depth bounds.
// Comments, CDATA, processing instructions, XML declarations, predefined
// entities (JUCE-compatible case-insensitive spelling), and well-formed scalar
// numeric references are supported. Content PIs followed by character data
// are rejected because pinned JUCE 8.0.10 loops without progress on that form.
// Emitted v1/v2 states require neither that form nor DTDs/custom entities.
// Text must already be decoded to UTF-8 if the input was UTF-16;
// callers retain their physical-file/binary byte caps before decoding as well.
//
// Work is bounded by the supplied byte cap: linear token scanning plus ordered
// name-set comparisons, O(bytes * log(maxMembers + 2)) worst case. Names and
// container counts are bounded before JUCE construction; limits are structural,
// not node/schema rules. XML child counts include elements, CDATA, and
// non-whitespace text runs (entity-only runs count conservatively); comments,
// processing instructions and ordinary formatting whitespace do not count.
juce::Result preflightJson(std::string_view utf8, Limits limits);
juce::Result preflightXml(std::string_view utf8, Limits limits);

// File compatibility only: decode UTF-8 or BOM UTF-16 LE/BE after validating
// every code unit/scalar. Unmarked invalid UTF-8 uses pinned JUCE Windows-1252
// mapping (including its single-byte special case); explicit BOMs never fall
// back to a codepage. maxBytes caps BOTH raw input and decoded UTF-8,
// inclusively. No I/O or JSON/XML parsing; output is assigned
// only on success, then MUST pass the corresponding preflight and JUCE parser.
// Rejects NUL, malformed BOM-declared UTF-8, odd/truncated UTF-16 and unpaired
// surrogates. All routes produce canonical UTF-8 for preflight. Binary is UTF-8
// only and should use preflightXml directly, not this file decoder.
juce::Result decodeFileText(std::string_view bytes, std::size_t maxBytes, juce::String& output);


} // namespace hem::stateinput
