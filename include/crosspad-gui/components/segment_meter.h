// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#ifdef USE_LVGL
#include "lvgl.h"
#include <cstdint>

namespace crosspad_gui {

/// The well's width: two columns of segments, 2 px apart, 2 px from each side.
constexpr int32_t kSegmentMeterW = 10;

/**
 * A stereo level meter in the look of Arrange's track meters: a dark well with
 * two columns of 2x2 segments, filled bottom up -- green to -6 dBFS, amber to
 * -1, red above -- each at a fifth of its colour until lit. Up at once, down
 * ~28 dB a second, no peak hold.
 *
 * It shows the levels crosspad_gui::vu_set_levels() is given (the master
 * output on the board), the loudest since its last 50 ms tick
 * (vu_take_peaks()), so one meter on screen at a time.
 *
 * @param parent  Parent LVGL object
 * @param height  The well's height: a segment every 3 px, as many as fit
 *                (10 at Arrange's 30 px), over the same -48..-1 dB as there.
 * @return The well, kSegmentMeterW wide; its timer goes with it.
 */
lv_obj_t * create_segment_meter(lv_obj_t * parent, int32_t height);

} // namespace crosspad_gui

#endif // USE_LVGL
