// SPDX-License-Identifier: GPL-3.0-or-later
#ifdef USE_LVGL

#include "crosspad-gui/components/ui_toast.h"
#include "crosspad-gui/theme/crosspad_theme.h"

static lv_obj_t * active_toast = NULL;
static lv_timer_t * toast_timer = NULL;

static void toast_timer_cb(lv_timer_t * timer) {
    ui_toast_dismiss();
}

void ui_toast_dismiss(void) {
    if (toast_timer) {
        lv_timer_delete(toast_timer);
        toast_timer = NULL;
    }
    if (active_toast) {
        lv_obj_delete(active_toast);
        active_toast = NULL;
    }
}

void ui_toast_show(const char * message, uint32_t duration_ms, uint8_t severity) {
    ui_toast_dismiss();

    lv_color_t text_color;
    lv_color_t bg_color;
    switch (severity) {
        case UI_TOAST_SUCCESS:
            text_color = lv_color_make(0, 200, 0);
            bg_color = lv_color_make(0, 40, 0);
            break;
        case UI_TOAST_WARNING:
            text_color = CROSSPAD_THEME_PRIMARY;
            bg_color = lv_color_make(40, 40, 0);
            break;
        case UI_TOAST_ERROR:
            text_color = lv_color_make(255, 60, 60);
            bg_color = lv_color_make(40, 0, 0);
            break;
        default:
            text_color = lv_color_white();
            bg_color = lv_color_make(30, 30, 30);
            break;
    }

    active_toast = lv_obj_create(crosspad_gui::getOverlayParent());
    lv_obj_set_size(active_toast, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_align(active_toast, LV_ALIGN_BOTTOM_MID, 0, -8);
    lv_obj_set_style_bg_color(active_toast, bg_color, 0);
    lv_obj_set_style_bg_opa(active_toast, LV_OPA_90, 0);
    lv_obj_set_style_border_color(active_toast, text_color, 0);
    lv_obj_set_style_border_width(active_toast, 1, 0);
    lv_obj_set_style_radius(active_toast, 8, 0);
    lv_obj_set_style_pad_hor(active_toast, 12, 0);
    lv_obj_set_style_pad_ver(active_toast, 6, 0);
    lv_obj_remove_flag(active_toast, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t * lbl = lv_label_create(active_toast);
    lv_label_set_text(lbl, message);
    lv_obj_set_style_text_color(lbl, text_color, 0);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
    /* As wide as its text, but never wider than the screen: sized to the
     * content alone, a long message was centred past both edges and read
     * from neither. A longer one wraps onto more lines, centred. */
    const int32_t screen_w = lv_display_get_horizontal_resolution(lv_obj_get_display(active_toast));
    const int32_t max_w = screen_w - 16 - 2 * 12 - 2;   // 8 px each side, the padding, the border
    lv_point_t text_size;
    lv_text_get_size(&text_size, message, &lv_font_montserrat_14, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    if (text_size.x > max_w) {
        lv_label_set_long_mode(lbl, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(lbl, max_w);
        lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
    }

    toast_timer = lv_timer_create(toast_timer_cb, duration_ms, NULL);
    lv_timer_set_repeat_count(toast_timer, 1);
}

#endif // USE_LVGL
