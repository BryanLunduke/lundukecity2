#pragma once

#include <filesystem>
#include <fstream>
#include <string>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#include <stdio.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

// Flush the temp file to disk before it replaces the destination. A crash
// after the rename then keeps the new bytes, not a half-written page.
inline void durable_flush(const std::string &path)
{
#ifdef _WIN32
    const int fd = _open(path.c_str(), _O_RDONLY | _O_BINARY);
    if (fd >= 0) {
        _commit(fd);
        _close(fd);
    }
#else
    const int fd = ::open(path.c_str(), O_RDONLY);
    if (fd >= 0) {
        ::fsync(fd);
        ::close(fd);
    }
#endif
}

// Write the whole file under a temporary name, then rename it into place so a
// crash cannot leave a half-written recent list or welcome flag.
//
// POSIX rename replaces an existing file. When a platform refuses that, the
// previous file is moved aside and put back if the new name cannot be
// installed. The destination is not deleted first.
inline bool write_text_atomic(const std::string &path, const std::string &body)
{
    if (path.empty()) {
        return false;
    }
    const std::string temp = path + ".tmp";
    {
        std::ofstream out(temp, std::ios::trunc | std::ios::binary);
        if (!out) {
            return false;
        }
        out << body;
        out.flush();
        if (!out) {
            std::error_code ec;
            std::filesystem::remove(temp, ec);
            return false;
        }
    }
    durable_flush(temp);
    std::error_code ec;
    std::filesystem::rename(temp, path, ec);
    if (!ec) {
        return true;
    }
    const std::string aside = path + ".bak";
    std::filesystem::remove(aside, ec);
    std::filesystem::rename(path, aside, ec);
    if (ec) {
        // Nothing was replaced. Drop the temp and keep the previous file.
        std::filesystem::remove(temp, ec);
        return false;
    }
    std::filesystem::rename(temp, path, ec);
    if (ec) {
        std::error_code restore;
        std::filesystem::rename(aside, path, restore);
        std::filesystem::remove(temp, restore);
        return false;
    }
    std::filesystem::remove(aside, ec);
    return true;
}
