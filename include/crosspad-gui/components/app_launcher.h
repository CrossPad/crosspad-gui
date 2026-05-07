#pragma once

#ifdef USE_LVGL
#include "lvgl.h"
#include <cstddef>

namespace crosspad_gui {

// Lightweight app descriptor for the launcher UI (decoupled from platform App class)
struct LauncherAppInfo {
    const char* name;
    const char* icon;
    void* app_ptr;  // Opaque pointer to platform's App object
};

// Called when the user clicks an app button in the launcher grid.
// app_ptr: the opaque pointer from LauncherAppInfo
// app_container: the LVGL container where the app should create its UI
typedef void (*launcher_app_select_cb_t)(void* app_ptr, lv_obj_t* app_container);

// Called when a popup app (e.g. Power OFF) is dismissed.
typedef void (*launcher_close_popup_cb_t)(void* app_ptr);

// Configuration for the launcher button grid
struct LauncherConfig {
    lv_group_t* encoder_group = nullptr;           // Encoder navigation group
    launcher_app_select_cb_t on_select = nullptr;  // App button click callback
    launcher_close_popup_cb_t on_close = nullptr;  // Popup dismiss callback
    int button_size = 64;                          // Button size in pixels
    int icon_size = 32;                            // Icon render size (sources scaled to fit, aspect preserved)
    int button_spacing = 10;                       // Spacing between buttons
    bool show_names = true;                        // Show app name labels under icons
};

// Register the Power OFF app in AppRegistry.
// Uses getGuiPlatform().assetPathPrefix() to build the icon path.
void launcher_register_power_off();

// Create the app button grid inside a parent container.
// Builds a flex-wrap row of icon buttons from the given app list.
// Returns the button wrapper object.
lv_obj_t* launcher_create_button_grid(
    lv_obj_t* parent,
    const LauncherAppInfo* apps,
    size_t app_count,
    const LauncherConfig* config
);

// Create the Power OFF confirmation dialog (message box).
// Returns the message box object.
lv_obj_t* launcher_create_power_off_dialog(
    lv_obj_t* parent,
    void* app_ptr,
    launcher_close_popup_cb_t on_close
);

} // namespace crosspad_gui

#endif // USE_LVGL
