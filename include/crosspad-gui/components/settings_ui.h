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

/// Register an extra platform-specific category (call before settings_ui_create).
/// Maximum 8 extra categories. Returns false if full.
bool settings_ui_add_category(const SettingsCategory& cat);

/// Clear all extra platform-specific categories.
void settings_ui_clear_extra_categories();

// ---- Widget helpers (for building custom category pages) ----

lv_obj_t * settings_section_header(lv_obj_t * parent, const char * text);

lv_obj_t * settings_row_switch(lv_obj_t * parent, lv_group_t * group,
                                const char * label, bool * setting,
                                lv_event_cb_t extra_cb = nullptr);

lv_obj_t * settings_row_slider(lv_obj_t * parent, lv_group_t * group,
                                const char * label, int min, int max,
                                uint8_t * setting,
                                lv_event_cb_t extra_cb = nullptr);

lv_obj_t * settings_row_info(lv_obj_t * parent, const char * label, const char * value);

lv_obj_t * settings_row_action(lv_obj_t * parent, lv_group_t * group,
                                const char * label, const char * btn_text,
                                lv_event_cb_t callback, void * user_data = nullptr);

} // namespace crosspad_gui

#endif // USE_LVGL
