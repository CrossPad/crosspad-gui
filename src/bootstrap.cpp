#ifdef USE_LVGL

#include "crosspad-gui/bootstrap.h"
#include "crosspad-gui/styles/styles.h"
#include "crosspad-gui/platform/IGuiPlatform.h"

namespace crosspad_gui {

void bootstrap(IGuiPlatform* platform) {
    // The platform adapter must be set first — other components query it during init
    setGuiPlatform(platform);
    // Note: initStyles() is NOT called here. It may need to be wrapped
    // in an LVGL mutex by the platform (e.g. lvgl_port_lock on ESP-IDF).
}

void shutdown() {
    deinitStyles();
}

} // namespace crosspad_gui

#endif // USE_LVGL
