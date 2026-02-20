#pragma once

#ifdef USE_LVGL

namespace crosspad_gui {

class IGuiPlatform;
class IFileSystem;

/**
 * @brief Initialize crosspad-gui framework essentials.
 *
 * Must be called once at startup, before using any crosspad-gui components.
 * Registers the platform adapter and filesystem implementation so all
 * crosspad-gui components can call getGuiPlatform() / getFileSystem().
 *
 * IMPORTANT: This does NOT call initStyles(), InitializeAppRegistry(),
 * AddApps(), or LoadMainScreen(). Those steps are platform-specific:
 *  - initStyles() may need to be wrapped in an LVGL mutex (ESP-IDF)
 *  - InitializeAppRegistry/AddApps/LoadMainScreen depend on the platform App class
 *
 * Typical usage (ESP-IDF):
 * @code
 *   static IdfGuiPlatform guiPlatform;
 *   static IdfFileSystem fileSystem;
 *   crosspad_gui::bootstrap(&guiPlatform, &fileSystem);
 *   // ... then:
 *   lvgl_port_lock(3000);
 *   initStyles();
 *   lvgl_port_unlock();
 *   InitializeAppRegistry();
 *   AddApps();
 *   LoadMainScreen(lv_scr_act());
 * @endcode
 *
 * @param platform  GUI platform adapter. Must outlive all GUI usage.
 * @param fs        Filesystem adapter. Must outlive all GUI usage.
 */
void bootstrap(IGuiPlatform* platform, IFileSystem* fs);

/**
 * @brief Shut down crosspad-gui framework resources.
 *
 * Call during shutdown, before lv_deinit(). Wraps deinitStyles() and
 * any future cleanup steps.
 */
void shutdown();

} // namespace crosspad_gui

#endif // USE_LVGL
