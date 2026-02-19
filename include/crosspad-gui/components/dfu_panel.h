#pragma once

#ifdef USE_LVGL
#include "lvgl.h"

// Create a full-screen DFU mode information panel
lv_obj_t * lv_CreateDfuScreen(lv_obj_t * parent);

#endif // USE_LVGL
