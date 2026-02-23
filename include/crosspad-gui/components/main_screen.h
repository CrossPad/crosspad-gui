#pragma once

#ifdef USE_LVGL

#include "lvgl.h"
#include "crosspad-gui/components/app_launcher.h"
#include <cstddef>

namespace crosspad_gui {

/**
 * @brief Configuration for building the main launcher screen.
 */
struct MainScreenConfig {
    lv_indev_t* encoder = nullptr;       ///< Rotary encoder input device
    launcher_app_select_cb_t on_select = nullptr;
    launcher_close_popup_cb_t on_close = nullptr;
    int button_size  = 64;
    int button_spacing = 10;
    bool show_names  = true;
    lv_color_t bg_color = lv_color_make(26, 26, 46);
};

/**
 * @brief Pointers returned by build_main_screen().
 */
struct MainScreenResult {
    lv_obj_t*  status_bar    = nullptr;  ///< Status bar container (child of parent)
    lv_obj_t*  app_container = nullptr;  ///< Container where apps render their UI
    lv_group_t* encoder_group = nullptr; ///< Encoder navigation group
    lv_obj_t*  launcher_grid = nullptr;  ///< The button grid widget
};

/**
 * @brief Build the standard main screen layout.
 *
 * Sets up the parent with a column flex layout, creates the status bar,
 * creates the app container, and populates a launcher button grid.
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
