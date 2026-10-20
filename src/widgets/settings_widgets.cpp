// SPDX-License-Identifier: GPL-3.0-or-later
#ifdef USE_LVGL

#include "crosspad-gui/widgets/settings_widgets.h"

/* lv_menu rows. Nothing in the firmware calls these any more, and the board
 * builds LVGL without lv_menu (1.8 kB of image); a build with it keeps them. */
#if LV_USE_MENU

// Standard lv_menu item builder (used by both Arduino and IDF projects)
lv_obj_t * create_text(lv_obj_t * parent, const char * icon, const char * txt, int builder_variant)
{
    lv_obj_t * obj = lv_menu_cont_create(parent);

    lv_obj_t * img = NULL;
    lv_obj_t * label = NULL;

    if (icon) {
        img = lv_image_create(obj);
        lv_image_set_src(img, icon);
    }

    if (txt) {
        label = lv_label_create(obj);
        lv_label_set_text(label, txt);
        lv_label_set_long_mode(label, LV_LABEL_LONG_SCROLL_CIRCULAR);
        lv_obj_set_flex_grow(label, 1);
    }

    if (builder_variant == LV_MENU_ITEM_BUILDER_VARIANT_2 && img && label) {
        lv_obj_add_flag(img, LV_OBJ_FLAG_FLEX_IN_NEW_TRACK);
        lv_obj_swap(img, label);
    }

    return obj;
}

static void menu_switch_event_cb(lv_event_t * e) {
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t * obj = (lv_obj_t *)lv_event_get_target(e);
    bool * setting = (bool *)lv_event_get_user_data(e);

    if (code == LV_EVENT_VALUE_CHANGED) {
        bool state = lv_obj_has_state(obj, LV_STATE_CHECKED);
        *setting = state;
    }
}

lv_obj_t * create_settings_switch(lv_obj_t * parent, const char * icon, const char * txt,
                                  bool * setting, lv_event_cb_t cb, lv_event_code_t cb_event_type)
{
    lv_obj_t * obj = create_text(parent, icon, txt, LV_MENU_ITEM_BUILDER_VARIANT_1);

    lv_obj_t * sw = lv_switch_create(obj);
    lv_obj_add_state(sw, *setting ? LV_STATE_CHECKED : LV_STATE_DEFAULT);
    lv_obj_add_event_cb(sw, menu_switch_event_cb, LV_EVENT_VALUE_CHANGED, setting);
    if (cb != NULL) {
        lv_obj_add_event_cb(sw, cb, cb_event_type, setting);
    }

    return sw;
}

static void menu_slider_event_cb(lv_event_t * e) {
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t * obj = (lv_obj_t *)lv_event_get_target(e);
    int * setting = (int *)lv_event_get_user_data(e);

    if (code == LV_EVENT_VALUE_CHANGED) {
        *setting = lv_slider_get_value(obj);
    }
}

lv_obj_t * create_settings_slider(lv_obj_t * parent, const char * icon, const char * txt,
                                  int min, int max, uint8_t * setting, lv_event_cb_t cb,
                                  lv_event_code_t cb_event_type)
{
    lv_obj_t * obj = create_text(parent, icon, txt, LV_MENU_ITEM_BUILDER_VARIANT_2);

    lv_obj_t * slider = lv_slider_create(obj);
    lv_obj_set_flex_grow(slider, 1);
    lv_slider_set_range(slider, min, max);
    lv_slider_set_value(slider, *setting, LV_ANIM_OFF);
    lv_obj_add_flag(slider, LV_OBJ_FLAG_ADV_HITTEST);
    lv_obj_add_event_cb(slider, menu_slider_event_cb, LV_EVENT_VALUE_CHANGED, setting);
    if (cb != NULL) {
        lv_obj_add_event_cb(slider, cb, cb_event_type, setting);
    }

    if (icon == NULL) {
        lv_obj_add_flag(slider, LV_OBJ_FLAG_FLEX_IN_NEW_TRACK);
    }

    return slider;
}

#endif // LV_USE_MENU

#endif // USE_LVGL
