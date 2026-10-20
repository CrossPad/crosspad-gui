// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

/**
 * @file cp_design.h
 * @brief CrossPad's pixel design language: palette, pixel fonts and icons.
 *
 * One include for the palette, the two pixel faces and the 12x12 icons, so a
 * screen written against the design needs nothing else.
 *
 * **The pixel faces cover ASCII 0x20-0x7E only.** They are for labels whose
 * width the design sets -- soft keys, transport, headings. Text from the
 * card, such as a kit name, can carry accented letters: draw it with
 * lv_font_montserrat_*, which crosspad-gui builds with Latin-1 Supplement
 * and Latin Extended-A.
 */

#include "lvgl.h"
#include "crosspad-gui/design/cp_icons.h"

/* The faces are C files: without C linkage MSVC looks for a mangled C++ name
 * and the link fails (GCC does not mangle data, so it never showed there). */
#ifdef __cplusplus
extern "C" {
#endif
LV_FONT_DECLARE(cp_font_pixel_8)   /* labels, headlines */
LV_FONT_DECLARE(cp_font_body_8)    /* descriptions */
#ifdef __cplusplus
}
#endif

/// The pixel face for labels and headlines (ASCII only).
#define CP_FONT_PIXEL  (&cp_font_pixel_8)
/// The pixel face for descriptions (ASCII only).
#define CP_FONT_BODY   (&cp_font_body_8)

/* The export's palette, by the names it used. `primary` is not here on
 * purpose: it is the theme's, so a screen passes CROSSPAD_THEME_PRIMARY in
 * and the design keeps working when the theme changes. */
#define CP_COL_HAIRLINE  lv_color_hex(0x16161E)   ///< 1px dividers and edges
#define CP_COL_NORMAL    lv_color_hex(0xB0B0BE)   ///< ordinary label
#define CP_COL_DIM       lv_color_hex(0x4A4A55)   ///< disabled, and the F-index

/// Opacity of the primary-colour tint behind a key that is "the main
/// action" (not a solid fill): 26/255, about 10 %.
#define CP_OPA_PRIMARY_TINT  26
/// Opacity for a slot that is empty or unavailable.
#define CP_OPA_UNAVAILABLE   LV_OPA_30
