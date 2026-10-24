// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

/**
 * @file fw_app.h
 * @brief The Firmware app: a launcher of firmware kits in tabs -- Library
 *        (the SD card and the slots), Store and Updates -- with pad LED
 *        feedback. A tile opens its kit card: download, install, run/switch,
 *        pin, delete. A tab's page exists only while it is shown. The
 *        platform registers the app (REGISTER_APP_PL, pad logic "Firmware")
 *        around these two calls and may end the Updates tab with a block of
 *        its own (the board's Over USB block). LVGL thread only.
 */

#ifdef USE_LVGL
#include <stddef.h>
#include "lvgl.h"
#include "crosspad-gui/app/App.hpp"

namespace crosspad_gui {

/// What the platform adds to the app. Every member may be nullptr.
struct FwAppHooks {
    /// Builds the platform's block at the end of the Updates tab into `page`,
    /// its focusables added to `group` in screen order and written to `tail`;
    /// returns how many (at most `cap`). They stay behind the update rows.
    size_t (*buildUpdatesTail)(lv_obj_t* page, lv_group_t* group, lv_obj_t** tail, size_t cap);
    /// The Updates tab closes: the block goes too. `deleteBlock` false: the
    /// app's root is being deleted with the screen, block included -- forget
    /// it, delete nothing.
    void (*closeUpdatesTail)(bool deleteBlock);
    /// The app is shown again.
    void (*shown)();
    /// The launcher's encoder group, given back when the app hides or goes.
    lv_group_t* (*homeGroup)();
};

/// The app's root, built into `parent`; `hooks` must outlive the app.
lv_obj_t* fw_app_create(lv_obj_t* parent, App* a, const FwAppHooks& hooks);

/// Tears the app down: an open confirm and a kit card first, then the tab.
/// `app` nullptr (AppBase: the root was deleted elsewhere, a launcher
/// rebuild): what the app holds is released and nothing is deleted, the views
/// having forgotten their own roots as those went.
void fw_app_destroy(lv_obj_t* app);

/// The container the running Firmware app was started in, where it opens the
/// Settings app; nullptr while it does not run (network mode hosts the views without it).
lv_obj_t* fw_app_container();

} // namespace crosspad_gui

#endif // USE_LVGL
