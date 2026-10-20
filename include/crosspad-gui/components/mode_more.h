// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
/**
 * @file mode_more.h
 * @brief "More" on the MODE map: every app that has no pad of its own.
 *
 * The map has sixteen pads; the firmware can hold more apps than that, and
 * every app a developer adds is one of them. Pad 8 opens this list, and a row
 * opens its app exactly as a pad would.
 *
 * mode_more_build() must be called from the LVGL task.
 */
#ifdef USE_LVGL
#include "lvgl.h"
#include "crosspad-gui/components/mode_overlay.h"

#include <cstddef>
#include <vector>

namespace crosspad_gui {

/// The names in @p registered (registry order) that no entry of @p map opens.
/// Compared case-insensitively, like the map's own lookup. Null names are
/// skipped, and so are launcher-only actions (Power OFF); the pointers are
/// returned, not copied.
std::vector<const char*> mode_more_apps(const ModeEntry* map, size_t count,
                                        const std::vector<const char*>& registered);

/// Called with the chosen app's name.
using ModeMorePickFn = void (*)(const char* name);

/// The list, a child of @p parent that fills it: one button per name, or one
/// label "No other apps". Buttons join @p group (if not null), the first focused.
lv_obj_t* mode_more_build(lv_obj_t* parent, const std::vector<const char*>& names,
                          ModeMorePickFn onPick, lv_group_t* group);

} // namespace crosspad_gui

#endif // USE_LVGL
