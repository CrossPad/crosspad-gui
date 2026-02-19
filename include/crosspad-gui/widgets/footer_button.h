#pragma once

#ifdef USE_LVGL
#include "lvgl.h"

// Create a styled footer button aligned at the bottom of parent
// Uses global styles: styleButton, styleButtonPressed, styleButtonInactive
lv_obj_t * lv_CreateFooterButton(lv_obj_t * parent, const char * text, int xOffset, lv_event_cb_t cb, void * user_data);

#endif // USE_LVGL
