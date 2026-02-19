#pragma once

#ifdef USE_LVGL
#include "lvgl.h"
#include <cstdint>

// Status bar height (shared define, can be overridden)
#ifndef LV_STATUS_BAR_HEIGHT
#define LV_STATUS_BAR_HEIGHT 22
#endif

#ifndef QUICK_BUTTON_SIZE
#define QUICK_BUTTON_SIZE 25
#endif

#ifndef LV_HIDE_QUICK_SETTINGS
#define LV_HIDE_QUICK_SETTINGS 0
#endif

namespace crosspad_gui {

// Set loading indicator progress (-1 to hide, 0-100 for progress)
void statusbar_set_loading(int progress);

// Get the loading subject pointer (for lv_subject_add_observer_obj)
lv_subject_t * statusbar_get_loading_subject();

// Show a loader message box on the top layer
// @param title   Title text
// @param message Body text
// @param isError If true, styled as error; if false, informational
void statusbar_show_loader_message(const char * title, const char * message, bool isError);

} // namespace crosspad_gui

// Create the status bar on lv_layer_top()
// Requires: extern CrosspadSettings * settings;
//           extern CrosspadStatus status;
//           extern lv_obj_t * status_c;
// @return The created status bar container
lv_obj_t * lv_CreateStatusBar();

#endif // USE_LVGL
