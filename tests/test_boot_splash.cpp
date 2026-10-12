// SPDX-License-Identifier: GPL-3.0-or-later
#include <catch2/catch_test_macros.hpp>

#include "crosspad-gui/components/boot_splash.h"
#include "gui_test_platform.h"

#include <vector>

namespace {

std::vector<uint8_t> s_backlight;

void record_backlight(uint8_t level)
{
    s_backlight.push_back(level);
}

/// LVGL time in frame-sized steps, so its timers run as they would on a board.
void run_for(guitest::GuiFixture& f, uint32_t ms)
{
    constexpr uint32_t kFrameMs = 10;
    for (uint32_t t = 0; t < ms; t += kFrameMs) f.advance(kFrameMs);
}

constexpr uint32_t kLongerThanTheSplashMs = 4000;
constexpr uint32_t kLongerThanAFadeMs = 1000;

} // namespace

TEST_CASE("boot splash: standalone, it plays, fades and leaves nothing behind", "[boot_splash]")
{
    guitest::GuiFixture f;
    s_backlight.clear();
    f.padLedController.setBrightness(200);

    boot_splash_start(f.screen, 80, record_backlight, true);
    CHECK(boot_splash_active());
    CHECK(lv_obj_get_child_count(f.screen) == 1);
    REQUIRE_FALSE(s_backlight.empty());
    CHECK(s_backlight.front() == 0);

    run_for(f, kLongerThanTheSplashMs);
    CHECK_FALSE(boot_splash_active());
    CHECK(lv_obj_get_child_count(f.screen) == 0);
    CHECK(s_backlight.back() == 80);
    CHECK(f.padLedController.getBrightness() == 200);
}

TEST_CASE("boot splash: at boot, the caller builds the next screen and reveals it", "[boot_splash]")
{
    guitest::GuiFixture f;
    s_backlight.clear();
    f.padLedController.setBrightness(150);

    boot_splash_start(f.screen, 60, record_backlight, false);
    run_for(f, kLongerThanTheSplashMs);
    CHECK_FALSE(boot_splash_active());
    CHECK(s_backlight.back() == 0);
    CHECK(lv_obj_get_child_count(f.screen) == 1);   // the caller's to replace

    lv_obj_clean(f.screen);
    boot_splash_reveal();
    run_for(f, kLongerThanAFadeMs);
    CHECK(s_backlight.back() == 60);
    CHECK(f.padLedController.getBrightness() == 150);
}

TEST_CASE("boot splash: without a backlight, and deleted early by someone else", "[boot_splash]")
{
    guitest::GuiFixture f;

    boot_splash_start(f.screen, 100, nullptr, true);
    run_for(f, 300);
    CHECK(boot_splash_active());

    lv_obj_clean(f.screen);
    CHECK_FALSE(boot_splash_active());
    run_for(f, kLongerThanTheSplashMs);
    CHECK(lv_obj_get_child_count(f.screen) == 0);

    boot_splash_start(f.screen, 100, nullptr, true);
    CHECK(boot_splash_active());
    run_for(f, kLongerThanTheSplashMs);
    CHECK_FALSE(boot_splash_active());
}
