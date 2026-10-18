// SPDX-License-Identifier: GPL-3.0-or-later
//
// Settings: the Analog / Velocity category.

#ifdef USE_LVGL

#include "settings_ui_internal.h"
#include "crosspad/config/crosspad_config.h"
#include "crosspad-gui/components/stm_pad_editor_screen.h"
#include "crosspad-gui/platform/IGuiPlatform.h"
#include "crosspad-gui/theme/crosspad_theme.h"
#include "crosspad/settings/CrosspadSettings.hpp"
#include "crosspad/settings/ISettingsUI.hpp"
#include "crosspad/stm32/reg_map.h"

using namespace crosspad;

namespace crosspad_gui {

using namespace settings_detail;

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

void settings_detail::cat_build_analog(lv_obj_t* parent, lv_group_t* group) {
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

} // namespace crosspad_gui

#endif // USE_LVGL
