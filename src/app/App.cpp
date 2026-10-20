// SPDX-License-Identifier: GPL-3.0-or-later
#ifdef USE_LVGL

#include "crosspad-gui/app/App.hpp"
#include "crosspad-gui/platform/GuiServices.h"
#include "crosspad/pad/PadManager.hpp"
#include "crosspad/pad/PadLedController.hpp"

crosspad::PadManager& App::padManager() {
    return crosspad::getPadManager();
}

crosspad::PadLedController& App::ledController() {
    return crosspad::getPadLedController();
}

CrosspadStatus& App::status() {
    return crosspad_gui::platformStatus();
}

CrosspadSettings& App::settings() {
    return crosspad_gui::platformSettings();
}

#endif // USE_LVGL
