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
// Call from GUI thread only. Hides loading indicator when done; an error goes to
// the kit loader's red bar (kit_loader.h).
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

// Override the text font for an existing icon. No-op if not found.
// Pass nullptr to reset to the status-bar default.
void statusbar_set_icon_font(const char * id, const lv_font_t * font);

// Show or hide an existing icon without removing its slot. No-op if not found.
void statusbar_set_icon_visible(const char * id, bool visible);

// Remove all app icons. Intended for app teardown / return to launcher.
void statusbar_clear_all_icons();

// ── Title ───────────────────────────────────────────────────────────────────

// Text centred in the bar in place of the CrossPad logo, for a screen whose
// status line would otherwise take a row of its own (Arrange). Returns the
// bar's label to set the text on -- the bar owns it and it goes with the bar
// -- or nullptr when there is no bar. statusbar_hide_title() brings the logo
// back. Must be called from LVGL task context.
lv_obj_t * statusbar_show_title();
void statusbar_hide_title();

// ── Performance Stats ────────────────────────────────────────────────────────

// Set performance stats display flags (bitfield).
// bit 0 = FPS column, bit 1 = CPU column, bit 2 = MEM column
#define PERF_FLAG_FPS  (1 << 0)
#define PERF_FLAG_CPU  (1 << 1)
#define PERF_FLAG_MEM  (1 << 2)
void statusbar_set_perf_level(uint8_t flags);

// ── Arrow handler ────────────────────────────────────────────────────────────

/* The arrow on the bar's right used to be the quick-settings drawer's open
 * button. The drawer is off, and the arrow is now the way back to whatever
 * replaced the bar: the transport bar installs transportbar_show(true) here.
 *
 * Installing a handler builds the arrow if the drawer had not already. With
 * no handler and no drawer the arrow is not built at all, rather than sitting
 * there doing nothing. */
void statusbar_set_arrow_handler(void (*handler)());

// ── Drawer (quick-settings) toggle ───────────────────────────────────────────

// Enable/disable the status-bar drawer (header dropdown + arrow button).
// When disabled: the open arrow is hidden and any open drawer is closed.
// Must be called from LVGL task context.
void statusbar_set_drawer_enabled(bool enabled);

// True while the quick-settings drawer is open (or opening).
bool statusbar_drawer_is_open();

// Close the drawer if it is open. No-op otherwise.
void statusbar_close_drawer();

// Open the drawer if closed, close it if open. Returns false when the gesture
// changed nothing — the drawer is disabled, or an animation is still running.
// This is what the power button's 0.5 s hold calls.
bool statusbar_toggle_drawer();

// Power-button hold feedback: the open arrow slides right over `duration_ms`
// as the button is held, so the gesture reads as "charging" toward the
// commit — call with the same duration as the hold threshold. No-op if the
// drawer is disabled, already open, or mid-open.
void statusbar_arrow_hold_begin(uint32_t duration_ms);

// Call on release before the hold threshold fires: eases the arrow back from
// wherever the charge animation had reached, rather than snapping it. No-op
// if nothing was charging.
void statusbar_arrow_hold_cancel();

} // namespace crosspad_gui

// Create the status bar as a child of the given parent.
// Requires: extern CrosspadSettings * settings;
//           extern CrosspadStatus status;
//           extern lv_obj_t * status_c;
// @param parent  Parent object (typically the main screen flex column)
// @return The created status bar container
lv_obj_t * lv_CreateStatusBar(lv_obj_t * parent);
