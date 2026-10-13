// SPDX-License-Identifier: GPL-3.0-or-later
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

// Cycle the overlay state: hidden → speakers → headphones → hidden.
// Call on every power button rising edge.
void volume_overlay_toggle();

// Dismiss the overlay immediately.
void volume_overlay_dismiss();

// Returns true if the overlay is currently visible.
bool volume_overlay_is_visible();

} // namespace crosspad_gui

#endif // USE_LVGL
