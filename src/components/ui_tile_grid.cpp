#ifdef USE_LVGL

#include "crosspad-gui/components/ui_tile_grid.h"

#include <cstdint>
#include <cstring>

namespace crosspad_gui {
namespace {

constexpr uint8_t kMaxCols = 6;
constexpr uint8_t kMaxSections = 4;
constexpr uint8_t kMaxSlots = 8;          // pooled rows, whatever the viewport
constexpr int32_t kSlackRows = 1;         // rows built past the viewport on each side
constexpr uint32_t kUnused = UINT32_MAX;  // a pooled tile that shows nothing
constexpr size_t kMaxTiles = static_cast<size_t>(kMaxSlots) * kMaxCols;

struct TileRow {
    int32_t y;        ///< top of its tiles
    int32_t top;      ///< where to scroll to show it: its section's title for a first row
    uint32_t first;   ///< item of its first tile
    uint8_t n;        ///< tiles in it
};

struct Slot {
    int32_t row;      ///< index into rows, -1 free
    lv_obj_t* tiles[kMaxCols];
};

struct Grid {
    TileGridConfig cfg;
    lv_obj_t* obj;
    lv_group_t* group;
    lv_obj_t* empty;
    lv_obj_t* tail;
    lv_obj_t* headers[kMaxSections];
    TileRow* rows;
    uint32_t rowCount;
    Slot slots[kMaxSlots];
    uint8_t slotCount;
    uint32_t count;
    int32_t sel;
    int32_t tileW;
    /// Inside our own scroll/focus/hide calls: the events they raise are not the user's.
    bool quiet;
};

Grid* grid_of(lv_obj_t* obj) { return obj ? static_cast<Grid*>(lv_obj_get_user_data(obj)) : nullptr; }

uint32_t index_of(lv_obj_t* tile) {
    return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(lv_obj_get_user_data(tile)));
}

void set_index(lv_obj_t* tile, uint32_t index) {
    lv_obj_set_user_data(tile, reinterpret_cast<void*>(static_cast<uintptr_t>(index)));
}

/// The caller's callbacks run with `quiet` set: whatever they call back into
/// the grid cannot be taken for the user's own scroll or focus.
void notify_selected(Grid* g, uint32_t index) {
    if (!g->cfg.selected) return;
    const bool wasQuiet = g->quiet;
    g->quiet = true;
    g->cfg.selected(index, g->cfg.user);
    g->quiet = wasQuiet;
}

void notify_window(Grid* g) {
    if (!g->cfg.window) return;
    const bool wasQuiet = g->quiet;
    g->quiet = true;
    g->cfg.window(g->cfg.user);
    g->quiet = wasQuiet;
}

int32_t pitch(const Grid* g) { return g->cfg.tileH + g->cfg.gap; }
int32_t view_h(const Grid* g) { return lv_obj_get_content_height(g->obj); }

int32_t row_of(const Grid* g, uint32_t index) {
    int32_t lo = 0;
    int32_t hi = static_cast<int32_t>(g->rowCount) - 1;
    while (lo <= hi) {
        const int32_t mid = (lo + hi) / 2;
        const TileRow& r = g->rows[mid];
        if (index < r.first) hi = mid - 1;
        else if (index >= r.first + r.n) lo = mid + 1;
        else return mid;
    }
    return -1;
}

lv_obj_t* tile_of(const Grid* g, uint32_t index) {
    const int32_t r = row_of(g, index);
    if (r < 0) return nullptr;
    for (uint8_t s = 0; s < g->slotCount; ++s)
        if (g->slots[s].row == r) return g->slots[s].tiles[index - g->rows[r].first];
    return nullptr;
}

bool is_tile(const Grid* g, const lv_obj_t* obj) {
    if (!obj) return false;
    for (uint8_t s = 0; s < g->slotCount; ++s)
        for (uint8_t c = 0; c < g->cfg.cols; ++c)
            if (g->slots[s].tiles[c] == obj) return true;
    return false;
}

void visible_rows(const Grid* g, int32_t& first, int32_t& last) {
    const int32_t sy = lv_obj_get_scroll_y(g->obj);
    const int32_t h = view_h(g);
    first = last = -1;
    for (uint32_t r = 0; r < g->rowCount; ++r) {
        const int32_t y = g->rows[r].y;
        if (y + g->cfg.tileH <= sy) continue;
        if (y >= sy + h) break;
        if (first < 0) first = static_cast<int32_t>(r);
        last = static_cast<int32_t>(r);
    }
}

void wanted_window(const Grid* g, int32_t& first, int32_t& last) {
    int32_t vf = 0;
    int32_t vl = 0;
    visible_rows(g, vf, vl);
    if (vf < 0) {
        first = 0;
        last = -1;
        return;
    }
    first = vf - kSlackRows < 0 ? 0 : vf - kSlackRows;
    last = vl + kSlackRows >= static_cast<int32_t>(g->rowCount) ? static_cast<int32_t>(g->rowCount) - 1
                                                                 : vl + kSlackRows;
    // The pool caps the window: give up slack on the side away from the selection.
    const int32_t selRow = g->sel >= 0 ? row_of(g, static_cast<uint32_t>(g->sel)) : vf;
    while (last - first + 1 > g->slotCount) {
        if (selRow - first > last - selRow) ++first;
        else --last;
    }
}

void free_tile(Grid* g, lv_obj_t* tile) {
    if (index_of(tile) != kUnused && g->cfg.unused) g->cfg.unused(tile, g->cfg.user);
    set_index(tile, kUnused);
    lv_obj_add_flag(tile, LV_OBJ_FLAG_HIDDEN);   // a focused tile hands focus on: quiet covers it
}

void free_slot(Grid* g, Slot& s) {
    s.row = -1;
    for (uint8_t c = 0; c < g->cfg.cols; ++c) free_tile(g, s.tiles[c]);
}

void place_row(Grid* g, Slot& s, int32_t row) {
    s.row = row;
    const TileRow& r = g->rows[row];
    for (uint8_t c = 0; c < g->cfg.cols; ++c) {
        lv_obj_t* t = s.tiles[c];
        if (c >= r.n) {
            free_tile(g, t);
            continue;
        }
        lv_obj_set_pos(t, c * (g->tileW + g->cfg.gap), r.y);
        set_index(t, r.first + c);
        lv_obj_remove_flag(t, LV_OBJ_FLAG_HIDDEN);
        if (g->cfg.fill) g->cfg.fill(t, r.first + c, g->cfg.user);
    }
}

/// The pool's tiles in the group in item order, unused ones last, without
/// moving anything else in the group: swaps among our own entries only.
void reorder(Grid* g) {
    if (!g->group) return;
    lv_obj_t* cur[kMaxTiles];
    size_t n = 0;
    const uint32_t total = lv_group_get_obj_count(g->group);
    for (uint32_t i = 0; i < total && n < kMaxTiles; ++i) {
        lv_obj_t* o = lv_group_get_obj_by_index(g->group, i);
        if (is_tile(g, o)) cur[n++] = o;
    }
    lv_obj_t* want[kMaxTiles];
    std::memcpy(want, cur, n * sizeof(lv_obj_t*));
    for (size_t i = 1; i < n; ++i) {
        lv_obj_t* v = want[i];
        size_t j = i;
        for (; j > 0 && index_of(want[j - 1]) > index_of(v); --j) want[j] = want[j - 1];
        want[j] = v;
    }
    for (size_t k = 0; k < n; ++k) {
        if (cur[k] == want[k]) continue;
        size_t j = k + 1;
        while (j < n && cur[j] != want[k]) ++j;
        if (j == n) continue;
        lv_group_swap_obj(cur[k], cur[j]);   // also moves the focus: put back by the caller
        lv_obj_t* tmp = cur[k];
        cur[k] = cur[j];
        cur[j] = tmp;
    }
}

/// Rows to the window around the viewport; true when the selection had to move.
bool update_window(Grid* g) {
    const bool wasQuiet = g->quiet;
    g->quiet = true;
    lv_obj_t* focused = g->group ? lv_group_get_focused(g->group) : nullptr;
    const bool focusOnGrid = is_tile(g, focused);
    int32_t first = 0;
    int32_t last = -1;
    wanted_window(g, first, last);
    for (uint8_t s = 0; s < g->slotCount; ++s) {
        const int32_t r = g->slots[s].row;
        if (r >= 0 && (r < first || r > last)) free_slot(g, g->slots[s]);
    }
    for (int32_t r = first; r <= last; ++r) {
        bool placed = false;
        for (uint8_t s = 0; s < g->slotCount && !placed; ++s) placed = g->slots[s].row == r;
        for (uint8_t s = 0; s < g->slotCount && !placed; ++s) {
            if (g->slots[s].row < 0) {
                place_row(g, g->slots[s], r);
                placed = true;
            }
        }
    }
    reorder(g);
    bool moved = false;
    if (g->sel >= 0 && !tile_of(g, static_cast<uint32_t>(g->sel))) {
        // A drag took the selected tile away: the nearest item in view takes over.
        int32_t vf = 0;
        int32_t vl = 0;
        visible_rows(g, vf, vl);
        if (vf >= 0) {
            const uint32_t lo = g->rows[vf].first;
            const uint32_t hi = g->rows[vl].first + g->rows[vl].n - 1;
            g->sel = static_cast<int32_t>(static_cast<uint32_t>(g->sel) < lo ? lo : hi);
            moved = true;
        }
    }
    if (focusOnGrid && g->sel >= 0) {
        lv_obj_t* t = tile_of(g, static_cast<uint32_t>(g->sel));
        if (t && lv_group_get_focused(g->group) != t) lv_group_focus_obj(t);
    }
    g->quiet = wasQuiet;
    return moved;
}

void scroll_into_view(Grid* g, uint32_t index) {
    const int32_t r = row_of(g, index);
    if (r < 0) return;
    const int32_t sy = lv_obj_get_scroll_y(g->obj);
    const int32_t h = view_h(g);
    int32_t want = sy;
    if (g->rows[r].top < sy) want = g->rows[r].top;
    else if (g->rows[r].y + g->cfg.tileH > sy + h) want = g->rows[r].y + g->cfg.tileH - h;
    if (want != sy) lv_obj_scroll_to_y(g->obj, want, LV_ANIM_OFF);
}

void select_index(Grid* g, uint32_t index, bool focus) {
    if (index >= g->count) return;
    g->sel = static_cast<int32_t>(index);
    const bool wasQuiet = g->quiet;
    g->quiet = true;
    scroll_into_view(g, index);
    update_window(g);
    lv_obj_t* t = tile_of(g, index);
    if (t && g->group && (focus || is_tile(g, lv_group_get_focused(g->group))) && lv_group_get_focused(g->group) != t)
        lv_group_focus_obj(t);
    g->quiet = wasQuiet;
    notify_selected(g, index);
    notify_window(g);
}

void tile_clicked_cb(lv_event_t* e) {
    lv_obj_t* tile = lv_event_get_target_obj(e);
    Grid* g = grid_of(lv_obj_get_parent(tile));
    const uint32_t index = index_of(tile);
    if (!g || index == kUnused || !g->cfg.clicked) return;
    g->cfg.clicked(index, g->cfg.user);
}

void tile_focused_cb(lv_event_t* e) {
    lv_obj_t* tile = lv_event_get_target_obj(e);
    Grid* g = grid_of(lv_obj_get_parent(tile));
    const uint32_t index = index_of(tile);
    if (!g || g->quiet || index == kUnused) return;
    select_index(g, index, false);
}

void scrolled_cb(lv_event_t* e) {
    Grid* g = grid_of(lv_event_get_target_obj(e));
    if (!g || g->quiet) return;
    if (update_window(g)) notify_selected(g, static_cast<uint32_t>(g->sel));
}

void scroll_end_cb(lv_event_t* e) {
    Grid* g = grid_of(lv_event_get_target_obj(e));
    if (!g || g->quiet) return;
    notify_window(g);
}

void deleted_cb(lv_event_t* e) {
    lv_obj_t* obj = lv_event_get_target_obj(e);
    Grid* g = grid_of(obj);
    if (!g) return;
    // The children go after this event, and removing a focused tile from the
    // group focuses another one: its callbacks must find no grid.
    lv_obj_set_user_data(obj, nullptr);
    lv_free(g->rows);
    lv_free(g);
}

lv_obj_t* make_tile(Grid* g) {
    lv_obj_t* t = lv_obj_create(g->obj);
    lv_obj_remove_style_all(t);
    lv_obj_set_size(t, g->tileW, g->cfg.tileH);
    lv_obj_add_flag(t, LV_OBJ_FLAG_CLICKABLE);
    // We scroll for the encoder ourselves (one row, no animation):
    // LVGL's scroll-on-focus would animate and fight the window.
    lv_obj_remove_flag(t, static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_SCROLL_ON_FOCUS));
    set_index(t, kUnused);
    lv_obj_add_flag(t, LV_OBJ_FLAG_HIDDEN);
    if (g->cfg.create) g->cfg.create(t, g->cfg.user);
    lv_obj_add_event_cb(t, tile_clicked_cb, LV_EVENT_CLICKED, nullptr);
    lv_obj_add_event_cb(t, tile_focused_cb, LV_EVENT_FOCUSED, nullptr);
    if (g->group) lv_group_add_obj(g->group, t);
    return t;
}

