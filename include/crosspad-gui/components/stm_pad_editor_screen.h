#pragma once

#include "lvgl.h"

namespace crosspad_gui {

// Create the interactive per-pad STM analog tuning screen as a full-size child
// of `parent`. The user presses a physical pad to select it, edits that pad's
// analog config with live STM writes, and closes (Close button) to persist.
// Returns the root container (deleted via lv_obj_delete_async on Close).
lv_obj_t* stm_pad_editor_create(lv_obj_t* parent);

} // namespace crosspad_gui
