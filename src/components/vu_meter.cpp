// SPDX-License-Identifier: GPL-3.0-or-later
#ifdef USE_LVGL

#include "crosspad-gui/components/vu_meter.h"
#include "crosspad-gui/styles/styles.h"
#include "crosspad-gui/platform/IGuiPlatform.h"
#include <atomic>
#include <cmath>

// ── Audio level storage (set via crosspad_gui::vu_set_levels) ────────────────
static int16_t s_audio_max[2] = {0, 0};
/* The loudest since the last vu_take_peaks(): a meter that ticks slower than
 * the audio blocks arrive would miss a hit between two ticks. 32-bit, a load
 * and a store on the audio side -- no read-modify-write there. */
static std::atomic<int32_t> s_audio_peak[2] = {{0}, {0}};

namespace crosspad_gui {
void vu_set_levels(int16_t left, int16_t right) {
    s_audio_max[0] = left;
    s_audio_max[1] = right;
    if (left > s_audio_peak[0].load(std::memory_order_relaxed))
        s_audio_peak[0].store(left, std::memory_order_relaxed);
    if (right > s_audio_peak[1].load(std::memory_order_relaxed))
        s_audio_peak[1].store(right, std::memory_order_relaxed);
}

void vu_take_peaks(int16_t& left, int16_t& right) {
    left  = static_cast<int16_t>(s_audio_peak[0].exchange(0, std::memory_order_relaxed));
    right = static_cast<int16_t>(s_audio_peak[1].exchange(0, std::memory_order_relaxed));
}
} // namespace crosspad_gui

static const int REFRESH_MS = 16; // 60 Hz

// int16 level (0-32767) to a dB-like logarithmic scale.
//
// The gate is the meter's zero point and the bottom of the dB range at once.
// With two disagreeing floors it was neither, and the display jumped from
// empty to two-fifths full with nothing in between. The old gate of 800 hid a
// DC offset the sample player left on the bus, since removed by the DC blocker
// in MarcelSampleNode; the idle residue is now ~29, so the gate sits just
// above it and the meter gets its low end back.
static const int16_t NOISE_GATE = 50;
static const float   GAIN_MULTIPLIER = 3.5f;

uint16_t int16toLogScale(int16_t val, int base) {
    if (val < NOISE_GATE) return 0;

    float normalized = ((float)val / 32767.0f) * GAIN_MULTIPLIER;
    if (normalized > 1.0f) normalized = 1.0f;

    // Derived from the gate, so the two cannot drift apart: the range is
    // exactly gate → full scale.
    static const float kRangeDb =
        -20.0f * log10f(((float)NOISE_GATE / 32767.0f) * GAIN_MULTIPLIER);

    float db = 20.0f * log10f(normalized);
    float db_normalized = (db + kRangeDb) / kRangeDb;
    if (db_normalized < 0.0f) db_normalized = 0.0f;
    if (db_normalized > 1.0f) db_normalized = 1.0f;

    uint32_t scaled = (uint32_t)(db_normalized * (float)base);
    if (scaled > (uint32_t)base) scaled = base;
    return (uint16_t)scaled;
}

// Peak hold state
struct VUMeterState {
    int16_t peakL;
    int16_t peakR;
    uint32_t peakTimeL;
    uint32_t peakTimeR;
};
static VUMeterState vuState = {0, 0, 0, 0};

