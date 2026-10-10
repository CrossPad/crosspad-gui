// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "crosspad-gui/platform/IFileSystem.h"

namespace crosspad_gui {

/// Portable IFileSystem implementation using POSIX opendir/readdir/stat.
/// No mutex — caller is responsible for synchronization if needed.
class PosixFileSystem : public IFileSystem {
public:
    bool listDirectory(const std::string& path,
                       std::vector<FileItem>& outEntries) override;
};

} // namespace crosspad_gui
