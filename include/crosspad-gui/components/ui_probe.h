#pragma once

/**
 * @file ui_probe.h
 * @brief The visible widgets of the current screen and top layer as flat data,
 *        for hosts that document or test the UI from outside (the web twin's
 *        GUI map).
 *
 * LVGL thread only. Strings point into LVGL objects and are valid until the
 * next LVGL call that edits them. Coordinates are LCD pixels, origin top-left,
 * as of the last layout pass: callers lay out first if they just created objects.
 */

#ifdef USE_LVGL
#include <cstdint>
#include <string>
#include "lvgl.h"

namespace crosspad_gui {

struct UiNode {
    const char* cls;      ///< lv_obj_class_t::name without its "lv_" prefix ("button", "label", ...), "obj" when none
    int32_t x, y, w, h;   ///< absolute, LCD pixels
    int16_t parent;       ///< index in the same array, -1 for a layer's root
    const char* text;     ///< own label text, else the first descendant label within 3 levels; nullptr if none
    bool interactive;     ///< LV_OBJ_FLAG_CLICKABLE, or in the navigation encoder's group
    bool checked, disabled, focused;
};

/// Pre-order walk of lv_screen_active() then lv_layer_top(), visible objects
/// only (not hidden, not transparent, on the LCD). Returns the count written.
uint16_t ui_tree_probe(UiNode* out, uint16_t max);

/// `{"running":..,"lcd":{"w":320,"h":240},"nodes":[..]}`, appended to @p out.
/// @p running may be nullptr (written as "").
void ui_tree_json(const UiNode* n, uint16_t count, const char* running, std::string& out);

}  // namespace crosspad_gui
#endif
