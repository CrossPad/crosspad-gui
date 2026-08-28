#ifdef USE_LVGL

#include "crosspad-gui/components/volume_overlay.h"
#include "crosspad-gui/theme/crosspad_theme.h"
#include "crosspad-gui/platform/IGuiPlatform.h"
#include "crosspad/settings/CrosspadSettings.hpp"
#include "crosspad/settings/ISettingsUI.hpp"

extern crosspad::CrosspadSettings * settings;

namespace crosspad_gui {

// ── Outputs ──────────────────────────────────────────────────────────────────
//
// One row per hardware output, all on screen at once. The overlay used to show
// a single slider and cycle which output it addressed on each power-button
// press, which meant the second output was only reachable by pressing again and
// nothing ever showed both levels together.

struct OutputDef {
    const char * icon;
    const char * name;
    uint8_t      codec;      // index into masterFX.outVolumeCodec
};

static const OutputDef kOutputs[] = {
    { LV_SYMBOL_VOLUME_MAX, "Speakers",   0 },
    { LV_SYMBOL_AUDIO,      "Headphones", 1 },
};
static constexpr int kOutputCount = (int)(sizeof(kOutputs) / sizeof(kOutputs[0]));

// ── State ────────────────────────────────────────────────────────────────────

static volume_apply_cb_t s_apply_cb = nullptr;

static lv_obj_t *    s_overlay     = nullptr;
static lv_obj_t *    s_slider[kOutputCount]      = {};
static lv_obj_t *    s_value_label[kOutputCount] = {};
static lv_group_t *  s_group       = nullptr;
static lv_group_t *  s_saved_group = nullptr;
static lv_timer_t *  s_auto_hide_timer = nullptr;

static constexpr int32_t  ROW_H         = 40;
static constexpr int32_t  OVERLAY_W     = 280;
static constexpr uint32_t AUTO_HIDE_MS  = 4000;

// ── Internal helpers ─────────────────────────────────────────────────────────

static uint8_t stored_volume(int idx) {
    if (!settings) return 0;
    return settings->masterFX.outVolumeCodec[kOutputs[idx].codec][0];
}

static void auto_hide_cb(lv_timer_t *) {
    s_auto_hide_timer = nullptr;   // one-shot: LVGL deletes it after this call
    volume_overlay_dismiss();
}

static void reset_auto_hide() {
    if (s_auto_hide_timer) {
        lv_timer_del(s_auto_hide_timer);
    }
    s_auto_hide_timer = lv_timer_create(auto_hide_cb, AUTO_HIDE_MS, nullptr);
    lv_timer_set_repeat_count(s_auto_hide_timer, 1);
}

static void apply_volume(int idx, uint8_t val) {
    if (settings) {
        settings->masterFX.outVolumeCodec[kOutputs[idx].codec][0] = val;
    }

    auto & plat = crosspad_gui::getGuiPlatform();
    if (kOutputs[idx].codec == 0) plat.setSpeakerVolume(val);
    else                          plat.setHeadphoneVolume(val);

    // Legacy hook keeps its old numbering: 1 = speakers, 2 = headphones.
    if (s_apply_cb) s_apply_cb(kOutputs[idx].codec + 1, val);
}

static void slider_cb(lv_event_t * e) {
    if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) return;
    lv_obj_t * slider = (lv_obj_t *)lv_event_get_target(e);
    const int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (idx < 0 || idx >= kOutputCount) return;

    const uint8_t val = (uint8_t)lv_slider_get_value(slider);
    apply_volume(idx, val);
    if (s_value_label[idx]) {
        lv_label_set_text_fmt(s_value_label[idx], "%d", (int)val);
    }
    reset_auto_hide();
}

// Any encoder activity — moving between rows as well as changing a value —
// counts as interaction, or the overlay would time out while you were still
// deciding which output to touch.
static void group_focus_cb(lv_group_t *) {
    reset_auto_hide();
}

