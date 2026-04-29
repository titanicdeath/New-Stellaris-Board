// pdx_node.cpp
// Implementation of the Paradox-format parser.
//
// Grammar (informal):
//   file       := entry*
//   entry      := key '=' value | value     // bare value = anonymous entry
//   key        := IDENT | INTEGER | QUOTED
//   value      := SCALAR | block
//   block      := '{' entry* '}'
//   SCALAR     := IDENT | NUMBER | QUOTED | yes | no
//
// The lexer is hand-rolled, single-pass over a string_view, producing tokens
// of fixed enum types. The parser is recursive-descent. Both work directly on
// the byte buffer with no intermediate string copies for the common path —
// scalars are copied only into the final Node.

#include "pdx_node.h"

#include <cctype>
#include <charconv>
#include <stdexcept>
#include <string>

namespace pdx {

// ---------------------------------------------------------------- Node helpers

const Node* Node::find(std::string_view key) const noexcept {
    for (const auto& e : entries) {
        if (e.key == key) return e.value.get();
    }
    return nullptr;
}

std::vector<const Node*> Node::find_all(std::string_view key) const {
    std::vector<const Node*> out;
    for (const auto& e : entries) {
        if (e.key == key) out.push_back(e.value.get());
    }
    return out;
}

std::string Node::as_string(std::string_view fallback) const {
    if (kind != NodeKind::Scalar) return std::string(fallback);
    return scalar;
}

long long Node::as_int(long long fallback) const {
    if (kind != NodeKind::Scalar || scalar.empty()) return fallback;
    long long v = 0;
    auto [ptr, ec] = std::from_chars(scalar.data(),
                                     scalar.data() + scalar.size(), v);
    if (ec != std::errc{}) {
        // Fall back to double parse for things stored as "1.0" but used as int
        try { return static_cast<long long>(std::stod(scalar)); }
        catch (...) { return fallback; }
    }
    return v;
}

double Node::as_double(double fallback) const {
    if (kind != NodeKind::Scalar || scalar.empty()) return fallback;
    try { return std::stod(scalar); }
    catch (...) { return fallback; }
}

bool Node::as_yesno(bool fallback) const {
    if (kind != NodeKind::Scalar) return fallback;
    if (scalar == "yes" || scalar == "1") return true;
    if (scalar == "no"  || scalar == "0") return false;
    return fallback;
}

std::string Node::child_string(std::string_view key, std::string_view fallback) const {
    if (auto* c = find(key)) return c->as_string(fallback);
    return std::string(fallback);
}
long long Node::child_int(std::string_view key, long long fallback) const {
    if (auto* c = find(key)) return c->as_int(fallback);
    return fallback;
}
double Node::child_double(std::string_view key, double fallback) const {
    if (auto* c = find(key)) return c->as_double(fallback);
    return fallback;
}
bool Node::child_yesno(std::string_view key, bool fallback) const {
    if (auto* c = find(key)) return c->as_yesno(fallback);
    return fallback;
}

// ---------------------------------------------------------------- Lexer

namespace {

enum class Tok : uint8_t {
    End,
    LBrace,    // {
    RBrace,    // }
    Equals,    // =
    Scalar,    // unquoted token (identifier, number, yes/no)
    Quoted,    // quoted string ("...")
};

struct Lexer {
    const char* p;       // current position
    const char* end;     // end of buffer
    const char* origin;  // start of buffer (for line/col reporting on error)

    // Current token state
    Tok kind = Tok::End;
    std::string_view text;  // for Scalar / Quoted; points into source

    explicit Lexer(std::string_view src) noexcept
        : p(src.data()), end(src.data() + src.size()), origin(src.data()) {
        advance();
    }

    [[noreturn]] void error(const char* msg) const {
        // Compute line/col by linear scan from origin. This only happens on
        // error, so the cost is fine. Slow-path is allowed to be slow.
        size_t line = 1, col = 1;
        for (const char* q = origin; q < p && q < end; ++q) {
            if (*q == '\n') { ++line; col = 1; } else { ++col; }
        }
        throw std::runtime_error(std::string(msg) +
            " at line " + std::to_string(line) +
            " col " + std::to_string(col));
    }

    static bool is_ws(char c) noexcept {
        return c == ' ' || c == '\t' || c == '\r' || c == '\n';
    }
    // Tokens that terminate a bare scalar. We accept anything except
    // whitespace, braces, and equals — Paradox identifiers can contain
    // dots, hyphens, underscores, digits, letters.
    static bool is_term(char c) noexcept {
        return is_ws(c) || c == '{' || c == '}' || c == '=';
    }

