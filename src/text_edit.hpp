#pragma once
// UTF-8 text fields: the caret is a byte index, and a selection is the range
// between the caret and the anchor.

#include <algorithm>
#include <string>

inline bool utf8_continuation(unsigned char byte)
{
    return (byte & 0xC0) == 0x80;
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
