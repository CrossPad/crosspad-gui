#ifdef USE_LVGL

#include "crosspad-gui/components/settings_ui.h"
#include "crosspad/config/crosspad_config.h"
#include "crosspad-gui/components/status_bar.h"
#include "crosspad-gui/components/ui_toast.h"
#include "crosspad-gui/components/ui_modal.h"
#include "crosspad-gui/components/dfu_panel.h"
#include "crosspad-gui/components/stm_pad_editor_screen.h"
#include "crosspad-gui/platform/IGuiPlatform.h"
#include "crosspad-gui/styles/styles.h"
#include "crosspad-gui/theme/crosspad_theme.h"
#include "crosspad/settings/CrosspadSettings.hpp"
#include "crosspad/settings/ISettingsUI.hpp"
#include "crosspad/settings/SettingsIo.hpp"
#include "crosspad/pad/PadLedController.hpp"
#include "crosspad/stm32/reg_map.h"
#include "crosspad/platform/IVibration.hpp"

#include <cstdio>
#include <cstring>

using namespace crosspad;

namespace crosspad_gui {

// ===================================================================
// Internal state
// ===================================================================

static lv_obj_t *  s_content  = nullptr;
static lv_group_t* s_group    = nullptr;
static int         s_current_category = -1;

// Extra platform-registered categories
static constexpr int MAX_EXTRA_CATEGORIES = 8;
static SettingsCategory s_extra_cats[MAX_EXTRA_CATEGORIES];
static int s_extra_cat_count = 0;

// ===================================================================
// Built-in category builders (forward declarations)
// ===================================================================

static void cat_build_display(lv_obj_t * parent, lv_group_t * group);
static void cat_build_audio(lv_obj_t * parent, lv_group_t * group);
static void cat_build_usb(lv_obj_t * parent, lv_group_t * group);
static void cat_build_pads(lv_obj_t * parent, lv_group_t * group);
static void cat_build_vibration(lv_obj_t * parent, lv_group_t * group);
static void cat_build_midi(lv_obj_t * parent, lv_group_t * group);
static void cat_build_analog(lv_obj_t * parent, lv_group_t * group);
#if FEAT_SETTINGS_BACKUP
static void cat_build_backup(lv_obj_t * parent, lv_group_t * group);
#endif
static void cat_build_programming(lv_obj_t * parent, lv_group_t * group);
static void cat_build_info(lv_obj_t * parent, lv_group_t * group);
static void cat_build_system(lv_obj_t * parent, lv_group_t * group);

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
static bool stm_config_available() {
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

static void save_settings() {
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
static void show_detail(int category_index);

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

static void focus_scroll_instant(lv_obj_t * obj) {
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

static void show_detail(int category_index) {
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
// Widget helpers
// ===================================================================

lv_obj_t * settings_section_header(lv_obj_t * parent, const char * text) {
    lv_obj_t * lbl = lv_label_create(parent);
    lv_label_set_text(lbl, text);
    lv_obj_add_style(lbl, &styleSettingsSection, 0);
    return lbl;
}

// ---- Switch row ----

struct SwitchRowCtx {
    bool * setting;
    lv_event_cb_t extra_cb;
};

static void switch_value_changed_cb(lv_event_t * e) {
    SwitchRowCtx * ctx = (SwitchRowCtx *)lv_event_get_user_data(e);
    lv_obj_t * sw = (lv_obj_t *)lv_event_get_target(e);
    bool checked = lv_obj_has_state(sw, LV_STATE_CHECKED);

    *(ctx->setting) = checked;
    save_settings();

    if (ctx->extra_cb) {
        ctx->extra_cb(e);
    }
}

static void switch_row_delete_cb(lv_event_t * e) {
    delete (SwitchRowCtx *)lv_event_get_user_data(e);
}

lv_obj_t * settings_row_switch(lv_obj_t * parent, lv_group_t * group,
                                const char * label, bool * setting,
                                lv_event_cb_t extra_cb) {
    lv_obj_t * row = lv_obj_create(parent);
    lv_obj_set_size(row, LV_PCT(100), 32);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_hor(row, 4, 0);
    lv_obj_set_style_pad_ver(row, 2, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t * lbl = lv_label_create(row);
    lv_label_set_text(lbl, label);
    lv_obj_set_flex_grow(lbl, 1);
    lv_obj_set_style_text_color(lbl, lv_color_white(), 0);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);

    lv_obj_t * sw = lv_switch_create(row);
    if (setting && *setting) {
        lv_obj_add_state(sw, LV_STATE_CHECKED);
    }

    SwitchRowCtx * ctx = new SwitchRowCtx{setting, extra_cb};
    lv_obj_add_event_cb(sw, switch_value_changed_cb, LV_EVENT_VALUE_CHANGED, ctx);
    lv_obj_add_event_cb(sw, switch_row_delete_cb, LV_EVENT_DELETE, ctx);

    focus_scroll_instant(sw);

    if (group) lv_group_add_obj(group, sw);

    return row;
}

// ---- Slider row ----

struct SliderRowCtx {
    uint8_t * setting;
    lv_event_cb_t extra_cb;
    lv_obj_t * value_label;
};

static void slider_value_changed_cb(lv_event_t * e) {
    SliderRowCtx * ctx = (SliderRowCtx *)lv_event_get_user_data(e);
    lv_obj_t * slider = (lv_obj_t *)lv_event_get_target(e);
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_VALUE_CHANGED) {
        int32_t val = lv_slider_get_value(slider);
        *(ctx->setting) = (uint8_t)val;
        if (ctx->value_label) {
            lv_label_set_text_fmt(ctx->value_label, "%d", (int)val);
        }
        // Live hardware apply during drag — NVS save deferred to RELEASED.
        if (ctx->extra_cb) {
            ctx->extra_cb(e);
        }
    } else if (code == LV_EVENT_RELEASED) {
        save_settings();
    }
}

static void slider_row_delete_cb(lv_event_t * e) {
    delete (SliderRowCtx *)lv_event_get_user_data(e);
}

lv_obj_t * settings_row_slider(lv_obj_t * parent, lv_group_t * group,
                                const char * label, int min, int max,
                                uint8_t * setting,
                                lv_event_cb_t extra_cb) {
    lv_obj_t * label_row = lv_obj_create(parent);
    lv_obj_set_size(label_row, LV_PCT(100), 18);
    lv_obj_set_style_bg_opa(label_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(label_row, 0, 0);
    lv_obj_set_style_pad_all(label_row, 0, 0);
    lv_obj_set_style_pad_hor(label_row, 4, 0);
    lv_obj_set_flex_flow(label_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(label_row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(label_row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t * lbl = lv_label_create(label_row);
    lv_label_set_text(lbl, label);
    lv_obj_set_flex_grow(lbl, 1);
    lv_obj_set_style_text_color(lbl, lv_color_white(), 0);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);

    lv_obj_t * val_lbl = lv_label_create(label_row);
    lv_label_set_text_fmt(val_lbl, "%d", setting ? (int)*setting : 0);
    lv_obj_set_style_text_color(val_lbl, CROSSPAD_THEME_PRIMARY, 0);
    lv_obj_set_style_text_font(val_lbl, &lv_font_montserrat_14, 0);

    lv_obj_t * slider = lv_slider_create(parent);
    lv_obj_set_size(slider, LV_PCT(95), 10);
    lv_slider_set_range(slider, min, max);
    if (setting) lv_slider_set_value(slider, *setting, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(slider, lv_color_make(60, 60, 60), LV_PART_MAIN);
    lv_obj_set_style_bg_color(slider, CROSSPAD_THEME_PRIMARY, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(slider, lv_color_white(), LV_PART_KNOB);
    lv_obj_set_style_pad_all(slider, 3, LV_PART_KNOB);

    SliderRowCtx * ctx = new SliderRowCtx{setting, extra_cb, val_lbl};
    lv_obj_add_event_cb(slider, slider_value_changed_cb, LV_EVENT_VALUE_CHANGED, ctx);
    lv_obj_add_event_cb(slider, slider_value_changed_cb, LV_EVENT_RELEASED, ctx);
    lv_obj_add_event_cb(slider, slider_row_delete_cb, LV_EVENT_DELETE, ctx);

    focus_scroll_instant(slider);

    if (group) lv_group_add_obj(group, slider);

    return label_row;
}

// ---- Info row ----

lv_obj_t * settings_row_info(lv_obj_t * parent, const char * label, const char * value) {
    lv_obj_t * row = lv_obj_create(parent);
    lv_obj_set_size(row, LV_PCT(100), 28);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_hor(row, 4, 0);
    lv_obj_set_style_pad_ver(row, 2, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t * lbl = lv_label_create(row);
    lv_label_set_text(lbl, label);
    lv_obj_set_flex_grow(lbl, 1);
    lv_obj_set_style_text_color(lbl, lv_color_make(160, 160, 160), 0);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);

    lv_obj_t * val = lv_label_create(row);
    lv_label_set_text(val, value);
    lv_obj_set_style_text_color(val, lv_color_white(), 0);
    lv_obj_set_style_text_font(val, &lv_font_montserrat_14, 0);

    return row;
}

// ---- Action button row ----

lv_obj_t * settings_row_action(lv_obj_t * parent, lv_group_t * group,
                                const char * label, const char * btn_text,
                                lv_event_cb_t callback, void * user_data) {
    lv_obj_t * row = lv_obj_create(parent);
    lv_obj_set_size(row, LV_PCT(100), 36);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_hor(row, 4, 0);
    lv_obj_set_style_pad_ver(row, 2, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    if (label) {
        lv_obj_t * lbl = lv_label_create(row);
        lv_label_set_text(lbl, label);
        lv_obj_set_flex_grow(lbl, 1);
        lv_obj_set_style_text_color(lbl, lv_color_white(), 0);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
    }

    lv_obj_t * btn = lv_button_create(row);
    lv_obj_set_size(btn, LV_SIZE_CONTENT, 28);
    lv_obj_set_style_bg_color(btn, lv_color_make(60, 60, 60), 0);
    lv_obj_set_style_radius(btn, 6, 0);
    lv_obj_set_style_pad_hor(btn, 12, 0);
    lv_obj_set_style_outline_width(btn, 2, LV_STATE_FOCUSED);
    lv_obj_set_style_outline_color(btn, CROSSPAD_THEME_PRIMARY, LV_STATE_FOCUSED);

    lv_obj_t * btn_lbl = lv_label_create(btn);
    lv_label_set_text(btn_lbl, btn_text);
    lv_obj_set_style_text_color(btn_lbl, lv_color_white(), 0);
    lv_obj_set_style_text_font(btn_lbl, &lv_font_montserrat_14, 0);
    lv_obj_center(btn_lbl);

    if (callback) {
        lv_obj_add_event_cb(btn, callback, LV_EVENT_CLICKED, user_data);
    }
    focus_scroll_instant(btn);
    if (group) lv_group_add_obj(group, btn);

    return row;
}

// ---- Dropdown row ----

struct DropdownRowCtx {
    uint8_t * setting;
    lv_event_cb_t extra_cb;
};

static void dropdown_value_changed_cb(lv_event_t * e) {
    DropdownRowCtx * ctx = (DropdownRowCtx *)lv_event_get_user_data(e);
    lv_obj_t * dd = (lv_obj_t *)lv_event_get_target(e);
    uint16_t sel = lv_dropdown_get_selected(dd);

    *(ctx->setting) = (uint8_t)sel;
    save_settings();

    if (ctx->extra_cb) {
        ctx->extra_cb(e);
    }
}

static void dropdown_row_delete_cb(lv_event_t * e) {
    delete (DropdownRowCtx *)lv_event_get_user_data(e);
}

lv_obj_t * settings_row_dropdown(lv_obj_t * parent, lv_group_t * group,
                                  const char * label, const char * options,
                                  uint8_t * setting,
                                  lv_event_cb_t extra_cb) {
    lv_obj_t * row = lv_obj_create(parent);
    lv_obj_set_size(row, LV_PCT(100), 36);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_hor(row, 4, 0);
    lv_obj_set_style_pad_ver(row, 2, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t * lbl = lv_label_create(row);
    lv_label_set_text(lbl, label);
    lv_obj_set_flex_grow(lbl, 1);
    lv_obj_set_style_text_color(lbl, lv_color_white(), 0);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);

    lv_obj_t * dd = lv_dropdown_create(row);
    lv_dropdown_set_options(dd, options);
    if (setting) lv_dropdown_set_selected(dd, *setting);
    lv_obj_set_width(dd, 90);
    lv_obj_set_style_text_font(dd, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_font(dd, &lv_font_montserrat_14, LV_PART_INDICATOR);
    lv_obj_set_style_pad_ver(dd, 4, 0);
    lv_obj_set_style_bg_color(dd, lv_color_make(60, 60, 60), 0);
    lv_obj_set_style_text_color(dd, lv_color_white(), 0);
    lv_obj_set_style_border_color(dd, lv_color_make(80, 80, 80), 0);
    lv_obj_set_style_outline_width(dd, 2, LV_STATE_FOCUSED);
    lv_obj_set_style_outline_color(dd, CROSSPAD_THEME_PRIMARY, LV_STATE_FOCUSED);

    // Style the dropdown list
    lv_obj_t * list = lv_dropdown_get_list(dd);
    if (list) {
        lv_obj_set_style_text_font(list, &lv_font_montserrat_14, 0);
        lv_obj_set_style_bg_color(list, lv_color_make(40, 40, 40), 0);
        lv_obj_set_style_text_color(list, lv_color_white(), 0);
        lv_obj_set_style_bg_color(list, CROSSPAD_THEME_PRIMARY,
                                  (lv_style_selector_t)LV_PART_SELECTED | LV_STATE_CHECKED);
    }

    DropdownRowCtx * ctx = new DropdownRowCtx{setting, extra_cb};
    lv_obj_add_event_cb(dd, dropdown_value_changed_cb, LV_EVENT_VALUE_CHANGED, ctx);
    lv_obj_add_event_cb(dd, dropdown_row_delete_cb, LV_EVENT_DELETE, ctx);

    focus_scroll_instant(dd);

    if (group) lv_group_add_obj(group, dd);

    return row;
}

// ---- Bit-switch row (toggle a single bit in a uint8_t bitfield) ----

struct BitSwitchRowCtx {
    uint8_t * flags;
    uint8_t bit;
    lv_event_cb_t extra_cb;
};

static void bitswitch_value_changed_cb(lv_event_t * e) {
    BitSwitchRowCtx * ctx = (BitSwitchRowCtx *)lv_event_get_user_data(e);
    lv_obj_t * sw = (lv_obj_t *)lv_event_get_target(e);
    bool checked = lv_obj_has_state(sw, LV_STATE_CHECKED);

    if (checked) *(ctx->flags) |= ctx->bit;
    else         *(ctx->flags) &= ~ctx->bit;

    save_settings();

    if (ctx->extra_cb) ctx->extra_cb(e);
}

static void bitswitch_row_delete_cb(lv_event_t * e) {
    delete (BitSwitchRowCtx *)lv_event_get_user_data(e);
}

static lv_obj_t * settings_row_bitswitch(lv_obj_t * parent, lv_group_t * group,
                                          const char * label, uint8_t * flags, uint8_t bit,
                                          lv_event_cb_t extra_cb = nullptr) {
    lv_obj_t * row = lv_obj_create(parent);
    lv_obj_set_size(row, LV_PCT(100), 32);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_hor(row, 4, 0);
    lv_obj_set_style_pad_ver(row, 2, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t * lbl = lv_label_create(row);
    lv_label_set_text(lbl, label);
    lv_obj_set_flex_grow(lbl, 1);
    lv_obj_set_style_text_color(lbl, lv_color_white(), 0);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);

    lv_obj_t * sw = lv_switch_create(row);
    if (flags && (*flags & bit)) {
        lv_obj_add_state(sw, LV_STATE_CHECKED);
    }

    BitSwitchRowCtx * ctx = new BitSwitchRowCtx{flags, bit, extra_cb};
    lv_obj_add_event_cb(sw, bitswitch_value_changed_cb, LV_EVENT_VALUE_CHANGED, ctx);
    lv_obj_add_event_cb(sw, bitswitch_row_delete_cb, LV_EVENT_DELETE, ctx);

    focus_scroll_instant(sw);

    if (group) lv_group_add_obj(group, sw);
    return row;
}

// ===================================================================
// Built-in category builders
// ===================================================================

static CrosspadSettings* get_settings() {
    return CrosspadSettings::getInstance();
}

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

/* The launcher's style through the dropdown's uint8_t; the launcher is
 * rebuilt on the way back to it, which is when the new style shows. */
static uint8_t s_launcherStyleIdx = 0;

static void cat_build_display(lv_obj_t * parent, lv_group_t * group) {
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

    s_launcherStyleIdx = (uint8_t)s->launcherStyle;
    settings_row_dropdown(parent, group, "Launcher", "Icon grid\nList",
                          &s_launcherStyleIdx,
                          [](lv_event_t *) {
                              get_settings()->launcherStyle = (crosspad::LauncherStyle)s_launcherStyleIdx;
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

// ---- Audio ----

// Audio engine dropdown index storage. Initialized from current settings on
// each panel build. extra_cb writes the resolved kSampleRates[]/kFrameCounts[]
// value back into audioEngine on change. Restart required to take effect.
static uint8_t s_audioEngineSrIdx  = 1;
static uint8_t s_audioEngineFcIdx  = 2;

static void audio_engine_sr_cb(lv_event_t * e) {
    (void)e;
    auto* s = get_settings();
    if (!s) return;
    if (s_audioEngineSrIdx < AudioEngineSettings::kSampleRatesCount) {
        s->audioEngine.sampleRate = AudioEngineSettings::kSampleRates[s_audioEngineSrIdx];
    }
}

// Speaker amplifier master switch. ON does not power the amp permanently — it
// permits Stm32Manager's SpeakerAmpGate to raise REG_CTL_AUDIO_EN while the
// output bus carries signal and to drop it after the quiet hold, so an idle
// device draws no amp current. OFF forces it down immediately.
static void speaker_amp_cb(lv_event_t * e) {
    (void)e;
    auto* s = get_settings();
    if (!s) return;
    // The gate itself reads speakersEnabled from settings on its own tick
    // (Stm32Manager), so the UI only mirrors the field and kills the amp at once
    // when switching off — waiting 100 ms for the poll would be audible.
    s->stm.audioEn = s->masterFX.speakersEnabled ? 1 : 0;
    if (!s->masterFX.speakersEnabled) {
        getGuiPlatform().writeStmConfig8(REG_CTL_AUDIO_EN, 0);
    } else {
        getGuiPlatform().setSpeakerVolume(s->masterFX.outVolumeCodec[0][0]);
    }
}

// The device enumerates one descriptor set at a time — CDC+MIDI or MIDI+UAC2 —
// and only one of them carries audio, so this is a USB profile choice, not an
// audio feature. s_usbModeIdx shadows the persisted bool because the dropdown
// row helper writes a uint8_t index.
static uint8_t s_usbModeIdx = 0;

static void usb_mode_cb(lv_event_t * e) {
    (void)e;
    auto* s = get_settings();
    if (!s) return;
    bool audio = (s_usbModeIdx != 0);
    if (audio == s->usbAudioEnabled) return;
    s->usbAudioEnabled = audio;
    // The row helper saves the shadow index before calling us; the bool it
    // stands for still needs its own store or the choice dies at reboot.
    save_settings();
    getGuiPlatform().setUsbAudioEnabled(audio);
    ui_toast_show(audio ? "USB Audio on — device re-enumerating"
                        : "USB Serial+MIDI on — device re-enumerating",
                  2500, UI_TOAST_INFO);
    show_detail(s_current_category);
}

static void speaker_volume_cb(lv_event_t * e) {
    (void)e;
    auto* s = get_settings();
    if (s) crosspad_gui::getGuiPlatform().setSpeakerVolume(s->masterFX.outVolumeCodec[0][0]);
}

static void headphone_volume_cb(lv_event_t * e) {
    (void)e;
    auto* s = get_settings();
    if (s) crosspad_gui::getGuiPlatform().setHeadphoneVolume(s->masterFX.outVolumeCodec[1][0]);
}

static void audio_engine_fc_cb(lv_event_t * e) {
    (void)e;
    auto* s = get_settings();
    if (!s) return;
    if (s_audioEngineFcIdx < AudioEngineSettings::kFrameCountsCount) {
        s->audioEngine.frameCount = AudioEngineSettings::kFrameCounts[s_audioEngineFcIdx];
    }
}

// ---- USB Audio routing ----
//
// Mirrors of the live codec routing, refreshed from the platform on every
// panel build. They are UI shadows, not settings: only the chosen preset is
// persisted (CrosspadSettings::usbAudioPreset) and re-applied at boot, because
// the individual codec registers are write-mostly and a half-restored routing
// is worse than none.
static uint8_t s_routePresetIdx  = 0;
static uint8_t s_routeCaptureIdx = 0;
static uint8_t s_routeAdcIdx[2]  = {0, 0};
static uint8_t s_routeDacIdx[2]  = {2, 2};
static bool    s_routeMute[2]    = {false, false};

// The dropdowns themselves, so a change on one row can correct the others in
// place. Rebuilding the panel from a row's own VALUE_CHANGED handler would
// delete the widget mid-dispatch. Rewritten on every build of this panel and
// only ever read from those widgets' own callbacks, so they cannot outlive it.
static lv_obj_t * s_routePresetDd  = nullptr;
static lv_obj_t * s_routeCaptureDd = nullptr;
static lv_obj_t * s_routeAdcDd[2]  = {nullptr, nullptr};

// Order must match the platform's preset table; the trailing entry is the
// read-only "no preset describes this" state.
static const char * kRoutePresetOptions =
    "Microphones\nLine in\nSpeaker bus\nHeadphone bus\nMix (mics + line)\nSampler\nCustom";
static constexpr uint8_t kRoutePresetCustomIdx = 6;

// The dropdown widget inside a row built by settings_row_dropdown(): label
// first, dropdown second.
static lv_obj_t * row_dropdown(lv_obj_t * row) {
    return row ? lv_obj_get_child(row, 1) : nullptr;
}

static void dd_select(lv_obj_t * dd, uint8_t idx) {
    if (dd) lv_dropdown_set_selected(dd, idx);
}

// Pull the live routing back into the widgets after any row changed it, and
// persist the preset the device ended up in (including "custom", so boot does
// not overwrite a hand-tuned routing with a preset the user moved away from).
static void route_sync_from_device() {
    AudioRouteState st{};
    if (!getGuiPlatform().getAudioRoute(st)) return;

    s_routePresetIdx  = (st.preset >= kRoutePresetCustomIdx) ? kRoutePresetCustomIdx : st.preset;
    s_routeCaptureIdx = st.captureCodec <= 1 ? st.captureCodec : 0;
    for (int c = 0; c < 2; ++c) {
        s_routeAdcIdx[c] = st.adcInput[c] <= 2 ? st.adcInput[c] : 0;
    }

    dd_select(s_routePresetDd, s_routePresetIdx);
    dd_select(s_routeCaptureDd, s_routeCaptureIdx);
    dd_select(s_routeAdcDd[0], s_routeAdcIdx[0]);
    dd_select(s_routeAdcDd[1], s_routeAdcIdx[1]);

    if (auto* s = get_settings()) {
        s->usbAudioPreset = (s_routePresetIdx >= kRoutePresetCustomIdx)
                              ? kAudioRoutePresetCustom : s_routePresetIdx;
        // The row helpers save before invoking extra_cb, so the write above
        // would otherwise miss its own store and the choice would not survive
        // a reboot.
        save_settings();
    }
}

// Applies immediately; the row helper has already written and saved the value.
// dB rather than percent: the digital capture tap has none of the codec ADC's
// input gain, and the ~11 dB a good recording level needs is past what a percent
// fits in a byte. The mixer clamps its output, so overdriving distorts.
static void daw_gain_cb(lv_event_t * e) {
    (void)e;
    if (auto* s = get_settings()) {
        crosspad_gui::getGuiPlatform().setDawOutputGain(s->usbAudioGainDb);
    }
}

static void route_preset_cb(lv_event_t * e) {
    (void)e;
    if (s_routePresetIdx >= kRoutePresetCustomIdx) {
        // "Custom" is a report, not a destination — re-applying it would mean
        // picking an arbitrary routing on the user's behalf. Snap back.
        route_sync_from_device();
        return;
    }
    getGuiPlatform().setAudioRoutePreset(s_routePresetIdx);
    route_sync_from_device();
}

static void route_capture_cb(lv_event_t * e) {
    (void)e;
    getGuiPlatform().setAudioCaptureCodec(s_routeCaptureIdx);
    route_sync_from_device();
}

static void route_adc0_cb(lv_event_t * e) {
    (void)e;
    getGuiPlatform().setAudioAdcInput(0, s_routeAdcIdx[0]);
    route_sync_from_device();
}

static void route_adc1_cb(lv_event_t * e) {
    (void)e;
    getGuiPlatform().setAudioAdcInput(1, s_routeAdcIdx[1]);
    route_sync_from_device();
}

// DAC codes are 1-based (1=LINE1 2=LINE2 3=ALL); the dropdown is 0-based.
// Output routing is independent of the capture path, so it never changes the
// preset.
static void route_dac0_cb(lv_event_t * e) {
    (void)e;
    getGuiPlatform().setAudioDacOutput(0, (uint8_t)(s_routeDacIdx[0] + 1));
}

static void route_dac1_cb(lv_event_t * e) {
    (void)e;
    getGuiPlatform().setAudioDacOutput(1, (uint8_t)(s_routeDacIdx[1] + 1));
}

static void route_mute0_cb(lv_event_t * e) {
    (void)e;
    getGuiPlatform().setAudioCodecMute(0, s_routeMute[0]);
}

static void route_mute1_cb(lv_event_t * e) {
    (void)e;
    getGuiPlatform().setAudioCodecMute(1, s_routeMute[1]);
}

// Builds the routing rows from the live state, so the panel always shows what
// the codecs are actually doing — including a routing a HIL script or a host
// tool set over SysEx behind the UI's back.
static void build_audio_route_rows(lv_obj_t * parent, lv_group_t * group) {
    s_routePresetDd = s_routeCaptureDd = s_routeAdcDd[0] = s_routeAdcDd[1] = nullptr;

    AudioRouteState st{};
    if (!getGuiPlatform().getAudioRoute(st)) return;

    s_routePresetIdx  = (st.preset >= kRoutePresetCustomIdx) ? kRoutePresetCustomIdx : st.preset;
    s_routeCaptureIdx = st.captureCodec <= 1 ? st.captureCodec : 0;
    for (int c = 0; c < 2; ++c) {
        s_routeAdcIdx[c] = st.adcInput[c] <= 2 ? st.adcInput[c] : 0;
        s_routeDacIdx[c] = (st.dacOutput[c] >= 1 && st.dacOutput[c] <= 3)
                             ? (uint8_t)(st.dacOutput[c] - 1) : 2;
        s_routeMute[c]   = st.mute[c] != 0;
    }

    s_routePresetDd = row_dropdown(
        settings_row_dropdown(parent, group, "Recording Source",
                              kRoutePresetOptions, &s_routePresetIdx, route_preset_cb));

    if (auto* cfg = get_settings()) {
        settings_row_slider(parent, group, "DAW Output dB", 0, 36,
                            &cfg->usbAudioGainDb, daw_gain_cb);
    }

    settings_section_header(parent, "Routing (advanced)");

    s_routeCaptureDd = row_dropdown(
        settings_row_dropdown(parent, group, "Capture Codec",
                              "0 - Speakers\n1 - Headphones",
                              &s_routeCaptureIdx, route_capture_cb));
    s_routeAdcDd[0] = row_dropdown(
        settings_row_dropdown(parent, group, "Codec 0 Input",
                              "Differential\nLINE1 (loop)\nLINE2 (mics)",
                              &s_routeAdcIdx[0], route_adc0_cb));
    s_routeAdcDd[1] = row_dropdown(
        settings_row_dropdown(parent, group, "Codec 1 Input",
                              "Differential\nLINE1 (loop)\nLINE2 (line in)",
                              &s_routeAdcIdx[1], route_adc1_cb));
    settings_row_dropdown(parent, group, "Codec 0 Output",
                          "LINE1\nLINE2\nBoth",
                          &s_routeDacIdx[0], route_dac0_cb);
    settings_row_dropdown(parent, group, "Codec 1 Output",
                          "LINE1\nLINE2\nBoth",
                          &s_routeDacIdx[1], route_dac1_cb);
    settings_row_switch(parent, group, "Mute Codec 0", &s_routeMute[0], route_mute0_cb);
    settings_row_switch(parent, group, "Mute Codec 1", &s_routeMute[1], route_mute1_cb);
}

static void cat_build_audio(lv_obj_t * parent, lv_group_t * group) {
    auto* s = get_settings();
    if (!s) return;

    settings_row_switch(parent, group, "Audio Engine",
                        &s->AudioEngineEnabled);

    settings_section_header(parent, "Engine (restart required)");

    s_audioEngineSrIdx = 1;
    for (uint8_t i = 0; i < AudioEngineSettings::kSampleRatesCount; ++i) {
        if (AudioEngineSettings::kSampleRates[i] == s->audioEngine.sampleRate) {
            s_audioEngineSrIdx = i; break;
        }
    }
    settings_row_dropdown(parent, group, "Sample Rate",
                          "44100\n48000\n96000",
                          &s_audioEngineSrIdx, audio_engine_sr_cb);

    s_audioEngineFcIdx = 2;
    for (uint8_t i = 0; i < AudioEngineSettings::kFrameCountsCount; ++i) {
        if (AudioEngineSettings::kFrameCounts[i] == s->audioEngine.frameCount) {
            s_audioEngineFcIdx = i; break;
        }
    }
    settings_row_dropdown(parent, group, "Frame Count",
                          "32\n48\n64\n128\n256\n512",
                          &s_audioEngineFcIdx, audio_engine_fc_cb);

    settings_section_header(parent, "Output Volume");

    settings_row_switch(parent, group, "Speakers (auto on play)",
                        &s->masterFX.speakersEnabled, speaker_amp_cb);

    // Codec volumes are the only ones on a real signal path (the drawer and the
    // volume overlay drive the same fields). masterFX.inVolume/outVolume and the
    // per-effect bypass flags have no consumer on any platform — they are not
    // exposed here so the panel cannot promise what the firmware does not do.
    settings_row_slider(parent, group, "Speakers", 0, 100,
                        &s->masterFX.outVolumeCodec[0][0], speaker_volume_cb);

    settings_row_slider(parent, group, "Headphones", 0, 100,
                        &s->masterFX.outVolumeCodec[1][0], headphone_volume_cb);
}

// ---- USB ----

static void usb_transfer_confirm_cb(bool confirmed, void * user_data) {
    (void)user_data;
    if (confirmed) getGuiPlatform().requestUsbTransfer();
}

static void usb_transfer_start_cb(lv_event_t * e) {
    (void)e;
    ui_modal_confirm("SD card as USB drive",
                     "The CrossPad restarts and audio stops. Your computer will see "
                     "the SD card as a USB drive.",
                     "Restart", "Cancel", usb_transfer_confirm_cb, nullptr, s_group);
}

static void cat_build_usb(lv_obj_t * parent, lv_group_t * group) {
    auto* s = get_settings();
    if (!s) return;

    // Persisted preference: re-applied on the next boot, so this row is both
    // the live switch and the default profile.
    s_usbModeIdx = s->usbAudioEnabled ? 1 : 0;
    lv_obj_t * mode_row = settings_row_dropdown(parent, group, "Mode",
                                                "MIDI + Serial\nMIDI + Audio",
                                                &s_usbModeIdx, usb_mode_cb);
    // The shared 90 px default clips both labels, and a mode you cannot read
    // is the reason this row exists.
    if (lv_obj_t * dd = row_dropdown(mode_row)) lv_obj_set_width(dd, 140);

    settings_row_info(parent, "Active", s->usbAudioEnabled ? "MIDI + Audio (UAC2)"
                                                           : "MIDI + Serial (CDC)");
    // Audio mode has no CDC — five IN endpoints, and MIDI plus UAC2 claim them
    // all — so say what the switch costs before it is thrown.
    settings_row_info(parent, "Serial console / OTA",
                      s->usbAudioEnabled ? "unavailable" : "available");
    // A host asks for the serial profile over SysEx; with this on, the device
    // confirms first (the platform raises the prompt). The row helper persists.
    settings_row_switch(parent, group, "Ask before USB serial", &s->usbSerialGuard, nullptr);

    if (getGuiPlatform().usbTransferSupported()) {
        settings_row_action(parent, group, "SD card as USB drive", "Start",
                            usb_transfer_start_cb);
    }

    // Routing is live in both USB profiles — the codecs do not care which
    // descriptor set is enumerated — so the rows stay usable while the device
    // is still on CDC+MIDI and the choice is ready before the switch.
    // audioRouteSupported() gates them for boards without it.
    if (getGuiPlatform().audioRouteSupported()) {
        build_audio_route_rows(parent, group);
    }
}

// ---- Pads & LEDs ----

static void rgb_brightness_hw_cb(lv_event_t * e) {
    (void)e;
    crosspad::getPadLedController().setBrightness(get_settings()->RGBbrightness);
}

static void cat_build_pads(lv_obj_t * parent, lv_group_t * group) {
    auto* s = get_settings();
    if (!s) return;

    settings_row_slider(parent, group, "RGB Brightness", 0, 255,
                        &s->RGBbrightness, rgb_brightness_hw_cb);

    settings_section_header(parent, "Behavior");

    // Gates pad→MIDI in PadManager. Was unreachable from the UI until now
    // (System → "STM Keypad Scan" is a different thing: the STM scan enable).
    settings_row_switch(parent, group, "Pad MIDI Output",
                        &s->keypad.enableKeypad);

    settings_row_switch(parent, group, "Note Off on Release",
                        &s->keypad.noteOffOnRelease);

    // Consumed by the sequencer app (top row = transport/function keys).
    settings_row_switch(parent, group, "Upper Row Functions",
                        &s->keypad.upperRowFunctions);
}

// ---- Vibration ----

static lv_obj_t * s_vibration_dependent = nullptr;

static void vibration_apply_cb(lv_event_t * e)
{
    (void)e;
    if (auto * s = get_settings()) {
        crosspad::getVibration().applySettings(s->vibration);
    }
}

static void vibration_enable_cb(lv_event_t * e) {
    if (!s_vibration_dependent) return;
    if (get_settings()->vibration.enable) {
        lv_obj_remove_flag(s_vibration_dependent, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_vibration_dependent, LV_OBJ_FLAG_HIDDEN);
    }
    vibration_apply_cb(e);
}

static void cat_build_vibration(lv_obj_t * parent, lv_group_t * group) {
    auto* s = get_settings();
    if (!s) return;

    settings_row_switch(parent, group, "Vibration Enable",
                        &s->vibration.enable, vibration_enable_cb);

    // Container for dependent settings (hidden when vibration disabled)
    s_vibration_dependent = lv_obj_create(parent);
    lv_obj_set_size(s_vibration_dependent, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(s_vibration_dependent, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_vibration_dependent, 0, 0);
    lv_obj_set_style_pad_all(s_vibration_dependent, 0, 0);
    lv_obj_set_style_pad_row(s_vibration_dependent, 4, 0);
    lv_obj_set_flex_flow(s_vibration_dependent, LV_FLEX_FLOW_COLUMN);

    if (!s->vibration.enable) {
        lv_obj_add_flag(s_vibration_dependent, LV_OBJ_FLAG_HIDDEN);
    }

    settings_row_switch(s_vibration_dependent, group, "On Touch",
                        &s->vibration.enableVibrationOnTouch, vibration_apply_cb);

    settings_row_switch(s_vibration_dependent, group, "On Error",
                        &s->vibration.enableVibrationOnError, vibration_apply_cb);

    settings_row_switch(s_vibration_dependent, group, "Audio to Vibe",
                        &s->vibration.enableAudioToVibe, vibration_apply_cb);

    settings_section_header(s_vibration_dependent, "Input Range");

    settings_row_slider(s_vibration_dependent, group, "Input Min", 0, 255,
                        &s->vibration.inputMin, vibration_apply_cb);

    settings_row_slider(s_vibration_dependent, group, "Input Max", 0, 255,
                        &s->vibration.inputMax, vibration_apply_cb);

    settings_section_header(s_vibration_dependent, "Output Range");

    settings_row_slider(s_vibration_dependent, group, "Output Min", 0, 255,
                        &s->vibration.outputMin, vibration_apply_cb);

    settings_row_slider(s_vibration_dependent, group, "Output Max", 0, 255,
                        &s->vibration.outputMax, vibration_apply_cb);

    settings_row_switch(s_vibration_dependent, group, "Debug Mode",
                        &s->vibration.debugging, vibration_apply_cb);
}

// ---- MIDI ----

// STM note-offset presets (semitones). int8_t field <- dropdown index.
static const int8_t kNoteOffsetVals[] = { -24, -12, -7, -5, 0, 5, 7, 12, 24 };
static uint8_t s_noteOffsetIdx = 4;  // default index -> 0 semitones

static void stm_midi_chan_cb(lv_event_t*) {
    auto* s = get_settings(); if (!s) return;
    crosspad_gui::getGuiPlatform().writeStmConfig8(REG_CTL_MIDI_CHAN, s->stm.midiChannel);
}
static void stm_note_offset_cb(lv_event_t*) {
    auto* s = get_settings(); if (!s) return;
    if (s_noteOffsetIdx >= (sizeof(kNoteOffsetVals)/sizeof(kNoteOffsetVals[0]))) s_noteOffsetIdx = 4;
    s->stm.noteOffset = kNoteOffsetVals[s_noteOffsetIdx];
    crosspad_gui::getGuiPlatform().writeStmConfig8(REG_CTL_NOTE_OFFSET, (uint8_t)s->stm.noteOffset);
}

static void cat_build_midi(lv_obj_t * parent, lv_group_t * group) {
    auto* s = get_settings();
    if (!s) return;

    settings_section_header(parent, "Keypad Routing");

    settings_row_switch(parent, group, "Send to STM",
                        &s->keypad.send2STM);

    settings_row_switch(parent, group, "Send to BLE",
                        &s->keypad.send2BLE);

    settings_row_switch(parent, group, "Send to USB",
                        &s->keypad.send2USB);

    settings_row_switch(parent, group, "Send CC",
                        &s->keypad.sendCC);

    settings_section_header(parent, "STM32 Routing");

    settings_row_switch(parent, group, "Routing Enabled",
                        &s->stmRouting.RoutingMatrixEnabled);

    settings_row_switch(parent, group, "MIDI -> Thru",
                        &s->stmRouting.Midi2MidiThru);

    settings_row_switch(parent, group, "MIDI -> USB",
                        &s->stmRouting.Midi2USB);

    settings_row_switch(parent, group, "MIDI -> ESP",
                        &s->stmRouting.Midi2ESP);

    settings_row_switch(parent, group, "USB -> MIDI",
                        &s->stmRouting.USB2Midi);

    settings_row_switch(parent, group, "USB -> Thru",
                        &s->stmRouting.USB2USBThru);

    settings_row_switch(parent, group, "USB -> ESP",
                        &s->stmRouting.USB2ESP);

    settings_row_switch(parent, group, "ESP -> MIDI",
                        &s->stmRouting.ESP2Midi);

    settings_row_switch(parent, group, "ESP -> USB",
                        &s->stmRouting.ESP2USB);

    settings_row_switch(parent, group, "ESP -> Thru",
                        &s->stmRouting.ESP2ESPThru);

    settings_row_switch(parent, group, "Buttons -> MIDI",
                        &s->stmRouting.Buttons2Midi);

    settings_row_switch(parent, group, "Buttons -> USB",
                        &s->stmRouting.Buttons2USB);

    settings_row_switch(parent, group, "Buttons -> ESP",
                        &s->stmRouting.Buttons2ESP);

    settings_row_switch(parent, group, "Config Btns as MIDI",
                        &s->stmRouting.ConfigButtonsAsMidi);

    settings_section_header(parent, "ESP32 Routing");

    // BLE2Audio / Midi2Audio have no consumer in the firmware — only the two
    // forwarding paths below are wired (MidiInputHandler).
    settings_row_switch(parent, group, "BLE -> MIDI",
                        &s->esp32Routing.BLE2Midi);

    settings_row_switch(parent, group, "MIDI -> BLE",
                        &s->esp32Routing.Midi2BLE);

    settings_section_header(parent, "STM Pads (MIDI)");
    // MIDI channel 0..15 (display 1..16 is fine to leave as 0..15 raw)
    settings_row_slider(parent, group, "STM MIDI Channel", 0, 15,
                        &s->stm.midiChannel, stm_midi_chan_cb);
    // Note offset preset dropdown -> kNoteOffsetVals
    {
        // sync proxy index from the stored value
        s_noteOffsetIdx = 4;
        for (uint8_t i = 0; i < sizeof(kNoteOffsetVals)/sizeof(kNoteOffsetVals[0]); ++i)
            if (kNoteOffsetVals[i] == s->stm.noteOffset) { s_noteOffsetIdx = i; break; }
        settings_row_dropdown(parent, group, "Note Offset",
                              "-24\n-12\n-7\n-5\n0\n+5\n+7\n+12\n+24",
                              &s_noteOffsetIdx, stm_note_offset_cb);
    }
}

// ---- Analog / Velocity ----

static bool s_panCcEnProxy = false;

static void stm_vel_curve_cb(lv_event_t*) {
    auto* s = get_settings(); if (!s) return;
    crosspad_gui::getGuiPlatform().writeStmConfig8(REG_CTL_VEL_CURVE_DEF, s->stm.velCurveDef);
}
static void stm_vel_floor_cb(lv_event_t*) {
    auto* s = get_settings(); if (!s) return;
    crosspad_gui::getGuiPlatform().writeStmConfig8(REG_CTL_VEL_FLOOR_DEF, s->stm.velFloorDef);
}
static void stm_cc_press_cb(lv_event_t*) {
    auto* s = get_settings(); if (!s) return;
    crosspad_gui::getGuiPlatform().writeStmConfig8(REG_CTL_CC_PRESS_BASE, s->stm.ccPressBase);
}
static void stm_pan_en_cb(lv_event_t*) {
    auto* s = get_settings(); if (!s) return;
    s->stm.panCcEn = s_panCcEnProxy ? 1 : 0;
    crosspad_gui::getGuiPlatform().writeStmConfig8(REG_CTL_PAN_CC_EN, s->stm.panCcEn);
}
static void stm_cc_pan_cb(lv_event_t*) {
    auto* s = get_settings(); if (!s) return;
    crosspad_gui::getGuiPlatform().writeStmConfig8(REG_CTL_CC_PAN_BASE, s->stm.ccPanBase);
}
static void stm_analog_minms_cb(lv_event_t*) {
    auto* s = get_settings(); if (!s) return;
    crosspad_gui::getGuiPlatform().writeStmConfig8(REG_CTL_ANALOG_MIN_MS, s->stm.analogMinMs);
}
static void stm_analog_delta_cb(lv_event_t*) {
    auto* s = get_settings(); if (!s) return;
    crosspad_gui::getGuiPlatform().writeStmConfig8(REG_CTL_ANALOG_DELTA, s->stm.analogDelta);
}

// ---- Global pad fallback (CROSSPAD_PAD_EDITOR_ENABLED == 0) ----

#if !CROSSPAD_PAD_EDITOR_ENABLED

static void global_sens_val_cb(lv_event_t* e) {
    auto* s = get_settings(); if (!s) return;
    s->stm.sensitivity(0) = (uint16_t)lv_slider_get_value((lv_obj_t*)lv_event_get_target(e));
    for (int i = 0; i < 16; ++i) getGuiPlatform().writeStmConfigPad((uint8_t)i);
    auto* ui = crosspad::getSettingsUI(); if (ui) ui->saveSettings();
}

static void global_push_pads_cb(lv_event_t*) {
    for (int i = 0; i < 16; ++i) getGuiPlatform().writeStmConfigPad((uint8_t)i);
    auto* ui = crosspad::getSettingsUI(); if (ui) ui->saveSettings();
}

// These rows are labelled "Global Pad Defaults" and the callbacks push all 16
// pads, so the edited value has to reach all 16 too. Binding a row straight to
// pad 0's field wrote one pad and pushed the other fifteen unchanged — the
// panel said "global" and the hardware ended up with pad 0 behaving unlike the
// rest. Each row edits a proxy; these fan it out before pushing.
static uint8_t s_globalLr      = 50;
static uint8_t s_globalCurveIdx = 0;   // 0 = Inherit, 1..3 = Linear/Soft/Hard
static uint8_t s_globalFloor   = 0;
static bool    s_globalFloorInherit = true;
static uint8_t s_globalAnalog  = 0;

// Same map the per-pad editor uses (stm_pad_editor_screen.cpp): the per-pad
// curve/floor carry an INHERIT sentinel that is not a point on either scale,
// so it needs its own dropdown entry rather than being clamped into one.
static uint8_t global_curve_idx_to_val(uint8_t idx) {
    switch (idx) {
        case 1:  return 0;   // Linear
        case 2:  return 1;   // Exp Soft
        case 3:  return 2;   // Exp Hard
        default: return crosspad::STM32_VEL_INHERIT;
    }
}
static uint8_t global_curve_val_to_idx(uint8_t val) {
    switch (val) {
        case 0:  return 1;
        case 1:  return 2;
        case 2:  return 3;
        default: return 0;   // INHERIT, and anything a stale NVS invented
    }
}

static void global_lr_cb(lv_event_t* e) {
    auto* s = get_settings(); if (!s) return;
    if (s_globalLr < 1)  s_globalLr = 1;
    if (s_globalLr > 99) s_globalLr = 99;
    for (int i = 0; i < 16; ++i) s->stm.lrRatio(i) = s_globalLr;
    global_push_pads_cb(e);
}

static void global_vcurve_cb(lv_event_t* e) {
    auto* s = get_settings(); if (!s) return;
    uint8_t val = global_curve_idx_to_val(s_globalCurveIdx);
    for (int i = 0; i < 16; ++i) s->stm.velCurve(i) = val;
    global_push_pads_cb(e);
}

static void global_vfloor_cb(lv_event_t* e) {
    auto* s = get_settings(); if (!s) return;
    uint8_t val = s_globalFloorInherit ? crosspad::STM32_VEL_INHERIT
                                       : (s_globalFloor > 127 ? 127 : s_globalFloor);
    for (int i = 0; i < 16; ++i) s->stm.velFloor(i) = val;
    global_push_pads_cb(e);
}

static void global_analog_cb(lv_event_t* e) {
    auto* s = get_settings(); if (!s) return;
    if (s_globalAnalog > 3) s_globalAnalog = 0;
    for (int i = 0; i < 16; ++i) s->stm.analogMode(i) = s_globalAnalog;
    global_push_pads_cb(e);
}

static void open_global_pad_sliders(lv_obj_t* parent, lv_group_t* group) {
    auto* s = get_settings(); if (!s) return;

    // Sensitivity: uint16_t (1..4095) — use a hand-rolled slider (no settings_row_slider uint16 variant).
    settings_section_header(parent, "Global Pad Defaults");
    {
        lv_obj_t* lbl = lv_label_create(parent);
        lv_label_set_text(lbl, "Sensitivity");
        lv_obj_set_style_text_color(lbl, lv_color_white(), 0);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
        lv_obj_t* sl = lv_slider_create(parent);
        lv_slider_set_range(sl, 1, 4095);
        lv_slider_set_value(sl, (int32_t)s->stm.sensitivity(0), LV_ANIM_OFF);
        lv_obj_set_size(sl, LV_PCT(95), 10);
        lv_obj_set_style_bg_color(sl, lv_color_make(60, 60, 60), LV_PART_MAIN);
        lv_obj_set_style_bg_color(sl, CROSSPAD_THEME_PRIMARY, LV_PART_INDICATOR);
        lv_obj_set_style_bg_color(sl, lv_color_white(), LV_PART_KNOB);
        lv_obj_set_style_pad_all(sl, 3, LV_PART_KNOB);
        lv_obj_add_event_cb(sl, global_sens_val_cb, LV_EVENT_VALUE_CHANGED, nullptr);
        lv_obj_add_event_cb(sl, global_sens_val_cb, LV_EVENT_RELEASED, nullptr);
        focus_scroll_instant(sl);
        if (group) lv_group_add_obj(group, sl);
    }

    // Seeded from pad 0 — with every row now fanning out to all 16, pad 0 is a
    // faithful sample of the set.
    s_globalLr           = s->stm.lrRatio(0);
    if (s_globalLr < 1)  s_globalLr = 1;
    if (s_globalLr > 99) s_globalLr = 99;
    s_globalCurveIdx     = global_curve_val_to_idx(s->stm.velCurve(0));
    s_globalFloorInherit = (s->stm.velFloor(0) == crosspad::STM32_VEL_INHERIT);
    s_globalFloor        = s_globalFloorInherit ? s->stm.velFloorDef : s->stm.velFloor(0);
    if (s_globalFloor > 127) s_globalFloor = 127;
    s_globalAnalog       = s->stm.analogMode(0) > 3 ? 0 : s->stm.analogMode(0);

    settings_row_slider(parent, group, "L/R Ratio", 1, 99,
                        &s_globalLr, global_lr_cb);
    settings_row_dropdown(parent, group, "Vel Curve",
                          "Inherit\nLinear\nExp Soft\nExp Hard",
                          &s_globalCurveIdx, global_vcurve_cb);
    settings_row_switch(parent, group, "Floor Inherit",
                        &s_globalFloorInherit, global_vfloor_cb);
    settings_row_slider(parent, group, "Vel Floor", 0, 127,
                        &s_globalFloor, global_vfloor_cb);
    settings_row_dropdown(parent, group, "Analog Mode",
                          "Disable\nCC\nAftertouch\nBoth",
                          &s_globalAnalog, global_analog_cb);
}

#endif // !CROSSPAD_PAD_EDITOR_ENABLED

static void cat_build_analog(lv_obj_t* parent, lv_group_t* group) {
    auto* s = get_settings(); if (!s) return;

    settings_section_header(parent, "Velocity");
    settings_row_dropdown(parent, group, "Curve", "Linear\nExp Soft\nExp Hard",
                          &s->stm.velCurveDef, stm_vel_curve_cb);
    settings_row_slider(parent, group, "Floor", 0, 127, &s->stm.velFloorDef, stm_vel_floor_cb);

    settings_section_header(parent, "Pressure / Pan CC");
    settings_row_slider(parent, group, "Pressure CC base", 0, 127,
                        &s->stm.ccPressBase, stm_cc_press_cb);
    s_panCcEnProxy = s->stm.panCcEn != 0;
    settings_row_switch(parent, group, "Pan -> CC", &s_panCcEnProxy, stm_pan_en_cb);
    settings_row_slider(parent, group, "Pan CC base", 0, 127,
                        &s->stm.ccPanBase, stm_cc_pan_cb);

    settings_section_header(parent, "Stream rate");
    settings_row_slider(parent, group, "Min interval (ms)", 4, 100,
                        &s->stm.analogMinMs, stm_analog_minms_cb);
    settings_row_slider(parent, group, "Min delta", 0, 32,
                        &s->stm.analogDelta, stm_analog_delta_cb);

    settings_section_header(parent, "Per-Pad");
#if CROSSPAD_PAD_EDITOR_ENABLED
    settings_row_action(parent, group, "Per-Pad Tuning", "Open",
        [](lv_event_t*) { crosspad_gui::stm_pad_editor_create(lv_screen_active()); },
        nullptr);
#else
    open_global_pad_sliders(parent, group);
#endif
}

// ---- System / STM Power+Input ----

static constexpr uint8_t idx_for_u16(const uint16_t* arr, uint8_t n, uint16_t val, uint8_t dflt) {
    for (uint8_t i = 0; i < n; ++i) if (arr[i] == val) return i;
    return dflt;
}

static const uint16_t kTimeoutSecs[] = { 0, 30, 60, 300, 600, 1800, 3600 }; // 0 = never
static const char* kTimeoutOpts = "Never\n30s\n1m\n5m\n10m\n30m\n1h";
static uint8_t s_dimIdx = 0, s_sleepIdx = 0;

static const uint8_t kUsbSuspSecs[] = { 0, 15, 30, 60, 120 };   // 0 = never
static const char* kUsbSuspOpts = "Never\n15s\n30s\n1m\n2m";
static uint8_t s_usbSuspIdx = 2;   // default 30 s

// The STM clamps REG_CTL_LONG_PRESS to 1000..6500 (reg_get_long_press_ms).
static constexpr uint16_t kLongPressMs[] = { 500, 1000, 1500, 2000, 3000, 4000 };
static const char* kLongPressOpts = "0.5s\n1s\n1.5s\n2s\n3s\n4s";
static constexpr uint8_t kLongPressN = sizeof(kLongPressMs) / sizeof(kLongPressMs[0]);
static constexpr uint16_t kLongPressDefaultMs = 4000;
static constexpr uint8_t kLongPressDefaultIdx =
    idx_for_u16(kLongPressMs, kLongPressN, kLongPressDefaultMs, 0);
static uint8_t s_longPressIdx = kLongPressDefaultIdx;

// A stored timeout that is not one of the presets (older firmware defaulted the
// dim timeout to 1000 s) used to fall back to index 0 = "Never" while the STM
// happily kept counting — the panel said one thing, the hardware did another.
// Snap to the closest preset instead and write the snapped value back.
static uint8_t nearest_idx_u16(const uint16_t* arr, uint8_t n, uint16_t val) {
    uint8_t best = 0;
    uint32_t bestDist = 0xFFFFFFFFu;
    for (uint8_t i = 0; i < n; ++i) {
        uint32_t d = (arr[i] > val) ? (uint32_t)(arr[i] - val) : (uint32_t)(val - arr[i]);
        if (d < bestDist) { bestDist = d; best = i; }
    }
    return best;
}

// autoSleep is uint8_t in Stm32Settings; use a bool proxy like s_panCcEnProxy
static bool s_autoSleepProxy = true;

static void stm_auto_sleep_cb(lv_event_t*) {
    auto* s = get_settings(); if (!s) return;
    s->stm.autoSleep = s_autoSleepProxy ? 1 : 0;
    crosspad_gui::getGuiPlatform().writeStmConfig8(REG_CTL_AUTO_SLEEP, s->stm.autoSleep);
}
static void stm_keypad_en_cb(lv_event_t*) {
    auto* s = get_settings(); if (!s) return;
    // REG_CTL_KEYPAD_DIS is the INVERSE of keypadEnabled
    crosspad_gui::getGuiPlatform().writeStmConfig8(REG_CTL_KEYPAD_DIS, s->stm.keypadEnabled ? 0 : 1);
}
static void stm_wake_bright_cb(lv_event_t*) {
    auto* s = get_settings(); if (!s) return;
    crosspad_gui::getGuiPlatform().writeStmConfig8(REG_CTL_WAKE_BRIGHT, s->stm.wakeBrightness);
}
// The STM only promotes LIGHT → DEEP while sleep_to > dim_to (sleep.c
// light_sleep_tick). Picking sleep <= dim therefore disables sleep silently, so
// the UI repairs the pair instead of storing a combination that does nothing.
static void stm_push_timeouts(crosspad::CrosspadSettings* s) {
    auto& plat = getGuiPlatform();
    plat.writeStmConfig16(REG_CTL_DIM_TO_S_HI, s->stm.dimTimeoutS);
    plat.writeStmConfig16(REG_CTL_SLEEP_TO_S_HI, s->stm.sleepTimeoutS);
}

// Everything the STM keeps from Stm32Settings, for paths that replace the whole
// struct at once (reset to defaults, backup import).
static void stm_push_config(crosspad::CrosspadSettings* s) {
    auto& plat = getGuiPlatform();
    for (uint8_t i = 0; i < crosspad::CROSSPAD_MAX_PADS; ++i) plat.writeStmConfigPad(i);
    plat.writeStmConfig8(REG_CTL_MIDI_CHAN,      s->stm.midiChannel);
    plat.writeStmConfig8(REG_CTL_NOTE_OFFSET,    (uint8_t)s->stm.noteOffset);
    plat.writeStmConfig8(REG_CTL_VEL_CURVE_DEF,  s->stm.velCurveDef);
    plat.writeStmConfig8(REG_CTL_VEL_FLOOR_DEF,  s->stm.velFloorDef);
    plat.writeStmConfig8(REG_CTL_CC_PRESS_BASE,  s->stm.ccPressBase);
    plat.writeStmConfig8(REG_CTL_PAN_CC_EN,      s->stm.panCcEn);
    plat.writeStmConfig8(REG_CTL_CC_PAN_BASE,    s->stm.ccPanBase);
    plat.writeStmConfig8(REG_CTL_ANALOG_MIN_MS,  s->stm.analogMinMs);
    plat.writeStmConfig8(REG_CTL_ANALOG_DELTA,   s->stm.analogDelta);
    plat.writeStmConfig8(REG_CTL_AUTO_SLEEP,     s->stm.autoSleep);
    plat.writeStmConfig8(REG_CTL_KEYPAD_DIS,     s->stm.keypadEnabled ? 0 : 1);
    plat.writeStmConfig8(REG_CTL_WAKE_BRIGHT,    s->stm.wakeBrightness);
    stm_push_timeouts(s);
    plat.writeStmConfig16(REG_CTL_LONG_PRESS_HI, s->stm.longPressMs);
}

static void stm_dim_to_cb(lv_event_t*) {
    auto* s = get_settings(); if (!s) return;
    if (s_dimIdx >= sizeof(kTimeoutSecs)/sizeof(kTimeoutSecs[0])) s_dimIdx = 0;
    s->stm.dimTimeoutS = kTimeoutSecs[s_dimIdx];
    if (s->stm.sleepTimeoutS > 0 && s->stm.sleepTimeoutS <= s->stm.dimTimeoutS) {
        s->stm.sleepTimeoutS = 0;   // "Never" — the honest reading of an unreachable sleep
        s_sleepIdx = 0;
        ui_toast_show("Sleep must be longer than Dim — sleep set to Never",
                      2500, UI_TOAST_WARNING);
        save_settings();
        stm_push_timeouts(s);
        show_detail(s_current_category);
        return;
    }
    stm_push_timeouts(s);
}
static void stm_sleep_to_cb(lv_event_t*) {
    auto* s = get_settings(); if (!s) return;
    if (s_sleepIdx >= sizeof(kTimeoutSecs)/sizeof(kTimeoutSecs[0])) s_sleepIdx = 0;
    s->stm.sleepTimeoutS = kTimeoutSecs[s_sleepIdx];
    if (s->stm.sleepTimeoutS > 0 && s->stm.dimTimeoutS >= s->stm.sleepTimeoutS) {
        // Pull dim down to the next preset below sleep so both stages stay live.
        uint16_t newDim = 0;
        uint8_t newIdx = 0;
        for (uint8_t i = 0; i < sizeof(kTimeoutSecs)/sizeof(kTimeoutSecs[0]); ++i) {
            if (kTimeoutSecs[i] > 0 && kTimeoutSecs[i] < s->stm.sleepTimeoutS &&
                kTimeoutSecs[i] > newDim) { newDim = kTimeoutSecs[i]; newIdx = i; }
        }
        s->stm.dimTimeoutS = newDim;   // 0 → dim disabled, STM goes straight to DEEP
        s_dimIdx = newIdx;
        ui_toast_show("Dim shortened to stay below Sleep", 2500, UI_TOAST_INFO);
        save_settings();
        stm_push_timeouts(s);
        show_detail(s_current_category);
        return;
    }
    stm_push_timeouts(s);
}
static void stm_usb_susp_cb(lv_event_t*) {
    auto* s = get_settings(); if (!s) return;
    if (s_usbSuspIdx >= sizeof(kUsbSuspSecs)/sizeof(kUsbSuspSecs[0])) s_usbSuspIdx = 2;
    s->stm.usbSuspendS = kUsbSuspSecs[s_usbSuspIdx];
    getGuiPlatform().writeStmConfig8(REG_CTL_USB_SUSP_S, s->stm.usbSuspendS);
}
static void stm_long_press_cb(lv_event_t*) {
    auto* s = get_settings(); if (!s) return;
    if (s_longPressIdx >= kLongPressN) s_longPressIdx = kLongPressDefaultIdx;
    s->stm.longPressMs = kLongPressMs[s_longPressIdx];
    crosspad_gui::getGuiPlatform().writeStmConfig16(REG_CTL_LONG_PRESS_HI, s->stm.longPressMs);
}

#if FEAT_SETTINGS_BACKUP
// ---- Backup (SD) ----
//
// Two independent files: the full settings snapshot and a pad-tuning-only file.
// Pads are separate on purpose — pad calibration belongs to a physical unit and
// is worth carrying between firmware profiles without dragging along someone
// else's brightness, routing and audio setup.

static const char* kBackupAllName  = "crosspad-settings.json";
static const char* kBackupPadsName = "crosspad-pads.json";

static bool backup_path(char* buf, size_t len, const char* file) {
    auto* ui = getSettingsUI();
    const char* dir = ui ? ui->getBackupDir() : nullptr;
    if (!dir) return false;
    snprintf(buf, len, "%s/%s", dir, file);
    return true;
}

static void backup_report(const crosspad::SettingsIoResult& r, const char* okMsg) {
    if (r.ok) {
        ui_toast_show(okMsg, 2000, UI_TOAST_INFO);
    } else {
        char msg[96];
        snprintf(msg, sizeof(msg), "%s", r.error ? r.error : "failed");
        ui_toast_show(msg, 3000, UI_TOAST_WARNING);
    }
}

static crosspad::SettingsIoResult backup_import_all(const char* p) {
    auto* s = get_settings();
    if (!s) return { false, 0, "no settings" };
    auto r = crosspad::settingsImportAll(*s, p);
    if (r.ok) {
        // Imported values only live in RAM until something persists and applies
        // them — do both, then redraw so the page shows what was loaded.
        save_settings();
        auto& plat = getGuiPlatform();
        plat.setLcdBrightness(s->LCDbrightness);
        plat.setSpeakerVolume(s->masterFX.outVolumeCodec[0][0]);
        plat.setHeadphoneVolume(s->masterFX.outVolumeCodec[1][0]);
        crosspad::getPadLedController().setBrightness(s->RGBbrightness);
        crosspad::getVibration().applySettings(s->vibration);
        if (stm_config_available()) stm_push_config(s);
        reinitStyles();
        // Only a page that is open can be redrawn; the companion imports
        // from the launcher too.
        if (s_content && s_current_category >= 0) show_detail(s_current_category);
    }
    return r;
}

static void backup_export_all_cb(lv_event_t *) {
    auto* s = get_settings(); char p[128];
    if (!s || !backup_path(p, sizeof(p), kBackupAllName)) return;
    backup_report(crosspad::settingsExportAll(*s, p), "Settings exported to SD");
}

static void backup_import_all_cb(lv_event_t *) {
    char p[128];
    if (!backup_path(p, sizeof(p), kBackupAllName)) return;
    backup_report(backup_import_all(p), "Settings imported");
}

bool settings_ui_backup_export(const char* path, const char** error) {
    auto* s = get_settings();
    if (!s) { if (error) *error = "no settings"; return false; }
    auto r = crosspad::settingsExportAll(*s, path);
    if (error) *error = r.ok ? nullptr : r.error;
    return r.ok;
}

bool settings_ui_backup_import(const char* path, const char** error) {
    auto r = backup_import_all(path);
    if (error) *error = r.ok ? nullptr : r.error;
    return r.ok;
}

const char* settings_ui_backup_default_path() {
    static char p[128];
    return backup_path(p, sizeof(p), kBackupAllName) ? p : nullptr;
}

static void backup_export_pads_cb(lv_event_t *) {
    auto* s = get_settings(); char p[128];
    if (!s || !backup_path(p, sizeof(p), kBackupPadsName)) return;
    backup_report(crosspad::settingsExportPads(*s, p), "Pad tuning exported to SD");
}

static void backup_import_pads_cb(lv_event_t *) {
    auto* s = get_settings(); char p[128];
    if (!s || !backup_path(p, sizeof(p), kBackupPadsName)) return;
    auto r = crosspad::settingsImportPads(*s, p);
    backup_report(r, "Pad tuning imported");
    if (r.ok) {
        save_settings();
        if (stm_config_available()) {
            auto& plat = getGuiPlatform();
            for (uint8_t i = 0; i < crosspad::CROSSPAD_MAX_PADS; ++i) plat.writeStmConfigPad(i);
            plat.writeStmConfig8(REG_CTL_VEL_CURVE_DEF, s->stm.velCurveDef);
            plat.writeStmConfig8(REG_CTL_VEL_FLOOR_DEF, s->stm.velFloorDef);
            plat.writeStmConfig8(REG_CTL_CC_PRESS_BASE, s->stm.ccPressBase);
            plat.writeStmConfig8(REG_CTL_PAN_CC_EN,     s->stm.panCcEn);
            plat.writeStmConfig8(REG_CTL_CC_PAN_BASE,   s->stm.ccPanBase);
            plat.writeStmConfig8(REG_CTL_ANALOG_MIN_MS, s->stm.analogMinMs);
            plat.writeStmConfig8(REG_CTL_ANALOG_DELTA,  s->stm.analogDelta);
        }
    }
}

static void cat_build_backup(lv_obj_t * parent, lv_group_t * group) {
    char path[128];
    if (!backup_path(path, sizeof(path), kBackupAllName)) {
        settings_row_info(parent, "Storage", "no SD card support");
        return;
    }

    settings_section_header(parent, "All settings");
    settings_row_action(parent, group, kBackupAllName, "Export",
                        backup_export_all_cb, nullptr);
    settings_row_action(parent, group, nullptr, "Import",
                        backup_import_all_cb, nullptr);

    settings_section_header(parent, "Pad tuning only");
    settings_row_action(parent, group, kBackupPadsName, "Export",
                        backup_export_pads_cb, nullptr);
    settings_row_action(parent, group, nullptr, "Import",
                        backup_import_pads_cb, nullptr);

    char ver[32];
    snprintf(ver, sizeof(ver), "v%u", (unsigned)crosspad::kSettingsFileVersion);
    settings_row_info(parent, "File format", ver);
}
#else
bool settings_ui_backup_export(const char*, const char** error) {
    if (error) *error = "backup not built";
    return false;
}
bool settings_ui_backup_import(const char*, const char** error) {
    if (error) *error = "backup not built";
    return false;
}
const char* settings_ui_backup_default_path() { return nullptr; }
#endif  // FEAT_SETTINGS_BACKUP

// ---- Programming ----

static void stm32_dfu_cb(lv_event_t * e);

static void coproc_update_cb(lv_event_t * e) {
    (void)e;
    auto* ui = getSettingsUI();
    if (ui) ui->requestCoprocFwUpdate();
}

static void cat_build_programming(lv_obj_t * parent, lv_group_t * group) {
    auto* ui = getSettingsUI();
    settings_section_header(parent, "Co-processor");

    // The platform may carry an STM32 image and flash it itself; then the
    // running and bundled versions sit side by side with the button that
    // starts the platform's own update flow. Same version = "Reflash", the
    // recovery for a co-processor that misbehaves.
    crosspad::CoprocFwInfo fw{};
    if (ui && ui->getCoprocFwInfo(fw)) {
        settings_row_info(parent, "STM32 running", fw.running[0] ? fw.running : "no link");
        if (fw.canUpdate) {
            settings_row_action(parent, group, fw.bundled, fw.due ? "Update" : "Reflash",
                                coproc_update_cb, nullptr);
        } else {
            settings_row_info(parent, "STM32 bundled", fw.bundled);
        }
    }
    settings_row_action(parent, group, "STM32 firmware", "DFU Mode",
                        stm32_dfu_cb, nullptr);
    settings_row_info(parent, "ESP32 firmware", "OTA over USB CDC");
}

// ---- Info ----

static void cat_build_info(lv_obj_t * parent, lv_group_t * group) {
    (void)group;
    auto* ui = getSettingsUI();

    settings_section_header(parent, "Device");
    settings_row_info(parent, "Platform", ui ? ui->getPlatformName() : "CrossPad");
    if (ui) {
        const char* profile = ui->getPlatformInfo();
        if (profile) settings_row_info(parent, "Profile", profile);
    }

    // Everything else is platform-reported (firmware/library revisions, STM32
    // identity, serial) so this page grows without touching the shared UI.
    if (ui) {
        int n = ui->getInfoRowCount();
        for (int i = 0; i < n; ++i) {
            const char* label = nullptr;
            const char* value = nullptr;
            if (ui->getInfoRow(i, &label, &value) && label && value) {
                settings_row_info(parent, label, value);
            }
        }
    }
}

// ---- System ----

static void factory_reset_cb(lv_event_t * e) {
    (void)e;
    auto * s = CrosspadSettings::getInstance();
    if (!s) return;
    // Reset to defaults by constructing fresh sub-structs
    s->keypad = crosspad::KeypadSettings();
    s->stmRouting = crosspad::STMRoutingSettings();
    s->esp32Routing = crosspad::ESPRoutingSettings();
    s->wireless = crosspad::WirelessSettings();
    s->vibration = crosspad::VibrationSettings();
    s->masterFX = crosspad::AudioSettings();
    s->previewAudio = crosspad::AudioSettings();
    s->stm = crosspad::Stm32Settings();
    s->audioEngine = crosspad::AudioEngineSettings();
    s->LCDbrightness = 100;
    s->themeColorIndex = CROSSPAD_THEME_DEFAULT_INDEX;
    s->perfStatsFlags = 0;
    s->RGBbrightness = 100;
    s->quickSettingsEnabled = true;
    s->launcherStyle = crosspad::LauncherStyle::Grid;
    s->AudioEngineEnabled = true;
    s->Kit = 0;
    for (int i = 0; i < crosspad::CROSSPAD_MAX_PADS; i++) {
        s->audio[i] = crosspad::AudioSettings();
    }
    save_settings();

    // Re-apply everything that lives in hardware — a reset that only rewrites
    // NVS leaves the device running the old brightness / LEDs / haptics until
    // the next reboot.
    auto& plat = getGuiPlatform();
    plat.setLcdBrightness(s->LCDbrightness);
    plat.setSpeakerVolume(s->masterFX.outVolumeCodec[0][0]);
    plat.setHeadphoneVolume(s->masterFX.outVolumeCodec[1][0]);
    crosspad::getPadLedController().setBrightness(s->RGBbrightness);
    crosspad::getVibration().applySettings(s->vibration);
    if (stm_config_available()) stm_push_config(s);
    statusbar_set_drawer_enabled(s->quickSettingsEnabled);
    statusbar_set_perf_level(s->perfStatsFlags);
    reinitStyles();
    plat.onFactoryReset();

    ui_toast_show("Settings reset to defaults", 2000, UI_TOAST_WARNING);
    show_detail(s_current_category);
}

static void stm32_dfu_cb(lv_event_t * e) {
    (void)e;
    auto* ui = getSettingsUI();
    if (ui) ui->enterStm32Dfu();
    lv_CreateDfuScreen(lv_scr_act());
}

static void cat_build_system(lv_obj_t * parent, lv_group_t * group) {
    // Versions live under Info and firmware flashing under Programming — System
    // is the runtime behaviour of this unit, nothing else.
    settings_section_header(parent, "Maintenance");

    settings_row_action(parent, group, nullptr, "Factory Reset",
                        factory_reset_cb, nullptr);

    auto* s = get_settings();
    if (s && stm_config_available()) {
        settings_section_header(parent, "STM Power / Input");
        s_autoSleepProxy = s->stm.autoSleep != 0;
        settings_row_switch(parent, group, "Auto Sleep", &s_autoSleepProxy, stm_auto_sleep_cb);
        // STM-side pad scanning (distinct from keypad.enableKeypad, which gates
        // pad→MIDI in PadManager — that one lives under Pads & LEDs).
        settings_row_switch(parent, group, "STM Keypad Scan", &s->stm.keypadEnabled, stm_keypad_en_cb);
        settings_row_slider(parent, group, "Wake Brightness", 0, 100,
                            &s->stm.wakeBrightness, stm_wake_bright_cb);
        constexpr uint8_t kTimeoutCount = sizeof(kTimeoutSecs)/sizeof(kTimeoutSecs[0]);
        s_dimIdx   = nearest_idx_u16(kTimeoutSecs, kTimeoutCount, s->stm.dimTimeoutS);
        s_sleepIdx = nearest_idx_u16(kTimeoutSecs, kTimeoutCount, s->stm.sleepTimeoutS);
        if (kTimeoutSecs[s_dimIdx] != s->stm.dimTimeoutS ||
            kTimeoutSecs[s_sleepIdx] != s->stm.sleepTimeoutS) {
            s->stm.dimTimeoutS   = kTimeoutSecs[s_dimIdx];
            s->stm.sleepTimeoutS = kTimeoutSecs[s_sleepIdx];
            save_settings();
            stm_push_timeouts(s);
        }
        settings_row_dropdown(parent, group, "Dim After", kTimeoutOpts, &s_dimIdx, stm_dim_to_cb);
        settings_row_dropdown(parent, group, "Sleep After", kTimeoutOpts, &s_sleepIdx, stm_sleep_to_cb);
        s_longPressIdx = idx_for_u16(kLongPressMs, kLongPressN, s->stm.longPressMs, kLongPressDefaultIdx);
        settings_row_dropdown(parent, group, "Power Long-Press", kLongPressOpts, &s_longPressIdx, stm_long_press_cb);
        // Host-suspend grace before the STM drops to soft-off. Was reachable only
        // over I2C — a device on a sleeping host powered itself down after 30 s
        // with no way to change or disable that from the UI.
        s_usbSuspIdx = 2;
        for (uint8_t i = 0; i < sizeof(kUsbSuspSecs)/sizeof(kUsbSuspSecs[0]); ++i)
            if (kUsbSuspSecs[i] == s->stm.usbSuspendS) { s_usbSuspIdx = i; break; }
        settings_row_dropdown(parent, group, "USB Suspend Off", kUsbSuspOpts,
                              &s_usbSuspIdx, stm_usb_susp_cb);
    }
}

// ===================================================================
// Public API
// ===================================================================

void settings_ui_create(lv_obj_t * content_area, lv_group_t * group) {
    s_content = content_area;
    s_group = group;
    s_current_category = -1;

    show_categories();
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
