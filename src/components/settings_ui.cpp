// SPDX-License-Identifier: GPL-3.0-or-later
#ifdef USE_LVGL

#include "crosspad-gui/components/settings_ui.h"
#include "settings_ui_internal.h"
#include "crosspad/config/crosspad_config.h"
#include "crosspad-gui/platform/IGuiPlatform.h"
#include "crosspad-gui/styles/styles.h"
#include "crosspad-gui/theme/crosspad_theme.h"
#include "crosspad/settings/CrosspadSettings.hpp"
#include "crosspad/settings/ISettingsUI.hpp"

#include <cstdio>
#include <cstring>

using namespace crosspad;

namespace crosspad_gui {

using namespace settings_detail;

// ===================================================================
// Internal state
// ===================================================================

lv_obj_t *  settings_detail::s_content  = nullptr;
lv_group_t* settings_detail::s_group    = nullptr;
int         settings_detail::s_current_category = -1;

/* A category a caller asked Settings to open on (the Firmware app's shortcut to
 * "Updates"); "" for the list. */
static constexpr size_t kOpenNextMax = 32;
static char s_open_next[kOpenNextMax];

// Extra platform-registered categories
static constexpr int MAX_EXTRA_CATEGORIES = 8;
static SettingsCategory s_extra_cats[MAX_EXTRA_CATEGORIES];
static int s_extra_cat_count = 0;

static const SettingsCategory s_builtin_categories[] = {
    { "Display",          LV_SYMBOL_EYE_OPEN,   cat_build_display },
    { "Audio",            LV_SYMBOL_AUDIO,       cat_build_audio },
    { "USB",              LV_SYMBOL_USB,         cat_build_usb },
    { "Pads & LEDs",      LV_SYMBOL_KEYBOARD,    cat_build_pads },
    { "Vibration",        LV_SYMBOL_BELL,        cat_build_vibration },
    { "MIDI",             LV_SYMBOL_SHUFFLE,     cat_build_midi },
    { "Analog / Velocity", LV_SYMBOL_KEYBOARD,   cat_build_analog },
#if FEAT_SETTINGS_BACKUP
    { "Backup (SD)",      LV_SYMBOL_SD_CARD,     cat_build_backup },
#endif
    { "Programming",      LV_SYMBOL_DOWNLOAD,    cat_build_programming },
    { "Info",             LV_SYMBOL_LIST,        cat_build_info },
    { "System",           LV_SYMBOL_SETTINGS,    cat_build_system },
};
static constexpr int s_builtin_count = sizeof(s_builtin_categories) / sizeof(s_builtin_categories[0]);

static int total_category_count() { return s_builtin_count + s_extra_cat_count; }

// STM-backed panels (Analog/Velocity, System → STM Power) only do something on
// boards whose GUI platform can write STM config registers.
bool settings_detail::stm_config_available() {
    return getGuiPlatform().hasStmConfigWrite();
}

static const SettingsCategory* get_category(int idx) {
    if (idx < s_builtin_count) return &s_builtin_categories[idx];
    int extra_idx = idx - s_builtin_count;
    if (extra_idx < s_extra_cat_count) return &s_extra_cats[extra_idx];
    return nullptr;
}

// ===================================================================
// Helper: save settings via platform callback
// ===================================================================

void settings_detail::save_settings() {
    auto* ui = getSettingsUI();
    if (ui) ui->saveSettings();
}

// ===================================================================
// Navigation helpers
// ===================================================================

static void clear_content() {
    if (!s_content) return;
    lv_obj_clean(s_content);
    if (s_group) {
        lv_group_remove_all_objs(s_group);
    }
}

static void show_categories();

static void category_row_cb(lv_event_t * e) {
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    show_detail(idx);
}

static void back_row_cb(lv_event_t * e) {
    (void)e;
    show_categories();
}

static void exit_row_cb(lv_event_t * e) {
    (void)e;
    save_settings();
    auto* ui = getSettingsUI();
    if (ui) ui->exitToHome();
}

// ---- Row builder helper for a clickable button row ----

/* Scroll the focused widget into view in one frame, and two rows past the
 * edge rather than just inside it.
 *
 * LVGL's own scroll-on-focus animates the list over 200-400 ms and every
 * frame of that redraws the whole list: 3-4 full screens per encoder step,
 * measured. `LV_ANIM_OFF` fixed that, and left a cheaper version of the same
 * shape -- `scroll_to_view` stops with the row *at* the edge, so the next
 * step is past it again and scrolls too. Every step then pays a full viewport
 * repaint: 64 476 px and 85 ms against 25 380 px and 24 ms for a step that
 * stays inside. Landing two rows in means most steps need no scroll; over a
 * full sweep of the launcher that took 116 ms a step down to 46.6. */
static constexpr int kSettingsScrollAheadPx = 64;   /* two 32 px rows */

static void scroll_focus_ahead(lv_obj_t * obj) {
    /* The scrollable ancestor, which for a row inside a section is not the
     * parent -- hence _recursive in the call this replaces. */
    lv_obj_t * list = lv_obj_get_parent(obj);
    while (list && !lv_obj_has_flag(list, LV_OBJ_FLAG_SCROLLABLE)) {
        list = lv_obj_get_parent(list);
    }
    if (!list) { lv_obj_scroll_to_view_recursive(obj, LV_ANIM_OFF); return; }

    lv_area_t rc, lc;
    lv_obj_get_coords(obj, &rc);
    lv_obj_get_content_coords(list, &lc);

    /* Positive dy moves the content down, i.e. scrolls towards the top. */
    int32_t dy;
    if (rc.y1 < lc.y1)      dy =  (lc.y1 - rc.y1) + kSettingsScrollAheadPx;
    else if (rc.y2 > lc.y2) dy = -((rc.y2 - lc.y2) + kSettingsScrollAheadPx);
    else return;            /* in view already: the two rows repaint, not the list */

    lv_obj_scroll_by_bounded(list, 0, dy, LV_ANIM_OFF);
}

void settings_detail::focus_scroll_instant(lv_obj_t * obj) {
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
    lv_obj_add_event_cb(obj, [](lv_event_t * e) {
        scroll_focus_ahead(lv_event_get_target_obj(e));
    }, LV_EVENT_FOCUSED, nullptr);
}

static lv_obj_t* make_nav_button(lv_obj_t* parent, const char* icon_text,
                                  const char* label_text, lv_color_t bg_color,
                                  lv_event_cb_t cb, void* user_data) {
    lv_obj_t * btn = lv_button_create(parent);
    lv_obj_set_size(btn, LV_PCT(100), 32);
    lv_obj_set_style_bg_color(btn, bg_color, 0);
    lv_obj_set_style_radius(btn, 8, 0);
    lv_obj_set_style_outline_width(btn, 2, LV_STATE_FOCUSED);
    lv_obj_set_style_outline_color(btn, lv_color_white(), LV_STATE_FOCUSED);

    lv_obj_t * inner = lv_obj_create(btn);
    lv_obj_set_size(inner, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_opa(inner, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(inner, 0, 0);
    lv_obj_set_style_pad_all(inner, 0, 0);
    lv_obj_set_flex_flow(inner, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(inner, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(inner, 6, 0);
    lv_obj_remove_flag(inner, (lv_obj_flag_t)(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE));
    lv_obj_add_flag(inner, LV_OBJ_FLAG_EVENT_BUBBLE);

    lv_obj_t * icon = lv_label_create(inner);
    lv_label_set_text(icon, icon_text);
    lv_obj_set_style_text_color(icon, lv_color_black(), 0);

    lv_obj_t * lbl = lv_label_create(inner);
    lv_label_set_text(lbl, label_text);
    lv_obj_set_style_text_color(lbl, lv_color_black(), 0);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);

    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, user_data);
    focus_scroll_instant(btn);
    if (s_group) lv_group_add_obj(s_group, btn);

    return btn;
}

// ===================================================================
// Show category list
// ===================================================================

static void show_categories() {
    clear_content();
    s_current_category = -1;

    // Exit button at top
    make_nav_button(s_content, LV_SYMBOL_LEFT, "Exit Settings",
                    CROSSPAD_THEME_PRIMARY, exit_row_cb, nullptr);

    // Category rows
    int total = total_category_count();
    for (int i = 0; i < total; i++) {
        const SettingsCategory* cat = get_category(i);
        if (!cat) continue;
        if (cat->build == cat_build_analog && !stm_config_available()) continue;
        if (cat->build == cat_build_usb && !getGuiPlatform().usbAudioSupported()) continue;

        lv_obj_t * row = lv_button_create(s_content);
        /* Without the theme: its 80 ms transition on every state change costs
         * three full redraws of two rows per focus step instead of one. */
        lv_obj_remove_style_all(row);
        lv_obj_set_size(row, LV_PCT(100), 36);
        lv_obj_add_style(row, &styleSettingsRow, 0);
        lv_obj_add_style(row, &styleSettingsRowFocused, LV_STATE_FOCUSED);

        lv_obj_t * inner = lv_obj_create(row);
        lv_obj_set_size(inner, LV_PCT(100), LV_PCT(100));
        lv_obj_set_style_bg_opa(inner, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(inner, 0, 0);
        lv_obj_set_style_pad_all(inner, 0, 0);
        lv_obj_set_flex_flow(inner, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(inner, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(inner, 8, 0);
        lv_obj_remove_flag(inner, (lv_obj_flag_t)(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE));
        lv_obj_add_flag(inner, LV_OBJ_FLAG_EVENT_BUBBLE);

        lv_obj_t * icon = lv_label_create(inner);
        lv_label_set_text(icon, cat->icon);
        lv_obj_set_style_text_color(icon, CROSSPAD_THEME_PRIMARY, 0);
        lv_obj_set_style_text_font(icon, &lv_font_montserrat_16, 0);

        lv_obj_t * name = lv_label_create(inner);
        lv_label_set_text(name, cat->name);
        lv_obj_set_flex_grow(name, 1);
        lv_obj_set_style_text_color(name, lv_color_white(), 0);
        lv_obj_set_style_text_font(name, &lv_font_montserrat_16, 0);

        lv_obj_t * chev = lv_label_create(inner);
        lv_label_set_text(chev, LV_SYMBOL_RIGHT);
        lv_obj_set_style_text_color(chev, lv_color_make(100, 100, 100), 0);

        lv_obj_add_event_cb(row, category_row_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        focus_scroll_instant(row);
        if (s_group) lv_group_add_obj(s_group, row);
    }

    printf("[Settings] Category list shown\n");
}

// ===================================================================
// Show detail page
// ===================================================================

void settings_detail::show_detail(int category_index) {
    if (category_index < 0 || category_index >= total_category_count()) return;

    clear_content();
    s_current_category = category_index;

    const SettingsCategory * cat = get_category(category_index);
    if (!cat) return;

    // Back button with category title
    char title_buf[64];
    snprintf(title_buf, sizeof(title_buf), "%s  %s", cat->icon, cat->name);
    make_nav_button(s_content, LV_SYMBOL_LEFT, title_buf,
                    CROSSPAD_THEME_PRIMARY, back_row_cb, nullptr);

    // Build category content
    if (cat->build) {
        cat->build(s_content, s_group);
    }

    printf("[Settings] Detail page: %s\n", cat->name);
}


// ===================================================================
// Built-in category builders
// ===================================================================

CrosspadSettings* settings_detail::get_settings() {
    return CrosspadSettings::getInstance();
}

// ===================================================================
// Public API
// ===================================================================

void settings_ui_create(lv_obj_t * content_area, lv_group_t * group) {
    s_content = content_area;
    s_group = group;
    s_current_category = -1;

    show_categories();
    if (!s_open_next[0]) return;
    const int total = total_category_count();
    for (int i = 0; i < total; i++) {
        const SettingsCategory * cat = get_category(i);
        if (cat && strcmp(cat->name, s_open_next) == 0) {
            show_detail(i);
            break;
        }
    }
    s_open_next[0] = '\0';
}

void settings_ui_open_next(const char * category) {
    snprintf(s_open_next, sizeof(s_open_next), "%s", category ? category : "");
}

bool settings_ui_back() {
    /* One level up: a category detail returns to the category list. At the
     * list there is nothing left to pop — the caller (the app's back hook)
     * decides what "back" means then, which is closing Settings. */
    if (!s_content || s_current_category < 0) return false;
    show_categories();
    return true;
}

void settings_ui_destroy() {
    save_settings();
    s_content = nullptr;
    s_group = nullptr;
    s_current_category = -1;
}

bool settings_ui_add_category(const SettingsCategory& cat) {
    if (s_extra_cat_count >= MAX_EXTRA_CATEGORIES) return false;
    s_extra_cats[s_extra_cat_count++] = cat;
    return true;
}

void settings_ui_clear_extra_categories() {
    s_extra_cat_count = 0;
}

} // namespace crosspad_gui

#endif // USE_LVGL
