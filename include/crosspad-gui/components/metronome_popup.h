// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#ifdef USE_LVGL
#include "lvgl.h"

namespace crosspad_gui {

/// The metronome's options over the screen, opened by holding the top bar's
/// metronome: how loud the click is, how many clicks a beat (1/4, 1/8, 1/16)
/// and the count-in before a take (off, one bar, two). Each change is heard
/// at once (crosspad::transportSetClick*); Done, a tap beside the card or the
/// power button's back closes it. The encoder gets its own group meanwhile.
void metronome_popup_open();

/// Close it, if open.
void metronome_popup_close();

bool metronome_popup_is_open();

} // namespace crosspad_gui

#endif // USE_LVGL
