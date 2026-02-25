#pragma once

#ifdef USE_LVGL
#include "lvgl.h"

// Allow per-project override via crosspad_theme_user.h
// Each project places this file in its own include path with custom colors.
#if __has_include("crosspad_theme_user.h")
#include "crosspad_theme_user.h"
#endif

// ── Runtime theme color palette ─────────────────────────────────────────────

#ifdef __cplusplus
namespace crosspad_gui {

struct ThemeColorEntry {
    const char * name;
    uint8_t r, g, b;
};

static constexpr ThemeColorEntry s_theme_palette[] = {
    {"Neon Green",  0,   255, 136},
    {"Yellow",      208, 240, 9  },
    {"Cyan",        0,   255, 255},
    {"Purple",      234, 0,   255},
    {"Orange",      255, 140, 0  },
    {"Pink",        255, 0,   136},
    {"Blue",        0,   136, 255},
    {"Red",         255, 50,  50 },
};
static constexpr int THEME_PALETTE_COUNT = sizeof(s_theme_palette) / sizeof(s_theme_palette[0]);

// Default palette index — overridable from crosspad_theme_user.h
#ifndef CROSSPAD_THEME_DEFAULT_INDEX
#define CROSSPAD_THEME_DEFAULT_INDEX 0
#endif

lv_color_t getThemePrimary();
int         getThemeColorCount();
const char* getThemeColorName(int idx);

} // namespace crosspad_gui
#endif // __cplusplus

// ── Primary theme color (runtime-selectable) ────────────────────────────────
// If a project defines CROSSPAD_THEME_PRIMARY in crosspad_theme_user.h,
// it stays as a static color. Otherwise, use the runtime palette.
#ifndef CROSSPAD_THEME_PRIMARY
#define CROSSPAD_THEME_PRIMARY     crosspad_gui::getThemePrimary()
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
