#pragma once

#ifdef USE_LVGL
#include "lvgl.h"

// Menu item builder variants (from LVGL menu examples)
#define LV_MENU_ITEM_BUILDER_VARIANT_1 0
#define LV_MENU_ITEM_BUILDER_VARIANT_2 1

// Create a text row for menu items (icon + label)
lv_obj_t * create_text(lv_obj_t * parent, const char * icon, const char * txt, int builder_variant);

// Create a settings switch with automatic bool* update
lv_obj_t * create_settings_switch(lv_obj_t * parent, const char * icon, const char * txt,
                                  bool * setting, lv_event_cb_t cb = NULL,
                                  lv_event_code_t cb_event_type = LV_EVENT_VALUE_CHANGED);

// Create a settings slider with automatic uint8_t* update
lv_obj_t * create_settings_slider(lv_obj_t * parent, const char * icon, const char * txt,
                                  int min, int max, uint8_t * setting, lv_event_cb_t cb = NULL,
                                  lv_event_code_t cb_event_type = LV_EVENT_VALUE_CHANGED);

#endif // USE_LVGL
