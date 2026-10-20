// SPDX-License-Identifier: GPL-3.0-or-later
#ifdef USE_LVGL

#include "crosspad-gui/widgets/footer_button.h"
#include "crosspad-gui/styles/styles.h"

lv_obj_t * lv_CreateFooterButton(lv_obj_t * parent, const char * text, int xOffset, lv_event_cb_t cb, void * user_data)
{
    lv_obj_t * btn = lv_button_create(parent);
    lv_obj_t * btnLabel = lv_label_create(btn);
    LV_LOG_INFO("Creating button: %s, label: %p", text, btnLabel);
    lv_label_set_text(btnLabel, text);
    lv_obj_add_style(btn, &styleButton, 0);
    lv_obj_add_style(btn, &styleButtonPressed, LV_STATE_PRESSED);
    lv_obj_add_style(btn, &styleButtonInactive, LV_STATE_DISABLED);
    const int bottomOffset = 5;
    lv_obj_align(btn, LV_ALIGN_BOTTOM_MID, xOffset, -bottomOffset);
    if (cb) lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, user_data);
    lv_obj_add_event_cb(btn, [](lv_event_t * e) {
        LV_LOG_WARN("Button %p pressed", lv_event_get_user_data(e));
    }, LV_EVENT_PRESSED, btnLabel);
    return btn;
}

#endif // USE_LVGL
