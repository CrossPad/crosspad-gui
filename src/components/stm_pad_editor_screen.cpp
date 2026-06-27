// SPDX-License-Identifier: MIT
//
// Interactive per-pad STM analog tuning screen.
//
// Open from Settings -> Analog/Velocity -> "Per-Pad Tuning". The user presses a
// physical pad to select it (StmPadEditorLogic highlights it on the RGB LEDs),
// edits that pad's analog config with the hand-rolled field widgets below, and
// every edit is written live to the STM via getGuiPlatform().writeStmConfigPad().
// Closing persists the edited per-pad arrays to NVS.
//
// IMPORTANT design notes (see task-3-override.md):
//  * STM writes go ONLY through crosspad_gui::getGuiPlatform().writeStmConfigPad()
//    -- never getStm32Manager(), which does not link from crosspad-gui.
//  * Field widgets are HAND-ROLLED (not settings_row_* helpers) because the edit
//    target pad (s_sel) changes at runtime; a fixed pointer would write the wrong
//    pad. load_pad_fields(pad) reloads the displayed values; callbacks write into
//    &stm.*[s_sel].
//  * The select callback fires from the pad-event task, NOT the LVGL task, so it
//    only sets a pending flag; an LVGL timer applies it (mirrors midi_ble_panel).

#ifdef USE_LVGL

#include "crosspad-gui/components/stm_pad_editor_screen.h"
#include "crosspad-gui/components/stm_pad_editor_logic.h"
#include "crosspad-gui/platform/IGuiPlatform.h"
#include "crosspad/settings/CrosspadSettings.hpp"
#include "crosspad/settings/ISettingsUI.hpp"
#include "crosspad/pad/PadManager.hpp"
#include "crosspad/led/RgbColor.hpp"
#include "crosspad/stm32/VelocityCurve.hpp"

#include <lvgl.h>
#include <cstdio>
#include <memory>

