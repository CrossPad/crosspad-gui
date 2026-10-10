// SPDX-License-Identifier: MIT
#pragma once

/**
 * @file cp_design.h
 * @brief The V5 "Pixel Arduino" design language, as the firmware uses it.
 *
 * Ported from the LVGL 8.3 export the designs arrived as. One include for the
 * palette, the two pixel faces and the 12x12 icons, so a screen written
 * against the design does not reach into the export's files.
 *
 * **The pixel faces cover ASCII 0x20-0x7E only.** They are for labels a
 * designer chose the width of -- soft keys, transport, headings in the
 * mockups. Never for a kit name: the firmware replaced LVGL's Montserrat with
 * faces carrying Latin-1 Supplement and Latin Extended-A precisely because a
 * kit called "SWIETE SLOWA" (with its diacritics) drew as boxes, and these
 * would bring the boxes back. Anything holding text from the card stays on
 * lv_font_montserrat_*.
 */

#include "lvgl.h"
#include "crosspad-gui/design/cp_icons.h"

LV_FONT_DECLARE(cp_font_pixel_8)   /* labels, headlines */
LV_FONT_DECLARE(cp_font_body_8)    /* descriptions */

#define CP_FONT_PIXEL  (&cp_font_pixel_8)
#define CP_FONT_BODY   (&cp_font_body_8)

/* The export's palette, by the names it used. `primary` is not here on
 * purpose: it is the theme's, so a screen passes CROSSPAD_THEME_PRIMARY in
 * and the design keeps working when the theme changes. */
#define CP_COL_HAIRLINE  lv_color_hex(0x16161E)   /* 1px dividers and edges */
#define CP_COL_NORMAL    lv_color_hex(0xB0B0BE)   /* ordinary label */
#define CP_COL_DIM       lv_color_hex(0x4A4A55)   /* disabled, and the F-index */

/* What the export used for a key that is "the main action": a tint of the
 * primary colour behind it, not a solid fill. 26/255 is ~10 %. */
#define CP_OPA_PRIMARY_TINT  26
/* And for a slot that is empty or unavailable. */
#define CP_OPA_UNAVAILABLE   LV_OPA_30
