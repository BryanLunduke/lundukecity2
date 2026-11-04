#pragma once
// UTF-8 text fields: the caret is a byte index, and a selection is the range
// between the caret and the anchor.

#include <algorithm>
#include <string>

inline bool utf8_continuation(unsigned char byte)
{
    return (byte & 0xC0) == 0x80;
}

// The engine's city-name filter stops at 48 bytes and can cut a character
// in half. Names are limited here, on a character boundary, before that.
inline constexpr int kCityNameMaxBytes = 48;

// Longest prefix of complete UTF-8 characters that fits in max_bytes.
inline std::string utf8_prefix(const std::string &text, int max_bytes)
{
    if (max_bytes < 1 || text.empty()) {
        return {};
    }
    const int n = static_cast<int>(text.size());
    const int limit = max_bytes < n ? max_bytes : n;
    int end = 0;
    while (end < limit) {
        const unsigned char lead = static_cast<unsigned char>(text[static_cast<std::size_t>(end)]);
        int need = 1;
        if ((lead & 0x80) == 0) {
            need = 1;
        } else if ((lead & 0xE0) == 0xC0) {
            need = 2;
        } else if ((lead & 0xF0) == 0xE0) {
            need = 3;
        } else if ((lead & 0xF8) == 0xF0) {
            need = 4;
        } else {
            break;
        }
        if (end + need > limit) {
            break;
        }
        bool ok = true;
        for (int i = 1; i < need; ++i) {
            if (!utf8_continuation(static_cast<unsigned char>(text[static_cast<std::size_t>(end + i)]))) {
                ok = false;
                break;
            }
        }
        if (!ok) {
            break;
        }
        end += need;
    }
    return text.substr(0, static_cast<std::size_t>(end));
}

inline int utf8_prev(const std::string &text, int index)
{
    if (index <= 0) {
        return 0;
    }
    int i = index - 1;
    while (i > 0 && utf8_continuation(static_cast<unsigned char>(text[static_cast<std::size_t>(i)]))) {
        --i;
    }
    return i;
}

inline int utf8_next(const std::string &text, int index)
{
    if (index >= static_cast<int>(text.size())) {
        return static_cast<int>(text.size());
    }
    int i = index + 1;
    while (i < static_cast<int>(text.size()) && utf8_continuation(static_cast<unsigned char>(text[static_cast<std::size_t>(i)]))) {
        ++i;
    }
    return i;
}

inline void field_clamp(const std::string &text, int &caret, int &anchor)
{
    const int n = static_cast<int>(text.size());
    if (caret < 0) {
        caret = 0;
    }
    if (caret > n) {
        caret = n;
    }
    if (anchor < 0) {
        anchor = 0;
    }
    if (anchor > n) {
        anchor = n;
    }
}

inline void field_select_all(const std::string &text, int &caret, int &anchor)
{
    anchor = 0;
    caret = static_cast<int>(text.size());
}

inline void field_delete_selection(std::string &text, int &caret, int &anchor)
{
    field_clamp(text, caret, anchor);
    if (caret == anchor) {
        return;
    }
    const int from = std::min(caret, anchor);
    const int to = std::max(caret, anchor);
    text.erase(static_cast<std::size_t>(from), static_cast<std::size_t>(to - from));
    caret = from;
    anchor = from;
}

inline bool field_backspace_would_delete(const std::string &text, int caret, int anchor)
{
    field_clamp(text, caret, anchor);
    if (caret != anchor) {
        return true;
    }
    return caret > 0;
}

inline void field_backspace(std::string &text, int &caret, int &anchor)
{
    field_clamp(text, caret, anchor);
    if (caret != anchor) {
        field_delete_selection(text, caret, anchor);
        return;
    }
    const int prev = utf8_prev(text, caret);
    text.erase(static_cast<std::size_t>(prev), static_cast<std::size_t>(caret - prev));
    caret = prev;
    anchor = prev;
}

inline bool field_insert(std::string &text, int &caret, int &anchor, const std::string &extra, int max_bytes)
{
    field_clamp(text, caret, anchor);
    if (extra.empty()) {
        return false;
    }
    const int from = std::min(caret, anchor);
    const int to = std::max(caret, anchor);
    const int next_size = static_cast<int>(text.size()) - (to - from) + static_cast<int>(extra.size());
    if (next_size > max_bytes) {
        return false;
    }
    field_delete_selection(text, caret, anchor);
    text.insert(static_cast<std::size_t>(caret), extra);
    caret += static_cast<int>(extra.size());
    anchor = caret;
    return true;
}

inline void field_move(const std::string &text, int &caret, int &anchor, int direction, bool extend)
{
    field_clamp(text, caret, anchor);
    if (!extend && caret != anchor) {
        const int edge = direction < 0 ? std::min(caret, anchor) : std::max(caret, anchor);
        caret = edge;
        anchor = edge;
        return;
    }
    caret = direction < 0 ? utf8_prev(text, caret) : utf8_next(text, caret);
    if (!extend) {
        anchor = caret;
    }
}

inline void field_home(const std::string &text, int &caret, int &anchor, bool extend)
{
    field_clamp(text, caret, anchor);
    caret = 0;
    if (!extend) {
        anchor = 0;
    }
}

inline void field_end(const std::string &text, int &caret, int &anchor, bool extend)
{
    field_clamp(text, caret, anchor);
    caret = static_cast<int>(text.size());
    if (!extend) {
        anchor = caret;
    }
}

inline bool text_has_path_separator(const std::string &text)
{
    return text.find('/') != std::string::npos || text.find('\\') != std::string::npos;
}

// Tab and newline become one space so pasted words stay apart. Other C0
// controls and DEL are dropped. The result stops on a UTF-8 boundary.
inline bool paste_break(unsigned char lead)
{
    return lead == '\t' || lead == '\n' || lead == '\r' || lead == '\v' || lead == '\f';
}

inline std::string sanitize_pasted_text(const std::string &text, int max_bytes)
{
    std::string out;
    if (max_bytes < 1) {
        return out;
    }
    for (std::size_t i = 0; i < text.size();) {
        const unsigned char lead = static_cast<unsigned char>(text[i]);
        if (paste_break(lead)) {
            if (out.empty() || out.back() != ' ') {
                if (static_cast<int>(out.size()) + 1 > max_bytes) {
                    break;
                }
                out.push_back(' ');
            }
            ++i;
            continue;
        }
        if (lead < 0x20 || lead == 0x7f) {
            ++i;
            continue;
        }
        int length = 1;
        if ((lead & 0x80) == 0) {
            length = 1;
        } else if ((lead & 0xE0) == 0xC0) {
            length = 2;
        } else if ((lead & 0xF0) == 0xE0) {
            length = 3;
        } else if ((lead & 0xF8) == 0xF0) {
            length = 4;
        } else {
            ++i;
            continue;
        }
        if (i + static_cast<std::size_t>(length) > text.size()) {
            break;
        }
        bool ok = true;
        for (int n = 1; n < length; ++n) {
            if (!utf8_continuation(static_cast<unsigned char>(text[i + static_cast<std::size_t>(n)]))) {
                ok = false;
                break;
            }
        }
        if (!ok) {
            ++i;
            continue;
        }
        if (static_cast<int>(out.size()) + length > max_bytes) {
            break;
        }
        out.append(text, i, static_cast<std::size_t>(length));
        i += static_cast<std::size_t>(length);
    }
    return out;
}
