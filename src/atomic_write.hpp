#pragma once

#include <filesystem>
#include <fstream>
#include <string>

// Write the whole file under a temporary name, then rename it into place so a
// crash cannot leave a half-written recent list or welcome flag.
inline bool write_text_atomic(const std::string &path, const std::string &body)
{
    if (path.empty()) {
        return false;
    }
    const std::string temp = path + ".tmp";
    {
        std::ofstream out(temp, std::ios::trunc);
        if (!out) {
            return false;
        }
        out << body;
        if (!out) {
            std::error_code ec;
            std::filesystem::remove(temp, ec);
            return false;
        }
    }
    std::error_code ec;
    std::filesystem::rename(temp, path, ec);
    if (ec) {
        std::filesystem::remove(path, ec);
        std::filesystem::rename(temp, path, ec);
    }
    if (ec) {
        std::filesystem::remove(temp, ec);
        return false;
    }
    return true;
}
