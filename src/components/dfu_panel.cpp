#ifdef USE_LVGL

#include "crosspad-gui/components/dfu_panel.h"

lv_obj_t * lv_CreateDfuScreen(lv_obj_t * parent) {
    lv_obj_clean(parent);
    // show simple DFU screen with important information
    lv_obj_t * title = lv_label_create(lv_scr_act());
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(title, lv_color_make(0, 0, 0), 0);
    lv_label_set_text(title, "STM32 is in DFU mode");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_t * link = lv_label_create(lv_scr_act());
    lv_obj_set_style_text_font(link, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(link, lv_color_make(255, 0, 0), 0);
    lv_label_set_text(link, "github.com/crosspad");
    lv_obj_align(link, LV_ALIGN_CENTER, 0, -30);
    lv_obj_t * info = lv_label_create(lv_scr_act());
    lv_obj_set_style_text_color(info, lv_color_make(0, 0, 0), 0);
    lv_label_set_text(info, "Press encoder to exit DFU mode\nDon't interrupt the process\nWhen finished, it should reboot\nWhen failed, recover using ST-LINK");
    lv_obj_align(info, LV_ALIGN_CENTER, 0, 60);
    lv_obj_set_style_bg_color(lv_scr_act(), lv_color_make(200, 190, 255), 0);

    return lv_scr_act();
}

#endif // USE_LVGL
