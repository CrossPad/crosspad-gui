#ifdef USE_LVGL

#include "crosspad-gui/components/settings_ui.h"
#include "crosspad-gui/components/status_bar.h"
#include "crosspad-gui/components/ui_toast.h"
#include "crosspad-gui/platform/IGuiPlatform.h"
#include "crosspad-gui/styles/styles.h"
#include "crosspad-gui/theme/crosspad_theme.h"
#include "crosspad/settings/CrosspadSettings.hpp"
#include "crosspad/settings/ISettingsUI.hpp"
#include "crosspad/pad/PadLedController.hpp"

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
static void cat_build_pads(lv_obj_t * parent, lv_group_t * group);
static void cat_build_vibration(lv_obj_t * parent, lv_group_t * group);
static void cat_build_connectivity(lv_obj_t * parent, lv_group_t * group);
static void cat_build_midi(lv_obj_t * parent, lv_group_t * group);
static void cat_build_system(lv_obj_t * parent, lv_group_t * group);

static const SettingsCategory s_builtin_categories[] = {
    { "Display",       LV_SYMBOL_EYE_OPEN,   cat_build_display },
    { "Audio",         LV_SYMBOL_AUDIO,       cat_build_audio },
    { "Pads & LEDs",   LV_SYMBOL_KEYBOARD,    cat_build_pads },
    { "Vibration",     LV_SYMBOL_BELL,        cat_build_vibration },
    { "Connectivity",  LV_SYMBOL_WIFI,        cat_build_connectivity },
    { "MIDI",          LV_SYMBOL_SHUFFLE,     cat_build_midi },
    { "System",        LV_SYMBOL_SETTINGS,    cat_build_system },
};
static constexpr int s_builtin_count = sizeof(s_builtin_categories) / sizeof(s_builtin_categories[0]);

static int total_category_count() { return s_builtin_count + s_extra_cat_count; }

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

        lv_obj_t * row = lv_button_create(s_content);
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
        lv_obj_set_style_bg_color(list, CROSSPAD_THEME_PRIMARY, LV_PART_SELECTED | LV_STATE_CHECKED);
    }

    DropdownRowCtx * ctx = new DropdownRowCtx{setting, extra_cb};
    lv_obj_add_event_cb(dd, dropdown_value_changed_cb, LV_EVENT_VALUE_CHANGED, ctx);
    lv_obj_add_event_cb(dd, dropdown_row_delete_cb, LV_EVENT_DELETE, ctx);

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

    settings_section_header(parent, "Perf Stats");
    settings_row_bitswitch(parent, group, "FPS", &s->perfStatsFlags, PERF_FLAG_FPS,
                           [](lv_event_t *) { statusbar_set_perf_level(get_settings()->perfStatsFlags); });
    settings_row_bitswitch(parent, group, "CPU", &s->perfStatsFlags, PERF_FLAG_CPU,
                           [](lv_event_t *) { statusbar_set_perf_level(get_settings()->perfStatsFlags); });
    settings_row_bitswitch(parent, group, "Memory", &s->perfStatsFlags, PERF_FLAG_MEM,
                           [](lv_event_t *) { statusbar_set_perf_level(get_settings()->perfStatsFlags); });
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

static void audio_engine_fc_cb(lv_event_t * e) {
    (void)e;
    auto* s = get_settings();
    if (!s) return;
    if (s_audioEngineFcIdx < AudioEngineSettings::kFrameCountsCount) {
        s->audioEngine.frameCount = AudioEngineSettings::kFrameCounts[s_audioEngineFcIdx];
    }
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

    settings_section_header(parent, "Master");

    settings_row_switch(parent, group, "Master Mute",
                        &s->masterFX.mute);

    settings_row_slider(parent, group, "Input Volume", 0, 127,
                        &s->masterFX.inVolume);

    settings_row_slider(parent, group, "Output Volume", 0, 127,
                        &s->masterFX.outVolume);

    settings_section_header(parent, "Effects Bypass");

    settings_row_switch(parent, group, "Delay",
                        &s->masterFX.delay.bypass);

    settings_row_switch(parent, group, "Reverb",
                        &s->masterFX.reverb.bypass);

    settings_row_switch(parent, group, "Distortion",
                        &s->masterFX.distortion.bypass);

    settings_row_switch(parent, group, "Chorus",
                        &s->masterFX.chorus.bypass);

    settings_row_switch(parent, group, "Flanger",
                        &s->masterFX.flanger.bypass);
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

    settings_row_switch(parent, group, "Note Off on Release",
                        &s->keypad.noteOffOnRelease);

    settings_row_switch(parent, group, "Inactive Lights",
                        &s->keypad.inactiveLights);

    settings_row_switch(parent, group, "Upper Row Functions",
                        &s->keypad.upperRowFunctions);

    settings_row_switch(parent, group, "Eco Mode",
                        &s->keypad.ecoMode);
}

