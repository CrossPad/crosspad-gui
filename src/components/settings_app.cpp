// SPDX-License-Identifier: MIT
//
// Generic settings app shell — creates the container, encoder group, and
// delegates to settings_ui_create().  Platform-specific categories are
// registered separately via settings_ui_add_category().

#ifdef USE_LVGL

#include <crosspad-gui/components/settings_app.h>
#include "crosspad-gui/widgets/gui_helpers.h"
#include <crosspad-gui/components/settings_ui.h>
#include <crosspad-gui/platform/IGuiPlatform.h>
#include <crosspad-gui/app/App.hpp>

#include <lvgl.h>

namespace crosspad_gui {

static lv_group_t* s_settings_group = nullptr;
static lv_group_t* s_saved_group    = nullptr;

lv_obj_t* settings_app_create(lv_obj_t* parent, App* /*app*/) {
    s_settings_group = lv_group_create();
    lv_indev_t* nav = getGuiPlatform().getNavigationEncoder();
    if (nav) {
        s_saved_group = lv_indev_get_group(nav);
        lv_indev_set_group(nav, s_settings_group);
    }

    lv_obj_t* container = lv_obj_create(parent);
    lv_obj_set_size(container, lv_obj_get_content_width(parent), lv_obj_get_content_height(parent));
    lv_obj_set_style_bg_color(container, lv_color_black(), 0);
    lv_obj_set_style_border_width(container, 0, 0);
    lv_obj_set_style_pad_all(container, 0, 0);
    lv_obj_set_style_radius(container, 0, 0);
    lv_obj_center(container);

    lv_obj_t* content = lv_obj_create(container);
    lv_obj_set_size(content, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(content, lv_color_black(), 0);
    lv_obj_set_style_border_width(content, 0, 0);
    lv_obj_set_style_pad_all(content, 6, 0);
    lv_obj_set_style_pad_row(content, 4, 0);
    lv_obj_set_style_radius(content, 0, 0);
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
    lv_obj_add_flag(content, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(content, LV_SCROLLBAR_MODE_AUTO);

    settings_ui_create(content, s_settings_group);

    return container;
}

void settings_app_destroy(lv_obj_t*) {
    settings_ui_destroy();

    lv_indev_t* nav = getGuiPlatform().getNavigationEncoder();
    if (nav && s_saved_group) {
        lv_indev_restore_group(nav, s_saved_group);
        s_saved_group = nullptr;
    }
    if (s_settings_group) {
        lv_group_del(s_settings_group);
        s_settings_group = nullptr;
    }
}

} // namespace crosspad_gui

#endif // USE_LVGL
