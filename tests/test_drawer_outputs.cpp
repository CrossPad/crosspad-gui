// SPDX-License-Identifier: GPL-3.0-or-later
// The quick-settings drawer's volume sliders: one per output the platform
// names (IOutputLevels), under that name, each moving its own output; the
// codecs' two levels as Settings names them where no outputs are named; and
// the whole drawer on the screen, the close arrow included.
#include <catch2/catch_test_macros.hpp>

#include "crosspad-gui/components/status_bar.h"
#include "gui_test_platform.h"

#include "crosspad/audio/IOutputLevels.hpp"
#include "crosspad/platform/PlatformServices.hpp"
#include "crosspad/settings/CrosspadSettings.hpp"

#include <cstring>
#include <string>
#include <vector>

using namespace crosspad_gui;

namespace {

/// The board's three, as IdfGuiPlatform's OutputLevelsPort lists them.
class FakeOutputLevels : public crosspad::IOutputLevels {
public:
    int outputs() const override { return 3; }
    const char* name(int o) const override { return kNames[o]; }
    const char* mixerOutput(int o) const override { return o == 2 ? "Out 3/4" : "Out 1/2"; }
    int level(int o) const override { return levels[o]; }
    bool setLevel(int o, int v) override { levels[o] = v; ++sets[o]; return true; }
    bool muted(int) const override { return false; }
    bool setMuted(int, bool) override { return true; }

    static constexpr const char* kNames[3] = {"Speakers", "Phones", "Line out"};
    int levels[3] = {22, 85, 100};
    int sets[3] = {};
};

void collect(lv_obj_t* obj, std::vector<lv_obj_t*>& labels, std::vector<lv_obj_t*>& sliders)
{
    if (lv_obj_check_type(obj, &lv_label_class)) labels.push_back(obj);
    if (lv_obj_check_type(obj, &lv_slider_class)) sliders.push_back(obj);
    for (uint32_t i = 0; i < lv_obj_get_child_count(obj); ++i)
        collect(lv_obj_get_child(obj, static_cast<int32_t>(i)), labels, sliders);
}

/// The drawer panel: the newest child of the overlay layer, the backdrop
/// under it.
lv_obj_t* drawer()
{
    lv_obj_t* layer = lv_layer_top();
    const uint32_t n = lv_obj_get_child_count(layer);
    return n ? lv_obj_get_child(layer, static_cast<int32_t>(n) - 1) : nullptr;
}

std::vector<std::string> texts(const std::vector<lv_obj_t*>& labels)
{
    std::vector<std::string> out;
    for (lv_obj_t* l : labels) out.emplace_back(lv_label_get_text(l));
    return out;
}

bool has(const std::vector<std::string>& v, const char* s)
{
    for (const auto& t : v) if (t == s) return true;
    return false;
}

/* The status bar keeps its widgets and timers in statics and is never taken
 * down on a board, so here it lives on a screen of its own that no test
 * deletes. A drawer test builds it with the drawer on; leaving, it is built
 * again with the drawer off -- no arrow, so a later test's power hold
 * cannot open a drawer -- and its timers are paused. */
lv_obj_t* bar_screen()
{
    static lv_obj_t* s = lv_obj_create(nullptr);
    return s;
}

void build_bar(bool drawer)
{
    statusbar_set_drawer_enabled(drawer);
    lv_obj_clean(bar_screen());
    lv_CreateStatusBar(bar_screen());
}

struct DrawerFixture : guitest::GuiFixture {
    DrawerFixture()
    {
        for (lv_timer_t* t = lv_timer_get_next(nullptr); t; t = lv_timer_get_next(t))
            if (!lv_timer_get_paused(t)) before.push_back(t);
        lv_screen_load(bar_screen());
        build_bar(true);
        advance(50);
    }
    ~DrawerFixture()
    {
        statusbar_close_drawer();
        settle();
        crosspad::getPlatformServices().outputLevels = nullptr;
        build_bar(false);
        for (lv_timer_t* t = lv_timer_get_next(nullptr); t; t = lv_timer_get_next(t)) {
            bool running_before = false;
            for (lv_timer_t* b : before) running_before = running_before || b == t;
            if (!running_before) lv_timer_pause(t);
        }
        lv_screen_load(screen);
    }
    /// An animation started in this frame has not moved yet: a second of
    /// frames, not one frame a second long.
    void settle() { for (int i = 0; i < 40; ++i) advance(30); }
    void open()
    {
        REQUIRE_FALSE(statusbar_drawer_is_open());
        REQUIRE(statusbar_toggle_drawer());
        settle();
        REQUIRE(statusbar_drawer_is_open());
    }
    std::vector<lv_timer_t*> before;   ///< running when the test began
};

} // namespace

TEST_CASE("Drawer: the volume sliders are the outputs the platform names", "[drawer]")
{
    FakeOutputLevels levels;
    crosspad::getPlatformServices().outputLevels = &levels;
    DrawerFixture f;
    f.open();

    std::vector<lv_obj_t*> labels, sliders;
    collect(drawer(), labels, sliders);
    const auto t = texts(labels);
    CHECK(has(t, "Speakers"));
    CHECK(has(t, "Phones"));
    CHECK(has(t, "Line out"));
    CHECK_FALSE(has(t, "Out 1"));
    CHECK_FALSE(has(t, "Out 2"));

    // The outputs, then Pads and Screen.
    REQUIRE(sliders.size() == 5);
    CHECK(lv_slider_get_value(sliders[0]) == 22);
    CHECK(lv_slider_get_value(sliders[1]) == 85);
    CHECK(lv_slider_get_value(sliders[2]) == 100);

    SECTION("a slider moves its own output and no other")
    {
        lv_slider_set_value(sliders[1], 40, LV_ANIM_OFF);
        lv_obj_send_event(sliders[1], LV_EVENT_VALUE_CHANGED, nullptr);
        CHECK(levels.levels[1] == 40);
        CHECK(levels.sets[0] == 0);
        CHECK(levels.sets[2] == 0);
    }
}

TEST_CASE("Drawer: without named outputs the two levels are Phones and Line out", "[drawer]")
{
    DrawerFixture f;
    f.open();

    std::vector<lv_obj_t*> labels, sliders;
    collect(drawer(), labels, sliders);
    const auto t = texts(labels);
    CHECK(has(t, "Phones"));
    CHECK(has(t, "Line out"));
    CHECK_FALSE(has(t, "Out 1"));
    CHECK_FALSE(has(t, "Out 2"));
    CHECK(sliders.size() == 4);
}

TEST_CASE("Drawer: every row fits on the screen, the close arrow too", "[drawer]")
{
    FakeOutputLevels levels;
    crosspad::getPlatformServices().outputLevels = &levels;
    DrawerFixture f;
    f.open();

    lv_obj_t* d = drawer();
    lv_obj_update_layout(d);
    lv_area_t panel;
    lv_obj_get_coords(d, &panel);
    const int32_t screen_h = lv_display_get_vertical_resolution(lv_display_get_default());
    CHECK(panel.y2 < screen_h);

    // Its content is no taller than the panel: nothing cut off at the foot.
    CHECK(lv_obj_get_self_height(d) <= lv_obj_get_content_height(d));
}
