// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

/**
 * @file midi_ble_panel.h
 * @brief The Settings "BLE MIDI" page, the same on every platform.
 *
 * BLE MIDI: Enabled, Role, Send transpose.
 * Status:   State, Peer, Find devices (Scan), Connection (Disconnect), and the
 *           devices the last scan found, each with Connect.
 *
 * The radio stacks differ (NimBLE on the board, SimpleBLE + RtMidi on the
 * desktop), and so does what they can do: a row whose callback is null is not
 * shown (the device rows need both `startScan` and `scanResults`). A platform
 * with rows of its own registers a builder that calls
 * midi_ble_settings_build() and adds them after it. Rows that change a
 * setting store it in CrosspadSettings::wireless first, then call the
 * platform, then save the settings.
 *
 * LVGL thread only. While the page is open an LVGL timer polls the status
 * callbacks, so no BLE thread ever touches LVGL: the getters are called on the
 * LVGL thread and must be safe there and quick. An action that blocks (a
 * connect that waits for the link) runs on the platform's own thread behind
 * `connect`, and `stateName` says so meanwhile. The page stops polling when
 * its objects are deleted.
 */

#ifdef USE_LVGL

#include <lvgl.h>

#include "crosspad/midi/IBleMidi.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace crosspad_gui {

/// What the page needs from the platform's BLE MIDI stack.
struct MidiBleCallbacks {
    // ── Settings rows ── called after the row stored the new value.
    /// "Enabled": wireless.enableBLE changed; start or stop the stack.
    void (*setEnabled)(bool on) = nullptr;
    /// "Role": wireless.bleMidiMode changed (0 host / central, 1 server / peripheral).
    void (*setMode)(uint8_t mode) = nullptr;
    /// "Send transpose": wireless.bleMidiTxOffset changed (an index into
    /// crosspad/midi/BleMidiOffsets.hpp).
    void (*applyTxOffset)() = nullptr;

    // ── Status, polled ──
    /// A word or two for the State row: "off", "scanning", "connected", ...
    const char* (*stateName)() = nullptr;
    /// The peer of the current link; an empty address when there is none.
    crosspad::BleMidiDevice (*connectedDevice)() = nullptr;
    /// The devices the last scan found.
    std::vector<crosspad::BleMidiDevice> (*scanResults)() = nullptr;

    // ── Actions ──
    void (*startScan)() = nullptr;
    void (*connect)(const std::string& address) = nullptr;
    void (*disconnect)() = nullptr;
};

/// The platform's callbacks; set before the page is opened.
void midi_ble_set_callbacks(const MidiBleCallbacks& cb);

/// SettingsCategory::build of the page:
/// settings_ui_add_category({"BLE MIDI", LV_SYMBOL_BLUETOOTH, midi_ble_settings_build}).
void midi_ble_settings_build(lv_obj_t* parent, lv_group_t* group);

} // namespace crosspad_gui

#endif // USE_LVGL
