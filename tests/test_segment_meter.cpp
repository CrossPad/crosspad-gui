// SPDX-License-Identifier: GPL-3.0-or-later
// The segment meter: how many segments a height gets, their colours, what a
// level lights on the next tick, the fall, and the timer going with the well.
#include <catch2/catch_test_macros.hpp>

#include "crosspad-gui/components/segment_meter.h"
#include "crosspad-gui/components/vu_meter.h"
#include "gui_test_platform.h"

#include <cmath>

namespace {

constexpr uint32_t kTickMs = 50;
constexpr int16_t kFullScale = 32767;
constexpr int32_t kArrangeHeight = 30;   ///< Arrange's track meter: ten segments
constexpr int kArrangeSegments = 10;

/// Amplitude of @p db dBFS.
int16_t level_at(float db)
{
    return static_cast<int16_t>(std::lround(kFullScale * std::pow(10.0f, db / 20.0f)));
}

/// Segment @p i (from the bottom) of column @p c in a well of @p n a column.
lv_obj_t* segment(lv_obj_t* well, int c, int i, int n)
{
    return lv_obj_get_child(well, c * n + i);
}

int lit(lv_obj_t* well, int c, int n)
{
    int count = 0;
    for (int i = 0; i < n; ++i) {
        if (lv_obj_get_style_bg_opa(segment(well, c, i, n), LV_PART_MAIN) == LV_OPA_COVER) ++count;
    }
    return count;
}

/// Peaks left over from an earlier test case would light the first tick.
void drain_peaks()
{
    int16_t l = 0, r = 0;
    crosspad_gui::vu_take_peaks(l, r);
}

} // namespace

TEST_CASE("segment meter: a segment every 3 px, as many as fit", "[segment_meter]")
{
    guitest::GuiFixture f;

    lv_obj_t* arrange = crosspad_gui::create_segment_meter(f.screen, kArrangeHeight);
    REQUIRE(arrange);
    CHECK(lv_obj_get_child_count(arrange) == 2 * kArrangeSegments);
    lv_obj_update_layout(arrange);
    CHECK(lv_obj_get_width(arrange) == crosspad_gui::kSegmentMeterW);
    CHECK(lv_obj_get_height(arrange) == kArrangeHeight);

    CHECK(lv_obj_get_child_count(crosspad_gui::create_segment_meter(f.screen, 2)) == 2);
    CHECK(lv_obj_get_child_count(crosspad_gui::create_segment_meter(f.screen, 1000)) == 2 * 40);
}

TEST_CASE("segment meter: green to -6 dBFS, amber to -1, red above", "[segment_meter]")
{
    guitest::GuiFixture f;
    lv_obj_t* well = crosspad_gui::create_segment_meter(f.screen, kArrangeHeight);
    REQUIRE(well);

    const lv_color_t green = lv_color_hex(0x9CD639);
    const lv_color_t amber = lv_color_hex(0xFFC857);
    const lv_color_t red   = lv_color_hex(0xFF5C5C);
    // Arrange's thresholds: -48 -42 -36 -30 -24 -18 -12 -6 -3 -1.
    for (int c = 0; c < 2; ++c) {
        for (int i = 0; i < 7; ++i) {
            CHECK(lv_color_eq(lv_obj_get_style_bg_color(segment(well, c, i, kArrangeSegments), LV_PART_MAIN), green));
        }
        CHECK(lv_color_eq(lv_obj_get_style_bg_color(segment(well, c, 7, kArrangeSegments), LV_PART_MAIN), amber));
        CHECK(lv_color_eq(lv_obj_get_style_bg_color(segment(well, c, 8, kArrangeSegments), LV_PART_MAIN), amber));
        CHECK(lv_color_eq(lv_obj_get_style_bg_color(segment(well, c, 9, kArrangeSegments), LV_PART_MAIN), red));
    }
}

TEST_CASE("segment meter: the loudest since the last tick, then the fall", "[segment_meter]")
{
    guitest::GuiFixture f;
    drain_peaks();
    lv_obj_t* well = crosspad_gui::create_segment_meter(f.screen, kArrangeHeight);
    REQUIRE(well);

    f.advance(kTickMs);
    CHECK(lit(well, 0, kArrangeSegments) == 0);
    CHECK(lit(well, 1, kArrangeSegments) == 0);

    // A hit between two ticks still shows: the peak, not the last block.
    crosspad_gui::vu_set_levels(kFullScale, level_at(-10.0f));
    crosspad_gui::vu_set_levels(0, 0);
    f.advance(kTickMs);
    CHECK(lit(well, 0, kArrangeSegments) == kArrangeSegments);
    CHECK(lit(well, 1, kArrangeSegments) == 7);   // -10 dBFS: up to the -12 segment

    // Down ~28 dB a second: one silent tick takes 0 dBFS under -1.
    f.advance(kTickMs);
    CHECK(lit(well, 0, kArrangeSegments) == kArrangeSegments - 1);

    for (int t = 0; t < 40; ++t) f.advance(kTickMs);
    CHECK(lit(well, 0, kArrangeSegments) == 0);
    CHECK(lit(well, 1, kArrangeSegments) == 0);
}

TEST_CASE("segment meter: its timer goes with the well", "[segment_meter]")
{
    guitest::GuiFixture f;
    drain_peaks();
    lv_obj_t* well = crosspad_gui::create_segment_meter(f.screen, kArrangeHeight);
    REQUIRE(well);
    lv_obj_delete(well);

    crosspad_gui::vu_set_levels(kFullScale, kFullScale);
    f.advance(kTickMs);   // a timer left behind would write to the freed segments
    int16_t l = 0, r = 0;
    crosspad_gui::vu_take_peaks(l, r);
    CHECK(l == kFullScale);   // nobody took them
    CHECK(r == kFullScale);
}
