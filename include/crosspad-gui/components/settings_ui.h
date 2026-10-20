// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#ifdef USE_LVGL
#include "lvgl.h"

namespace crosspad { class CrosspadSettings; }

namespace crosspad_gui {

// ---- Category system ----

struct SettingsCategory {
    const char * name;
    const char * icon;  // LV_SYMBOL_*
    void (*build)(lv_obj_t * parent, lv_group_t * group);
};

// ---- Settings UI public API ----

/// Initialize the settings UI inside the given content area.
/// @param content_area  Scrollable LVGL container to build settings into
/// @param group         Optional encoder navigation group (nullptr for touch-only)
void settings_ui_create(lv_obj_t * content_area, lv_group_t * group);

/// Clean up settings UI state. Called when leaving the settings screen.
void settings_ui_destroy();

// Step one level back: from a category detail to the category list. Returns
// false when already at the list (nothing to pop) — what an app's back hook
// returns to let the framework close Settings instead.
bool settings_ui_back();

/// The next settings_ui_create() opens this category's page instead of the
/// list (Back still leads to the list). Matched by name; consumed once.
void settings_ui_open_next(const char* category);

/// Register an extra platform-specific category (call before settings_ui_create).
/// Maximum 8 extra categories. Returns false if full.
bool settings_ui_add_category(const SettingsCategory& cat);

/// Clear all extra platform-specific categories.
void settings_ui_clear_extra_categories();

// ---- Settings backup (the Settings → Backup rows, callable by a companion) ----
// The whole-settings JSON file, same envelope as the SD rows write. Import
// validates, persists and applies (brightness, volumes, LEDs, vibration, STM
// pad config). Both run on the LVGL thread; `error` gets a static string
// when they return false. Without FEAT_SETTINGS_BACKUP they only report that.
bool settings_ui_backup_export(const char* path, const char** error);
bool settings_ui_backup_import(const char* path, const char** error);
// Where the Backup page writes its file, or nullptr with no backup directory.
const char* settings_ui_backup_default_path();

// ---- Widget helpers (for building custom category pages) ----

lv_obj_t * settings_section_header(lv_obj_t * parent, const char * text);

lv_obj_t * settings_row_switch(lv_obj_t * parent, lv_group_t * group,
                                const char * label, bool * setting,
                                lv_event_cb_t extra_cb = nullptr);

lv_obj_t * settings_row_slider(lv_obj_t * parent, lv_group_t * group,
                                const char * label, int min, int max,
                                uint8_t * setting,
                                lv_event_cb_t extra_cb = nullptr);

lv_obj_t * settings_row_dropdown(lv_obj_t * parent, lv_group_t * group,
                                  const char * label, const char * options,
                                  uint8_t * setting,
                                  lv_event_cb_t extra_cb = nullptr);

lv_obj_t * settings_row_info(lv_obj_t * parent, const char * label, const char * value);

lv_obj_t * settings_row_action(lv_obj_t * parent, lv_group_t * group,
                                const char * label, const char * btn_text,
                                lv_event_cb_t callback, void * user_data = nullptr);

} // namespace crosspad_gui

#endif // USE_LVGL
