// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

/**
 * @file power_button_classifier.h
 * @brief Click / hold timing of the power button, from samples of its state.
 *
 * Pure logic, no LVGL and no clock of its own: each sample carries the time
 * it was taken. The firmware feeds it from its STM32 register poll, the
 * simulator from its emulated button; what a gesture then does is
 * power_gesture.h's business.
 */

#include <cstdint>

namespace crosspad_gui {

/// The hold threshold both platforms use, and the STM32's own
/// LONG_PRESS_SLEEP_MS: under it a press is a click, at it the hold fires.
/// Acting on the crossing rather than on the release keeps the two gestures
/// from racing each other.
constexpr uint32_t kPowerHoldMs = 500;

/// A press that woke the device from LIGHT sleep or soft-off is consumed by
/// the wake: one that starts sooner than this after a sample last saw the
/// device asleep is not a gesture.
constexpr uint32_t kPowerWakeGuardMs = 700;

/// The STM enters LIGHT sleep (screen and pad LEDs off) when a hold between
/// 0.5 s and soft-off is released -- the same hold that fires the hold
/// gesture. Nothing on the ESP can veto that, but a user-activity kick brings
/// the screen straight back; one kick is allowed when LIGHT sleep follows a
/// hold gesture within this window. The real fix belongs on the STM (no LIGHT
/// sleep on that release) and would make it unnecessary.
constexpr uint32_t kPowerLightCancelWindowMs = 2500;

enum class PowerGesture : uint8_t {
    None,
    Click,   ///< released before kPowerHoldMs
    Hold,    ///< kPowerHoldMs crossed while still held
};

class PowerButtonClassifier {
public:
    struct Sample {
        bool     pressed    = false;
        bool     awake      = true;   ///< the device is up (not LIGHT, not soft-off)
        bool     lightSleep = false;  ///< the device is in LIGHT sleep right now
        bool     guiReady   = true;   ///< the GUI can take a gesture
        uint32_t nowMs      = 0;      ///< monotonic; wraps like any 32-bit tick
    };

    struct Result {
        PowerGesture gesture          = PowerGesture::None;
        /// LIGHT sleep followed a hold gesture: wake the device back up.
        bool         cancelLightSleep = false;
    };

    /// One sample. The first one only sets the baseline: a button already
    /// held then belongs to the wake gesture that booted the device.
    Result feed(const Sample& s);

    /// Drop the LIGHT-sleep cancel a hold armed (a hold fired without the
    /// button replaces it).
    void clearLightSleepCancel() { lightCancelUntilMs_ = 0; }

private:
    bool     haveBaseline_       = false;
    bool     prevPressed_        = false;
    bool     pressActive_        = false;
    bool     holdFired_          = false;
    bool     gestureOk_          = false;
    bool     sawNotAwake_        = false;
    uint32_t pressStartMs_       = 0;
    uint32_t lastNotAwakeMs_     = 0;
    uint32_t lightCancelUntilMs_ = 0;
};

} // namespace crosspad_gui
