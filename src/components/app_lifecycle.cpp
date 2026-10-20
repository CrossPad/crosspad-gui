// SPDX-License-Identifier: GPL-3.0-or-later
#ifdef USE_LVGL

#include "crosspad-gui/components/app_lifecycle.h"

namespace crosspad_gui {

void app_default_show(lv_obj_t* app) {
    lv_obj_remove_flag(app, LV_OBJ_FLAG_HIDDEN);
}

void app_default_hide(lv_obj_t* app) {
    lv_obj_add_flag(app, LV_OBJ_FLAG_HIDDEN);
}

void app_default_destroy(lv_obj_t* app) {
    if (app == NULL) return;
    if (lv_obj_is_valid(app)) lv_obj_delete(app);
}

void app_default_destroy_async(lv_obj_t* app) {
    lv_async_call([](void* p) {
        lv_obj_t* obj = static_cast<lv_obj_t*>(p);
        if (obj == NULL) return;
        lv_obj_delete(obj);
    }, app);
}

} // namespace crosspad_gui

#endif // USE_LVGL
