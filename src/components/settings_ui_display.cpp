// SPDX-License-Identifier: GPL-3.0-or-later
//
// Settings: the Display category.

#ifdef USE_LVGL

#include "settings_ui_internal.h"
#include "crosspad/config/crosspad_config.h"
#include "crosspad-gui/components/status_bar.h"
#include "crosspad-gui/platform/IGuiPlatform.h"
#include "crosspad-gui/styles/styles.h"
#include "crosspad-gui/theme/crosspad_theme.h"
#include "crosspad/settings/CrosspadSettings.hpp"

#include <cstring>

using namespace crosspad;

namespace crosspad_gui {

using namespace settings_detail;

// ---- Display ----

static void theme_color_changed_cb(lv_event_t * e) {
    (void)e;
    reinitStyles();
    show_detail(s_current_category);
}

static void lcd_brightness_hw_cb(lv_event_t * e) {
    (void)e;
    crosspad_gui::getGuiPlatform().setLcdBrightness(get_settings()->LCDbrightness);
}

/* The home screen's style through the dropdown's uint8_t. */
static uint8_t s_launcherStyleIdx = 0;
static uint8_t s_touchSensIdx = 0;

void settings_detail::cat_build_display(lv_obj_t * parent, lv_group_t * group) {
    auto* s = get_settings();
    if (!s) return;

    settings_row_slider(parent, group, "Brightness", 5, 100,
                        &s->LCDbrightness, lcd_brightness_hw_cb);

    // Build theme color options string from palette (bounded to prevent overflow)
    static char theme_options[256];
    theme_options[0] = '\0';
    int count = crosspad_gui::getThemeColorCount();
    for (int i = 0; i < count; i++) {
        const char* name = crosspad_gui::getThemeColorName(i);
        if (!name) continue;
        size_t len = strlen(theme_options);
        size_t nameLen = strlen(name);
        size_t needed = (i > 0 ? 1 : 0) + nameLen; // separator + name
        if (len + needed >= sizeof(theme_options)) break;
        if (i > 0) theme_options[len++] = '\n';
        memcpy(theme_options + len, name, nameLen + 1);
    }

    settings_row_dropdown(parent, group, "Theme Color",
                          theme_options,
                          &s->themeColorIndex,
                          theme_color_changed_cb);

    /* In crosspad::LauncherStyle's order. The main screen is rebuilt in the
     * new style on the way back to it. */
    s_launcherStyleIdx = (uint8_t)s->launcherStyle;
    settings_row_dropdown(parent, group, "Home screen", "Icon grid\nList\nPad map",
                          &s_launcherStyleIdx,
                          [](lv_event_t *) {
                              get_settings()->launcherStyle = (crosspad::LauncherStyle)s_launcherStyleIdx;
                          });

    s_touchSensIdx = (uint8_t)s->touchSensitivity;
    settings_row_dropdown(parent, group, "Touch Sensitivity", "Low\nMedium\nHigh",
                          &s_touchSensIdx,
                          [](lv_event_t *) {
                              get_settings()->touchSensitivity = (crosspad::TouchSensitivity)s_touchSensIdx;
                              save_settings();
                              crosspad_gui::getGuiPlatform().setTouchSensitivity(get_settings()->touchSensitivity);
                          });

    settings_row_switch(parent, group, "Quick Settings Drawer",
                        &s->quickSettingsEnabled,
                        [](lv_event_t *) {
                            crosspad_gui::statusbar_set_drawer_enabled(
                                get_settings()->quickSettingsEnabled);
                        });

#if FEAT_PERF_STATS
    settings_section_header(parent, "Perf Stats");
    settings_row_bitswitch(parent, group, "FPS", &s->perfStatsFlags, PERF_FLAG_FPS,
                           [](lv_event_t *) { statusbar_set_perf_level(get_settings()->perfStatsFlags); });
    settings_row_bitswitch(parent, group, "CPU", &s->perfStatsFlags, PERF_FLAG_CPU,
                           [](lv_event_t *) { statusbar_set_perf_level(get_settings()->perfStatsFlags); });
    settings_row_bitswitch(parent, group, "Memory", &s->perfStatsFlags, PERF_FLAG_MEM,
                           [](lv_event_t *) { statusbar_set_perf_level(get_settings()->perfStatsFlags); });
#endif
}

} // namespace crosspad_gui

#endif // USE_LVGL
