#ifdef USE_LVGL

#include "crosspad-gui/widgets/settings_widgets.h"

// Note: create_text() uses lv_menu widget - it must be defined by the project
// since lv_menu is used differently in Arduino (lv_menu) vs IDF (custom nav).
// For now we provide the switch/slider helpers that work with any parent container.

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
    lv_obj_add_state(sw, *setting ? LV_STATE_CHECKED : 0);
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

#endif // USE_LVGL
