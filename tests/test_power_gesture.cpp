// SPDX-License-Identifier: GPL-3.0-or-later
#include <catch2/catch_test_macros.hpp>

#include "crosspad-gui/app/App.hpp"
#include "crosspad-gui/components/power_gesture.h"
#include "gui_test_platform.h"

#include <vector>

using namespace crosspad_gui;

namespace {

/// An overlay registered on the back stack that records each time it is
/// asked and answers as told.
struct Overlay {
    bool consumes = true;
    int asked = 0;
    uint32_t token = 0;

    void push() { token = ui_back_push(&Overlay::cb, this); }
    void remove() { ui_back_remove(token); token = 0; }

    static bool cb(void* user)
    {
        auto* o = static_cast<Overlay*>(user);
        o->asked++;
        return o->consumes;
    }
};

lv_obj_t* create_root(lv_obj_t* parent, App*) { return lv_obj_create(parent); }
void destroy_root(lv_obj_t* root) { lv_obj_delete_async(root); }

} // namespace

TEST_CASE("powerBack: a bare screen has nothing to go back from", "[power][back]")
{
    guitest::GuiFixture f;
    CHECK(powerBack() == BackResult::None);
}

TEST_CASE("powerBack: an open dropdown closes first", "[power][back]")
{
    guitest::GuiFixture f;
    Overlay overlay;
    overlay.push();

    lv_obj_t* dd = lv_dropdown_create(f.screen);
    lv_dropdown_set_options(dd, "a\nb\nc");
    lv_dropdown_open(dd);
    REQUIRE(lv_dropdown_is_open(dd));

    CHECK(powerBack() == BackResult::Dropdown);
    CHECK_FALSE(lv_dropdown_is_open(dd));
    CHECK(overlay.asked == 0);
    overlay.remove();
}

TEST_CASE("powerBack: overlays are asked newest first, and may decline", "[power][back]")
{
    guitest::GuiFixture f;
    Overlay older;
    Overlay newer;
    older.push();
    newer.push();
    REQUIRE(ui_back_has_overlay());

    CHECK(powerBack() == BackResult::Overlay);
    CHECK(newer.asked == 1);
    CHECK(older.asked == 0);

    newer.consumes = false;
    CHECK(powerBack() == BackResult::Overlay);
    CHECK(newer.asked == 2);
    CHECK(older.asked == 1);

    older.remove();
    newer.remove();
    CHECK_FALSE(ui_back_has_overlay());
}

TEST_CASE("powerBack: an overlay comes before encoder edit mode, edit mode before the app", "[power][back]")
{
    guitest::GuiFixture f;
    lv_obj_t* slider = lv_slider_create(f.screen);
    lv_group_add_obj(f.group, slider);
    lv_group_set_editing(f.group, true);

    Overlay overlay;
    overlay.push();
    CHECK(powerBack() == BackResult::Overlay);
    CHECK(lv_group_get_editing(f.group));
    overlay.remove();

    CHECK(powerBack() == BackResult::Editing);
    CHECK_FALSE(lv_group_get_editing(f.group));
    CHECK(powerBack() == BackResult::None);
}

TEST_CASE("powerBack: the running app's own back comes before closing it", "[power][back]")
{
    guitest::GuiFixture f;
    App app(f.screen, "Test", nullptr, create_root, destroy_root);
    app.start(f.screen);
    REQUIRE(app.isStarted());

    static int backs = 0;
    backs = 0;
    app.setOnBack([](lv_obj_t*) { return ++backs == 1; });

    CHECK(powerBack() == BackResult::App);
    CHECK(backs == 1);
    CHECK(app.isStarted());

    CHECK(powerBack() == BackResult::App);   // the hook declines: the app closes
    f.advance(1);
    CHECK_FALSE(app.isStarted());
    CHECK(powerBack() == BackResult::None);
}

TEST_CASE("emulated power button: a short press is the contextual back", "[power][emulated]")
{
    guitest::GuiFixture f;
    Overlay overlay;
    overlay.push();

    powerButtonPress();
    f.advance(kPowerHoldMs - 100);
    powerButtonRelease();
    CHECK(overlay.asked == 1);
    overlay.remove();
}

TEST_CASE("emulated power button: holding past the threshold is not a back", "[power][emulated]")
{
    guitest::GuiFixture f;
    Overlay overlay;
    overlay.push();

    powerButtonPress();
    f.advance(kPowerHoldMs);
    f.advance(1);
    powerButtonRelease();
    f.advance(1);
    CHECK(overlay.asked == 0);

    powerButtonPress();   // the next short press is a click again
    f.advance(10);
    powerButtonRelease();
    CHECK(overlay.asked == 1);
    overlay.remove();
}
