#pragma once

#ifdef USE_LVGL
#include "lvgl.h"

// Create a 4x4 button grid with pad colors from PadManager
// callback: event handler for button clicks
// userData: passed to callback
// labels: show pad numbers on buttons
lv_obj_t * createButtons(lv_obj_t * parent, lv_event_cb_t callback, void * userData, bool labels);

// Create smaller 4x4 button grid (75% size) without labels - for sequencer
lv_obj_t * createButtonsSequencer(lv_obj_t * parent, lv_event_cb_t callback, void * userData);

#endif // USE_LVGL
