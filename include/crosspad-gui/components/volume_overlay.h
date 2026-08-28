#pragma once

#ifdef USE_LVGL
#include "lvgl.h"
#include <cstdint>

namespace crosspad_gui {

// Platform callback to apply volume to hardware.
// mode: 1 = speakers, 2 = headphones
// value: 0-127
typedef void (*volume_apply_cb_t)(int mode, uint8_t value);

// Register the platform-specific hardware volume callback.
// Call once at startup before any overlay interactions.
void volume_overlay_set_apply_cb(volume_apply_cb_t cb);

// Show the overlay if hidden, dismiss it if shown. Every output has its own
// slider on screen at once, so there is nothing to cycle through.
// Call on every power button rising edge.
void volume_overlay_toggle();

// Dismiss the overlay immediately.
void volume_overlay_dismiss();

// Returns true if the overlay is currently visible.
bool volume_overlay_is_visible();

// The encoder group the overlay owns while visible, nullptr otherwise.
// Exposed so a test harness can tell "the overlay is drawn" apart from "the
// overlay also has the encoder".
lv_group_t * volume_overlay_group();

} // namespace crosspad_gui

#endif // USE_LVGL
