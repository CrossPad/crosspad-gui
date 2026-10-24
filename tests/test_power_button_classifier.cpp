// SPDX-License-Identifier: GPL-3.0-or-later
#include <catch2/catch_test_macros.hpp>

#include "crosspad-gui/components/power_button_classifier.h"

using namespace crosspad_gui;

namespace {

using Sample = PowerButtonClassifier::Sample;

Sample at(uint32_t ms, bool pressed)
{
    Sample s;
    s.pressed = pressed;
    s.nowMs = ms;
    return s;
}

Sample asleep(uint32_t ms, bool pressed = false)
{
    Sample s = at(ms, pressed);
    s.awake = false;
    return s;
}

/// A classifier past its baseline: released, awake, the GUI up.
PowerButtonClassifier started(uint32_t ms = 0)
{
    PowerButtonClassifier c;
    c.feed(at(ms, false));
    return c;
}

} // namespace

TEST_CASE("power button: the first sample is the baseline, not a press", "[power]")
{
    PowerButtonClassifier c;
    CHECK(c.feed(at(1000, true)).gesture == PowerGesture::None);
    CHECK(c.feed(at(1100, false)).gesture == PowerGesture::None);
    CHECK(c.feed(at(2000, true)).gesture == PowerGesture::None);
    CHECK(c.feed(at(2100, false)).gesture == PowerGesture::Click);
}

TEST_CASE("power button: a release before the threshold is a click", "[power]")
{
    auto c = started();
    CHECK(c.feed(at(1000, true)).gesture == PowerGesture::None);
    CHECK(c.feed(at(1000 + kPowerHoldMs - 1, false)).gesture == PowerGesture::Click);
}

TEST_CASE("power button: crossing the threshold while held is one hold", "[power]")
{
    auto c = started();
    c.feed(at(1000, true));
    CHECK(c.feed(at(1000 + kPowerHoldMs - 1, true)).gesture == PowerGesture::None);
    CHECK(c.feed(at(1000 + kPowerHoldMs, true)).gesture == PowerGesture::Hold);
    CHECK(c.feed(at(1000 + kPowerHoldMs + 100, true)).gesture == PowerGesture::None);
    CHECK(c.feed(at(1000 + kPowerHoldMs + 200, false)).gesture == PowerGesture::None);
}

TEST_CASE("power button: a release at the threshold no sample saw held is nothing", "[power]")
{
    auto c = started();
    c.feed(at(1000, true));
    CHECK(c.feed(at(1000 + kPowerHoldMs, false)).gesture == PowerGesture::None);
}

TEST_CASE("power button: the press that woke the device is not a gesture", "[power]")
{
    auto c = started();
    c.feed(asleep(1000));
    c.feed(at(1100, false));

    SECTION("inside the guard") {
        c.feed(at(1000 + kPowerWakeGuardMs - 1, true));
        CHECK(c.feed(at(1000 + kPowerWakeGuardMs + 50, false)).gesture == PowerGesture::None);
    }
    SECTION("past the guard") {
        c.feed(at(1000 + kPowerWakeGuardMs, true));
        CHECK(c.feed(at(1000 + kPowerWakeGuardMs + 50, false)).gesture == PowerGesture::Click);
    }
}

TEST_CASE("power button: without a sleep there is no wake guard", "[power]")
{
    auto c = started(0);
    c.feed(at(10, true));
    CHECK(c.feed(at(20, false)).gesture == PowerGesture::Click);
}

TEST_CASE("power button: falling asleep while held cancels the gesture", "[power]")
{
    auto c = started();
    c.feed(at(1000, true));
    c.feed(asleep(1200, true));
    CHECK(c.feed(at(1000 + kPowerHoldMs + 10, true)).gesture == PowerGesture::None);
    CHECK(c.feed(at(1000 + kPowerHoldMs + 20, false)).gesture == PowerGesture::None);
}

TEST_CASE("power button: a press while asleep is not a gesture", "[power]")
{
    auto c = started();
    c.feed(asleep(1000, true));
    CHECK(c.feed(asleep(1100, false)).gesture == PowerGesture::None);
}

TEST_CASE("power button: nothing before the GUI is up", "[power]")
{
    auto c = started();
    Sample press = at(1000, true);
    press.guiReady = false;
    c.feed(press);
    CHECK(c.feed(at(1100, false)).gesture == PowerGesture::None);

    press.nowMs = 2000;
    c.feed(press);
    CHECK(c.feed(at(2000 + kPowerHoldMs, true)).gesture == PowerGesture::None);
}

TEST_CASE("power button: LIGHT sleep right after a hold is cancelled once", "[power]")
{
    auto c = started();
    c.feed(at(1000, true));
    const uint32_t holdAt = 1000 + kPowerHoldMs;
    REQUIRE(c.feed(at(holdAt, true)).gesture == PowerGesture::Hold);
    c.feed(at(holdAt + 100, false));

    Sample light = asleep(holdAt + kPowerLightCancelWindowMs - 1);
    light.lightSleep = true;

    SECTION("inside the window, one kick") {
        CHECK(c.feed(light).cancelLightSleep);
        CHECK_FALSE(c.feed(light).cancelLightSleep);
    }
    SECTION("at the end of the window, none") {
        light.nowMs = holdAt + kPowerLightCancelWindowMs;
        CHECK_FALSE(c.feed(light).cancelLightSleep);
    }
    SECTION("cleared, none") {
        c.clearLightSleepCancel();
        CHECK_FALSE(c.feed(light).cancelLightSleep);
    }
}

TEST_CASE("power button: LIGHT sleep after a click is left alone", "[power]")
{
    auto c = started();
    c.feed(at(1000, true));
    REQUIRE(c.feed(at(1100, false)).gesture == PowerGesture::Click);
    Sample light = asleep(1200);
    light.lightSleep = true;
    CHECK_FALSE(c.feed(light).cancelLightSleep);
}

TEST_CASE("power button: timing survives the 32-bit tick wrapping", "[power]")
{
    auto c = started(0xFFFFFF00u);

    SECTION("click across the wrap") {
        c.feed(at(0xFFFFFFF0u, true));
        CHECK(c.feed(at(0x10u, false)).gesture == PowerGesture::Click);
    }
    SECTION("hold across the wrap") {
        c.feed(at(0xFFFFFF00u, true));
        CHECK(c.feed(at(0xFFFFFF00u + kPowerHoldMs - 1, true)).gesture == PowerGesture::None);
        CHECK(c.feed(at(0xFFFFFF00u + kPowerHoldMs, true)).gesture == PowerGesture::Hold);
    }
}