/// Fits the pool to the grid's size now: tiles take the current width and rows
/// are added (never removed) until the viewport and its slack are covered.
void fit_pool(Grid* g) {
    lv_obj_update_layout(g->obj);
    const int32_t w = lv_obj_get_content_width(g->obj);
    const int32_t tileW = (w - (g->cfg.cols - 1) * g->cfg.gap) / g->cfg.cols;
    g->tileW = tileW > 1 ? tileW : 1;
    int32_t rows = (view_h(g) + pitch(g) - 1) / pitch(g) + 2 * kSlackRows;
    if (rows > kMaxSlots) rows = kMaxSlots;
    const bool wasQuiet = g->quiet;
    g->quiet = true;
    while (g->slotCount < rows) {
        Slot& s = g->slots[g->slotCount];
        s.row = -1;
        for (uint8_t c = 0; c < g->cfg.cols; ++c) s.tiles[c] = make_tile(g);
        ++g->slotCount;
    }
    for (uint8_t s = 0; s < g->slotCount; ++s) {
        for (uint8_t c = 0; c < g->cfg.cols; ++c) {
            lv_obj_set_size(g->slots[s].tiles[c], g->tileW, g->cfg.tileH);
            if (g->slots[s].row >= 0) lv_obj_set_x(g->slots[s].tiles[c], c * (g->tileW + g->cfg.gap));
        }
    }
    g->quiet = wasQuiet;
}

