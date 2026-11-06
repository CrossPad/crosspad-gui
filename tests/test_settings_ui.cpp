// SPDX-License-Identifier: GPL-3.0-or-later
//
// Settings: rows that did nothing are not offered, Factory Reset asks first, and
// Info carries the licence and the no-warranty notice.

#include <catch2/catch_test_macros.hpp>

#include "crosspad-gui/components/main_screen.h"
#include "crosspad-gui/components/power_gesture.h"
#include "crosspad-gui/components/settings_ui.h"
#include "crosspad-gui/components/ui_toast.h"
#include "gui_test_platform.h"

#include "crosspad/platform/PlatformServices.hpp"
#include "crosspad/settings/CrosspadSettings.hpp"
#include "crosspad/stm32/reg_map.h"

#include <cstring>

using namespace crosspad_gui;

namespace {

/// A platform whose STM config registers are live, so System shows its STM rows.
class StmGuiPlatform : public guitest::TestGuiPlatform {
public:
    bool hasStmConfigWrite() override { return true; }
};

/// The first label under @p root whose text is @p text, or nullptr.
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

/// Settings opened on @p category inside a fresh content area.
lv_obj_t* open_settings(guitest::GuiFixture& f, const char* category)
{
    lv_obj_t* content = lv_obj_create(f.screen);
    settings_ui_open_next(category);
    settings_ui_create(content, f.group);
    return content;
}

void click_focused(lv_indev_t* encoder)
{
    lv_obj_t* focused = lv_group_get_focused(lv_indev_get_group(encoder));
    REQUIRE(focused != nullptr);
    lv_obj_send_event(focused, LV_EVENT_CLICKED, nullptr);
}

/// Press the Factory Reset button on the System page.
void press_factory_reset(lv_obj_t* content)
{
    lv_obj_t* label = find_label(content, "Factory Reset");
    REQUIRE(label != nullptr);
    lv_obj_send_event(lv_obj_get_parent(label), LV_EVENT_CLICKED, nullptr);
}

/// A few values a reset puts back, moved away from their defaults.
void dirty_settings(crosspad::CrosspadSettings* s)
{
    s->LCDbrightness = 37;
    s->RGBbrightness = 12;
    s->keypad.send2BLE = false;
}

bool is_dirty(const crosspad::CrosspadSettings* s)
{
    return s->LCDbrightness == 37 && s->RGBbrightness == 12 && !s->keypad.send2BLE;
}

bool is_reset(const crosspad::CrosspadSettings* s)
{
    return s->LCDbrightness == 100 && s->RGBbrightness == 100 && s->keypad.send2BLE;
}

} // namespace

TEST_CASE("Settings: Factory Reset asks first; Cancel changes nothing", "[settings]")
{
    guitest::GuiFixture f;
    auto* s = crosspad::CrosspadSettings::getInstance();
    dirty_settings(s);

    lv_obj_t* content = open_settings(f, "System");
    press_factory_reset(content);

    // A question, not a reset.
    CHECK(is_dirty(s));
    CHECK(ui_back_has_overlay());
    CHECK(find_label(lv_layer_top(), "Factory reset?") != nullptr);
    CHECK(find_label(lv_layer_top(), "Reset") != nullptr);

    // The encoder starts on Cancel.
    lv_obj_t* focused = lv_group_get_focused(lv_indev_get_group(f.gui.encoder));
    REQUIRE(focused != nullptr);
    CHECK(find_label(focused, "Cancel") != nullptr);
    click_focused(f.gui.encoder);

    CHECK(is_dirty(s));
    CHECK_FALSE(ui_back_has_overlay());
    CHECK(lv_indev_get_group(f.gui.encoder) == f.group);

    settings_ui_destroy();
}

TEST_CASE("Settings: the power button's back on the question changes nothing", "[settings]")
{
    guitest::GuiFixture f;
    auto* s = crosspad::CrosspadSettings::getInstance();
    dirty_settings(s);

    lv_obj_t* content = open_settings(f, "System");
    press_factory_reset(content);
    CHECK(powerBack() == BackResult::Overlay);

    CHECK(is_dirty(s));
    CHECK_FALSE(ui_back_has_overlay());

    settings_ui_destroy();
}

TEST_CASE("Settings: Reset on the question resets as before", "[settings]")
{
    guitest::GuiFixture f;
    // The reset re-applies the haptics; crosspad_platform_reset() of an
    // earlier test case left no vibration service, so give it the null one.
    crosspad::getPlatformServices().setVibration(nullptr);
    auto* s = crosspad::CrosspadSettings::getInstance();
    dirty_settings(s);

    lv_obj_t* content = open_settings(f, "System");
    press_factory_reset(content);
    lv_group_focus_next(lv_indev_get_group(f.gui.encoder));
    click_focused(f.gui.encoder);

    CHECK(is_reset(s));
    CHECK_FALSE(ui_back_has_overlay());
    // The page is rebuilt and still offers the reset.
    CHECK(find_label(content, "Factory Reset") != nullptr);

    settings_ui_destroy();
    ui_toast_dismiss();   // "Settings reset to defaults" and its timer
}

TEST_CASE("Settings: leaving Settings with the question open answers Cancel", "[settings]")
{
    guitest::GuiFixture f;
    auto* s = crosspad::CrosspadSettings::getInstance();
    dirty_settings(s);

    lv_obj_t* content = open_settings(f, "System");
    press_factory_reset(content);
    settings_ui_destroy();

    CHECK(is_dirty(s));
    CHECK_FALSE(ui_back_has_overlay());
    CHECK(lv_obj_get_child_count(lv_layer_top()) == 0);
}

