// SPDX-License-Identifier: GPL-3.0-or-later
// The scope view in SPECTRUM mode on a headless display: the mode cycle, a
// source that goes away, and (with SCOPE_SNAPSHOT_DIR set) PNGs of what it draws.
#include <catch2/catch_test_macros.hpp>

#include "crosspad-gui/components/scope_view.h"
#include "lvgl_headless.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

using crosspad_gui::ScopeViewMode;

namespace {

constexpr int32_t kW = 150;   // the Sampler's waveform slot
constexpr int32_t kH = 60;
constexpr uint32_t kRate = 44100;

struct Voice { float level; };

/// A 220 Hz tone with a few harmonics, a 3.1 kHz ping, and a noise-free floor.
uint32_t pull(void* ctx, float* out, uint32_t n, float* hz, uint32_t* rate) {
    const float level = static_cast<Voice*>(ctx)->level;
    static uint64_t t = 0;
    for (uint32_t i = 0; i < n; ++i, ++t) {
        const float ph = 2.0f * 3.14159265f * static_cast<float>(t) / static_cast<float>(kRate);
        out[i] = level * (0.5f * sinf(220.0f * ph) + 0.25f * sinf(440.0f * ph) +
                          0.12f * sinf(660.0f * ph) + 0.06f * sinf(3100.0f * ph));
    }
    *hz = 220.0f;
    *rate = kRate;
    return n;
}

void run_frames(int frames) {
    for (int i = 0; i < frames; ++i) {
        lv_tick_inc(40);
        lv_timer_handler();
    }
}

} // namespace

TEST_CASE("scope view cycles Live, RMS, Spectrum and dies in Spectrum", "[scope_spectrum]") {
    lvgl_headless_init();
    lv_obj_t* screen = lv_obj_create(nullptr);
    lv_screen_load(screen);
    Voice voice{ 1.0f };
    lv_obj_t* scope = create_scope_view(screen, kW, kH);
    REQUIRE(scope);
    scope_view_set_source(scope, pull, &voice);

    for (ScopeViewMode m : { ScopeViewMode::HistoryRms, ScopeViewMode::Spectrum,
                             ScopeViewMode::Live, ScopeViewMode::Spectrum }) {
        scope_view_set_mode(scope, m);
        REQUIRE(scope_view_get_mode(scope) == m);
        run_frames(5);
    }

    // Source swap and silence in Spectrum: levels decay, nothing crashes.
    scope_view_clear_source(scope);
    run_frames(60);
    scope_view_set_active(scope, false);
    run_frames(3);
    scope_view_set_active(scope, true);
    scope_view_set_source(scope, pull, &voice);
    run_frames(3);

    scope_view_clear_source(scope);
    lv_obj_delete(screen);   // with SPECTRUM selected
}

TEST_CASE("snapshots of the spectrum", "[scope_spectrum]") {
    const char* dir = std::getenv("SCOPE_SNAPSHOT_DIR");
    if (!dir) return;
    lvgl_headless_init();
    lv_obj_t* screen = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x0E1014), 0);
    lv_screen_load(screen);
    Voice voice{ 1.0f };
    lv_obj_t* scope = create_scope_view(screen, kW, kH);
    lv_obj_set_pos(scope, 10, 10);
    scope_view_set_source(scope, pull, &voice);
    lv_obj_t* big = create_scope_view(screen, 300, 120);
    lv_obj_set_pos(big, 10, 90);
    scope_view_set_source(big, pull, &voice);
    for (lv_obj_t* s : { scope, big }) scope_view_set_mode(s, ScopeViewMode::Spectrum);

    auto shot = [&](const char* name) {
        char path[512];
        std::snprintf(path, sizeof path, "%s/%s.png", dir, name);
        CHECK(lvgl_headless_write_png(path));
    };
    run_frames(10);
    shot("spectrum_playing");
    voice.level = 0.0f;
    run_frames(6);
    shot("spectrum_decaying");
    run_frames(60);
    shot("spectrum_silent");

    scope_view_clear_source(scope);
    scope_view_clear_source(big);
    lv_obj_delete(screen);
}
