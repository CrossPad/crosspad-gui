// SPDX-License-Identifier: MIT
#pragma once

#ifdef USE_LVGL

#include <lvgl.h>

class App;

namespace crosspad_gui {

/// Platform callbacks for BLE MIDI operations.
struct MidiBleCallbacks {
    bool (*isConnected)()     = nullptr;
    void (*setEnabled)(bool)  = nullptr;
};

/// Register platform BLE callbacks. Must be called before creating the panel.
void midi_ble_set_callbacks(const MidiBleCallbacks& cb);

/// Retrieve the currently registered BLE callbacks.
const MidiBleCallbacks& midi_ble_get_callbacks();

/// Create the MIDI BLE settings panel.
lv_obj_t* lv_CreateMidiBLE(lv_obj_t* parent, App* app);

/// Destroy the MIDI BLE settings panel.
void lv_MidiBLE_destroy(lv_obj_t* obj);

} // namespace crosspad_gui

#endif // USE_LVGL
