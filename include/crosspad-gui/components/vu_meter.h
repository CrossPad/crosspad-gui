#pragma once

#ifdef USE_LVGL
#include "lvgl.h"
#include <cstdint>

namespace crosspad_gui {

// Set audio levels for the VU meter (call from audio task)
// Values are int16 amplitude (0-32767)
void vu_set_levels(int16_t left, int16_t right);

} // namespace crosspad_gui

// Convert int16 audio level (0-32767) to dB-like logarithmic scale
// @param val   Audio amplitude (0-32767)
// @param base  Output scale maximum (default 100)
uint16_t int16toLogScale(int16_t val, int base = 100);

// Create a professional stereo VU meter with peak hold
// @param parent  Parent LVGL object
// @param width   Width of each bar in pixels
// @param height  Height of each bar in pixels
// @return Container object holding both bars
lv_obj_t * create_vu_meter(lv_obj_t * parent, int width, int height);

#endif // USE_LVGL
