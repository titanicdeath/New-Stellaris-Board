// json_writer.h
// Tiny streaming JSON writer. We avoid a JSON library dependency because:
//   - We only need to WRITE, never read.
//   - Output volume is small (13 planets + one country block).
//   - It keeps the build a single g++ invocation.

#pragma once

#include <cstdint>
#include <ostream>
#include <string>
#include <string_view>
#include <vector>

namespace jw {

class Writer {
public:
    explicit Writer(std::ostream& out, bool pretty = true)
        : out_(out), pretty_(pretty) {}

    void begin_object();
    void end_object();
    void begin_array();
    void end_array();

    // Key for the next value in an object context.
    void key(std::string_view k);

    // Values.
    void string(std::string_view s);
    void integer(long long v);
    void number(double v);
    void boolean(bool v);
    void null();

    // Convenience: key + value in one call.
    void kv(std::string_view k, std::string_view v) { key(k); string(v); }
    void kv(std::string_view k, long long v)        { key(k); integer(v); }
    void kv(std::string_view k, int v)              { key(k); integer(v); }
    void kv(std::string_view k, double v)           { key(k); number(v); }
    void kv(std::string_view k, bool v)             { key(k); boolean(v); }

private:
    void before_value();
    void indent();
    void escape_to_stream(std::string_view s);

    std::ostream& out_;
    bool pretty_;
    int depth_ = 0;
    // For each open container, true if we still need to emit a separator
    // before the next element (i.e. it's not the first).
    std::vector<bool> need_comma_;
    // For each open container, true if it's an array (so key() is illegal).
    std::vector<bool> is_array_;
    // True if we just emitted a key and are waiting for its value.
    bool pending_value_after_key_ = false;
};

} // namespace jw
