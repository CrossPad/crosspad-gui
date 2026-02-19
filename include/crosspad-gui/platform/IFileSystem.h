#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace crosspad_gui {

struct FileItem {
    std::string path;
    std::string name;
    bool isFolder;
};

class IFileSystem {
public:
    virtual ~IFileSystem() = default;

    // List directory contents (one level deep)
    // Returns true if at least one entry was found
    virtual bool listDirectory(const std::string & path, std::vector<FileItem> & outEntries) = 0;
};

// Set the filesystem implementation (call once at startup)
void setFileSystem(IFileSystem * fs);

// Get the filesystem implementation
IFileSystem & getFileSystem();

} // namespace crosspad_gui
