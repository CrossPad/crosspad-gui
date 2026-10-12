// SPDX-License-Identifier: GPL-3.0-or-later
#include <catch2/catch_test_macros.hpp>

#include "crosspad-gui/components/midi_ble_panel.h"
#include "crosspad/settings/CrosspadSettings.hpp"
#include "gui_test_platform.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace crosspad_gui;

namespace {

/// A BLE stack the test drives by hand: a connect only asks, the link comes
/// up when the test says so -- as on a radio.
struct FakeBle {
    bool enabled = false;
    bool scanning = false;
    bool connected = false;
    std::string peer;
    std::vector<crosspad::BleMidiDevice> found;
    std::vector<std::string> connectRequests;
    int scans = 0;
    int disconnects = 0;
    int modes = 0;
};
FakeBle s_ble;

MidiBleCallbacks fake_callbacks()
{
    MidiBleCallbacks cb;
    cb.setEnabled = [](bool on) { s_ble.enabled = on; };
    cb.setMode = [](uint8_t) { s_ble.modes++; };
    cb.stateName = []() -> const char* {
        return s_ble.connected ? "connected" : s_ble.scanning ? "scanning" : "idle";
    };
    cb.connectedDevice = []() {
        crosspad::BleMidiDevice d;
        if (s_ble.connected) d.address = s_ble.peer;
        return d;
    };
    cb.scanResults = []() { return s_ble.found; };
    cb.startScan = []() { s_ble.scans++; s_ble.scanning = true; };
    cb.connect = [](const std::string& address) { s_ble.connectRequests.push_back(address); };
    cb.disconnect = []() { s_ble.disconnects++; s_ble.connected = false; };
    return cb;
}

crosspad::BleMidiDevice device(const char* name, const char* address, int8_t rssi)
{
    crosspad::BleMidiDevice d;
    d.name = name;
    d.address = address;
    d.rssi = rssi;
    return d;
}

/// The page in a column like the Settings content area.
lv_obj_t* open_page(guitest::GuiFixture& f)
{
    lv_obj_t* content = lv_obj_create(f.screen);
    lv_obj_set_size(content, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
    midi_ble_settings_build(content, f.group);
    return content;
}

/// The first label whose text is @p text, anywhere under @p obj.
lv_obj_t* find_label(lv_obj_t* obj, const char* text)
{
    if (lv_obj_check_type(obj, &lv_label_class) && strcmp(lv_label_get_text(obj), text) == 0) return obj;
    for (uint32_t i = 0; i < lv_obj_get_child_count(obj); ++i) {
        if (lv_obj_t* hit = find_label(lv_obj_get_child(obj, (int32_t)i), text)) return hit;
    }
    return nullptr;
}

int count_labels(lv_obj_t* obj, const char* text)
{
    int n = lv_obj_check_type(obj, &lv_label_class) && strcmp(lv_label_get_text(obj), text) == 0;
    for (uint32_t i = 0; i < lv_obj_get_child_count(obj); ++i) {
        n += count_labels(lv_obj_get_child(obj, (int32_t)i), text);
    }
    return n;
}

/// The value of an info row: the label beside @p name.
const char* row_value(lv_obj_t* page, const char* name)
{
    lv_obj_t* label = find_label(page, name);
    REQUIRE(label != nullptr);
    return lv_label_get_text(lv_obj_get_child(lv_obj_get_parent(label), -1));
}

/// Press the button whose text is @p text (the @p nth of them).
void press(lv_obj_t* page, const char* text, int nth = 0)
{
    std::vector<lv_obj_t*> hits;
    struct Walk {
        static void go(lv_obj_t* o, const char* t, std::vector<lv_obj_t*>& out)
        {
            if (lv_obj_check_type(o, &lv_label_class) && strcmp(lv_label_get_text(o), t) == 0) {
                out.push_back(o);
            }
            for (uint32_t i = 0; i < lv_obj_get_child_count(o); ++i) go(lv_obj_get_child(o, (int32_t)i), t, out);
        }
    };
    Walk::go(page, text, hits);
    REQUIRE((int)hits.size() > nth);
    lv_obj_send_event(lv_obj_get_parent(hits[(size_t)nth]), LV_EVENT_CLICKED, nullptr);
}

constexpr uint32_t kPollMs = 300;

} // namespace

TEST_CASE("BLE MIDI page: scan results appear, connect and disconnect move the state", "[midi_ble]")
{
    guitest::GuiFixture f;
    s_ble = FakeBle{};
    midi_ble_set_callbacks(fake_callbacks());
    lv_obj_t* page = open_page(f);

    CHECK(std::string(row_value(page, "State")) == "idle");
    CHECK(std::string(row_value(page, "Peer")) == "-");
    CHECK(find_label(page, "Devices found") == nullptr);

    press(page, "Scan");
    CHECK(s_ble.scans == 1);
    CHECK(std::string(row_value(page, "State")) == "scanning");

    const uint32_t focusableBefore = lv_group_get_obj_count(f.group);
    s_ble.found = {device("Keys", "AA:BB:CC:00:00:01", -60), device("Drums", "AA:BB:CC:00:00:02", -72)};
    f.advance(kPollMs);
    CHECK(find_label(page, "Devices found") != nullptr);
    CHECK(find_label(page, "Keys (-60 dBm)") != nullptr);
    CHECK(find_label(page, "Drums (-72 dBm)") != nullptr);
    CHECK(count_labels(page, "Connect") == 2);
    CHECK(lv_group_get_obj_count(f.group) == focusableBefore + 2);

    press(page, "Connect", 1);
    REQUIRE(s_ble.connectRequests.size() == 1);
    CHECK(s_ble.connectRequests[0] == "AA:BB:CC:00:00:02");
    CHECK(std::string(row_value(page, "State")) == "scanning");   // asked, not linked yet

    s_ble.scanning = false;
    s_ble.connected = true;
    s_ble.peer = "AA:BB:CC:00:00:02";
    f.advance(kPollMs);
    CHECK(std::string(row_value(page, "State")) == "connected");
    CHECK(std::string(row_value(page, "Peer")) == "AA:BB:CC:00:00:02");

    press(page, "Disconnect");
    CHECK(s_ble.disconnects == 1);
    CHECK(std::string(row_value(page, "State")) == "idle");
    CHECK(std::string(row_value(page, "Peer")) == "-");

    s_ble.found.clear();
    f.advance(kPollMs);
    CHECK(find_label(page, "Devices found") == nullptr);
    CHECK(lv_group_get_obj_count(f.group) == focusableBefore);
}

TEST_CASE("BLE MIDI page: the settings rows store, then call the platform", "[midi_ble]")
{
    guitest::GuiFixture f;
    s_ble = FakeBle{};
    midi_ble_set_callbacks(fake_callbacks());
    auto* s = crosspad::CrosspadSettings::getInstance();
    REQUIRE(s != nullptr);
    s->wireless.enableBLE = false;
    lv_obj_t* page = open_page(f);

    lv_obj_t* enabled = find_label(page, "Enabled");
    REQUIRE(enabled != nullptr);
    lv_obj_t* sw = lv_obj_get_child(lv_obj_get_parent(enabled), -1);
    lv_obj_add_state(sw, LV_STATE_CHECKED);
    lv_obj_send_event(sw, LV_EVENT_VALUE_CHANGED, nullptr);
    CHECK(s->wireless.enableBLE);
    CHECK(s_ble.enabled);

    lv_obj_t* role = find_label(page, "Role");
    REQUIRE(role != nullptr);
    lv_obj_t* dd = lv_obj_get_child(lv_obj_get_parent(role), -1);
    lv_dropdown_set_selected(dd, 0);
    lv_obj_send_event(dd, LV_EVENT_VALUE_CHANGED, nullptr);
    CHECK(s->wireless.bleMidiMode == 0);
    CHECK(s_ble.modes == 1);
}

TEST_CASE("BLE MIDI page: a row without its callback is not there", "[midi_ble]")
{
    guitest::GuiFixture f;
    s_ble = FakeBle{};
    MidiBleCallbacks cb = fake_callbacks();
    cb.setEnabled = nullptr;
    cb.disconnect = nullptr;
    midi_ble_set_callbacks(cb);
    lv_obj_t* page = open_page(f);

    CHECK(find_label(page, "Enabled") == nullptr);
    CHECK(find_label(page, "Disconnect") == nullptr);
    CHECK(find_label(page, "Send transpose") == nullptr);
    CHECK(find_label(page, "MIDI monitor") == nullptr);
    CHECK(find_label(page, "Role") != nullptr);
    CHECK(find_label(page, "Scan") != nullptr);
}

TEST_CASE("BLE MIDI page: closed, it stops asking", "[midi_ble]")
{
    guitest::GuiFixture f;
    s_ble = FakeBle{};
    static int s_polls;
    s_polls = 0;
    MidiBleCallbacks cb = fake_callbacks();
    cb.stateName = []() -> const char* { s_polls++; return "idle"; };
    midi_ble_set_callbacks(cb);
    lv_obj_t* page = open_page(f);
    f.advance(kPollMs);
    CHECK(s_polls >= 2);

    lv_obj_delete(page);
    const int polls = s_polls;
    s_ble.found = {device("Keys", "AA:BB:CC:00:00:01", -60)};
    f.advance(kPollMs * 3);
    CHECK(s_polls == polls);
}
