#pragma once

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

// ── Loading API ─────────────────────────────────────────────────────────────

// Set loading indicator progress (-1 to hide, 0-100 for progress)
void statusbar_set_loading(int progress);

// Get the loading subject pointer (for lv_subject_add_observer_obj)
lv_subject_t * statusbar_get_loading_subject();

// Show a loader message box on the top layer
// @param title   Title text
// @param message Body text
// @param isError If true, styled as error; if false, informational
void statusbar_show_loader_message(const char * title, const char * message, bool isError);

// Process a single loading status update from any background loader.
// Call from GUI thread only. Hides loading indicator when done, shows error message if needed.
void statusbar_process_loader_status(uint8_t progress, bool done, bool error, const char * msg);

// ── Dynamic App Icon API ────────────────────────────────────────────────────

#ifndef STATUSBAR_MAX_APP_ICONS
#define STATUSBAR_MAX_APP_ICONS 8
#endif

// Which side of the status bar to place an app icon
enum class StatusIconSide : uint8_t {
    Left,   // After system icons (battery, wifi, etc.)
    Right,  // Right-aligned, before the drawer button
};

// Add or update a named icon in the status bar.
// If an icon with this id already exists, it is updated in place.
// @param id       Unique string identifier (e.g. "seq.tempo"). Caller must ensure lifetime.
// @param content  LV_SYMBOL_* string or short text (e.g. "120", "REC"). Caller must ensure lifetime.
// @param color    Icon/text color
// @param side     Left or Right zone (default: Right)
// @return true if added/updated, false if no free slots
// Must be called from LVGL task context.
bool statusbar_add_icon(const char * id, const char * content,
                        lv_color_t color,
                        StatusIconSide side = StatusIconSide::Right);

// Remove a named icon. No-op if not found. Safe to call multiple times.
void statusbar_remove_icon(const char * id);

// Update color of an existing icon. No-op if not found.
void statusbar_set_icon_color(const char * id, lv_color_t color);

// Update content (symbol or text) of an existing icon. No-op if not found.
void statusbar_set_icon_content(const char * id, const char * content);

// Show or hide an existing icon without removing its slot. No-op if not found.
void statusbar_set_icon_visible(const char * id, bool visible);

// Remove all app icons. Intended for app teardown / return to launcher.
void statusbar_clear_all_icons();

// ── Performance Stats ────────────────────────────────────────────────────────

// Set performance stats display flags (bitfield).
// bit 0 = FPS column, bit 1 = CPU column, bit 2 = MEM column
#define PERF_FLAG_FPS  (1 << 0)
#define PERF_FLAG_CPU  (1 << 1)
#define PERF_FLAG_MEM  (1 << 2)
void statusbar_set_perf_level(uint8_t flags);

} // namespace crosspad_gui

// Create the status bar as a child of the given parent.
// Requires: extern CrosspadSettings * settings;
//           extern CrosspadStatus status;
//           extern lv_obj_t * status_c;
// @param parent  Parent object (typically the main screen flex column)
// @return The created status bar container
lv_obj_t * lv_CreateStatusBar(lv_obj_t * parent);
