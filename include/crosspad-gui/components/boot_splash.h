// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file boot_splash.h
 * @brief The cold-boot splash: the 4x4 pads light up 01 to 16, bottom left
 *        first, flash lime together, the CrossPad logo rises in -- over a
 *        purple gradient with a glow behind the pads and a perspective floor
 *        scrolling towards the viewer. About 2.2 s, backlight fades included;
 *        the pads' LEDs go along with the screen.
 *
 * Cheap to draw on purpose: the SPI panel takes 30 ms for a whole frame, so
 * nothing here repaints the whole screen. The fades are the backlight's, the
 * glows are alpha sprites made once and tinted (LVGL's blurred shadows cost
 * tens of ms a frame), and what moves is small. Ported from the LVGL 8
 * mockup (cp_splash.c, "CrossPad OS Splash.html").
 */
#pragma once

#ifdef USE_LVGL

#include "lvgl.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Sets the panel's backlight, 0..100. */
typedef void (*boot_splash_backlight_fn)(uint8_t level);

/**
 * Start it on @p parent with the backlight dark, fading it up to @p level.
 * It ends with the backlight faded out again. @p standalone: it then deletes
 * itself and brings the backlight back over whatever is under it; otherwise
 * the caller builds the next screen in the dark and calls
 * boot_splash_reveal(). Call holding the LVGL lock.
 *
 * It covers the parent's top left 320x240, over any layout the parent has.
 * @p backlight may be null on a platform without one: the screen then does
 * not fade, the pads still do.
 */
void boot_splash_start(lv_obj_t* parent, uint8_t level, boot_splash_backlight_fn backlight, bool standalone);

/** True until it has faded out. */
bool boot_splash_active(void);

/** The next screen is built: the backlight back up to the level the splash
 *  was started with, and the pads back to their own colours, rising with it.
 *  Call holding the LVGL lock. */
void boot_splash_reveal(void);

#ifdef __cplusplus
}
#endif

#endif // USE_LVGL
