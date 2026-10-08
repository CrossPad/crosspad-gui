#pragma once

#ifdef USE_LVGL

#include "lvgl.h"
#include "crosspad-gui/components/app_launcher.h"
#include "crosspad/settings/CrosspadSettings.hpp"
#include <cstddef>

namespace crosspad_gui {

/**
 * @brief Configuration for building the main launcher screen.
 */
struct MainScreenConfig {
    lv_indev_t* encoder = nullptr;       ///< Rotary encoder input device
    crosspad::LauncherStyle style = crosspad::LauncherStyle::Grid;
    launcher_app_select_cb_t on_select = nullptr;
    launcher_close_popup_cb_t on_close = nullptr;
    int button_size  = 64;
    int button_spacing = 10;
    bool show_names  = true;
    lv_color_t bg_color = lv_color_make(26, 26, 46);
    /// UX 2000: the transport bar on top in place of the status bar, and
    /// MODE + pad to switch apps. The status bar is still built -- its drawer,
    /// loader and timers keep working -- but hidden.
    bool transport_bar = false;
    /// Build the launcher into the app container.
    ///
    /// UX 2000 has no launcher: the board opens on its home app and apps are
    /// switched with MODE and a pad. A platform that names a home app
    /// (mode_overlay_set_home) turns this off, and then pays nothing for a
    /// grid of icons that home covers the moment it starts -- every one of
    /// them a decoded image in the PSRAM the app itself has to live in.
    bool launcher = true;
};

/**
 * @brief Pointers returned by build_main_screen().
 */
struct MainScreenResult {
    lv_obj_t*  status_bar    = nullptr;  ///< Status bar container (child of parent)
    lv_obj_t*  transport_bar = nullptr;  ///< Transport bar, when config.transport_bar
    lv_obj_t*  app_container = nullptr;  ///< Container where apps render their UI
    lv_group_t* encoder_group = nullptr; ///< Encoder navigation group
    lv_obj_t*  launcher_grid = nullptr;  ///< The button grid, when style is Grid
    lv_obj_t*  launcher_menu = nullptr;  ///< The list menu, when style is List
};

/**
 * @brief Build the standard main screen layout.
 *
 * Sets up the parent with a column flex layout, creates the status bar,
 * creates the app container, and populates the launcher -- the button grid
 * or the list menu, as config.style says.
 *
 * Cleans the parent before building.
 *
 * @param parent     Root LVGL screen/object to build on
 * @param apps       Array of LauncherAppInfo descriptors
 * @param app_count  Number of elements in @p apps
 * @param config     Layout and callback configuration
 * @return           Pointers to the created UI elements
 */
MainScreenResult build_main_screen(
    lv_obj_t* parent,
    const LauncherAppInfo* apps,
    size_t app_count,
    const MainScreenConfig& config);

} // namespace crosspad_gui

#endif // USE_LVGL
