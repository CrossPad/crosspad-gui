#include <catch2/catch_test_macros.hpp>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "crosspad-gui/components/ui_tile_grid.h"
#include "lvgl_headless.h"

using namespace crosspad_gui;

namespace {

constexpr int32_t kGridW = 300;
constexpr int32_t kGridH = 170;   // two rows and a sliver: a pool of 3 + 2 slack rows
constexpr int32_t kTileH = 80;
constexpr int32_t kGap = 4;
constexpr int32_t kPitch = kTileH + kGap;
constexpr int32_t kHeaderH = 18;

struct Fixture {
    lv_obj_t* screen = nullptr;
    lv_group_t* group = nullptr;
    lv_obj_t* grid = nullptr;
    int fills = 0;
    int unused = 0;
    std::vector<uint32_t> clicks;
    std::vector<uint32_t> selections;
    int refillsFromSelected = 0;
    int windows = 0;

    explicit Fixture(uint32_t count, int32_t gridH = kGridH) {
        lvgl_headless_init();
        screen = lv_obj_create(nullptr);
        lv_screen_load(screen);
        group = lv_group_create();
        lv_group_set_wrap(group, false);
        TileGridConfig cfg;
        cfg.cols = 3;
        cfg.tileH = kTileH;
        cfg.gap = kGap;
        cfg.headerH = kHeaderH;
        cfg.create = [](lv_obj_t* tile, void*) { lv_label_create(tile); };
        cfg.fill = [](lv_obj_t* tile, uint32_t index, void* user) {
            lv_label_set_text_fmt(lv_obj_get_child(tile, 0), "%u", static_cast<unsigned>(index));
            static_cast<Fixture*>(user)->fills++;
        };
        cfg.unused = [](lv_obj_t*, void* user) { static_cast<Fixture*>(user)->unused++; };
        cfg.clicked = [](uint32_t index, void* user) { static_cast<Fixture*>(user)->clicks.push_back(index); };
        cfg.selected = [](uint32_t index, void* user) {
            auto* f = static_cast<Fixture*>(user);
            f->selections.push_back(index);
            ui_tile_grid_refill(f->grid, index);   // calling back into the grid from a callback is allowed
            f->refillsFromSelected++;
        };
        cfg.window = [](void* user) { static_cast<Fixture*>(user)->windows++; };
        cfg.user = this;
        cfg.emptyText = "Nothing here";
        grid = ui_tile_grid_create(screen, group, cfg);
        lv_obj_set_size(grid, kGridW, gridH);
        const TileGridSection one{nullptr, count};
        ui_tile_grid_set_sections(grid, &one, 1);
    }
    ~Fixture() {
        lv_obj_delete(screen);
        lv_group_delete(group);
    }
    uint32_t shown(lv_obj_t* tile) const {
        return static_cast<uint32_t>(std::atoi(lv_label_get_text(lv_obj_get_child(tile, 0))));
    }
};

}  // namespace

TEST_CASE("only the rows around the viewport have objects", "[tile_grid]") {
    Fixture f(40);
    CHECK(ui_tile_grid_count(f.grid) == 40);
    CHECK(ui_tile_grid_pool_rows(f.grid) == 5);
    CHECK(ui_tile_grid_built_rows(f.grid) == 4);   // rows 0-1, the 2 px of row 2 in view, one below
    REQUIRE(ui_tile_grid_tile_of(f.grid, 0));
    CHECK(f.shown(ui_tile_grid_tile_of(f.grid, 0)) == 0);
    CHECK_FALSE(ui_tile_grid_tile_of(f.grid, 39));
    CHECK(ui_tile_grid_index_visible(f.grid, 2));
    CHECK_FALSE(ui_tile_grid_index_visible(f.grid, 6));   // row 2 starts at 168: 2 px of 80 in view
}

