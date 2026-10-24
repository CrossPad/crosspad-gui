// SPDX-License-Identifier: GPL-3.0-or-later
#include "crosspad-gui/components/power_button_classifier.h"

namespace crosspad_gui {

PowerButtonClassifier::Result PowerButtonClassifier::feed(const Sample& s)
{
    Result r;
    if (!s.awake) {
        sawNotAwake_    = true;
        lastNotAwakeMs_ = s.nowMs;
    }

    if (s.lightSleep && lightCancelUntilMs_ != 0 &&
        static_cast<int32_t>(lightCancelUntilMs_ - s.nowMs) > 0) {
        lightCancelUntilMs_ = 0;
        r.cancelLightSleep = true;
    }

    if (!haveBaseline_) {
        haveBaseline_ = true;
        prevPressed_  = s.pressed;
        return r;
    }

    const bool wasPressed = prevPressed_;
    prevPressed_ = s.pressed;

    if (!wasPressed && s.pressed) {
        pressStartMs_ = s.nowMs;
        pressActive_  = true;
        holdFired_    = false;
        gestureOk_    = s.awake && s.guiReady &&
                        (!sawNotAwake_ || (s.nowMs - lastNotAwakeMs_) >= kPowerWakeGuardMs);
        return r;
    }

    if (!pressActive_) return r;

    if (s.pressed) {
        if (!s.awake) gestureOk_ = false;
        if (!holdFired_ && gestureOk_ && (s.nowMs - pressStartMs_) >= kPowerHoldMs) {
            holdFired_          = true;
            lightCancelUntilMs_ = s.nowMs + kPowerLightCancelWindowMs;
            r.gesture           = PowerGesture::Hold;
        }
        return r;
    }

    pressActive_ = false;
    if (gestureOk_ && !holdFired_ && (s.nowMs - pressStartMs_) < kPowerHoldMs) {
        r.gesture = PowerGesture::Click;
    }
    gestureOk_ = false;
    return r;
}

} // namespace crosspad_gui
