// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#ifdef USE_LVGL
#include "lvgl.h"

/// The screen shown while the STM32 sits in its ROM DFU bootloader: a panel
/// filling `parent` (crosspad_gui::getOverlayParent() keeps it above any app).
/// In DFU the STM reads neither the encoder nor the power button, so the way
/// out is a tap: it asks first (a tap during a USB update would cut it off),
/// then calls ISettingsUI::leaveStm32Dfu() and closes the panel. One panel at
/// a time: a second call returns the open one.
lv_obj_t * lv_CreateDfuScreen(lv_obj_t * parent);

#endif // USE_LVGL