TEST_CASE("encoder focus walks logical indices and rows are recycled", "[tile_grid]") {
    Fixture f(40);
    lv_group_focus_obj(ui_tile_grid_tile_of(f.grid, 0));
    for (uint32_t i = 1; i < 40; ++i) {
        lv_group_focus_next(f.group);
        REQUIRE(ui_tile_grid_selected(f.grid) == static_cast<int32_t>(i));
        lv_obj_t* t = ui_tile_grid_tile_of(f.grid, i);
        REQUIRE(t);
        CHECK(lv_group_get_focused(f.group) == t);
        CHECK(f.shown(t) == i);
        CHECK(ui_tile_grid_index_visible(f.grid, i));
        CHECK(ui_tile_grid_built_rows(f.grid) <= ui_tile_grid_pool_rows(f.grid));
    }
    CHECK(f.unused > 0);                             // recycled rows let go of their items
    REQUIRE(f.selections.size() == 40);              // once per step, in order
    for (uint32_t i = 0; i < 40; ++i) CHECK(f.selections[i] == i);
    CHECK(f.refillsFromSelected == 40);
    for (int32_t i = 38; i >= 0; --i) {
        lv_group_focus_prev(f.group);
        REQUIRE(ui_tile_grid_selected(f.grid) == i);
        CHECK(lv_group_get_focused(f.group) == ui_tile_grid_tile_of(f.grid, static_cast<uint32_t>(i)));
    }
}

TEST_CASE("the group keeps the tiles in logical order after recycling", "[tile_grid]") {
    Fixture f(40);
    ui_tile_grid_select(f.grid, 20, true);
    uint32_t last = 0;
    bool first = true;
    for (uint32_t i = 0; i < lv_group_get_obj_count(f.group); ++i) {
        lv_obj_t* o = lv_group_get_obj_by_index(f.group, i);
        if (lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN)) continue;
        const uint32_t idx = f.shown(o);
        if (!first) CHECK(idx > last);
        last = idx;
        first = false;
    }
    CHECK(lv_group_get_focused(f.group) == ui_tile_grid_tile_of(f.grid, 20));
}

TEST_CASE("a fast spin far down and back lands on the right tiles", "[tile_grid]") {
    Fixture f(40);
    ui_tile_grid_select(f.grid, 0, true);
    for (int i = 0; i < 100; ++i) lv_group_focus_next(f.group);   // past the end: stops at 39
    CHECK(ui_tile_grid_selected(f.grid) == 39);
    for (int i = 0; i < 100; ++i) lv_group_focus_prev(f.group);
    CHECK(ui_tile_grid_selected(f.grid) == 0);
    CHECK(lv_group_get_focused(f.group) == ui_tile_grid_tile_of(f.grid, 0));
}

TEST_CASE("a fling moves the window and the selection follows into view", "[tile_grid]") {
    Fixture f(40);
    ui_tile_grid_select(f.grid, 0, true);
    lv_obj_scroll_to_y(f.grid, 6 * kPitch, LV_ANIM_OFF);   // rows 6..7 in view, row 0 long gone
    const int32_t sel = ui_tile_grid_selected(f.grid);
    REQUIRE(sel >= 18);
    lv_obj_t* t = ui_tile_grid_tile_of(f.grid, static_cast<uint32_t>(sel));
    REQUIRE(t);
    CHECK(f.shown(t) == static_cast<uint32_t>(sel));
    CHECK(lv_group_get_focused(f.group) == t);
    CHECK(f.selections.back() == static_cast<uint32_t>(sel));
    CHECK(ui_tile_grid_built_rows(f.grid) <= ui_tile_grid_pool_rows(f.grid));
}

TEST_CASE("sections start on a new row under their title", "[tile_grid]") {
    Fixture f(0);
    const TileGridSection two[] = {{"Official", 2}, {"Community", 5}};
    const int unusedBefore = f.unused;
    ui_tile_grid_set_sections(f.grid, two, 2);
    CHECK(f.unused == unusedBefore);                 // an empty grid had nothing to let go of
    CHECK(ui_tile_grid_count(f.grid) == 7);
    lv_obj_t* a = ui_tile_grid_tile_of(f.grid, 0);
    lv_obj_t* b = ui_tile_grid_tile_of(f.grid, 2);   // first community tile
    REQUIRE(a);
    REQUIRE(b);
    lv_obj_update_layout(f.grid);                    // positions are applied by a layout pass
    CHECK(lv_obj_get_y(a) == kHeaderH);
    CHECK(lv_obj_get_y(b) == kHeaderH + kPitch + kHeaderH);
    CHECK(lv_obj_get_x(b) == 0);
    std::vector<uint32_t> seen;
    ui_tile_grid_for_each(f.grid, [](lv_obj_t*, uint32_t index, void* user) {
        static_cast<std::vector<uint32_t>*>(user)->push_back(index);
    }, &seen);
    CHECK(seen.size() == 7);
    for (uint32_t i = 0; i < seen.size(); ++i) CHECK(seen[i] == i);
}

