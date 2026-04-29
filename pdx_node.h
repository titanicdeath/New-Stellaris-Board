// pdx_node.h
// Tree representation for Paradox key=value-with-braces format.
//
// Design notes for this format (Stellaris gamestate, ~53 MiB, ~4M lines):
// - Duplicate keys at the same scope are common AND semantically meaningful.
//   e.g. tech_status has alternating `technology="X"` `level=1` pairs, and
//   relations_manager emits multiple `modifier=` blocks. We therefore store
//   children as an ORDERED VECTOR of (key, value) pairs, not a map.
// - Anonymous list entries also exist: `player={ { name=... country=0 } }`,
//   and `owned_fleets={ { fleet=0 } { fleet=12 } ... }`. We represent these
//   with an empty string as the key.
// - Bare primitive lists (`{ 2 252 362 ... }`) appear inline. We model them
//   as anonymous-keyed scalar children — a primitive list is just a block
//   whose children all have empty keys and scalar values.
// - Strings may or may not be quoted; dates are quoted YYYY.MM.DD. We keep
//   the raw scalar text and let consumers decide how to interpret it; this
//   avoids losing the distinction between e.g. quoted "0" and bare 0 when
//   it matters.

#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace pdx {

struct Node;
using NodePtr = std::unique_ptr<Node>;

enum class NodeKind : uint8_t {
    Scalar, // leaf: a single value token (number, string, yes/no, date, ...)
    Block,  // { ... }: ordered list of (key, child) entries
};

struct Node {
    NodeKind kind = NodeKind::Scalar;

    // Scalar payload. We keep the raw text exactly as it appeared in the
    // source (quotes stripped from quoted strings, but no other coercion).
    // `quoted` records whether the source had surrounding double quotes,
    // because in Paradox saves "0" (a quoted token) and 0 (an integer)
    // are not always interchangeable.
    std::string scalar;
    bool quoted = false;

    // Block payload. Each entry has a key (possibly empty for anonymous
    // entries / list members) and a child node.
    struct Entry {
        std::string key;
        NodePtr value;
    };
    std::vector<Entry> entries;

    // ---- helpers used by the extractor (not the parser core) ----

    // First child whose key matches. nullptr if absent. Linear scan — fine
    // for blocks with a small key set, which is what we use this for.
    const Node* find(std::string_view key) const noexcept;

    // All children whose key matches, in source order.
    std::vector<const Node*> find_all(std::string_view key) const;

    // Scalar accessors with sensible defaults. The parser never throws on
    // missing fields; callers get defaults and decide how to react.
    std::string as_string(std::string_view fallback = {}) const;
    long long   as_int(long long fallback = 0) const;
    double      as_double(double fallback = 0.0) const;
    bool        as_yesno(bool fallback = false) const;

    // Convenience: get a scalar child's text, or fallback.
    std::string child_string(std::string_view key, std::string_view fallback = {}) const;
    long long   child_int(std::string_view key, long long fallback = 0) const;
    double      child_double(std::string_view key, double fallback = 0.0) const;
    bool        child_yesno(std::string_view key, bool fallback = false) const;
};

// Parse an entire Paradox-format file from a UTF-8 byte buffer.
// Throws std::runtime_error with line/column info on syntax errors.
NodePtr parse(std::string_view source);

} // namespace pdx
