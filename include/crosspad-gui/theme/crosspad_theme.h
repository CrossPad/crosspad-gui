#pragma once

#ifdef USE_LVGL
#include "lvgl.h"

// Allow per-project override via crosspad_theme_user.h
// Each project places this file in its own include path with custom colors.
#if __has_include("crosspad_theme_user.h")
#include "crosspad_theme_user.h"
#endif

// ── Base defaults (green/white) ─────────────────────────────────────────────
#ifndef CROSSPAD_THEME_PRIMARY
#define CROSSPAD_THEME_PRIMARY     lv_color_make(0, 255, 136)      // Neon green
#endif

#ifndef CROSSPAD_THEME_SECONDARY
#define CROSSPAD_THEME_SECONDARY   lv_color_make(255, 255, 255)    // White
#endif

#ifndef CROSSPAD_THEME_ACCENT
#define CROSSPAD_THEME_ACCENT      lv_color_make(255, 0, 136)      // Pink
#endif

#ifndef CROSSPAD_THEME_BG_DARK
#define CROSSPAD_THEME_BG_DARK     lv_color_make(26, 26, 46)       // Dark bg
#endif

#ifndef CROSSPAD_THEME_CARD_BG
#define CROSSPAD_THEME_CARD_BG     lv_color_make(42, 42, 42)       // Card bg
#endif

// ── LCD resolution (for GUI layout, may differ from physical display) ────────
#ifndef CP_LCD_HOR_RES
#define CP_LCD_HOR_RES LV_HOR_RES
#endif

#ifndef CP_LCD_VER_RES
#define CP_LCD_VER_RES LV_VER_RES
#endif

// ── Overlay parent (for status bar, modals, popups) ──────────────────────────
// Defaults to lv_layer_top(). PC simulator overrides this to a container inside
// the LCD area so overlays render within the device body, not the full window.
#ifdef __cplusplus
namespace crosspad_gui {
    void setOverlayParent(lv_obj_t * parent);
    lv_obj_t * getOverlayParent();
}
#endif

#endif // USE_LVGL
