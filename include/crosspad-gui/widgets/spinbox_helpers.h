#pragma once

#ifdef USE_LVGL
#include "lvgl.h"

// Create a reusable standard spinbox with styling, cursor management, and group support
lv_obj_t * lv_CreateStandardSpinbox(lv_obj_t * parent, int32_t min_value, int32_t max_value,
                                    int32_t initial_value, lv_event_cb_t value_changed_cb,
                                    lv_group_t * group, void * user_data, const lv_font_t * font = NULL);

// Create a simplified spinbox (no styling, no cursor management)
lv_obj_t * lv_CreateSimpleSpinbox(lv_obj_t * parent, int32_t min_value, int32_t max_value,
                                  int32_t initial_value, lv_event_cb_t value_changed_cb,
                                  lv_group_t * group, void * user_data);

#endif // USE_LVGL
