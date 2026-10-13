// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#ifdef USE_LVGL

#include <lvgl.h>

class App;

namespace crosspad_gui {

/// Create the generic settings app shell (container + encoder group + settings_ui).
lv_obj_t* settings_app_create(lv_obj_t* parent, App* app);

/// Destroy the settings app shell.
void settings_app_destroy(lv_obj_t* obj);

} // namespace crosspad_gui

#endif // USE_LVGL