lv_obj_t * create_vu_meter(lv_obj_t * parent, int width, int height) {
    lv_obj_t * cont = lv_obj_create(parent);
    lv_obj_add_style(cont, &styleAppContainer, 0);
    lv_obj_set_size(cont, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_all(cont, 0, 0);
    lv_obj_set_style_pad_column(cont, 2, 0);

    lv_obj_t * barL = lv_bar_create(cont);
    lv_obj_set_style_radius(barL, 0, 0);
    lv_obj_set_size(barL, width, height);
    lv_bar_set_range(barL, 0, height);
    lv_bar_set_value(barL, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(barL, lv_color_hex(0x202020), LV_PART_MAIN);
    lv_obj_set_style_border_width(barL, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(barL, lv_color_hex(0x404040), LV_PART_MAIN);

    lv_obj_t * barR = lv_bar_create(cont);
    lv_obj_set_style_radius(barR, 0, 0);
    lv_obj_set_size(barR, width, height);
    lv_bar_set_range(barR, 0, height);
    lv_bar_set_value(barR, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(barR, lv_color_hex(0x202020), LV_PART_MAIN);
    lv_obj_set_style_border_width(barR, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(barR, lv_color_hex(0x404040), LV_PART_MAIN);

    lv_obj_t * peakL = lv_obj_create(barL);
    lv_obj_set_size(peakL, width - 2, 2);
    lv_obj_set_style_bg_color(peakL, lv_color_hex(0xFF0000), 0);
    lv_obj_set_style_border_width(peakL, 0, 0);
    lv_obj_set_style_radius(peakL, 0, 0);
    lv_obj_add_flag(peakL, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t * peakR = lv_obj_create(barR);
    lv_obj_set_size(peakR, width - 2, 2);
    lv_obj_set_style_bg_color(peakR, lv_color_hex(0xFF0000), 0);
    lv_obj_set_style_border_width(peakR, 0, 0);
    lv_obj_set_style_radius(peakR, 0, 0);
    lv_obj_add_flag(peakR, LV_OBJ_FLAG_HIDDEN);

    lv_timer_t * timer = lv_timer_create([](lv_timer_t * t) {
        lv_obj_t * cont = (lv_obj_t *)lv_timer_get_user_data(t);
        if (!cont) return;
        lv_obj_t * barL = lv_obj_get_child(cont, 0);
        lv_obj_t * barR = lv_obj_get_child(cont, 1);

        int barHeight = lv_obj_get_height(barL);

        int16_t levelL = int16toLogScale(s_audio_max[0], barHeight);
        int16_t levelR = int16toLogScale(s_audio_max[1], barHeight);

        lv_bar_set_value(barL, levelL, LV_ANIM_OFF);
        lv_bar_set_value(barR, levelR, LV_ANIM_OFF);

        // Dynamic color: green → yellow → red
        auto updateBarColor = [](lv_obj_t* bar, int16_t level, int height) {
            float percent = (float)level / (float)height;
            lv_color_t color;
            if (percent < 0.60f) {
                color = lv_color_hex(0x00FF00);
            } else if (percent < 0.85f) {
                float transition = (percent - 0.60f) / 0.25f;
                uint8_t red = (uint8_t)(transition * 255);
                color = lv_color_make(red, 255, 0);
            } else {
                float transition = (percent - 0.85f) / 0.15f;
                uint8_t green = (uint8_t)((1.0f - transition) * 255);
                color = lv_color_make(255, green, 0);
            }
            lv_obj_set_style_bg_color(bar, color, LV_PART_INDICATOR);
        };

        updateBarColor(barL, levelL, barHeight);
        updateBarColor(barR, levelR, barHeight);

        // Peak hold (1.5 seconds)
        uint32_t now = crosspad_gui::getGuiPlatform().millis();
        const uint32_t PEAK_HOLD_MS = 1500;

        lv_obj_t * peakIndicatorL = lv_obj_get_child(barL, 0);
        if (levelL > vuState.peakL) {
            vuState.peakL = levelL;
            vuState.peakTimeL = now;
        }
        if (now - vuState.peakTimeL > PEAK_HOLD_MS) {
            vuState.peakL = levelL;
        }
        if (vuState.peakL > 2) {
            int yPos = barHeight - vuState.peakL;
            lv_obj_set_pos(peakIndicatorL, 1, yPos);
            lv_obj_clear_flag(peakIndicatorL, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(peakIndicatorL, LV_OBJ_FLAG_HIDDEN);
        }

        lv_obj_t * peakIndicatorR = lv_obj_get_child(barR, 0);
        if (levelR > vuState.peakR) {
            vuState.peakR = levelR;
            vuState.peakTimeR = now;
        }
        if (now - vuState.peakTimeR > PEAK_HOLD_MS) {
            vuState.peakR = levelR;
        }
        if (vuState.peakR > 2) {
            int yPos = barHeight - vuState.peakR;
            lv_obj_set_pos(peakIndicatorR, 1, yPos);
            lv_obj_clear_flag(peakIndicatorR, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(peakIndicatorR, LV_OBJ_FLAG_HIDDEN);
        }
    }, REFRESH_MS, cont);

    lv_obj_add_event_cb(cont, [](lv_event_t * e) {
        if (lv_event_get_code(e) == LV_EVENT_DELETE) {
            lv_timer_t * t = (lv_timer_t *)lv_event_get_user_data(e);
            if (t) lv_timer_delete(t);
        }
    }, LV_EVENT_DELETE, timer);

    return cont;
}

#endif // USE_LVGL
