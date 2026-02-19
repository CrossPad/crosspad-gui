#pragma once

#ifdef USE_LVGL
#include "lvgl.h"

// Get contrast (black/white) text color for a given background color
lv_color_t lv_color_bw_from_color(lv_color_t color);

// Set object visibility (wraps LV_OBJ_FLAG_HIDDEN)
void lv_obj_set_hidden(lv_obj_t * obj, bool hidden);

// Delete all children from an LVGL list/container
void lv_list_cleanup(lv_obj_t * list);

// Calculate number of digits in an integer (for spinbox sizing)
int CalcDigits(int value);

#endif // USE_LVGL