TEST_CASE("an empty grid has no tiles and no selection", "[tile_grid]") {
    Fixture f(0);
    CHECK(ui_tile_grid_count(f.grid) == 0);
    CHECK(ui_tile_grid_selected(f.grid) == -1);
    CHECK(ui_tile_grid_built_rows(f.grid) == 0);
    CHECK_FALSE(ui_tile_grid_tile_of(f.grid, 0));
    lv_group_focus_next(f.group);                    // nothing to land on, nothing breaks
    CHECK(ui_tile_grid_selected(f.grid) == -1);
}

TEST_CASE("new sections clamp the selection; refills touch built tiles only", "[tile_grid]") {
    Fixture f(40);
    ui_tile_grid_select(f.grid, 39, true);
    const TileGridSection fewer{nullptr, 31};
    f.unused = 0;
    ui_tile_grid_set_sections(f.grid, &fewer, 1);
    CHECK(f.unused > 0);                             // every built tile let go of its item
    CHECK(ui_tile_grid_selected(f.grid) == 30);
    f.fills = 0;
    ui_tile_grid_refill(f.grid, 30);
    CHECK(f.fills == 1);
    ui_tile_grid_refill(f.grid, 0);                  // row 0 is far outside the window
    CHECK(f.fills == 1);
    f.fills = 0;
    ui_tile_grid_refill_all(f.grid);
    CHECK(f.fills > 0);
    CHECK(f.fills <= static_cast<int>(3 * ui_tile_grid_built_rows(f.grid)));
}

TEST_CASE("new sections keep the encoder on the grid", "[tile_grid]") {
    Fixture f(40);
    lv_obj_t* other = lv_button_create(f.screen);   // something else in the group (a tab bar)
    lv_group_add_obj(f.group, other);
    ui_tile_grid_select(f.grid, 5, true);
    const TileGridSection same{nullptr, 40};
    ui_tile_grid_set_sections(f.grid, &same, 1);
    CHECK(ui_tile_grid_selected(f.grid) == 5);
    CHECK(lv_group_get_focused(f.group) == ui_tile_grid_tile_of(f.grid, 5));
    ui_tile_grid_select(f.grid, 30, true);
    const TileGridSection fewer{nullptr, 3};
    ui_tile_grid_set_sections(f.grid, &fewer, 1);
    CHECK(ui_tile_grid_selected(f.grid) == 2);
    REQUIRE(ui_tile_grid_tile_of(f.grid, 2));
    CHECK(lv_group_get_focused(f.group) == ui_tile_grid_tile_of(f.grid, 2));
    lv_group_focus_obj(other);                      // focus elsewhere stays elsewhere
    ui_tile_grid_set_sections(f.grid, &same, 1);
    CHECK(lv_group_get_focused(f.group) == other);
}

