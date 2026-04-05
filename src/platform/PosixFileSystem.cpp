// SPDX-License-Identifier: MIT
#include "crosspad-gui/platform/PosixFileSystem.hpp"

#include <sys/stat.h>
#include <dirent.h>

namespace crosspad_gui {

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

} // namespace crosspad_gui