TEST_CASE("Settings: rows that did nothing are not offered", "[settings]")
{
    guitest::GuiFixture f;
    StmGuiPlatform stm;
    stm.encoder = f.gui.encoder;
    setGuiPlatform(&stm);
    auto* s = crosspad::CrosspadSettings::getInstance();
    s->vibration.enable = true;   // On Error sat in the rows Vibration Enable hides

    struct Case { const char* category; const char* kept; const char* gone[12]; };
    const Case cases[] = {
        {"Audio", "Sample Rate", {"Audio Engine"}},
        {"Pads & LEDs", "Pad MIDI Output", {"Note Off on Release", "Upper Row Functions"}},
        {"Vibration", "On Touch", {"On Error"}},
        {"MIDI", "MIDI -> BLE",
         {"Send to STM", "BLE -> MIDI", "Routing Enabled", "MIDI -> ESP", "USB -> MIDI",
          "USB -> Thru", "USB -> ESP", "ESP -> MIDI", "ESP -> USB", "ESP -> Thru",
          "Buttons -> MIDI", "Config Btns as MIDI"}},
        {"System", "Auto Sleep", {"Wake Brightness"}},
    };
    for (const Case& c : cases) {
        INFO(c.category);
        lv_obj_t* content = open_settings(f, c.category);
        CHECK(find_label(content, c.kept) != nullptr);
        for (const char* gone : c.gone) {
            if (!gone) break;
            INFO(gone);
            CHECK(find_label(content, gone) == nullptr);
        }
        settings_ui_destroy();
        lv_obj_delete(content);
    }

    // The MIDI rows that do something are all still there.
    lv_obj_t* content = open_settings(f, "MIDI");
    for (const char* kept : {"Send to BLE", "Send to USB", "MIDI -> Thru", "MIDI -> USB",
                             "STM MIDI Channel", "Note Offset"}) {
        INFO(kept);
        CHECK(find_label(content, kept) != nullptr);
    }
    settings_ui_destroy();

    setGuiPlatform(&f.gui);
}

TEST_CASE("Settings: a power-off hold that is not a preset is shown as the nearest one and written back", "[settings]")
{
    // A v1.1.1 backup carries the old 4000 ms default; the STM runs it as 3.7 s.
    // The dropdown fell back to "2.4s" and nothing wrote that back.
    struct RecordingStm : StmGuiPlatform {
        uint16_t longPressReg = 0;
        void writeStmConfig16(uint16_t hiReg, uint16_t val) override {
            if (hiReg == REG_CTL_LONG_PRESS_HI) longPressReg = val;
        }
    };
    guitest::GuiFixture f;
    RecordingStm stm;
    stm.encoder = f.gui.encoder;
    setGuiPlatform(&stm);
    auto* s = crosspad::CrosspadSettings::getInstance();
    const uint16_t before = s->stm.longPressMs;

    s->stm.longPressMs = 4000;
    lv_obj_t* content = open_settings(f, "System");
    lv_obj_t* label = find_label(content, "Power Long-Press");
    REQUIRE(label != nullptr);
    char shown[16] = {};
    lv_dropdown_get_selected_str(lv_obj_get_child(lv_obj_get_parent(label), 1), shown, sizeof(shown));
    CHECK(std::strcmp(shown, "3s") == 0);
    CHECK(s->stm.longPressMs == 3000);
    CHECK(stm.longPressReg == 3000);
    settings_ui_destroy();
    lv_obj_delete(content);

    // A preset is left as it is and not pushed again.
    stm.longPressReg = 0;
    s->stm.longPressMs = 1500;
    content = open_settings(f, "System");
    CHECK(s->stm.longPressMs == 1500);
    CHECK(stm.longPressReg == 0);
    settings_ui_destroy();
    lv_obj_delete(content);

    s->stm.longPressMs = before;
    setGuiPlatform(&f.gui);
}

TEST_CASE("Settings: Home screen offers the grid, the list and the pad map", "[settings]")
{
    guitest::GuiFixture f;
    lv_obj_t* content = open_settings(f, "Display");
    CHECK(find_label(content, "Theme Color") != nullptr);
    CHECK(find_label(content, "Home screen") != nullptr);
    CHECK(find_label(content, "Launcher") == nullptr);
    settings_ui_destroy();
    lv_indev_set_group(f.gui.encoder, f.group);
}

TEST_CASE("Settings: Info shows the licence and the no-warranty notice", "[settings]")
{
    guitest::GuiFixture f;
    lv_obj_t* content = open_settings(f, "Info");
    CHECK(find_label(content, "Legal") != nullptr);
    CHECK(find_label(content, "GPL-3.0-or-later") != nullptr);
    CHECK(find_label(content, "\xC2\xA9 CrossPad contributors") != nullptr);

    lv_obj_t* read = find_label(content, "Read");
    REQUIRE(read != nullptr);
    lv_obj_send_event(lv_obj_get_parent(read), LV_EVENT_CLICKED, nullptr);
    CHECK(find_label(lv_layer_top(), "No warranty") != nullptr);
    CHECK(ui_back_has_overlay());

    // OK closes it and gives the encoder back to the page.
    click_focused(f.gui.encoder);
    CHECK_FALSE(ui_back_has_overlay());
    CHECK(lv_indev_get_group(f.gui.encoder) == f.group);

    settings_ui_destroy();
}

TEST_CASE("Settings: leaving Settings with the notice open closes it", "[settings]")
{
    guitest::GuiFixture f;
    lv_obj_t* content = open_settings(f, "Info");
    lv_obj_t* read = find_label(content, "Read");
    REQUIRE(read != nullptr);
    lv_obj_send_event(lv_obj_get_parent(read), LV_EVENT_CLICKED, nullptr);
    settings_ui_destroy();

    CHECK_FALSE(ui_back_has_overlay());
    CHECK(lv_obj_get_child_count(lv_layer_top()) == 0);
}