void size_changed_cb(lv_event_t* e) {
    Grid* g = grid_of(lv_event_get_target_obj(e));
    if (!g || !g->slotCount) return;   // before the first set_sections the pool is built at its size
    fit_pool(g);
    update_window(g);
    notify_window(g);
}

} // namespace

lv_obj_t* ui_tile_grid_create(lv_obj_t* parent, lv_group_t* group, const TileGridConfig& cfg) {
    lv_obj_t* obj = lv_obj_create(parent);
    lv_obj_remove_style_all(obj);
    lv_obj_set_scroll_dir(obj, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_OFF);
    auto* g = static_cast<Grid*>(lv_malloc_zeroed(sizeof(Grid)));
    g->cfg = cfg;
    if (g->cfg.cols == 0) g->cfg.cols = 1;
    if (g->cfg.cols > kMaxCols) g->cfg.cols = kMaxCols;
    g->obj = obj;
    g->group = group;
    g->sel = -1;
    lv_obj_set_user_data(obj, g);
    // Rows come and go; this one pixel at the end of the last row is what
    // gives the content its height (the scroll extent).
    g->tail = lv_obj_create(obj);
    lv_obj_remove_style_all(g->tail);
    lv_obj_set_size(g->tail, 1, 1);
    lv_obj_remove_flag(g->tail, LV_OBJ_FLAG_CLICKABLE);
    g->empty = lv_label_create(obj);
    lv_label_set_text(g->empty, cfg.emptyText ? cfg.emptyText : "");
    lv_obj_center(g->empty);
    lv_obj_add_flag(g->empty, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(obj, scrolled_cb, LV_EVENT_SCROLL, nullptr);
    lv_obj_add_event_cb(obj, size_changed_cb, LV_EVENT_SIZE_CHANGED, nullptr);
    lv_obj_add_event_cb(obj, scroll_end_cb, LV_EVENT_SCROLL_END, nullptr);
    lv_obj_add_event_cb(obj, deleted_cb, LV_EVENT_DELETE, nullptr);
    return obj;
}

void ui_tile_grid_set_sections(lv_obj_t* obj, const TileGridSection* sections, uint8_t n) {
    Grid* g = grid_of(obj);
    if (!g) return;
    fit_pool(g);
    const bool wasQuiet = g->quiet;
    g->quiet = true;
    for (uint8_t s = 0; s < g->slotCount; ++s) free_slot(g, g->slots[s]);
    for (lv_obj_t*& h : g->headers) {
        if (h) lv_obj_delete(h);
        h = nullptr;
    }
    lv_free(g->rows);
    g->rows = nullptr;
    g->rowCount = 0;
    if (n > kMaxSections) n = kMaxSections;
    uint32_t count = 0;
    uint32_t rowCount = 0;
    for (uint8_t i = 0; i < n; ++i) {
        count += sections[i].count;
        rowCount += (sections[i].count + g->cfg.cols - 1) / g->cfg.cols;
    }
    g->count = count;
    if (rowCount) g->rows = static_cast<TileRow*>(lv_malloc(rowCount * sizeof(TileRow)));
    int32_t y = 0;
    uint32_t first = 0;
    uint32_t r = 0;
    for (uint8_t i = 0; i < n; ++i) {
        const TileGridSection& sec = sections[i];
        if (sec.count == 0) continue;
        const int32_t top = y;
        if (sec.title) {
            lv_obj_t* h = lv_label_create(obj);
            if (g->cfg.headerStyle) lv_obj_add_style(h, g->cfg.headerStyle, 0);
            lv_label_set_text(h, sec.title);
            lv_obj_set_pos(h, 0, y);
            g->headers[i] = h;
            y += g->cfg.headerH;
        }
        for (uint32_t k = 0; k < sec.count; k += g->cfg.cols) {
            TileRow& row = g->rows[r++];
            row.y = y;
            row.top = k == 0 ? top : y;
            row.first = first + k;
            row.n = static_cast<uint8_t>(sec.count - k < g->cfg.cols ? sec.count - k : g->cfg.cols);
            y += pitch(g);
        }
        first += sec.count;
    }
    g->rowCount = r;
    lv_obj_set_pos(g->tail, 0, y > g->cfg.gap ? y - g->cfg.gap : 0);
    if (g->sel >= static_cast<int32_t>(count)) g->sel = static_cast<int32_t>(count) - 1;
    if (count == 0) {
        g->sel = -1;
        lv_obj_remove_flag(g->empty, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(g->empty, LV_OBJ_FLAG_HIDDEN);
    }
    lv_obj_update_layout(obj);
    lv_obj_readjust_scroll(obj, LV_ANIM_OFF);
    update_window(g);
    g->quiet = wasQuiet;
    notify_window(g);
}

void ui_tile_grid_refill(lv_obj_t* obj, uint32_t index) {
    Grid* g = grid_of(obj);
    if (!g || !g->cfg.fill) return;
    lv_obj_t* t = tile_of(g, index);
    if (t) g->cfg.fill(t, index, g->cfg.user);
}

void ui_tile_grid_refill_all(lv_obj_t* obj) {
    Grid* g = grid_of(obj);
    if (!g || !g->cfg.fill) return;
    for (uint8_t s = 0; s < g->slotCount; ++s) {
        if (g->slots[s].row < 0) continue;
        const TileRow& r = g->rows[g->slots[s].row];
        for (uint8_t c = 0; c < r.n; ++c) g->cfg.fill(g->slots[s].tiles[c], r.first + c, g->cfg.user);
    }
}

int32_t ui_tile_grid_selected(lv_obj_t* obj) {
    Grid* g = grid_of(obj);
    return g ? g->sel : -1;
}

void ui_tile_grid_select(lv_obj_t* obj, uint32_t index, bool focus) {
    Grid* g = grid_of(obj);
    if (g) select_index(g, index, focus);
}

uint32_t ui_tile_grid_count(lv_obj_t* obj) {
    Grid* g = grid_of(obj);
    return g ? g->count : 0;
}

lv_obj_t* ui_tile_grid_tile_of(lv_obj_t* obj, uint32_t index) {
    Grid* g = grid_of(obj);
    return g ? tile_of(g, index) : nullptr;
}

bool ui_tile_grid_index_visible(lv_obj_t* obj, uint32_t index) {
    Grid* g = grid_of(obj);
    if (!g) return false;
    const int32_t r = row_of(g, index);
    if (r < 0) return false;
    const int32_t sy = lv_obj_get_scroll_y(g->obj);
    const int32_t top = g->rows[r].y > sy ? g->rows[r].y : sy;
    const int32_t rowBottom = g->rows[r].y + g->cfg.tileH;
    const int32_t viewBottom = sy + view_h(g);
    const int32_t bottom = rowBottom < viewBottom ? rowBottom : viewBottom;
    return (bottom - top) * 2 >= g->cfg.tileH;
}

void ui_tile_grid_for_each(lv_obj_t* obj, TileVisitor visit, void* user) {
    Grid* g = grid_of(obj);
    if (!g || !visit) return;
    for (uint8_t s = 0; s < g->slotCount; ++s) {
        if (g->slots[s].row < 0) continue;
        const TileRow& r = g->rows[g->slots[s].row];
        for (uint8_t c = 0; c < r.n; ++c) visit(g->slots[s].tiles[c], r.first + c, user);
    }
}

uint32_t ui_tile_grid_built_rows(lv_obj_t* obj) {
    Grid* g = grid_of(obj);
    if (!g) return 0;
    uint32_t n = 0;
    for (uint8_t s = 0; s < g->slotCount; ++s) n += g->slots[s].row >= 0;
    return n;
}

uint32_t ui_tile_grid_pool_rows(lv_obj_t* obj) {
    Grid* g = grid_of(obj);
    return g ? g->slotCount : 0;
}

} // namespace crosspad_gui

#endif // USE_LVGL
