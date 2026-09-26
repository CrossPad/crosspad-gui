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

// Called when the user opens an app from the launcher menu.
// app_ptr: the opaque pointer from LauncherAppInfo
// app_container: the LVGL container where the app should create its UI
typedef void (*launcher_app_select_cb_t)(void* app_ptr, lv_obj_t* app_container);

// Called when a popup app (e.g. Power OFF) is dismissed.
typedef void (*launcher_close_popup_cb_t)(void* app_ptr);

// Configuration for the launcher menu
struct LauncherConfig {
    lv_group_t* encoder_group = nullptr;           // Encoder navigation group
    launcher_app_select_cb_t on_select = nullptr;  // App open callback
    launcher_close_popup_cb_t on_close = nullptr;  // Popup dismiss callback
};

// Register the Power OFF app in AppRegistry.
// Uses getGuiPlatform().assetPathPrefix() to build the icon path.
void launcher_register_power_off();

// Create the launcher menu inside a parent container: one row per app, the
// selected one expanded with its icon, name and description. Encoder turns
// move the selection, a press opens it; on touch the first tap selects and a
// tap on the selected row opens. @p parent is the container apps start in.
// Returns the menu's root object.
lv_obj_t* launcher_create_menu(
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
