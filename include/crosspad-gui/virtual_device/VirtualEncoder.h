// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#ifdef USE_LVGL
#include "lvgl.h"
#include <cstdint>

namespace crosspad_gui {

/**
 * @brief Visual rotary encoder knob rendered in LVGL.
 *
 * Renders a circular knob with a radial indicator line that rotates.
 * Input is platform-agnostic: call handleWheelDelta() for rotation and
 * handleButtonPress() for press/release from whatever input source the
 * platform provides (mouse wheel, touchscreen gesture, MIDI CC, etc.).
 *
 * Portable across platforms (PC emulator, ESP32-S3 touchscreen, etc.).
 */
class VirtualEncoder {
public:
    VirtualEncoder() = default;

    /// Build the encoder on @p parent at the given position / size.
    void create(lv_obj_t* parent, int32_t x, int32_t y, int32_t size);

    /// Apply current angle + pressed state to the LVGL objects.
    /// Call from a periodic timer (~30 fps).
    void update();

    /// Rotate the encoder by a delta (e.g. from mouse wheel or touch gesture).
    void handleWheelDelta(int dy);

    /// Set encoder button press/release state.
    void handleButtonPress(bool pressed);

    /// Set encoder angle from an incoming CC value using shortest-path delta.
    /// @param value       Current CC value
    /// @param ccRange     Total number of distinct CC values before wrap (e.g. 31 for 0-30)
    /// @param stepsPerRev Physical encoder detents per full revolution (default 18)
    void setFromCC(uint8_t value, uint8_t ccRange = 31, uint8_t stepsPerRev = 18);

private:
    lv_obj_t* obj_       = nullptr;   // outer knob circle
    lv_obj_t* indicator_ = nullptr;   // radial mark
    int32_t   angle_     = 0;         // accumulated rotation (0.1-degree units)
    bool      pressed_   = false;
    int32_t   size_      = 0;
    int8_t    lastCC_    = -1;        // previous CC value for delta tracking

    static void onClicked(lv_event_t* e);
};

} // namespace crosspad_gui

#endif // USE_LVGL