namespace crosspad_gui {

// ── File-static state ───────────────────────────────────────────────────────

static StmPadEditorLogic s_logic;
static uint8_t           s_sel = 0;

static lv_obj_t* s_root        = nullptr;
static lv_obj_t* s_headerLabel = nullptr;
static lv_obj_t* s_sensSlider  = nullptr;  // padSensitivity (1..4095)
static lv_obj_t* s_lrSlider    = nullptr;  // padLrRatio (1..99)
static lv_obj_t* s_analogDrop  = nullptr;  // padAnalogMode (0..3)
static lv_obj_t* s_curveDrop   = nullptr;  // padVelCurve (0xFF/0/1/2)
static lv_obj_t* s_floorSlider = nullptr;  // padVelFloor (0..127 or 0xFF inherit)
static lv_obj_t* s_floorInherit = nullptr; // switch: floor == 0xFF (inherit)

// Live-feedback widgets (RAW/VEL readout + velocity-curve chart with hit marker).
static lv_obj_t*          s_rawLabel = nullptr;
static lv_obj_t*          s_velLabel = nullptr;
static lv_obj_t*          s_chart    = nullptr;
static lv_chart_series_t* s_curveSer = nullptr; // cyan: velocity curve, 128 pts
static lv_chart_series_t* s_markSer  = nullptr; // red: single-point last-hit marker
static uint8_t            s_lastRaw  = 0xFF;

static crosspad::RgbColor s_prevColors[16];

// Cross-task select marshaling (callback fires from the pad-event task).
static volatile uint8_t s_pendingSel      = 0;
static volatile bool    s_pendingSelValid = false;

static lv_timer_t* s_pollTimer = nullptr;

// Encoder focus group (restored on delete).
static lv_group_t* s_group      = nullptr;
static lv_group_t* s_savedGroup = nullptr;

// Guard so field callbacks don't write while load_pad_fields() is mutating widgets.
static bool s_loading = false;

// ── Helpers ─────────────────────────────────────────────────────────────────

static inline crosspad::CrosspadSettings* get_settings() {
    return crosspad::CrosspadSettings::getInstance();
}

static void save_settings() {
    auto* ui = crosspad::getSettingsUI();
    if (ui) ui->saveSettings();
}

// Curve value <-> dropdown index map.
//   value 0xFF -> idx 0 (Inherit), 0 -> idx 1 (Linear),
//   1 -> idx 2 (Exp Soft), 2 -> idx 3 (Exp Hard)
static uint16_t curve_val_to_idx(uint8_t v) {
    switch (v) {
        case 0xFF: return 0;
        case 0:    return 1;
        case 1:    return 2;
        case 2:    return 3;
        default:   return 0;
    }
}
static uint8_t curve_idx_to_val(uint16_t idx) {
    switch (idx) {
        case 0:  return 0xFF;
        case 1:  return 0;
        case 2:  return 1;
        case 3:  return 2;
        default: return 0xFF;
    }
}

// ── Velocity-curve chart helpers ────────────────────────────────────────────
// Redraw the cyan velocity curve for the currently-selected pad's resolved
// (curve, floor). Called from load_pad_fields() and every curve/floor edit.
static void redraw_curve(void) {
    auto* s = crosspad::CrosspadSettings::getInstance();
    if (!s || !s_chart || !s_curveSer) return;
    uint8_t curve = s->stm.padVelCurve[s_sel];
    uint8_t floor = s->stm.padVelFloor[s_sel];
    if (curve == crosspad::STM32_VEL_INHERIT) curve = s->stm.velCurveDef;
    if (floor == crosspad::STM32_VEL_INHERIT) floor = s->stm.velFloorDef;
    for (uint16_t x = 0; x < 128; ++x)
        lv_chart_set_series_value_by_id(s_chart, s_curveSer, x,
                                        crosspad::stm32VelCurveApply((uint8_t)x, curve, floor));
    lv_chart_refresh(s_chart);
}

// Place the red marker at the last hit's (raw, vel). All other points hidden.
static void update_marker(uint8_t raw, uint8_t vel) {
    if (!s_chart || !s_markSer) return;
    for (uint16_t x = 0; x < 128; ++x)
        lv_chart_set_series_value_by_id(s_chart, s_markSer, x, LV_CHART_POINT_NONE);
    if (raw < 128) lv_chart_set_series_value_by_id(s_chart, s_markSer, raw, vel);
    lv_chart_refresh(s_chart);
}

// ── Load the currently-selected pad's values into the widgets ───────────────
// Runs ON the LVGL task only.
static void load_pad_fields(uint8_t pad) {
    auto* s = get_settings();
    if (!s || pad >= 16) return;

    s_loading = true;

    if (s_headerLabel) {
        // 1-based label is friendlier for the user (physical pads are labelled 1..16).
        lv_label_set_text_fmt(s_headerLabel, "Pad %u", (unsigned)(pad + 1));
    }
    if (s_sensSlider)  lv_slider_set_value(s_sensSlider, s->stm.padSensitivity[pad], LV_ANIM_OFF);
    if (s_lrSlider)    lv_slider_set_value(s_lrSlider, s->stm.padLrRatio[pad], LV_ANIM_OFF);
    if (s_analogDrop)  lv_dropdown_set_selected(s_analogDrop, s->stm.padAnalogMode[pad]);
    if (s_curveDrop)   lv_dropdown_set_selected(s_curveDrop, curve_val_to_idx(s->stm.padVelCurve[pad]));

    // Floor: 0xFF means "inherit"; otherwise a 0..127 value.
    bool floorInherit = (s->stm.padVelFloor[pad] == 0xFF);
    if (s_floorInherit) {
        if (floorInherit) lv_obj_add_state(s_floorInherit, LV_STATE_CHECKED);
        else              lv_obj_remove_state(s_floorInherit, LV_STATE_CHECKED);
    }
    if (s_floorSlider) {
        lv_slider_set_value(s_floorSlider, floorInherit ? 0 : s->stm.padVelFloor[pad], LV_ANIM_OFF);
        if (floorInherit) lv_obj_add_state(s_floorSlider, LV_STATE_DISABLED);
        else              lv_obj_remove_state(s_floorSlider, LV_STATE_DISABLED);
    }

    s_loading = false;

    // Repaint the curve for the new pad and force the next velraw read to repaint.
    s_lastRaw = 0xFF;
    redraw_curve();
}

static void push_pad() {
    crosspad_gui::getGuiPlatform().writeStmConfigPad(s_sel);
}

// ── Field change callbacks ──────────────────────────────────────────────────

static void sens_cb(lv_event_t* e) {
    if (s_loading) return;
    auto* s = get_settings(); if (!s) return;
    s->stm.padSensitivity[s_sel] = (uint16_t)lv_slider_get_value((lv_obj_t*)lv_event_get_target(e));
    push_pad();
}

static void lr_cb(lv_event_t* e) {
    if (s_loading) return;
    auto* s = get_settings(); if (!s) return;
    s->stm.padLrRatio[s_sel] = (uint8_t)lv_slider_get_value((lv_obj_t*)lv_event_get_target(e));
    push_pad();
}

static void analog_cb(lv_event_t* e) {
    if (s_loading) return;
    auto* s = get_settings(); if (!s) return;
    // analog dropdown index == value (0=Disable,1=CC,2=Aftertouch,3=Both).
    s->stm.padAnalogMode[s_sel] = (uint8_t)lv_dropdown_get_selected((lv_obj_t*)lv_event_get_target(e));
    push_pad();
}

static void curve_cb(lv_event_t* e) {
    if (s_loading) return;
    auto* s = get_settings(); if (!s) return;
    uint16_t idx = lv_dropdown_get_selected((lv_obj_t*)lv_event_get_target(e));
    s->stm.padVelCurve[s_sel] = curve_idx_to_val(idx);
    push_pad();
    redraw_curve();
}

static void floor_slider_cb(lv_event_t* e) {
    if (s_loading) return;
    auto* s = get_settings(); if (!s) return;
    // Moving the slider implies a concrete floor (not inherit).
    s->stm.padVelFloor[s_sel] = (uint8_t)lv_slider_get_value((lv_obj_t*)lv_event_get_target(e));
    push_pad();
    redraw_curve();
}

static void floor_inherit_cb(lv_event_t* e) {
    if (s_loading) return;
    auto* s = get_settings(); if (!s) return;
    bool inherit = lv_obj_has_state((lv_obj_t*)lv_event_get_target(e), LV_STATE_CHECKED);
    if (inherit) {
        s->stm.padVelFloor[s_sel] = 0xFF;
        if (s_floorSlider) lv_obj_add_state(s_floorSlider, LV_STATE_DISABLED);
    } else {
        // Drop to the slider's current value as the concrete floor.
        uint8_t v = s_floorSlider ? (uint8_t)lv_slider_get_value(s_floorSlider) : 0;
        s->stm.padVelFloor[s_sel] = v;
        if (s_floorSlider) lv_obj_remove_state(s_floorSlider, LV_STATE_DISABLED);
    }
    push_pad();
    redraw_curve();
}

// ── Cross-task select marshaling ────────────────────────────────────────────

// Fires from the pad-event task; set flags ONLY, no LVGL calls.
static void on_pad_selected(uint8_t pad, void*) {
    s_pendingSel      = pad;
    s_pendingSelValid = true;
}

// Runs on the LVGL task.
static void poll_cb(lv_timer_t*) {
    if (s_pendingSelValid) {
        s_pendingSelValid = false;
        s_sel = s_pendingSel;
        if (s_sel >= 16) s_sel = 0;
        load_pad_fields(s_sel); // resets s_lastRaw, repaints curve
    }

    // Live per-pad RAW (pre-curve linear hit) + computed VELOCITY + hit marker.
    uint8_t raw = 0;
    if (crosspad_gui::getGuiPlatform().readStmPadVelRaw(s_sel, raw)) {
        if (raw != s_lastRaw) {
            s_lastRaw = raw;
            auto* s = crosspad::CrosspadSettings::getInstance();
            if (s) {
                uint8_t curve = s->stm.padVelCurve[s_sel];
                uint8_t floor = s->stm.padVelFloor[s_sel];
                if (curve == crosspad::STM32_VEL_INHERIT) curve = s->stm.velCurveDef;
                if (floor == crosspad::STM32_VEL_INHERIT) floor = s->stm.velFloorDef;
                uint8_t vel = crosspad::stm32VelCurveApply(raw, curve, floor);
                if (s_rawLabel) lv_label_set_text_fmt(s_rawLabel, "RAW: %u", (unsigned)raw);
                if (s_velLabel) lv_label_set_text_fmt(s_velLabel, "VEL: %u", (unsigned)vel);
                update_marker(raw, vel);
            }
        }
    }
}

// ── Lifecycle ───────────────────────────────────────────────────────────────

static void on_delete(lv_event_t*) {
    auto& pm = crosspad::getPadManager();
    pm.setActivePadLogic("");
    pm.unregisterPadLogic("StmPadEditor");

    // Restore RGB colors snapshotted at create.
    for (int i = 0; i < 16; ++i) pm.setPadColor((uint8_t)i, s_prevColors[i]);

    if (s_pollTimer) { lv_timer_delete(s_pollTimer); s_pollTimer = nullptr; }

    // Restore encoder group BEFORE deleting our group, else the indev dangles.
    lv_indev_t* nav = getGuiPlatform().getNavigationEncoder();
    if (nav) lv_indev_set_group(nav, s_savedGroup);
    s_savedGroup = nullptr;
    if (s_group) { lv_group_delete(s_group); s_group = nullptr; }

    // Clear widget pointers (root + children are being destroyed).
    s_root = s_headerLabel = s_sensSlider = s_lrSlider = nullptr;
    s_analogDrop = s_curveDrop = s_floorSlider = s_floorInherit = nullptr;
    s_rawLabel = s_velLabel = s_chart = nullptr;
    s_curveSer = s_markSer = nullptr;
    s_lastRaw = 0xFF;

    // Persist the edited per-pad arrays to NVS.
    save_settings();
}

// ── Small builder: labelled row holding one focusable widget ─────────────────
static lv_obj_t* make_row(lv_obj_t* parent, const char* label) {
    lv_obj_t* row = lv_obj_create(parent);
    lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_hor(row, 4, 0);
    lv_obj_set_style_pad_ver(row, 4, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* lbl = lv_label_create(row);
    lv_label_set_text(lbl, label);
    lv_obj_set_style_text_color(lbl, lv_color_white(), 0);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
    return row;
}

lv_obj_t* stm_pad_editor_create(lv_obj_t* parent) {
    // 1. Full-size root container.
    lv_obj_t* root = lv_obj_create(parent);
    lv_obj_set_size(root, lv_obj_get_content_width(parent), lv_obj_get_content_height(parent));
    lv_obj_center(root);
    lv_obj_set_flex_flow(root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(root, 4, 0);
    lv_obj_add_event_cb(root, on_delete, LV_EVENT_DELETE, nullptr);
    s_root = root;

    // 2. Snapshot current pad colors so we can restore on close.
    auto& pm = crosspad::getPadManager();
    for (int i = 0; i < 16; ++i) s_prevColors[i] = pm.getPadColor((uint8_t)i);

    // 3. Header + the 5 hand-rolled field widgets + a Close button.
    s_headerLabel = lv_label_create(root);
    lv_label_set_text(s_headerLabel, "Press a pad to select");
    lv_obj_set_style_text_color(s_headerLabel, lv_color_white(), 0);
    lv_obj_set_style_text_font(s_headerLabel, &lv_font_montserrat_14, 0);

    // Sensitivity slider (1..4095).
    {
        lv_obj_t* row = make_row(root, "Sensitivity");
        s_sensSlider = lv_slider_create(row);
        lv_slider_set_range(s_sensSlider, 1, 4095);
        lv_obj_set_width(s_sensSlider, LV_PCT(55));
        lv_obj_add_event_cb(s_sensSlider, sens_cb, LV_EVENT_VALUE_CHANGED, nullptr);
    }

    // L/R ratio slider (1..99).
    {
        lv_obj_t* row = make_row(root, "L/R Ratio");
        s_lrSlider = lv_slider_create(row);
        lv_slider_set_range(s_lrSlider, 1, 99);
        lv_obj_set_width(s_lrSlider, LV_PCT(55));
        lv_obj_add_event_cb(s_lrSlider, lr_cb, LV_EVENT_VALUE_CHANGED, nullptr);
    }

    // Analog mode dropdown.
    {
        lv_obj_t* row = make_row(root, "Analog Mode");
        s_analogDrop = lv_dropdown_create(row);
        lv_dropdown_set_options(s_analogDrop, "Disable\nCC\nAftertouch\nBoth");
        lv_obj_set_width(s_analogDrop, LV_PCT(55));
        lv_obj_add_event_cb(s_analogDrop, analog_cb, LV_EVENT_VALUE_CHANGED, nullptr);
    }

    // Velocity curve dropdown.
    {
        lv_obj_t* row = make_row(root, "Velocity Curve");
        s_curveDrop = lv_dropdown_create(row);
        lv_dropdown_set_options(s_curveDrop, "Inherit\nLinear\nExp Soft\nExp Hard");
        lv_obj_set_width(s_curveDrop, LV_PCT(55));
        lv_obj_add_event_cb(s_curveDrop, curve_cb, LV_EVENT_VALUE_CHANGED, nullptr);
    }

    // Velocity floor: inherit switch + slider (0..127).
    {
        lv_obj_t* row = make_row(root, "Floor Inherit");
        s_floorInherit = lv_switch_create(row);
        lv_obj_add_event_cb(s_floorInherit, floor_inherit_cb, LV_EVENT_VALUE_CHANGED, nullptr);
    }
    {
        lv_obj_t* row = make_row(root, "Velocity Floor");
        s_floorSlider = lv_slider_create(row);
        lv_slider_set_range(s_floorSlider, 0, 127);
        lv_obj_set_width(s_floorSlider, LV_PCT(55));
        lv_obj_add_event_cb(s_floorSlider, floor_slider_cb, LV_EVENT_VALUE_CHANGED, nullptr);
    }

    // Live RAW + computed VELOCITY readout (one row, side by side).
    {
        lv_obj_t* row = make_row(root, "Last Hit");
        s_rawLabel = lv_label_create(row);
        lv_label_set_text(s_rawLabel, "RAW: --");
        lv_obj_set_style_text_color(s_rawLabel, lv_color_white(), 0);
        lv_obj_set_style_text_font(s_rawLabel, &lv_font_montserrat_14, 0);
        s_velLabel = lv_label_create(row);
        lv_label_set_text(s_velLabel, "VEL: --");
        lv_obj_set_style_text_color(s_velLabel, lv_color_white(), 0);
        lv_obj_set_style_text_font(s_velLabel, &lv_font_montserrat_14, 0);
    }

    // Velocity-curve chart: input 0..127 on X (point index), velocity 0..127 on Y.
    // Cyan series = the curve; red series = a single-point marker at the last hit.
    s_chart = lv_chart_create(root);
    lv_obj_set_size(s_chart, LV_PCT(90), 120);
    lv_obj_set_style_pad_all(s_chart, 2, 0);
    lv_chart_set_type(s_chart, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(s_chart, 128);
    lv_chart_set_axis_range(s_chart, LV_CHART_AXIS_PRIMARY_Y, 0, 127);
    s_curveSer = lv_chart_add_series(s_chart, lv_palette_main(LV_PALETTE_CYAN), LV_CHART_AXIS_PRIMARY_Y);
    s_markSer  = lv_chart_add_series(s_chart, lv_palette_main(LV_PALETTE_RED),  LV_CHART_AXIS_PRIMARY_Y);
    // Marker: fat dot, no connecting line (single visible point).
    lv_obj_set_style_size(s_chart, 6, 6, LV_PART_INDICATOR);

    // Close button.
    lv_obj_t* closeBtn = lv_button_create(root);
    lv_obj_set_size(closeBtn, LV_SIZE_CONTENT, 32);
    lv_obj_add_event_cb(closeBtn, [](lv_event_t*) {
        if (s_root) lv_obj_delete_async(s_root);
    }, LV_EVENT_CLICKED, nullptr);
    {
        lv_obj_t* lbl = lv_label_create(closeBtn);
        lv_label_set_text(lbl, "Close");
        lv_obj_center(lbl);
    }

    // 4. Register the pad logic handler with a no-op deleter (s_logic is static).
    static auto s_logicSp = std::shared_ptr<StmPadEditorLogic>(&s_logic, [](StmPadEditorLogic*){});
    pm.registerPadLogic("StmPadEditor", s_logicSp);
    s_logic.setSelectCallback(on_pad_selected, nullptr);
    pm.setActivePadLogic("StmPadEditor");

    // 5. Initialize selection.
    s_sel = 0;
    load_pad_fields(0);

    // 6. Cross-task poll timer.
    s_pendingSelValid = false;
    s_pollTimer = lv_timer_create(poll_cb, 100, nullptr);

    // 7. Encoder focus: save the caller's group, build ours, bind the encoder.
    //    Mirrors midi_ble_panel.cpp (crosspad-gui native, no platform-idf dep).
    s_group = lv_group_create();
    lv_group_add_obj(s_group, s_sensSlider);
    lv_group_add_obj(s_group, s_lrSlider);
    lv_group_add_obj(s_group, s_analogDrop);
    lv_group_add_obj(s_group, s_curveDrop);
    lv_group_add_obj(s_group, s_floorInherit);
    lv_group_add_obj(s_group, s_floorSlider);
    lv_group_add_obj(s_group, closeBtn);
    lv_indev_t* nav = getGuiPlatform().getNavigationEncoder();
    if (nav) {
        s_savedGroup = lv_indev_get_group(nav);
        lv_indev_set_group(nav, s_group);
    }

    return root;
}

} // namespace crosspad_gui

#endif // USE_LVGL
