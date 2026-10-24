// SPDX-License-Identifier: GPL-3.0-or-later
#ifdef USE_LVGL

#include "crosspad-gui/components/segment_meter.h"
#include "crosspad-gui/components/vu_meter.h"

#include <algorithm>
#include <cmath>

namespace {

constexpr int32_t  kSegSize   = 2;
constexpr int32_t  kSegPitch  = 3;
constexpr int      kMaxSegs   = 40;      ///< a 121 px well
constexpr uint32_t kTickMs    = 50;
constexpr float    kFall      = 0.85f;   ///< a tick's fall: ~28 dB a second
constexpr float    kFullScale = 32767.0f; ///< int16 peak of a full-scale signal
constexpr float    kSilenceDb = -96.0f;  ///< level of a zero peak, below every segment

/// Arrange's ten thresholds; a taller well spreads its segments over them.
constexpr float kArrangeDb[10] = {-48, -42, -36, -30, -24, -18, -12, -6, -3, -1};

const lv_color_t kWell  = lv_color_hex(0x1B1E22);
const lv_color_t kGreen = lv_color_hex(0x9CD639);
const lv_color_t kAmber = lv_color_hex(0xFFC857);
const lv_color_t kRed   = lv_color_hex(0xFF5C5C);

struct SegmentMeter {
    lv_obj_t*   seg[2][kMaxSegs];
    float       db[kMaxSegs];     ///< where each segment lights
    int         count;
    float       shown[2];
    uint8_t     lit[2];
    lv_timer_t* timer;
};

/// Segment @p i of @p n: Arrange's table, read at the same fraction of it.
float threshold(int i, int n)
{
    if (n <= 1) return kArrangeDb[9];
    const float x = 9.0f * static_cast<float>(i) / static_cast<float>(n - 1);
    const int k = static_cast<int>(x);
    if (k >= 9) return kArrangeDb[9];
    return kArrangeDb[k] + (x - static_cast<float>(k)) * (kArrangeDb[k + 1] - kArrangeDb[k]);
}

/// Only the segments whose state changed are touched.
void tick_cb(lv_timer_t* t)
{
    auto* m = static_cast<SegmentMeter*>(lv_timer_get_user_data(t));
    int16_t peak[2];
    crosspad_gui::vu_take_peaks(peak[0], peak[1]);
    for (int c = 0; c < 2; ++c) {
        m->shown[c] = std::max(static_cast<float>(peak[c]) / kFullScale, m->shown[c] * kFall);
        const float db = m->shown[c] > 0.0f ? 20.0f * std::log10(m->shown[c]) : kSilenceDb;
        uint8_t lit = 0;
        while (lit < m->count && db >= m->db[lit]) ++lit;
        if (lit == m->lit[c]) continue;
        for (int i = 0; i < m->count; ++i) {
            const bool on = i < lit, was = i < m->lit[c];
            if (on != was) lv_obj_set_style_bg_opa(m->seg[c][i], on ? LV_OPA_COVER : LV_OPA_20, 0);
        }
        m->lit[c] = lit;
    }
}

void delete_cb(lv_event_t* e)
{
    auto* m = static_cast<SegmentMeter*>(lv_event_get_user_data(e));
    lv_timer_delete(m->timer);
    lv_free(m);
}

} // namespace

namespace crosspad_gui {

lv_obj_t * create_segment_meter(lv_obj_t * parent, int32_t height)
{
    auto* m = static_cast<SegmentMeter*>(lv_malloc_zeroed(sizeof(SegmentMeter)));
    if (!m) return nullptr;
    m->count = std::clamp(static_cast<int>((height - 3) / kSegPitch) + 1, 1, kMaxSegs);

    lv_obj_t* well = lv_obj_create(parent);
    lv_obj_remove_style_all(well);
    lv_obj_set_size(well, kSegmentMeterW, height);
    lv_obj_set_style_radius(well, 2, 0);
    lv_obj_set_style_bg_color(well, kWell, 0);
    lv_obj_set_style_bg_opa(well, LV_OPA_COVER, 0);
    lv_obj_remove_flag(well, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(well, LV_OBJ_FLAG_CLICKABLE);

    for (int i = 0; i < m->count; ++i) m->db[i] = threshold(i, m->count);
    for (int c = 0; c < 2; ++c) {
        for (int i = 0; i < m->count; ++i) {
            lv_obj_t* seg = lv_obj_create(well);
            lv_obj_remove_style_all(seg);
            lv_obj_set_pos(seg, 2 + c * 4, height - 2 - i * kSegPitch);
            lv_obj_set_size(seg, kSegSize, kSegSize);
            lv_obj_set_style_bg_color(seg, m->db[i] >= -1.0f ? kRed : m->db[i] >= -6.0f ? kAmber : kGreen, 0);
            lv_obj_set_style_bg_opa(seg, LV_OPA_20, 0);
            lv_obj_remove_flag(seg, LV_OBJ_FLAG_CLICKABLE);
            m->seg[c][i] = seg;
        }
    }

    m->timer = lv_timer_create(tick_cb, kTickMs, m);
    lv_obj_add_event_cb(well, delete_cb, LV_EVENT_DELETE, m);
    return well;
}

} // namespace crosspad_gui

#endif // USE_LVGL
