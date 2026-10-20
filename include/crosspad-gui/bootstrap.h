// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#ifdef USE_LVGL

namespace crosspad_gui {

class IGuiPlatform;

/**
 * @brief Initialize crosspad-gui framework essentials.
 *
 * Must be called once at startup, before using any crosspad-gui components.
 * Registers the platform adapter so all crosspad-gui components can call
 * getGuiPlatform(). Files are crosspad::getFileSystem(), which the platform
 * sets in crosspad-core (PlatformServices), not here.
 *
 * IMPORTANT: This does NOT call initStyles(), InitializeAppRegistry(),
 * AddApps(), or LoadMainScreen(). Those steps are platform-specific:
 *  - initStyles() may need to be wrapped in an LVGL mutex (ESP-IDF)
 *  - InitializeAppRegistry/AddApps/LoadMainScreen depend on the platform App class
 *
 * Typical usage (ESP-IDF):
 * @code
 *   static IdfGuiPlatform guiPlatform;
 *   crosspad_gui::bootstrap(&guiPlatform);
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
 */
void bootstrap(IGuiPlatform* platform);

/**
 * @brief Shut down crosspad-gui framework resources.
 *
 * Call during shutdown, before lv_deinit(). Wraps deinitStyles() and
 * any future cleanup steps.
 */
void shutdown();

} // namespace crosspad_gui

#endif // USE_LVGL
