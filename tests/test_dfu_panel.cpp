// SPDX-License-Identifier: GPL-3.0-or-later
//
// Settings → Programming → DFU Mode: the screen it leaves has a way out that
// works without the STM (touch), asks first, and promises nothing the ROM
// bootloader cannot do.

#include <catch2/catch_test_macros.hpp>

#include "crosspad-gui/components/power_gesture.h"
#include "crosspad-gui/components/settings_ui.h"
#include "gui_test_platform.h"

#include "crosspad/settings/ISettingsUI.hpp"

#include <cstring>

using namespace crosspad_gui;

namespace {

class DfuSettingsUI : public crosspad::ISettingsUI {
public:
    void saveSettings() override {}
    void exitToHome() override {}
    void enterStm32Dfu() override { entered++; }
    void leaveStm32Dfu() override { left++; }
    int entered = 0;
    int left = 0;
};

lv_obj_t* find_label(lv_obj_t* root, const char* text)
{
    if (!root) return nullptr;
    if (lv_obj_check_type(root, &lv_label_class) &&
        std::strcmp(lv_label_get_text(root), text) == 0) {
        return root;
    }
    const uint32_t n = lv_obj_get_child_count(root);
    for (uint32_t i = 0; i < n; ++i) {
        if (lv_obj_t* hit = find_label(lv_obj_get_child(root, (int32_t)i), text)) return hit;
    }
    return nullptr;
}

bool has_text_containing(lv_obj_t* root, const char* needle)
{
    if (!root) return false;
    if (lv_obj_check_type(root, &lv_label_class) &&
        std::strstr(lv_label_get_text(root), needle) != nullptr) {
        return true;
    }
    const uint32_t n = lv_obj_get_child_count(root);
    for (uint32_t i = 0; i < n; ++i) {
        if (has_text_containing(lv_obj_get_child(root, (int32_t)i), needle)) return true;
    }
    return false;
}

void click_parent_of(lv_obj_t* root, const char* text)
{
    lv_obj_t* label = find_label(root, text);
    REQUIRE(label != nullptr);
    lv_obj_send_event(lv_obj_get_parent(label), LV_EVENT_CLICKED, nullptr);
}

/// The DFU panel: the layer-top child carrying the title.
lv_obj_t* dfu_panel()
{
    lv_obj_t* title = find_label(lv_layer_top(), "STM32 is in DFU mode");
    return title ? lv_obj_get_parent(title) : nullptr;
}

/// Settings on Programming, DFU Mode pressed.
lv_obj_t* enter_dfu(guitest::GuiFixture& f)
{
    lv_obj_t* content = lv_obj_create(f.screen);
    settings_ui_open_next("Programming");
    settings_ui_create(content, f.group);
    click_parent_of(content, "DFU Mode");
    return content;
}

} // namespace

TEST_CASE("DFU Mode: the panel covers Settings without wiping it, and says how to leave", "[dfu]")
{
    guitest::GuiFixture f;
    DfuSettingsUI ui;
    crosspad::setSettingsUI(&ui);

    lv_obj_t* content = enter_dfu(f);
    CHECK(ui.entered == 1);
    REQUIRE(dfu_panel() != nullptr);
    CHECK(find_label(content, "DFU Mode") != nullptr);   // Settings is still there

    CHECK(find_label(lv_layer_top(), "Tap the screen to leave DFU mode") != nullptr);
    CHECK_FALSE(has_text_containing(lv_layer_top(), "encoder to exit"));
    CHECK_FALSE(has_text_containing(lv_layer_top(), "ST-LINK"));

    settings_ui_destroy();
    crosspad::setSettingsUI(nullptr);
}

TEST_CASE("DFU Mode: a tap asks first; Stay and the power button's back keep DFU", "[dfu]")
{
    guitest::GuiFixture f;
    DfuSettingsUI ui;
    crosspad::setSettingsUI(&ui);
    enter_dfu(f);

    lv_obj_send_event(dfu_panel(), LV_EVENT_CLICKED, nullptr);
    CHECK(find_label(lv_layer_top(), "Leave DFU mode?") != nullptr);
    lv_obj_t* focused = lv_group_get_focused(lv_indev_get_group(f.gui.encoder));
    REQUIRE(focused != nullptr);
    CHECK(find_label(focused, "Stay") != nullptr);

    // A second tap on the panel while it asks opens no second question.
    lv_obj_send_event(dfu_panel(), LV_EVENT_CLICKED, nullptr);
    lv_obj_send_event(focused, LV_EVENT_CLICKED, nullptr);
    f.advance(50);
    CHECK(ui.left == 0);
    CHECK(dfu_panel() != nullptr);
    CHECK(find_label(lv_layer_top(), "Leave DFU mode?") == nullptr);

    lv_obj_send_event(dfu_panel(), LV_EVENT_CLICKED, nullptr);
    CHECK(powerBack() == BackResult::Overlay);
    f.advance(50);
    CHECK(ui.left == 0);
    CHECK(dfu_panel() != nullptr);

    settings_ui_destroy();
    crosspad::setSettingsUI(nullptr);
}

TEST_CASE("DFU Mode: Leave resets the STM and closes the panel", "[dfu]")
{
    guitest::GuiFixture f;
    DfuSettingsUI ui;
    crosspad::setSettingsUI(&ui);
    lv_obj_t* content = enter_dfu(f);

    lv_obj_send_event(dfu_panel(), LV_EVENT_CLICKED, nullptr);
    click_parent_of(lv_layer_top(), "Leave");
    f.advance(50);
    CHECK(ui.left == 1);
    CHECK(dfu_panel() == nullptr);
    CHECK(find_label(content, "DFU Mode") != nullptr);   // back on the page it came from

    // And DFU Mode can be entered again.
    click_parent_of(content, "DFU Mode");
    CHECK(ui.entered == 2);
    CHECK(dfu_panel() != nullptr);

    settings_ui_destroy();
    crosspad::setSettingsUI(nullptr);
}
