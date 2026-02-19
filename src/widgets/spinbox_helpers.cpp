#ifdef USE_LVGL

#include "crosspad-gui/widgets/spinbox_helpers.h"
#include "crosspad-gui/widgets/gui_helpers.h"
#include "crosspad-gui/styles/styles.h"

lv_obj_t * lv_CreateStandardSpinbox(lv_obj_t * parent, int32_t min_value, int32_t max_value,
                                    int32_t initial_value, lv_event_cb_t value_changed_cb,
                                    lv_group_t * group, void * user_data, const lv_font_t * font) {
    lv_obj_t * spinbox = lv_spinbox_create(parent);
    uint8_t digits = CalcDigits(max_value);
    lv_obj_set_width(spinbox, 5 + (8 * digits));

    lv_spinbox_set_range(spinbox, min_value, max_value);
    lv_spinbox_set_value(spinbox, initial_value);
    lv_spinbox_set_step(spinbox, 1);
    lv_spinbox_set_digit_format(spinbox, digits, 0);
    lv_spinbox_set_rollover(spinbox, false);

    lv_obj_add_style(spinbox, &styleSpinbox, 0);
    lv_obj_add_style(spinbox, &styleSpinbox, LV_PART_CURSOR);

    if (font != NULL) {
        lv_obj_set_style_text_font(spinbox, font, 0);
    }

    if (value_changed_cb) {
        lv_obj_add_event_cb(spinbox, value_changed_cb, LV_EVENT_VALUE_CHANGED, user_data);
    }

    lv_obj_add_event_cb(spinbox, [](lv_event_t * e) {
        lv_obj_t * obj = (lv_obj_t *)lv_event_get_target(e);
        lv_spinbox_set_cursor_pos(obj, 0);
        auto group = lv_obj_get_group(obj);
        if (group) lv_group_set_editing(group, false);
    }, LV_EVENT_SHORT_CLICKED, NULL);

    if (group != NULL) {
        lv_group_add_obj(group, spinbox);
    }

    lv_spinbox_set_cursor_pos(spinbox, 0);
    return spinbox;
}

lv_obj_t * lv_CreateSimpleSpinbox(lv_obj_t * parent, int32_t min_value, int32_t max_value,
                                  int32_t initial_value, lv_event_cb_t value_changed_cb,
                                  lv_group_t * group, void * user_data) {
    lv_obj_t * spinbox = lv_spinbox_create(parent);
    uint8_t digits = CalcDigits(max_value);
    lv_obj_set_width(spinbox, 5 + (8 * digits));

    lv_spinbox_set_range(spinbox, min_value, max_value);
    lv_spinbox_set_value(spinbox, initial_value);
    lv_spinbox_set_step(spinbox, 1);
    lv_spinbox_set_digit_format(spinbox, digits, 0);
    lv_spinbox_set_rollover(spinbox, false);

    if (value_changed_cb) {
        lv_obj_add_event_cb(spinbox, value_changed_cb, LV_EVENT_VALUE_CHANGED, user_data);
    }

    if (group != NULL) {
        lv_group_add_obj(group, spinbox);
    }

    return spinbox;
}

#endif // USE_LVGL