// ---- Vibration ----

static lv_obj_t * s_vibration_dependent = nullptr;

static void vibration_enable_cb(lv_event_t * e) {
    (void)e;
    if (!s_vibration_dependent) return;
    if (get_settings()->vibration.enable) {
        lv_obj_remove_flag(s_vibration_dependent, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_vibration_dependent, LV_OBJ_FLAG_HIDDEN);
    }
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
                        &s->vibration.enableVibrationOnTouch);

    settings_row_switch(s_vibration_dependent, group, "On Error",
                        &s->vibration.enableVibrationOnError);

    settings_row_switch(s_vibration_dependent, group, "Audio to Vibe",
                        &s->vibration.enableAudioToVibe);

    settings_section_header(s_vibration_dependent, "Input Range");

    settings_row_slider(s_vibration_dependent, group, "Input Min", 0, 255,
                        &s->vibration.inputMin);

    settings_row_slider(s_vibration_dependent, group, "Input Max", 0, 255,
                        &s->vibration.inputMax);

    settings_section_header(s_vibration_dependent, "Output Range");

    settings_row_slider(s_vibration_dependent, group, "Output Min", 0, 255,
                        &s->vibration.outputMin);

    settings_row_slider(s_vibration_dependent, group, "Output Max", 0, 255,
                        &s->vibration.outputMax);

    settings_row_switch(s_vibration_dependent, group, "Debug Mode",
                        &s->vibration.debugging);
}

// ---- Connectivity ----

static void cat_build_connectivity(lv_obj_t * parent, lv_group_t * group) {
    auto* s = get_settings();
    if (!s) return;

    settings_row_switch(parent, group, "WiFi",
                        &s->wireless.enableWiFi);

    settings_row_switch(parent, group, "Bluetooth LE",
                        &s->wireless.enableBLE);

    settings_row_switch(parent, group, "OSC",
                        &s->wireless.enableOSC);

    settings_row_switch(parent, group, "OSC Server",
                        &s->wireless.enableOSCServer);

    settings_row_switch(parent, group, "UDP",
                        &s->wireless.enableUDP);

    settings_row_switch(parent, group, "TCP",
                        &s->wireless.enableTCP);

    settings_row_switch(parent, group, "Web Server",
                        &s->wireless.enableWebServer);
}

// ---- MIDI ----

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

    settings_row_switch(parent, group, "BLE -> MIDI",
                        &s->esp32Routing.BLE2Midi);

    settings_row_switch(parent, group, "BLE -> Audio",
                        &s->esp32Routing.BLE2Audio);

    settings_row_switch(parent, group, "MIDI -> BLE",
                        &s->esp32Routing.Midi2BLE);

    settings_row_switch(parent, group, "MIDI -> Audio",
                        &s->esp32Routing.Midi2Audio);
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
    s->LCDbrightness = 100;
    s->themeColorIndex = CROSSPAD_THEME_DEFAULT_INDEX;
    s->perfStatsFlags = 0;
    s->RGBbrightness = 100;
    s->AudioEngineEnabled = true;
    s->Kit = 0;
    for (int i = 0; i < crosspad::CROSSPAD_MAX_PADS; i++) {
        s->audio[i] = crosspad::AudioSettings();
    }
    save_settings();
    ui_toast_show("Settings reset to defaults", 2000, UI_TOAST_WARNING);
}

static void cat_build_system(lv_obj_t * parent, lv_group_t * group) {
    settings_section_header(parent, "Information");

    auto* ui = getSettingsUI();
    const char* platform = ui ? ui->getPlatformName() : "CrossPad";
    settings_row_info(parent, "Platform", platform);

    const char* info = ui ? ui->getPlatformInfo() : nullptr;
    if (info) {
        settings_row_info(parent, "Profile", info);
    }

    settings_section_header(parent, "Maintenance");

    settings_row_action(parent, group, nullptr, "Factory Reset",
                        factory_reset_cb, nullptr);
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
