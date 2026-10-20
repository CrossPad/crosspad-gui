// SPDX-License-Identifier: GPL-3.0-or-later
#ifdef USE_LVGL

#include "crosspad-gui/platform/touchpad_interaction.h"
#include "crosspad-gui/platform/IGuiPlatform.h"

namespace crosspad_gui {

void gui_touchpad_event_cb(lv_event_t *event)
{
    lv_event_code_t code = lv_event_get_code(event);
    using IE = InteractionEvent;

    switch (code)
    {
    case LV_EVENT_PRESSED:
        getGuiPlatform().onUserInteraction(IE::Pressed);
        break;
    case LV_EVENT_PRESS_LOST:
        getGuiPlatform().onUserInteraction(IE::PressLost);
        break;
    case LV_EVENT_LONG_PRESSED:
        getGuiPlatform().onUserInteraction(IE::LongPressed);
        break;
    case LV_EVENT_LONG_PRESSED_REPEAT:
        getGuiPlatform().onUserInteraction(IE::LongPressedRepeat);
        break;
    case LV_EVENT_RELEASED:
        getGuiPlatform().onUserInteraction(IE::Released);
        break;
    default:
        break;
    }
}

} // namespace crosspad_gui

#endif // USE_LVGL
