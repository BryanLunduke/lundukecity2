#pragma once
// In-window file list: hide dotfiles, keep ".." first, folders before files,
// then a case-insensitive natural order (city2 before city10).

#include "platform.hpp"

#include <cctype>
#include <fstream>
#include <string>
#include <vector>

inline bool file_row_is_dir(const std::string &name)
{
    return name == ".." || (name.size() > 1 && name.back() == '/');
}

inline bool file_name_hidden(const std::string &name)
{
    return !name.empty() && name[0] == '.';
}

inline std::string file_row_label(const std::string &name)
{
    if (name.size() > 1 && name.back() == '/') {
        return name.substr(0, name.size() - 1);
    }
    return name;
}

inline bool natural_less(const std::string &left, const std::string &right)
{
    std::size_t i = 0;
    std::size_t j = 0;
    while (i < left.size() && j < right.size()) {
        const unsigned char ca = static_cast<unsigned char>(left[i]);
        const unsigned char cb = static_cast<unsigned char>(right[j]);
        if (std::isdigit(ca) && std::isdigit(cb)) {
            const std::size_t i_start = i;
            const std::size_t j_start = j;
            while (i < left.size() && left[i] == '0') {
                ++i;
            }
            while (j < right.size() && right[j] == '0') {
                ++j;
            }
            const std::size_t i_digits = i;
            const std::size_t j_digits = j;
            while (i < left.size() && std::isdigit(static_cast<unsigned char>(left[i]))) {
                ++i;
            }
            while (j < right.size() && std::isdigit(static_cast<unsigned char>(right[j]))) {
                ++j;
            }
            const std::size_t i_len = i - i_digits;
            const std::size_t j_len = j - j_digits;
            if (i_len != j_len) {
                return i_len < j_len;
            }
            const int cmp = left.compare(i_digits, i_len, right, j_digits, j_len);
            if (cmp != 0) {
                return cmp < 0;
            }
            const std::size_t i_span = i - i_start;
            const std::size_t j_span = j - j_start;
            if (i_span != j_span) {
                return i_span < j_span;
            }
            continue;
        }
        const unsigned char la = static_cast<unsigned char>(std::tolower(ca));
        const unsigned char lb = static_cast<unsigned char>(std::tolower(cb));
        if (la != lb) {
            return la < lb;
        }
        ++i;
        ++j;
    }
    return left.size() < right.size();
}

inline bool file_row_less(const std::string &left, const std::string &right)
{
    if (left == "..") {
        return right != "..";
    }
    if (right == "..") {
        return false;
    }
    const bool left_dir = file_row_is_dir(left);
    const bool right_dir = file_row_is_dir(right);
    if (left_dir != right_dir) {
        return left_dir;
    }
    return natural_less(file_row_label(left), file_row_label(right));
}

inline std::string documents_dir()
{
    const std::string home = user_home_dir();
    if (home.empty()) {
        return {};
    }
#ifdef _WIN32
    return home + "\\Documents";
#else
    std::ifstream in(home + "/.config/user-dirs.dirs");
    std::string line;
    while (std::getline(in, line)) {
        const std::string key = "XDG_DOCUMENTS_DIR=";
        if (line.compare(0, key.size(), key) != 0) {
            continue;
        }
        std::string value = line.substr(key.size());
        if (!value.empty() && value.back() == '\r') {
            value.pop_back();
        }
        if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
            value = value.substr(1, value.size() - 2);
        }
        if (value.compare(0, 5, "$HOME") == 0) {
            value = home + value.substr(5);
        }
        if (!value.empty()) {
            return value;
        }
    }
    return home + "/Documents";
#endif
}

inline std::string default_cities_dir()
{
    const std::string docs = documents_dir();
    if (docs.empty()) {
        return {};
    }
#ifdef _WIN32
    return docs + "\\Lunduke City 2";
#else
    return docs + "/Lunduke City 2";
#endif
}
