// SPDX-License-Identifier: GPL-3.0-or-later
//
// Settings: the Vibration category.

#ifdef USE_LVGL

#include "settings_ui_internal.h"
#include "crosspad/settings/CrosspadSettings.hpp"
#include "crosspad/platform/IVibration.hpp"

using namespace crosspad;

namespace crosspad_gui {

using namespace settings_detail;

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

void settings_detail::cat_build_vibration(lv_obj_t * parent, lv_group_t * group) {
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

} // namespace crosspad_gui

#endif // USE_LVGL
