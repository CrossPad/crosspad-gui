#include "crosspad-gui/platform/IGuiPlatform.h"
#include "crosspad-gui/platform/IFileSystem.h"
#include <cassert>

namespace crosspad_gui {

static IGuiPlatform * s_guiPlatform = nullptr;
static IFileSystem * s_fileSystem = nullptr;

void setGuiPlatform(IGuiPlatform * platform) {
    s_guiPlatform = platform;
}

IGuiPlatform & getGuiPlatform() {
    assert(s_guiPlatform != nullptr && "GUI platform not initialized! Call setGuiPlatform() first.");
    return *s_guiPlatform;
}

void setFileSystem(IFileSystem * fs) {
    s_fileSystem = fs;
}

IFileSystem & getFileSystem() {
    assert(s_fileSystem != nullptr && "File system not initialized! Call setFileSystem() first.");
    return *s_fileSystem;
}

} // namespace crosspad_gui
