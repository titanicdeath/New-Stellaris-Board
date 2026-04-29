// json_writer.cpp
#include "json_writer.h"

#include <cmath>
#include <cstdio>

namespace jw {

void Writer::indent() {
    if (!pretty_) return;
    out_.put('\n');
    for (int i = 0; i < depth_; ++i) out_.put(' '), out_.put(' ');
}

void Writer::before_value() {
    if (pending_value_after_key_) {
        // We emitted "key": already; just write the value with optional space.
        if (pretty_) out_.put(' ');
        pending_value_after_key_ = false;
        return;
    }
    if (!need_comma_.empty()) {
        if (need_comma_.back()) out_.put(',');
        need_comma_.back() = true;
    }
    indent();
}

void Writer::escape_to_stream(std::string_view s) {
    out_.put('"');
    for (unsigned char c : s) {
        switch (c) {
            case '"':  out_ << "\\\""; break;
            case '\\': out_ << "\\\\"; break;
            case '\b': out_ << "\\b"; break;
            case '\f': out_ << "\\f"; break;
            case '\n': out_ << "\\n"; break;
            case '\r': out_ << "\\r"; break;
            case '\t': out_ << "\\t"; break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out_ << buf;
                } else {
                    out_.put(static_cast<char>(c));
                }
        }
    }
    out_.put('"');
}

void Writer::begin_object() {
    before_value();
    out_.put('{');
    ++depth_;
    need_comma_.push_back(false);
    is_array_.push_back(false);
}

void Writer::end_object() {
    --depth_;
    if (need_comma_.back()) indent(); // only newline-indent if non-empty
    need_comma_.pop_back();
    is_array_.pop_back();
    out_.put('}');
}

void Writer::begin_array() {
    before_value();
    out_.put('[');
    ++depth_;
    need_comma_.push_back(false);
    is_array_.push_back(true);
}

void Writer::end_array() {
    --depth_;
    if (need_comma_.back()) indent();
    need_comma_.pop_back();
    is_array_.pop_back();
    out_.put(']');
}

void Writer::key(std::string_view k) {
    if (!need_comma_.empty()) {
        if (need_comma_.back()) out_.put(',');
        need_comma_.back() = true;
    }
    indent();
    escape_to_stream(k);
    out_.put(':');
    pending_value_after_key_ = true;
}

void Writer::string(std::string_view s) {
    before_value();
    escape_to_stream(s);
}

void Writer::integer(long long v) {
    before_value();
    out_ << v;
}

void Writer::number(double v) {
    before_value();
    if (std::isnan(v) || std::isinf(v)) {
        // JSON has no NaN/Inf. Emit null and let consumers decide.
        out_ << "null";
    } else {
        // Use general format with enough precision to round-trip a double.
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%.17g", v);
        out_ << buf;
    }
}

void Writer::boolean(bool v) {
    before_value();
    out_ << (v ? "true" : "false");
}

void Writer::null() {
    before_value();
    out_ << "null";
}

} // namespace jw
