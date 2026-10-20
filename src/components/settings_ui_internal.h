// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

/**
 * @file settings_ui_internal.h
 * @brief What the settings_ui*.cpp translation units share: the open page's
 *        state, the shell's helpers, and the built-in category builders.
 *        Not part of crosspad-gui's API.
 */

#ifdef USE_LVGL

#include "lvgl.h"
#include "crosspad-gui/components/settings_ui.h"

#include <cstdint>

namespace crosspad { class CrosspadSettings; }

namespace crosspad_gui {
namespace settings_detail {

// ---- Page state (settings_ui.cpp) ----

extern lv_obj_t *  s_content;
extern lv_group_t* s_group;
extern int         s_current_category;

// ---- Shell helpers (settings_ui.cpp) ----

void save_settings();
crosspad::CrosspadSettings* get_settings();
void show_detail(int category_index);
void focus_scroll_instant(lv_obj_t * obj);
bool stm_config_available();

// ---- Row helpers (settings_ui_rows.cpp) ----

lv_obj_t * settings_row_bitswitch(lv_obj_t * parent, lv_group_t * group,
                                  const char * label, uint8_t * flags, uint8_t bit,
                                  lv_event_cb_t extra_cb = nullptr);

// ---- Output volumes (settings_ui_audio.cpp) ----

void apply_output_volumes(crosspad::CrosspadSettings* s);

// ---- STM config (settings_ui_system.cpp) ----

void stm_push_config(crosspad::CrosspadSettings* s);
// Close the Factory Reset question, as Cancel, when Settings goes away.
void system_dialogs_dismiss();

// ---- Built-in category builders, one translation unit per category ----

void cat_build_display(lv_obj_t * parent, lv_group_t * group);
void cat_build_audio(lv_obj_t * parent, lv_group_t * group);
void cat_build_usb(lv_obj_t * parent, lv_group_t * group);
void cat_build_pads(lv_obj_t * parent, lv_group_t * group);
void cat_build_vibration(lv_obj_t * parent, lv_group_t * group);
void cat_build_midi(lv_obj_t * parent, lv_group_t * group);
void cat_build_analog(lv_obj_t * parent, lv_group_t * group);
void cat_build_backup(lv_obj_t * parent, lv_group_t * group);
void cat_build_programming(lv_obj_t * parent, lv_group_t * group);
void cat_build_info(lv_obj_t * parent, lv_group_t * group);
void cat_build_system(lv_obj_t * parent, lv_group_t * group);

} // namespace settings_detail
} // namespace crosspad_gui

#endif // USE_LVGL
