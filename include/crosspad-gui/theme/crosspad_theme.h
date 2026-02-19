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

#endif // USE_LVGL