    void skip_ws() noexcept {
        while (p < end && is_ws(*p)) ++p;
    }

    void advance() {
        skip_ws();
        if (p >= end) { kind = Tok::End; return; }

        char c = *p;
        if (c == '{') { kind = Tok::LBrace;  ++p; return; }
        if (c == '}') { kind = Tok::RBrace;  ++p; return; }
        if (c == '=') { kind = Tok::Equals;  ++p; return; }

        if (c == '"') {
            const char* start = ++p; // skip opening quote
            while (p < end && *p != '"') {
                // Paradox saves don't appear to use backslash escapes in the
                // observed data, but we'll be defensive: skip escaped char.
                if (*p == '\\' && p + 1 < end) p += 2;
                else ++p;
            }
            if (p >= end) error("unterminated string");
            text = std::string_view(start, static_cast<size_t>(p - start));
            ++p; // skip closing quote
            kind = Tok::Quoted;
            return;
        }

        // Bare scalar: read until terminator.
        const char* start = p;
        while (p < end && !is_term(*p)) ++p;
        if (p == start) error("unexpected character");
        text = std::string_view(start, static_cast<size_t>(p - start));
        kind = Tok::Scalar;
    }
};

// ---------------------------------------------------------------- Parser

// Forward declaration — block parsing is recursive.
NodePtr parse_value(Lexer& lx);

// Parse a block body until matching '}'. Lexer must be positioned just after
// the '{' (i.e. on the first token of the body).
//
// Each iteration handles ONE entry. The format has three entry shapes:
//   1.  KEY = VALUE              (typical key/value pair)
//   2.  KEY = SCALAR ... SCALAR  (no — scalars never chain; this would parse
//                                  as separate entries with the same KEY)
//   3.  SCALAR                   (anonymous entry; key = "")
//   4.  { ... }                  (anonymous block entry; key = "")
//
// Distinguishing shape 1 from shape 3 requires lookahead of one token: we
// read a scalar/quoted, then peek to see if '=' follows.
NodePtr parse_block_body(Lexer& lx) {
    auto node = std::make_unique<Node>();
    node->kind = NodeKind::Block;

    while (lx.kind != Tok::RBrace && lx.kind != Tok::End) {
        if (lx.kind == Tok::LBrace) {
            // Anonymous nested block
            lx.advance(); // consume '{'
            auto child = parse_block_body(lx);
            if (lx.kind != Tok::RBrace) lx.error("expected '}'");
            lx.advance();
            node->entries.push_back({std::string(), std::move(child)});
            continue;
        }

        if (lx.kind != Tok::Scalar && lx.kind != Tok::Quoted) {
            lx.error("expected key or value");
        }

        // Buffer the token text (lexer advances will invalidate text view)
        std::string token_text(lx.text);
        bool token_quoted = (lx.kind == Tok::Quoted);
        lx.advance();

        if (lx.kind == Tok::Equals) {
            // KEY = VALUE form
            lx.advance(); // consume '='
            auto value = parse_value(lx);
            node->entries.push_back({std::move(token_text), std::move(value)});
        } else {
            // Anonymous scalar entry (member of a primitive list, civic name,
            // etc.). The token we already consumed IS the value.
            auto leaf = std::make_unique<Node>();
            leaf->kind = NodeKind::Scalar;
            leaf->scalar = std::move(token_text);
            leaf->quoted = token_quoted;
            node->entries.push_back({std::string(), std::move(leaf)});
        }
    }
    return node;
}

NodePtr parse_value(Lexer& lx) {
    if (lx.kind == Tok::LBrace) {
        lx.advance();
        auto block = parse_block_body(lx);
        if (lx.kind != Tok::RBrace) lx.error("expected '}'");
        lx.advance();
        return block;
    }
    if (lx.kind == Tok::Scalar || lx.kind == Tok::Quoted) {
        auto leaf = std::make_unique<Node>();
        leaf->kind = NodeKind::Scalar;
        leaf->scalar.assign(lx.text);
        leaf->quoted = (lx.kind == Tok::Quoted);
        lx.advance();
        return leaf;
    }
    lx.error("expected value");
}

} // namespace

// ---------------------------------------------------------------- entry point

NodePtr parse(std::string_view source) {
    Lexer lx(source);
    // The whole file is a sequence of top-level entries — same grammar as a
    // block body, just without enclosing braces. We reuse parse_block_body
    // and accept End instead of RBrace as the terminator.
    auto root = parse_block_body(lx);
    if (lx.kind != Tok::End) {
        lx.error("trailing input after root");
    }
    return root;
}

} // namespace pdx
