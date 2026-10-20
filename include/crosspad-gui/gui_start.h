// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#ifdef USE_LVGL

#include "crosspad-gui/components/app_orchestrator.h"
#include "crosspad-gui/components/main_screen.h"

namespace crosspad_gui {

class IGuiPlatform;

/**
 * @brief Configuration for gui_start() — one-call GUI initialization.
 */
struct GuiStartConfig {
    // Platform adapter (required)
    IGuiPlatform* platform = nullptr;

    // App orchestrator hooks
    OrchestratorConfig orchestrator;

    // Main screen layout
    MainScreenConfig main_screen;

    /// Optional LVGL mutex for thread-safe access.
    /// If provided, gui_start() wraps all LVGL API calls (initStyles,
    /// loadMainScreen, etc.) in lock/unlock. Required by esp_lvgl_port
    /// on ESP-IDF; ESP32-S3 (single task) and PC leave it null.
    struct LvglMutex {
        bool (*lock)(uint32_t timeout_ms) = nullptr;
        void (*unlock)() = nullptr;
    } lvgl_mutex;
};

/**
 * @brief Result of gui_start() — pointers to created UI elements.
 */
struct GuiStartResult {
    lv_obj_t*   status_bar     = nullptr;
    lv_obj_t*   app_container  = nullptr;
    lv_group_t* encoder_group  = nullptr;
    lv_obj_t*   launcher_grid  = nullptr;
    lv_obj_t*   launcher_menu  = nullptr;
    bool        success        = false;
};

/**
 * @brief Initialize the full GUI stack in one call.
 *
 * Performs: bootstrap → initStyles → register power off →
 * orchestrator init → populateApps → loadMainScreen.
 *
 * Does NOT start a loop, call lv_timer_handler, or create tasks.
 * The main loop remains platform-specific.
 *
 * @param screen  Root LVGL screen to build on (typically lv_scr_act())
 * @param config  Platform adapters, orchestrator hooks, layout config
 * @return        Pointers to created UI elements + success flag
 */
GuiStartResult gui_start(lv_obj_t* screen, const GuiStartConfig& config);

} // namespace crosspad_gui

#endif // USE_LVGL
