// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

/**
 * @file ui_tile_grid.h
 * @brief A vertical grid of tiles over any number of items, of which only the
 *        rows in view (±1) have LVGL objects.
 *
 * Built for the Firmware app's kit tiles, reusable by anything with a long
 * tiled list. A fixed pool of rows is created once; scrolling moves rows that
 * left the window to rows that entered it and calls `fill` for their new
 * items. Encoder focus moves over logical indices: the pool's tiles sit in the
 * caller's group in item order (rearranged after every recycle), and a tile
 * that gets focus scrolls its row into view. The group's walk coming onto the
 * grid from another object lands on item 0 going forward and on the last item
 * going back, whatever rows are built. Touch scrolls freely; the
 * selection follows into view. Tiles have no style of their own: `create`
 * builds their content once, `fill` shows an item in it.
 *
 * LVGL thread only. The grid owns nothing the callbacks allocate; a caller
 * that hangs state on a tile frees it from the tile's LV_EVENT_DELETE.
 *
 * Callbacks may call any function here (refill, tile_of, select, set_sections);
 * the grid ignores the focus and scroll events they raise, so move the
 * selection with ui_tile_grid_select, not lv_group_focus_obj. A callback that
 * selects from `selected` is called again for that selection.
 * The pool grows with the grid when it is resized (never shrinks); the rows
 * table is the one allocation that follows the item count, O(items / cols).
 */

#ifdef USE_LVGL
#include <cstdint>
#include "lvgl.h"

namespace crosspad_gui {

/// One run of items under an optional title; each section starts a new row.
struct TileGridSection {
    const char* title;   ///< nullptr: no title row
    uint32_t count;
};

struct TileGridConfig {
    uint8_t cols = 3;
    int32_t tileH = 80;
    int32_t gap = 4;
    int32_t headerH = 18;
    const lv_style_t* headerStyle = nullptr;
    /// Builds a pooled tile's children, once per tile (cols x pool rows calls).
    void (*create)(lv_obj_t* tile, void* user) = nullptr;
    /// Shows item `index` in `tile`: on build and every time the row is reused.
    void (*fill)(lv_obj_t* tile, uint32_t index, void* user) = nullptr;
    /// A tile stopped showing an item (its row left the window, or the items changed):
    /// let go of what `fill` attached to it, an image buffer above all.
    void (*unused)(lv_obj_t* tile, void* user) = nullptr;
    void (*clicked)(uint32_t index, void* user) = nullptr;
    /// The selection moved (encoder focus, a select call, or a drag that left it out of view).
    void (*selected)(uint32_t index, void* user) = nullptr;
    /// Rows were rebuilt or a scroll came to rest: which items are visible may have changed.
    void (*window)(void* user) = nullptr;
    void* user = nullptr;
    const char* emptyText = "";
};

using TileVisitor = void (*)(lv_obj_t* tile, uint32_t index, void* user);

/// The grid is a plain scrollable object: size it like any other.
lv_obj_t* ui_tile_grid_create(lv_obj_t* parent, lv_group_t* group, const TileGridConfig& cfg);

/// Replaces the items (at most 4 sections). Keeps the selection, clamped, and
/// an encoder that was on a tile stays on the grid, on that selection.
void ui_tile_grid_set_sections(lv_obj_t* grid, const TileGridSection* sections, uint8_t n);

/// Calls `fill` again for `index` if it has a tile now; for every built tile.
void ui_tile_grid_refill(lv_obj_t* grid, uint32_t index);
void ui_tile_grid_refill_all(lv_obj_t* grid);

/// The selected item, -1 when there is none (an empty grid).
int32_t ui_tile_grid_selected(lv_obj_t* grid);

/// Selects `index`, scrolls its row into view and, with `focus`, puts the encoder on it.
void ui_tile_grid_select(lv_obj_t* grid, uint32_t index, bool focus);

uint32_t ui_tile_grid_count(lv_obj_t* grid);

/// The tile showing `index`, nullptr when its row has no objects now.
lv_obj_t* ui_tile_grid_tile_of(lv_obj_t* grid, uint32_t index);

/// At least half of the item's tile is inside the viewport (what deserves an image).
bool ui_tile_grid_index_visible(lv_obj_t* grid, uint32_t index);

/// Every tile that shows an item now.
void ui_tile_grid_for_each(lv_obj_t* grid, TileVisitor visit, void* user);

/// Rows with objects now / the pool's size (tests and memory budgets).
uint32_t ui_tile_grid_built_rows(lv_obj_t* grid);
uint32_t ui_tile_grid_pool_rows(lv_obj_t* grid);

} // namespace crosspad_gui

#endif // USE_LVGL
