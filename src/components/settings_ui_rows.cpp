// SPDX-License-Identifier: GPL-3.0-or-later
//
// Settings: the row builders every category page is made of.

#ifdef USE_LVGL

#include "settings_ui_internal.h"
#include "crosspad-gui/styles/styles.h"
#include "crosspad-gui/theme/crosspad_theme.h"

using namespace crosspad;

namespace crosspad_gui {

using namespace settings_detail;

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

lv_obj_t * settings_detail::settings_row_bitswitch(lv_obj_t * parent, lv_group_t * group,
                                                   const char * label, uint8_t * flags, uint8_t bit,
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

} // namespace crosspad_gui

#endif // USE_LVGL
