#ifdef USE_LVGL

#include "crosspad-gui/bootstrap.h"
#include "crosspad-gui/styles/styles.h"
#include "crosspad-gui/platform/IGuiPlatform.h"
#include "crosspad-gui/platform/IFileSystem.h"

namespace crosspad_gui {

void bootstrap(IGuiPlatform* platform, IFileSystem* fs) {
    // Platform adapters must be set first — other components query these during init
    setGuiPlatform(platform);
    setFileSystem(fs);
    // Note: initStyles() is NOT called here. It may need to be wrapped
    // in an LVGL mutex by the platform (e.g. lvgl_port_lock on ESP-IDF).
}

void shutdown() {
    deinitStyles();
}

} // namespace crosspad_gui

#endif // USE_LVGL
