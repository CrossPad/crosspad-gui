// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#ifdef USE_LVGL
#include "lvgl.h"

namespace crosspad_gui {

// Default LVGL app lifecycle handlers.
// Shared between ESP-IDF and Arduino platforms.

/// Show an app by removing the HIDDEN flag
void app_default_show(lv_obj_t* app);

/// Hide an app by adding the HIDDEN flag
void app_default_hide(lv_obj_t* app);

/// Destroy an app synchronously (safe if called from LVGL context)
void app_default_destroy(lv_obj_t* app);

/// Destroy an app asynchronously via lv_async_call (safe during event processing)
void app_default_destroy_async(lv_obj_t* app);

} // namespace crosspad_gui

// Legacy names — thin wrappers so existing code keeps compiling.
// New code should use crosspad_gui::app_default_* directly.
inline void lv_Default_onShow(lv_obj_t* app)          { crosspad_gui::app_default_show(app); }
inline void lv_Default_onHide(lv_obj_t* app)          { crosspad_gui::app_default_hide(app); }
inline void lv_Default_onDestroy(lv_obj_t* app)       { crosspad_gui::app_default_destroy(app); }
inline void lv_Default_onDestroy_async(lv_obj_t* app) { crosspad_gui::app_default_destroy_async(app); }

#endif // USE_LVGL
