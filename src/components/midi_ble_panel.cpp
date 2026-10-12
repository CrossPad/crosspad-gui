// SPDX-License-Identifier: GPL-3.0-or-later
//
// The Settings "BLE MIDI" page -- see midi_ble_panel.h.

#ifdef USE_LVGL

#include "crosspad-gui/components/midi_ble_panel.h"
#include "crosspad-gui/components/settings_ui.h"
#include "settings_ui_internal.h"

#include "crosspad/midi/BleMidiOffsets.hpp"
#include "crosspad/settings/CrosspadSettings.hpp"

#include <cstdint>
#include <cstdio>
#include <cstring>

namespace crosspad_gui {

using namespace settings_detail;

namespace {

constexpr uint32_t kPollMs = 300;
constexpr uint32_t kNoDevices = 0;
constexpr uint32_t kFnvOffsetBasis = 2166136261u;
constexpr uint32_t kFnvPrime = 16777619u;
constexpr size_t   kMonitorLen = 48;

/// Receive transpose, whole octaves either way.
constexpr int8_t kRxOffsets[] = {-48, -36, -24, -12, 0, 12, 24, 36, 48};
constexpr const char* kRxOffsetOptions = "-48\n-36\n-24\n-12\n0\n+12\n+24\n+36\n+48";
constexpr uint8_t kRxOffsetCount = (uint8_t)(sizeof(kRxOffsets) / sizeof(kRxOffsets[0]));
constexpr uint8_t kRxNoOffset = 4;
static_assert(kRxOffsets[kRxNoOffset] == 0, "the default receive transpose is none");

MidiBleCallbacks s_cb;

/* The open page. Value labels are updated in place; the device rows are
 * rebuilt when the set of devices changes. */
struct Page {
    lv_obj_t*   state = nullptr;
    lv_obj_t*   peer = nullptr;
    lv_obj_t*   devices = nullptr;
    lv_obj_t*   midiIn = nullptr;
    lv_obj_t*   midiOut = nullptr;
    /* The Scan button, to find the encoder group by when the device rows are
     * rebuilt: the group can be deleted before the page is (Settings
     * closing), and LVGL then clears it from its members, so a rebuild never
     * adds to a freed one. */
    lv_obj_t*   scanButton = nullptr;
    lv_timer_t* timer = nullptr;
    uint32_t    shownDevices = kNoDevices;   ///< devices_key() of the rows shown
    uint8_t     rxIndex = kRxNoOffset;
};
Page s_page;

lv_obj_t* value_label(lv_obj_t* infoRow)
{
    return lv_obj_get_child(infoRow, -1);
}

/* Only a changed text: setting the same one still repaints the label. */
void set_text(lv_obj_t* label, const char* text)
{
    if (label && strcmp(lv_label_get_text(label), text) != 0) lv_label_set_text(label, text);
}

/* Which devices, in which order (FNV-1a over the addresses): the rows are
 * rebuilt when it changes, and a Connect press finds its address by row
 * index only while it has not. */
uint32_t devices_key(const std::vector<crosspad::BleMidiDevice>& found)
{
    if (found.empty()) return kNoDevices;
    uint32_t h = kFnvOffsetBasis;
    for (const auto& d : found) {
        for (const char c : d.address) h = (h ^ (uint8_t)c) * kFnvPrime;
        h = (h ^ '\n') * kFnvPrime;
    }
    return h == kNoDevices ? kNoDevices + 1 : h;
}

void connect_cb(lv_event_t* e);

void show_devices(const std::vector<crosspad::BleMidiDevice>& found)
{
    const uint32_t key = devices_key(found);
    if (key == s_page.shownDevices) return;
    s_page.shownDevices = key;

    lv_obj_clean(s_page.devices);
    if (found.empty()) {
        lv_obj_add_flag(s_page.devices, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    lv_obj_remove_flag(s_page.devices, LV_OBJ_FLAG_HIDDEN);
    lv_group_t* group = lv_obj_get_group(s_page.scanButton);
    settings_section_header(s_page.devices, "Devices found");
    for (size_t i = 0; i < found.size(); ++i) {
        char label[64];
        snprintf(label, sizeof(label), "%s (%d dBm)", found[i].name.c_str(), (int)found[i].rssi);
        settings_row_action(s_page.devices, group, label, "Connect", connect_cb, (void*)(intptr_t)i);
    }
}

/* State and peer: what a press on one of the page's buttons changes at once.
 * The device rows wait for the timer -- a Connect press must not rebuild the
 * list its own button is in. */
void show_status()
{
    if (s_cb.stateName && s_page.state) set_text(s_page.state, s_cb.stateName());
    if (s_cb.connectedDevice && s_page.peer) {
        const crosspad::BleMidiDevice peer = s_cb.connectedDevice();
        set_text(s_page.peer, peer.address.empty() ? "-" : peer.address.c_str());
    }
}

void poll(lv_timer_t*)
{
    show_status();
    if (s_cb.scanResults && s_page.scanButton) show_devices(s_cb.scanResults());
    if (s_cb.lastMessages && s_page.midiIn) {
        char in[kMonitorLen] = "", out[kMonitorLen] = "";
        s_cb.lastMessages(in, sizeof(in), out, sizeof(out));
        set_text(s_page.midiIn, in);
        set_text(s_page.midiOut, out);
    }
}

void page_deleted_cb(lv_event_t* e)
{
    if (lv_event_get_target(e) != s_page.devices) return;
    if (s_page.timer) lv_timer_delete(s_page.timer);
    s_page = Page{};
}

void enable_cb(lv_event_t*)
{
    auto* s = crosspad::CrosspadSettings::getInstance();
    if (!s) return;
    s_cb.setEnabled(s->wireless.enableBLE);
    save_settings();
    show_status();
}

void mode_cb(lv_event_t*)
{
    auto* s = crosspad::CrosspadSettings::getInstance();
    if (!s) return;
    s_cb.setMode(s->wireless.bleMidiMode);
    save_settings();
    show_status();
}

void tx_offset_cb(lv_event_t*)
{
    s_cb.applyTxOffset();
    save_settings();
}

void rx_offset_cb(lv_event_t*)
{
    auto* s = crosspad::CrosspadSettings::getInstance();
    if (!s || s_page.rxIndex >= kRxOffsetCount) return;
    s->wireless.bleMidiNoteOffset = kRxOffsets[s_page.rxIndex];
    s_cb.applyRxOffset();
    save_settings();
}

void scan_cb(lv_event_t*)
{
    s_cb.startScan();
    show_status();
}

void disconnect_cb(lv_event_t*)
{
    s_cb.disconnect();
    show_status();
}

void connect_cb(lv_event_t* e)
{
    const size_t i = (size_t)(intptr_t)lv_event_get_user_data(e);
    const std::vector<crosspad::BleMidiDevice> found = s_cb.scanResults();
    if (devices_key(found) != s_page.shownDevices || i >= found.size()) return;   // the rows are stale
    s_cb.connect(found[i].address);
    show_status();
}

/* Options from crosspad-core's table, so the list and the values it selects
 * cannot drift apart. */
const char* tx_offset_options()
{
    static char options[256];
    if (options[0]) return options;
    size_t len = 0;
    for (uint8_t i = 0; i < crosspad::kBleMidiTxOffsetCount; ++i) {
        const char* name = crosspad::bleMidiTxOffsetName(i);
        const size_t nameLen = strlen(name);
        if (len + (i ? 1 : 0) + nameLen >= sizeof(options)) break;
        if (i) options[len++] = '\n';
        memcpy(options + len, name, nameLen + 1);
        len += nameLen;
    }
    return options;
}

uint8_t rx_offset_index(int8_t semitones)
{
    for (uint8_t i = 0; i < kRxOffsetCount; ++i) {
        if (kRxOffsets[i] == semitones) return i;
    }
    return kRxNoOffset;
}

} // namespace

void midi_ble_set_callbacks(const MidiBleCallbacks& cb)
{
    s_cb = cb;
}

void midi_ble_settings_build(lv_obj_t* parent, lv_group_t* group)
{
    auto* s = crosspad::CrosspadSettings::getInstance();
    if (!s) return;
    if (s_page.timer) lv_timer_delete(s_page.timer);
    s_page = Page{};

    settings_section_header(parent, "BLE MIDI");
    if (s_cb.setEnabled) settings_row_switch(parent, group, "Enabled", &s->wireless.enableBLE, enable_cb);
    if (s_cb.setMode) {
        settings_row_dropdown(parent, group, "Role", "Host (connect to)\nServer (advertise)",
                              &s->wireless.bleMidiMode, mode_cb);
    }
    if (s_cb.applyTxOffset) {
        settings_row_dropdown(parent, group, "Send transpose", tx_offset_options(),
                              &s->wireless.bleMidiTxOffset, tx_offset_cb);
    }
    if (s_cb.applyRxOffset) {
        s_page.rxIndex = rx_offset_index(s->wireless.bleMidiNoteOffset);
        settings_row_dropdown(parent, group, "Receive transpose", kRxOffsetOptions, &s_page.rxIndex,
                              rx_offset_cb);
    }

    settings_section_header(parent, "Status");
    if (s_cb.stateName) s_page.state = value_label(settings_row_info(parent, "State", ""));
    if (s_cb.connectedDevice) s_page.peer = value_label(settings_row_info(parent, "Peer", ""));
    /* Both actions are always listed, whatever the link is doing: a press on
     * one that does not apply is a no-op, and rows that came and went with
     * the state would move the encoder's walk under the user. */
    if (s_cb.startScan) {
        s_page.scanButton = lv_obj_get_child(settings_row_action(parent, group, "Find devices", "Scan", scan_cb), -1);
    }
    if (s_cb.disconnect) settings_row_action(parent, group, "Connection", "Disconnect", disconnect_cb);

    /* Always there, hidden while empty: deleting it is how the page learns it
     * is gone. Rows only with a Scan to fill them. */
    s_page.devices = lv_obj_create(parent);
    lv_obj_remove_style_all(s_page.devices);
    lv_obj_set_size(s_page.devices, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(s_page.devices, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(s_page.devices, lv_obj_get_style_pad_row(parent, LV_PART_MAIN), 0);
    lv_obj_remove_flag(s_page.devices, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_page.devices, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(s_page.devices, page_deleted_cb, LV_EVENT_DELETE, nullptr);

    if (s_cb.lastMessages) {
        settings_section_header(parent, "MIDI monitor");
        s_page.midiIn = value_label(settings_row_info(parent, "In", ""));
        s_page.midiOut = value_label(settings_row_info(parent, "Out", ""));
    }

    poll(nullptr);
    s_page.timer = lv_timer_create(poll, kPollMs, nullptr);
}

} // namespace crosspad_gui

#endif // USE_LVGL
