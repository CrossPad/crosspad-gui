// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "lvgl.h"
#include "crosspad/config/crosspad_config.h"

#if FEAT_PAD_EDITOR && (CROSSPAD_PAD_CONFIG_LEVEL == CROSSPAD_PAD_CONFIG_FULL)
#  define CROSSPAD_PAD_EDITOR_ENABLED 1
#else
#  define CROSSPAD_PAD_EDITOR_ENABLED 0
#endif

namespace crosspad_gui {

#if CROSSPAD_PAD_EDITOR_ENABLED
// Create the interactive per-pad STM analog tuning screen as a full-size child
// of `parent`. The user presses a physical pad to select it, edits that pad's
// analog config with live STM writes, and closes (Close button) to persist.
// Returns the root container (deleted via lv_obj_delete_async on Close).
lv_obj_t* stm_pad_editor_create(lv_obj_t* parent);
#endif

} // namespace crosspad_gui