TEST_CASE("the walk onto the grid from outside starts at item 0 and covers every item", "[tile_grid]") {
    // The bench sequence: tiles, then a tab bar, wrap on; a deep walk leaves
    // the window at the bottom, so the first pooled tile in the group shows item 27.
    Fixture f(34);
    lv_group_set_wrap(f.group, true);
    lv_obj_t* tabs[3];
    for (lv_obj_t*& t : tabs) {
        t = lv_button_create(f.screen);
        lv_group_add_obj(f.group, t);
    }
    ui_tile_grid_select(f.grid, 0, true);
    for (int i = 0; i < 33; ++i) lv_group_focus_next(f.group);
    REQUIRE(ui_tile_grid_selected(f.grid) == 33);
    CHECK_FALSE(ui_tile_grid_tile_of(f.grid, 0));    // row 0 has no objects now
    for (lv_obj_t* t : tabs) {
        lv_group_focus_next(f.group);
        REQUIRE(lv_group_get_focused(f.group) == t);
    }
    lv_group_focus_next(f.group);                    // forward from the tab bar onto the grid
    CHECK(ui_tile_grid_selected(f.grid) == 0);
    REQUIRE(ui_tile_grid_tile_of(f.grid, 0));
    CHECK(lv_group_get_focused(f.group) == ui_tile_grid_tile_of(f.grid, 0));
    CHECK(ui_tile_grid_index_visible(f.grid, 0));
    std::vector<uint32_t> visited{0};
    for (int i = 0; i < 64; ++i) {
        lv_group_focus_next(f.group);
        if (lv_group_get_focused(f.group) == tabs[0]) break;
        visited.push_back(static_cast<uint32_t>(ui_tile_grid_selected(f.grid)));
        REQUIRE(lv_group_get_focused(f.group) == ui_tile_grid_tile_of(f.grid, visited.back()));
    }
    REQUIRE(visited.size() == 34);                   // every item once, in order, then the tab bar
    for (uint32_t i = 0; i < visited.size(); ++i) CHECK(visited[i] == i);

    lv_group_focus_prev(f.group);                    // back from the tab bar: the last item
    CHECK(ui_tile_grid_selected(f.grid) == 33);
    CHECK(lv_group_get_focused(f.group) == ui_tile_grid_tile_of(f.grid, 33));
    for (int32_t i = 32; i >= 0; --i) {
        lv_group_focus_prev(f.group);
        REQUIRE(ui_tile_grid_selected(f.grid) == i);
        REQUIRE(lv_group_get_focused(f.group) == ui_tile_grid_tile_of(f.grid, static_cast<uint32_t>(i)));
    }
    lv_group_focus_prev(f.group);                    // past item 0 the walk leaves the grid
    CHECK(lv_group_get_focused(f.group) == tabs[2]);

    ui_tile_grid_select(f.grid, 20, true);           // a deep selection, then out and back in
    lv_group_focus_obj(tabs[1]);
    lv_group_focus_prev(f.group);
    lv_group_focus_next(f.group);
    lv_group_focus_next(f.group);
    CHECK(lv_group_get_focused(f.group) == tabs[2]);
    lv_group_focus_next(f.group);
    CHECK(ui_tile_grid_selected(f.grid) == 0);
}

TEST_CASE("a touch on a tile keeps that tile", "[tile_grid]") {
    Fixture f(40);
    lv_obj_t* other = lv_button_create(f.screen);
    lv_group_add_obj(f.group, other);
    ui_tile_grid_select(f.grid, 20, true);
    lv_group_focus_obj(other);
    lv_group_focus_obj(ui_tile_grid_tile_of(f.grid, 22));   // focus set from elsewhere, mid-grid
    CHECK(ui_tile_grid_selected(f.grid) == 22);
    CHECK(lv_group_get_focused(f.group) == ui_tile_grid_tile_of(f.grid, 22));
}

TEST_CASE("a click reports the logical index of the tile", "[tile_grid]") {
    Fixture f(40);
    ui_tile_grid_select(f.grid, 25, true);
    f.windows = 0;
    lv_obj_send_event(ui_tile_grid_tile_of(f.grid, 25), LV_EVENT_CLICKED, nullptr);
    REQUIRE(f.clicks.size() == 1);
    CHECK(f.clicks[0] == 25);
    CHECK(f.windows == 0);                           // a click moves nothing
}

TEST_CASE("a grid sized after its items grows its pool and tiles", "[tile_grid]") {
    Fixture f(40, 40);                               // too short for a row: a minimum pool
    const uint32_t small = ui_tile_grid_pool_rows(f.grid);
    lv_obj_set_size(f.grid, 200, kGridH);
    lv_obj_update_layout(f.grid);
    CHECK(ui_tile_grid_pool_rows(f.grid) > small);
    CHECK(ui_tile_grid_pool_rows(f.grid) == 5);
    struct Probe { int32_t tileW; int visited; } probe{(200 - 2 * kGap) / 3, 0};
    ui_tile_grid_for_each(f.grid, [](lv_obj_t* tile, uint32_t, void* user) {
        auto* p = static_cast<Probe*>(user);
        CHECK(lv_obj_get_width(tile) == p->tileW);
        CHECK_FALSE(lv_obj_has_flag(tile, LV_OBJ_FLAG_HIDDEN));
        ++p->visited;
    }, &probe);
    CHECK(probe.visited == 12);                            // 4 rows built: two in view, the sliver, one below
    CHECK(f.shown(ui_tile_grid_tile_of(f.grid, 11)) == 11);
    ui_tile_grid_select(f.grid, 20, true);
    CHECK(f.shown(ui_tile_grid_tile_of(f.grid, 20)) == 20);
    CHECK(lv_group_get_focused(f.group) == ui_tile_grid_tile_of(f.grid, 20));
}

