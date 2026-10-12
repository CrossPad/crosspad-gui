// SPDX-License-Identifier: GPL-3.0-or-later
#include <catch2/catch_test_macros.hpp>

#include "crosspad-gui/components/power_gesture.h"
#include "crosspad-gui/components/ui_modal.h"
#include "gui_test_platform.h"

using namespace crosspad_gui;

namespace {

struct Answer {
    int calls = 0;
    int index = -2;
    bool flag = false;
};

void on_confirm(bool confirmed, void* user)
{
    auto* a = static_cast<Answer*>(user);
    a->calls++;
    a->flag = confirmed;
}

void on_choice(int index, void* user)
{
    auto* a = static_cast<Answer*>(user);
    a->calls++;
    a->index = index;
}

void on_select(int index, bool cancelled, void* user)
{
    auto* a = static_cast<Answer*>(user);
    a->calls++;
    a->index = index;
    a->flag = cancelled;
}

/// Press whatever the encoder is on, as the encoder's own click would.
void click_focused(lv_indev_t* encoder)
{
    lv_obj_t* focused = lv_group_get_focused(lv_indev_get_group(encoder));
    REQUIRE(focused != nullptr);
    lv_obj_send_event(focused, LV_EVENT_CLICKED, nullptr);
}

} // namespace

TEST_CASE("ui_modal_confirm: the encoder starts on yes, and gets its group back", "[modal]")
{
    guitest::GuiFixture f;
    Answer a;
    lv_obj_t* overlay = ui_modal_confirm("Title", "Message", "Yes", "No", on_confirm, &a, nullptr);
    REQUIRE(overlay != nullptr);
    CHECK(lv_indev_get_group(f.gui.encoder) != f.group);
    CHECK(ui_back_has_overlay());

    click_focused(f.gui.encoder);
    CHECK(a.calls == 1);
    CHECK(a.flag);
    CHECK(lv_indev_get_group(f.gui.encoder) == f.group);
    CHECK_FALSE(ui_back_has_overlay());
    CHECK(lv_obj_get_child_count(lv_layer_top()) == 0);
}

TEST_CASE("ui_modal_confirm: the power button's back answers no", "[modal]")
{
    guitest::GuiFixture f;
    Answer a;
    ui_modal_confirm("Title", "Message", "Yes", "No", on_confirm, &a, nullptr);

    CHECK(powerBack() == BackResult::Overlay);
    CHECK(a.calls == 1);
    CHECK_FALSE(a.flag);
    CHECK(lv_indev_get_group(f.gui.encoder) == f.group);
    CHECK(powerBack() == BackResult::None);
}

TEST_CASE("ui_modal_confirm_dismiss closes it from code, as a button would", "[modal]")
{
    guitest::GuiFixture f;
    Answer a;
    lv_obj_t* overlay = ui_modal_confirm("Title", "Message", "Yes", "No", on_confirm, &a, nullptr);

    ui_modal_confirm_dismiss(overlay, true);
    CHECK(a.calls == 1);
    CHECK(a.flag);
    CHECK_FALSE(ui_back_has_overlay());
}

TEST_CASE("ui_modal_choice: the focused button's index, -1 for back", "[modal]")
{
    guitest::GuiFixture f;
    const ui_modal_button_t buttons[] = {
        {"Keep", UI_MODAL_BUTTON_NORMAL},
        {"Replace", UI_MODAL_BUTTON_PRIMARY},
        {"Delete", UI_MODAL_BUTTON_DANGER},
    };
    Answer a;

    SECTION("a button") {
        ui_modal_choice("Title", "Message", buttons, 3, 2, on_choice, &a, nullptr);
        click_focused(f.gui.encoder);
        CHECK(a.index == 2);
    }
    SECTION("back") {
        ui_modal_choice("Title", "Message", buttons, 3, 1, on_choice, &a, nullptr);
        CHECK(powerBack() == BackResult::Overlay);
        CHECK(a.index == -1);
    }
    CHECK(a.calls == 1);
    CHECK_FALSE(ui_back_has_overlay());
}

TEST_CASE("ui_modal_choice: an explicit group is the one the encoder gets back", "[modal]")
{
    guitest::GuiFixture f;
    lv_group_t* other = lv_group_create();
    const ui_modal_button_t buttons[] = {{"OK", UI_MODAL_BUTTON_PRIMARY}};
    Answer a;
    ui_modal_choice("Title", nullptr, buttons, 1, 0, on_choice, &a, other);
    click_focused(f.gui.encoder);
    CHECK(lv_indev_get_group(f.gui.encoder) == other);
    lv_indev_set_group(f.gui.encoder, f.group);
    lv_group_delete(other);
}

TEST_CASE("ui_modal_selection: the current option has the encoder", "[modal]")
{
    guitest::GuiFixture f;
    const char* options[] = {"Low", "Medium", "High", nullptr};
    Answer a;

    SECTION("picked") {
        ui_modal_selection(nullptr, options, 1, on_select, &a, f.group);
        click_focused(f.gui.encoder);
        CHECK(a.index == 1);
        CHECK_FALSE(a.flag);
    }
    SECTION("back cancels") {
        ui_modal_selection(nullptr, options, 1, on_select, &a, f.group);
        CHECK(powerBack() == BackResult::Overlay);
        CHECK(a.index == -1);
        CHECK(a.flag);
    }
    CHECK(a.calls == 1);
    CHECK(lv_indev_get_group(f.gui.encoder) == f.group);
    CHECK_FALSE(ui_back_has_overlay());
}

TEST_CASE("ui_modal_selection: a title does not shift the picked index", "[modal]")
{
    guitest::GuiFixture f;
    const char* options[] = {"Low", "Medium", "High", nullptr};
    Answer a;
    ui_modal_selection("Quality", options, 2, on_select, &a, f.group);
    click_focused(f.gui.encoder);
    CHECK(a.calls == 1);
    CHECK(a.index == 2);
    CHECK_FALSE(a.flag);
    CHECK_FALSE(ui_back_has_overlay());
}
