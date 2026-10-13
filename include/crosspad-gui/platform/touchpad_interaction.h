// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#ifdef USE_LVGL

#include "lvgl.h"

namespace crosspad_gui {

/**
 * @brief Standard LVGL event callback that maps touch events to
 *        IGuiPlatform::onUserInteraction().
 *
 * Attach to any pointer-type lv_indev via:
 *   lv_indev_add_event_cb(indev, crosspad_gui::gui_touchpad_event_cb, LV_EVENT_ALL, NULL);
 */
void gui_touchpad_event_cb(lv_event_t *event);

} // namespace crosspad_gui

#endif // USE_LVGL
