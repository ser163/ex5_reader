#pragma once
// Minimal UTF-8 <-> codepoint helpers.
// The ex5 spec defines range_start/range_end as *character* offsets,
// so we need codepoint-level indexing over UTF-8 chapter text.

#include <string>
#include <vector>
#include <cstdint>

namespace utf8 {

// Decode UTF-8 string into codepoints. Invalid bytes are kept as U+FFFD.
inline std::vector<uint32_t> decode(const std::string& s) {
    std::vector<uint32_t> out;
    out.reserve(s.size());
    size_t i = 0;
    while (i < s.size()) {
        unsigned char c = (unsigned char)s[i];
        uint32_t cp; size_t len;
        if (c < 0x80)            { cp = c; len = 1; }
        else if ((c >> 5) == 6)  { cp = c & 0x1F; len = 2; }
        else if ((c >> 4) == 14) { cp = c & 0x0F; len = 3; }
        else if ((c >> 3) == 30) { cp = c & 0x07; len = 4; }
        else { out.push_back(0xFFFD); ++i; continue; }
        if (i + len > s.size()) { out.push_back(0xFFFD); ++i; continue; }
        bool ok = true;
        for (size_t k = 1; k < len; ++k) {
            unsigned char cc = (unsigned char)s[i + k];
            if ((cc >> 6) != 2) { ok = false; break; }
            cp = (cp << 6) | (cc & 0x3F);
        }
        if (!ok) { out.push_back(0xFFFD); ++i; continue; }
        out.push_back(cp);
        i += len;
    }
    return out;
}

// Encode one codepoint to UTF-8.
inline void encodeOne(std::string& out, uint32_t cp) {
    if (cp < 0x80) out += (char)cp;
    else if (cp < 0x800) {
        out += (char)(0xC0 | (cp >> 6));
        out += (char)(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        out += (char)(0xE0 | (cp >> 12));
        out += (char)(0x80 | ((cp >> 6) & 0x3F));
        out += (char)(0x80 | (cp & 0x3F));
    } else {
        out += (char)(0xF0 | (cp >> 18));
        out += (char)(0x80 | ((cp >> 12) & 0x3F));
        out += (char)(0x80 | ((cp >> 6) & 0x3F));
        out += (char)(0x80 | (cp & 0x3F));
    }
}

inline std::string encode(const std::vector<uint32_t>& cps, size_t begin, size_t end) {
    std::string out;
    if (end > cps.size()) end = cps.size();
    if (begin > end) begin = end;
    for (size_t i = begin; i < end; ++i) encodeOne(out, cps[i]);
    return out;
}

inline size_t charCount(const std::string& s) { return decode(s).size(); }

// Substring by character offsets [start, end).
inline std::string charSubstr(const std::string& s, size_t start, size_t end) {
    auto cps = decode(s);
    return encode(cps, start, end);
}

} // namespace utf8
