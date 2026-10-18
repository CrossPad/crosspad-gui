// SPDX-License-Identifier: GPL-3.0-or-later
// The kit loader bar on the overlay layer: what it says through a load, an
// error that a late progress cannot write over, the fade, the tap that puts
// an error away, and a rebuild after the layer is cleaned under it.
#include <catch2/catch_test_macros.hpp>

#include "crosspad-gui/components/kit_loader.h"
#include "gui_test_platform.h"

#include <cstring>

using namespace crosspad_gui;

namespace {

constexpr int32_t kBarW = 312;
constexpr uint32_t kDoneShownMs = 600;
constexpr uint32_t kFadeMs = 250;
constexpr uint32_t kStepMs = 30;

/// The bar: the newest child of the overlay layer, where every show puts it.
lv_obj_t* bar()
{
    lv_obj_t* layer = lv_layer_top();
    const uint32_t n = lv_obj_get_child_count(layer);
    return n ? lv_obj_get_child(layer, static_cast<int32_t>(n) - 1) : nullptr;
}

/// The light name and percentage labels, the first two children of the bar.
/// The name is dotted to its width, which it only has once laid out.
const char* name_text()
{
    lv_obj_update_layout(bar());
    return lv_label_get_text(lv_obj_get_child(bar(), 0));
}
const char* pct_text()  { return lv_label_get_text(lv_obj_get_child(bar(), 1)); }
lv_obj_t* fill()        { return lv_obj_get_child(bar(), 2); }

bool shown() { return bar() && !lv_obj_has_flag(bar(), LV_OBJ_FLAG_HIDDEN); }

void pass(guitest::GuiFixture& f, uint32_t ms)
{
    for (uint32_t t = 0; t < ms; t += kStepMs) f.advance(kStepMs);
}

} // namespace

TEST_CASE("kit loader: a load from start to the fade", "[kit_loader]")
{
    guitest::GuiFixture f;

    kit_loader_start("FINGERDRUMMING_BOSS");
    REQUIRE(shown());
    CHECK(std::strcmp(name_text(), "LOADING FINGERDRUMMING_BOSS") == 0);
    CHECK(std::strcmp(pct_text(), "0%") == 0);
    CHECK(!lv_obj_has_flag(bar(), LV_OBJ_FLAG_CLICKABLE));

    kit_loader_set(38);
    CHECK(std::strcmp(pct_text(), "38%") == 0);
    lv_obj_update_layout(bar());
    CHECK(lv_obj_get_width(fill()) == kBarW * 38 / 100);

    kit_loader_set(150);
    CHECK(std::strcmp(pct_text(), "100%") == 0);

    kit_loader_done(15, 16);
    CHECK(std::strcmp(name_text(), "KIT LOADED") == 0);
    CHECK(std::strcmp(pct_text(), "15/16") == 0);

    pass(f, kDoneShownMs - kStepMs);
    CHECK(shown());
    pass(f, kStepMs + kFadeMs + 2 * kStepMs);
    CHECK(!shown());

    // Hidden: a stray progress or done does not bring it back.
    kit_loader_set(10);
    kit_loader_done(1, 1);
    CHECK(!shown());
}

TEST_CASE("kit loader: an error stays until tapped", "[kit_loader]")
{
    guitest::GuiFixture f;

    kit_loader_start(nullptr);
    CHECK(std::strcmp(name_text(), "LOADING KIT") == 0);

    kit_loader_error("MISSING open_hat.wav");
    REQUIRE(shown());
    CHECK(std::strcmp(name_text(), "MISSING open_hat.wav") == 0);
    CHECK(lv_obj_has_flag(bar(), LV_OBJ_FLAG_CLICKABLE));

    kit_loader_set(50);
    kit_loader_done(16, 16);
    pass(f, kDoneShownMs + kFadeMs + 2 * kStepMs);
    CHECK(shown());
    CHECK(std::strcmp(name_text(), "MISSING open_hat.wav") == 0);

    lv_obj_send_event(bar(), LV_EVENT_CLICKED, nullptr);
    CHECK(!shown());

    kit_loader_error("");
    CHECK(std::strcmp(name_text(), "LOAD FAILED") == 0);

    // The next load takes the bar back from the error.
    kit_loader_start("B");
    CHECK(std::strcmp(name_text(), "LOADING B") == 0);
    CHECK(!lv_obj_has_flag(bar(), LV_OBJ_FLAG_CLICKABLE));
}

TEST_CASE("kit loader: built again after the layer is cleaned", "[kit_loader]")
{
    guitest::GuiFixture f;

    kit_loader_start("A");
    kit_loader_done(16, 16);
    lv_obj_clean(lv_layer_top());
    pass(f, kDoneShownMs + kFadeMs + 2 * kStepMs);   // the hide timer went with the bar

    kit_loader_set(20);   // nothing up: ignored, nothing built
    CHECK(lv_obj_get_child_count(lv_layer_top()) == 0);

    kit_loader_start("A");
    REQUIRE(shown());
    CHECK(std::strcmp(name_text(), "LOADING A") == 0);
}
