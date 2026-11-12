// SPDX-License-Identifier: GPL-3.0-or-later
#ifdef USE_LVGL

#include "crosspad-gui/components/dfu_panel.h"
#include "crosspad-gui/components/ui_modal.h"
#include "crosspad/settings/ISettingsUI.hpp"

/* The ROM bootloader serves only USB: the encoder and the power button are
 * the STM's, so neither can bring it back. Touch is the ESP's and still
 * works, which makes a tap the one way out on the device (ISettingsUI resets
 * the STM; its app boots from the DFU return marker). */

static lv_obj_t * s_panel = nullptr;
static lv_obj_t * s_ask = nullptr;

enum { kStay = 0, kLeave = 1 };

static void leave_answer_cb(int index, void * user_data) {
    (void)user_data;
    s_ask = nullptr;
    if (index != kLeave || !s_panel) return;
    if (auto * ui = crosspad::getSettingsUI()) ui->leaveStm32Dfu();
    lv_obj_delete_async(s_panel);
    s_panel = nullptr;
}

static void panel_clicked_cb(lv_event_t * e) {
    (void)e;
    if (s_ask) return;
    static const ui_modal_button_t buttons[] = {
        {"Stay",  UI_MODAL_BUTTON_NORMAL},
        {"Leave", UI_MODAL_BUTTON_DANGER},
    };
    s_ask = ui_modal_choice(
        "Leave DFU mode?",
        "The STM32 restarts. An update running over USB stops and has to be "
        "started again.",
        buttons, 2, kStay, leave_answer_cb, nullptr, nullptr);
}

static void panel_deleted_cb(lv_event_t * e) {
    if (lv_event_get_target(e) == s_panel) s_panel = nullptr;
}

lv_obj_t * lv_CreateDfuScreen(lv_obj_t * parent) {
    if (s_panel) return s_panel;

    lv_obj_t * panel = lv_obj_create(parent);
    lv_obj_remove_style_all(panel);
    lv_obj_set_size(panel, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(panel, lv_color_make(200, 190, 255), 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(panel, 8, 0);
    lv_obj_add_flag(panel, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(panel, panel_clicked_cb, LV_EVENT_CLICKED, nullptr);
    lv_obj_add_event_cb(panel, panel_deleted_cb, LV_EVENT_DELETE, nullptr);

    lv_obj_t * title = lv_label_create(panel);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(title, lv_color_make(0, 0, 0), 0);
    lv_label_set_text(title, "STM32 is in DFU mode");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 4);

    lv_obj_t * info = lv_label_create(panel);
    lv_obj_set_style_text_color(info, lv_color_make(0, 0, 0), 0);
    lv_obj_set_style_text_align(info, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(info,
        "Ready for an update over USB.\n"
        "The encoder and the power button\n"
        "do not work until you leave.\n"
        "After an update the STM32 restarts.\n"
        "If an update fails, run it again over USB.");
    lv_obj_align(info, LV_ALIGN_CENTER, 0, -2);

    lv_obj_t * tap = lv_label_create(panel);
    lv_obj_set_style_text_font(tap, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(tap, lv_color_make(160, 0, 0), 0);
    lv_label_set_text(tap, "Tap the screen to leave DFU mode");
    lv_obj_align(tap, LV_ALIGN_BOTTOM_MID, 0, -6);

    s_panel = panel;
    return panel;
}

#endif // USE_LVGL
