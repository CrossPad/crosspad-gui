// SPDX-License-Identifier: GPL-3.0-or-later
//
// Settings: the Pads & LEDs category.

#ifdef USE_LVGL

#include "settings_ui_internal.h"
#include "crosspad/settings/CrosspadSettings.hpp"
#include "crosspad/pad/PadLedController.hpp"

using namespace crosspad;

namespace crosspad_gui {

using namespace settings_detail;

// ---- Pads & LEDs ----

static void rgb_brightness_hw_cb(lv_event_t * e) {
    (void)e;
    crosspad::getPadLedController().setBrightness(get_settings()->RGBbrightness);
}

void settings_detail::cat_build_pads(lv_obj_t * parent, lv_group_t * group) {
    auto* s = get_settings();
    if (!s) return;

    settings_row_slider(parent, group, "RGB Brightness", 0, 255,
                        &s->RGBbrightness, rgb_brightness_hw_cb);

    settings_section_header(parent, "Behavior");

    // Gates pad→MIDI in PadManager. Was unreachable from the UI until now
    // (System → "STM Keypad Scan" is a different thing: the STM scan enable).
    settings_row_switch(parent, group, "Pad MIDI Output",
                        &s->keypad.enableKeypad);

}

} // namespace crosspad_gui

#endif // USE_LVGL
