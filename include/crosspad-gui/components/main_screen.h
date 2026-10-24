// SPDX-License-Identifier: GPL-3.0-or-later
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
    /// The home screen: the icon grid, the list or the pad map (Settings ->
    /// Display -> Home screen). The pad map decodes no icons.
    crosspad::LauncherStyle style = crosspad::LauncherStyle::Pads;
    launcher_app_select_cb_t on_select = nullptr;
    launcher_close_popup_cb_t on_close = nullptr;
    int button_size  = 64;
    int button_spacing = 10;
    bool show_names  = true;
    lv_color_t bg_color = lv_color_make(26, 26, 46);
    /// The transport bar on top in place of the status bar, with MODE. The
    /// status bar is still built -- its drawer, loader and timers keep
    /// working -- as the bar's second page.
    bool transport_bar = false;
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
    lv_obj_t*  home_map      = nullptr;  ///< The pad map, when style is Pads
};

/**
 * @brief Build the standard main screen layout.
 *
 * Sets up the parent with a column flex layout, creates the status bar,
 * creates the app container, and builds the home screen into it -- the
 * button grid, the list menu or the pad map, as config.style says.
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
