// SPDX-License-Identifier: MIT
#include "crosspad-gui/platform/PosixFileSystem.hpp"

#ifdef _WIN32
#include <filesystem>
#else
#include <sys/stat.h>
#include <dirent.h>
#endif

namespace crosspad_gui {

#ifdef _WIN32
// Windows has no dirent.h (the PC simulator built with MSVC).
bool PosixFileSystem::listDirectory(const std::string& path,
                                     std::vector<FileItem>& outEntries) {
    std::error_code ec;
    std::filesystem::directory_iterator it(std::filesystem::u8path(path), ec);
    if (ec) return false;
    for (const auto& ent : it) {
        const std::string name = ent.path().filename().u8string();
        if (name.empty() || name[0] == '.') continue;

        FileItem item;
        item.name = name;
        item.path = path + "/" + name;
        item.isFolder = ent.is_directory(ec);
        outEntries.push_back(std::move(item));
    }
    return !outEntries.empty();
}
#else
bool PosixFileSystem::listDirectory(const std::string& path,
                                     std::vector<FileItem>& outEntries) {
    DIR* dir = opendir(path.c_str());
    if (!dir) return false;

    struct dirent* ent;
    while ((ent = readdir(dir)) != nullptr) {
        if (ent->d_name[0] == '.') continue;

        FileItem item;
        item.name = ent->d_name;
        item.path = path + "/" + item.name;

        // Determine if directory
        if (ent->d_type == DT_DIR) {
            item.isFolder = true;
        } else if (ent->d_type == DT_UNKNOWN) {
            // Fallback: some filesystems don't set d_type
            struct stat st;
            item.isFolder = (stat(item.path.c_str(), &st) == 0 && S_ISDIR(st.st_mode));
        } else {
            item.isFolder = false;
        }

        outEntries.push_back(std::move(item));
    }

    closedir(dir);
    return !outEntries.empty();
}
#endif

} // namespace crosspad_gui
