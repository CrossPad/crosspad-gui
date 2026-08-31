#ifdef USE_LVGL

#include "crosspad-gui/components/volume_overlay.h"
#include "crosspad-gui/widgets/gui_helpers.h"
#include "crosspad-gui/theme/crosspad_theme.h"
#include "crosspad-gui/platform/IGuiPlatform.h"
#include "crosspad/settings/CrosspadSettings.hpp"
#include "crosspad/settings/ISettingsUI.hpp"

extern crosspad::CrosspadSettings * settings;

namespace crosspad_gui {

// ── State ────────────────────────────────────────────────────────────────────

static volume_apply_cb_t s_apply_cb = nullptr;

static lv_obj_t *    s_overlay       = nullptr;
static lv_obj_t *    s_slider        = nullptr;
static lv_obj_t *    s_label         = nullptr;
static lv_obj_t *    s_value_label   = nullptr;
static lv_group_t *  s_group         = nullptr;
static lv_group_t *  s_saved_group   = nullptr;
static int           s_mode          = 0; // 0=hidden, 1=speakers, 2=headphones
static lv_timer_t *  s_auto_hide_timer = nullptr;

// ── Internal helpers ─────────────────────────────────────────────────────────

static void auto_hide_cb(lv_timer_t *) {
    volume_overlay_dismiss();
}

static void reset_auto_hide() {
    if (s_auto_hide_timer) {
        lv_timer_del(s_auto_hide_timer);
    }
    s_auto_hide_timer = lv_timer_create(auto_hide_cb, 3000, nullptr);
    lv_timer_set_repeat_count(s_auto_hide_timer, 1);
}

static void slider_cb(lv_event_t * e) {
    if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) return;
    lv_obj_t * slider = (lv_obj_t *)lv_event_get_target(e);
    uint8_t val = (uint8_t)lv_slider_get_value(slider);

    // Persist into settings (per-codec, OUT1 = speakers/headphones).
    if (settings) {
        if (s_mode == 1) {
            settings->masterFX.outVolumeCodec[0][0] = val;
        } else if (s_mode == 2) {
            settings->masterFX.outVolumeCodec[1][0] = val;
        }
    }

    // Hardware apply through the GUI platform.
    auto & plat = crosspad_gui::getGuiPlatform();
    if (s_mode == 1) plat.setSpeakerVolume(val);
    else if (s_mode == 2) plat.setHeadphoneVolume(val);

    // Optional legacy callback hook (kept for compatibility).
    if (s_apply_cb) s_apply_cb(s_mode, val);

    if (s_value_label) {
        lv_label_set_text_fmt(s_value_label, "%d", (int)val);
    }
    reset_auto_hide();
}

static void show(int mode) {
    // If already visible, just update mode and labels
    if (s_overlay) {
        s_mode = mode;
        if (mode == 1) {
            lv_label_set_text(s_label, LV_SYMBOL_VOLUME_MAX " Speakers");
            uint8_t v = settings->masterFX.outVolumeCodec[0][0];
            lv_slider_set_value(s_slider, v, LV_ANIM_OFF);
            if (s_value_label) lv_label_set_text_fmt(s_value_label, "%d", (int)v);
        } else {
            lv_label_set_text(s_label, LV_SYMBOL_AUDIO " Headphones");
            uint8_t v = settings->masterFX.outVolumeCodec[1][0];
            lv_slider_set_value(s_slider, v, LV_ANIM_OFF);
            if (s_value_label) lv_label_set_text_fmt(s_value_label, "%d", (int)v);
        }
        if (s_group) lv_group_set_editing(s_group, true);
        reset_auto_hide();
        return;
    }

    s_mode = mode;

    s_overlay = lv_obj_create(crosspad_gui::getOverlayParent());
    lv_obj_set_size(s_overlay, 280, 55);
    lv_obj_align(s_overlay, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(s_overlay, lv_color_hex(0x1A1A1A), 0);
    lv_obj_set_style_bg_opa(s_overlay, LV_OPA_90, 0);
    lv_obj_set_style_border_color(s_overlay, lv_color_hex(0x444444), 0);
    lv_obj_set_style_border_width(s_overlay, 1, 0);
    lv_obj_set_style_radius(s_overlay, 8, 0);
    lv_obj_set_style_pad_all(s_overlay, 8, 0);
    lv_obj_clear_flag(s_overlay, LV_OBJ_FLAG_SCROLLABLE);

    s_label = lv_label_create(s_overlay);
    lv_obj_set_style_text_color(s_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(s_label, &lv_font_montserrat_14, 0);
    lv_obj_align(s_label, LV_ALIGN_TOP_LEFT, 0, 0);

    s_value_label = lv_label_create(s_overlay);
    lv_obj_set_style_text_color(s_value_label, CROSSPAD_THEME_PRIMARY, 0);
    lv_obj_set_style_text_font(s_value_label, &lv_font_montserrat_14, 0);
    lv_obj_align(s_value_label, LV_ALIGN_TOP_RIGHT, 0, 0);

    s_slider = lv_slider_create(s_overlay);
    lv_slider_set_range(s_slider, 0, 100);  // codec volume range
    lv_obj_set_width(s_slider, 256);
    lv_obj_set_height(s_slider, 10);
    lv_obj_align(s_slider, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_add_event_cb(s_slider, slider_cb, LV_EVENT_VALUE_CHANGED, nullptr);

    if (mode == 1) {
        lv_label_set_text(s_label, LV_SYMBOL_VOLUME_MAX " Speakers");
        uint8_t v = settings->masterFX.outVolumeCodec[0][0];
        lv_slider_set_value(s_slider, v, LV_ANIM_OFF);
        lv_label_set_text_fmt(s_value_label, "%d", (int)v);
    } else {
        lv_label_set_text(s_label, LV_SYMBOL_AUDIO " Headphones");
        uint8_t v = settings->masterFX.outVolumeCodec[1][0];
        lv_slider_set_value(s_slider, v, LV_ANIM_OFF);
        lv_label_set_text_fmt(s_value_label, "%d", (int)v);
    }

    s_group = lv_group_create();
    lv_group_add_obj(s_group, s_slider);
    lv_indev_t * nav = crosspad_gui::getGuiPlatform().getNavigationEncoder();
    if (nav) {
        s_saved_group = lv_indev_get_group(nav);
        lv_indev_set_group(nav, s_group);
    }
    lv_group_focus_obj(s_slider);
    lv_group_set_editing(s_group, true);

    reset_auto_hide();
}

// ── Public API ───────────────────────────────────────────────────────────────

void volume_overlay_set_apply_cb(volume_apply_cb_t cb) {
    s_apply_cb = cb;
}

void volume_overlay_toggle() {
    if (s_mode == 0) {
        show(1);
    } else if (s_mode == 1) {
        show(2);
    } else {
        volume_overlay_dismiss();
    }
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
        s_overlay     = nullptr;
        s_slider      = nullptr;
        s_label       = nullptr;
        s_value_label = nullptr;
    }
    if (s_group) {
        lv_indev_t * nav = crosspad_gui::getGuiPlatform().getNavigationEncoder();
        if (s_saved_group && nav) {
            lv_indev_restore_group(nav, s_saved_group);
        }
        lv_group_del(s_group);
        s_group       = nullptr;
        s_saved_group = nullptr;
    }
    s_mode = 0;
}

bool volume_overlay_is_visible() {
    return s_overlay != nullptr;
}

} // namespace crosspad_gui

#endif // USE_LVGL
