// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#ifdef USE_LVGL
#include <cstdint>
#include "lvgl.h"
#include "crosspad/net/WifiTypes.hpp"

namespace crosspad_gui {

// Four bars of rising height, plain rectangles (LV_SYMBOL_WIFI has no levels;
// no font, no image), drawn as crosspad::wifiBarsLook() says.
lv_obj_t* ui_wifi_bars_create(lv_obj_t* parent);

// Call on every refresh; repaints only when the look changes. tick_ms drives
// the connecting sweep (lv_tick_get()).
void ui_wifi_bars_update(lv_obj_t* bars, bool allowed, crosspad::WifiLinkState state,
                         int8_t rssi, uint32_t tick_ms);

} // namespace crosspad_gui

#endif // USE_LVGL
