// SPDX-License-Identifier: GPL-3.0-or-later
#include <catch2/catch_test_macros.hpp>

#include "crosspad-gui/components/app_launcher.h"
#include "gui_test_platform.h"

#include <vector>

using namespace crosspad_gui;

namespace {

/// The home container under the 40 px top bar, as build_main_screen() leaves it.
lv_obj_t* make_home(guitest::GuiFixture& f)
{
    lv_obj_t* home = lv_obj_create(f.screen);
    lv_obj_set_pos(home, 0, 40);
    lv_obj_set_size(home, 320, 200);
    lv_obj_set_style_pad_all(home, 0, 0);
    lv_obj_set_style_border_width(home, 0, 0);
    lv_obj_remove_flag(home, LV_OBJ_FLAG_SCROLLABLE);
    return home;
}

lv_obj_t* make_grid(guitest::GuiFixture& f, lv_obj_t* home, size_t count)
{
    static int apps[32];
    std::vector<LauncherAppInfo> infos;
    for (size_t i = 0; i < count; i++) infos.push_back({"App", nullptr, &apps[i], false});
    LauncherConfig lc;
    lc.encoder_group = f.group;
    lv_obj_t* grid = launcher_create_button_grid(home, infos.data(), infos.size(), &lc);
    f.advance(1);
    return grid;
}

bool inside(lv_obj_t* tile, lv_obj_t* home)
{
    lv_area_t t, h;
    lv_obj_get_coords(tile, &t);
    lv_obj_get_coords(home, &h);
    return t.y1 >= h.y1 && t.y2 <= h.y2;
}

} // namespace

TEST_CASE("Launcher grid: more tiles than fit start in view and scroll to every row",
          "[launcher]")
{
    guitest::GuiFixture f;
    lv_obj_t* home = make_home(f);
    lv_obj_t* grid = make_grid(f, home, 19);   // five rows of four

    lv_obj_t* first = lv_obj_get_child(grid, 0);
    lv_obj_t* last = lv_obj_get_child(grid, -1);
    CHECK(inside(first, home));
    CHECK_FALSE(inside(last, home));
    CHECK(lv_obj_has_flag(grid, LV_OBJ_FLAG_SCROLLABLE));

    lv_group_focus_obj(last);
    f.advance(1000);
    CHECK(inside(last, home));

    lv_group_focus_obj(first);
    f.advance(1000);
    CHECK(inside(first, home));
}

TEST_CASE("Launcher grid: tiles that fit stay centred and do not scroll", "[launcher]")
{
    guitest::GuiFixture f;
    lv_obj_t* home = make_home(f);
    lv_obj_t* grid = make_grid(f, home, 4);   // one row

    CHECK_FALSE(lv_obj_has_flag(grid, LV_OBJ_FLAG_SCROLLABLE));
    lv_area_t t, h;
    lv_obj_get_coords(lv_obj_get_child(grid, 0), &t);
    lv_obj_get_coords(home, &h);
    const int32_t above = t.y1 - h.y1;
    const int32_t below = h.y2 - t.y2;
    CHECK(above - below <= 1);
    CHECK(below - above <= 1);
}