static void build_row(lv_obj_t * parent, int idx) {
    lv_obj_t * row = lv_obj_create(parent);
    lv_obj_set_size(row, LV_PCT(100), ROW_H);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t * name = lv_label_create(row);
    lv_label_set_text_fmt(name, "%s %s", kOutputs[idx].icon, kOutputs[idx].name);
    lv_obj_set_style_text_color(name, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(name, &lv_font_montserrat_14, 0);
    lv_obj_align(name, LV_ALIGN_TOP_LEFT, 0, 0);

    lv_obj_t * value = lv_label_create(row);
    lv_obj_set_style_text_color(value, CROSSPAD_THEME_PRIMARY, 0);
    lv_obj_set_style_text_font(value, &lv_font_montserrat_14, 0);
    lv_obj_align(value, LV_ALIGN_TOP_RIGHT, 0, 0);
    s_value_label[idx] = value;

    lv_obj_t * slider = lv_slider_create(row);
    lv_slider_set_range(slider, 0, 100);  // codec volume range
    lv_obj_set_width(slider, OVERLAY_W - 16 - 8);
    lv_obj_set_height(slider, 10);
    lv_obj_align(slider, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_add_event_cb(slider, slider_cb, LV_EVENT_VALUE_CHANGED, (void *)(intptr_t)idx);
    s_slider[idx] = slider;

    const uint8_t v = stored_volume(idx);
    lv_slider_set_value(slider, v, LV_ANIM_OFF);
    lv_label_set_text_fmt(value, "%d", (int)v);
}

static void show() {
    if (s_overlay) {          // already up — just refresh the levels
        for (int i = 0; i < kOutputCount; ++i) {
            const uint8_t v = stored_volume(i);
            lv_slider_set_value(s_slider[i], v, LV_ANIM_OFF);
            lv_label_set_text_fmt(s_value_label[i], "%d", (int)v);
        }
        reset_auto_hide();
        return;
    }

    s_overlay = lv_obj_create(crosspad_gui::getOverlayParent());
    lv_obj_set_size(s_overlay, OVERLAY_W,
                    16 + kOutputCount * ROW_H + (kOutputCount - 1) * 6);
    lv_obj_align(s_overlay, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(s_overlay, lv_color_hex(0x1A1A1A), 0);
    lv_obj_set_style_bg_opa(s_overlay, LV_OPA_90, 0);
    lv_obj_set_style_border_color(s_overlay, lv_color_hex(0x444444), 0);
    lv_obj_set_style_border_width(s_overlay, 1, 0);
    lv_obj_set_style_radius(s_overlay, 8, 0);
    lv_obj_set_style_pad_all(s_overlay, 8, 0);
    lv_obj_set_style_pad_row(s_overlay, 6, 0);
    lv_obj_set_flex_flow(s_overlay, LV_FLEX_FLOW_COLUMN);
    lv_obj_remove_flag(s_overlay, LV_OBJ_FLAG_SCROLLABLE);

    for (int i = 0; i < kOutputCount; ++i) {
        build_row(s_overlay, i);
    }

    // Encoder: rotate walks between the rows, press enters and leaves a row's
    // edit mode. The old single-slider overlay went straight into edit mode
    // because there was nowhere else to go; with more than one output that
    // would strand the encoder on whichever row happened to be first.
    s_group = lv_group_create();
    for (int i = 0; i < kOutputCount; ++i) {
        lv_group_add_obj(s_group, s_slider[i]);
    }
    lv_group_set_focus_cb(s_group, group_focus_cb);

    lv_indev_t * nav = crosspad_gui::getGuiPlatform().getNavigationEncoder();
    if (nav) {
        s_saved_group = lv_indev_get_group(nav);
        lv_indev_set_group(nav, s_group);
    }
    lv_group_focus_obj(s_slider[0]);

    reset_auto_hide();
}

// ── Public API ───────────────────────────────────────────────────────────────

void volume_overlay_set_apply_cb(volume_apply_cb_t cb) {
    s_apply_cb = cb;
}

void volume_overlay_toggle() {
    if (s_overlay) volume_overlay_dismiss();
    else           show();
}

void volume_overlay_dismiss() {
    if (s_auto_hide_timer) {
        lv_timer_del(s_auto_hide_timer);
        s_auto_hide_timer = nullptr;
    }
    if (s_overlay) {
        // Persist updated volumes to NVS once the overlay closes.
        auto * ui = crosspad::getSettingsUI();
        if (ui) ui->saveSettings();
        lv_obj_del(s_overlay);
        s_overlay = nullptr;
        for (int i = 0; i < kOutputCount; ++i) {
            s_slider[i]      = nullptr;
            s_value_label[i] = nullptr;
        }
    }
    if (s_group) {
        lv_indev_t * nav = crosspad_gui::getGuiPlatform().getNavigationEncoder();
        if (s_saved_group && nav) {
            lv_indev_set_group(nav, s_saved_group);
        }
        lv_group_del(s_group);
        s_group       = nullptr;
        s_saved_group = nullptr;
    }
}

bool volume_overlay_is_visible() {
    return s_overlay != nullptr;
}

lv_group_t * volume_overlay_group() {
    return s_group;
}

} // namespace crosspad_gui

#endif // USE_LVGL