TEST_CASE("snapshots of the grid as the device draws it", "[tile_grid]") {
    const char* dir = std::getenv("TILE_GRID_SNAPSHOT_DIR");
    if (!dir) return;

    struct Item { const char* name; uint32_t color; };
    static const Item items[] = {
        {"Hip Hop", 0xE5484D}, {"Lo-Fi", 0x3E63DD},  {"Techno", 0x8E4EC6},
        {"Trap", 0xF76B15},    {"House", 0x30A46C},  {"Jungle", 0x12A594},
        {"808 Kit", 0xE93D82}, {"Acoustic", 0xFFB224}, {"Foley", 0x0090FF},
        {"Vocal", 0x7CE2FE},
    };
    lvgl_headless_init();
    static lv_style_t tileStyle, focusStyle, headerStyle;
    lv_style_init(&tileStyle);
    lv_style_set_bg_color(&tileStyle, lv_color_hex(0x1C1F26));
    lv_style_set_bg_opa(&tileStyle, LV_OPA_COVER);
    lv_style_set_radius(&tileStyle, 6);
    lv_style_init(&focusStyle);
    lv_style_set_outline_color(&focusStyle, lv_color_hex(0x00D4FF));
    lv_style_set_outline_width(&focusStyle, 2);
    lv_style_set_outline_pad(&focusStyle, 0);
    lv_style_init(&headerStyle);
    lv_style_set_text_color(&headerStyle, lv_color_hex(0x9AA0AA));

    lv_obj_t* screen = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x0E1014), 0);
    lv_obj_set_style_text_color(screen, lv_color_white(), 0);
    lv_screen_load(screen);
    lv_group_t* group = lv_group_create();

    TileGridConfig cfg;
    cfg.tileH = 70;
    cfg.headerStyle = &headerStyle;
    cfg.create = [](lv_obj_t* tile, void*) {
        lv_obj_add_style(tile, &tileStyle, 0);
        lv_obj_add_style(tile, &focusStyle, LV_STATE_FOCUSED);
        lv_obj_t* icon = lv_obj_create(tile);
        lv_obj_remove_style_all(icon);
        lv_obj_set_size(icon, 36, 36);
        lv_obj_set_style_bg_opa(icon, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(icon, 8, 0);
        lv_obj_align(icon, LV_ALIGN_TOP_MID, 0, 6);
        lv_obj_t* label = lv_label_create(tile);
        lv_obj_align(label, LV_ALIGN_BOTTOM_MID, 0, -6);
    };
    cfg.fill = [](lv_obj_t* tile, uint32_t index, void*) {
        const Item& it = items[index % (sizeof items / sizeof items[0])];
        lv_obj_set_style_bg_color(lv_obj_get_child(tile, 0), lv_color_hex(it.color), 0);
        lv_label_set_text(lv_obj_get_child(tile, 1), it.name);
    };
    lv_obj_t* grid = ui_tile_grid_create(screen, group, cfg);
    lv_obj_set_pos(grid, 8, 8);
    lv_obj_set_size(grid, 304, 224);
    const TileGridSection sections[] = {{"Official", 4}, {"Community", 6}};
    ui_tile_grid_set_sections(grid, sections, 2);

    auto shot = [&](const char* name) {
        char path[512];
        std::snprintf(path, sizeof path, "%s/%s.png", dir, name);
        CHECK(lvgl_headless_write_png(path));
    };
    shot("tile_grid_top");
    lv_obj_scroll_to_y(grid, 100, LV_ANIM_OFF);
    shot("tile_grid_scrolled");
    lv_obj_scroll_to_y(grid, 0, LV_ANIM_OFF);
    ui_tile_grid_select(grid, 1, true);
    shot("tile_grid_focused");

    lv_obj_delete(screen);
    lv_group_delete(group);
}
