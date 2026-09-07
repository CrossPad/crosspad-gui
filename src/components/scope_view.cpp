// SPDX-License-Identifier: MIT

#ifdef USE_LVGL

#include "crosspad-gui/components/scope_view.h"

#include <crosspad/dsp/ScopeTrigger.hpp>

#include <new>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#ifdef ESP_PLATFORM
#include <esp_heap_caps.h>
#endif

namespace {

/// 30 Hz. Fast enough to look live, slow enough that the pull and the redraw
/// cost nothing next to the rest of a screen.
constexpr uint32_t kRefreshMs = 33;

/**
 * @brief How much history one frame pulls.
 *
 * The widest window scopeWindowSamples() ever asks for is 10 ms — 480 samples
 * at 48 kHz — and the trigger searches *backwards* from the end of the
 * snapshot for an edge that still leaves a whole window after it, so it needs
 * a second window's worth of room to search in. 1024 covers both with slack,
 * under the 2048 ceiling a UI-side buffer should stay below.
 */
constexpr uint32_t kSnapshotSamples = 1024;

constexpr uint32_t kBgColor     = 0x111111;
constexpr uint32_t kBorderColor = 0x444444;
constexpr uint32_t kAxisColor   = 0x333333;
constexpr uint32_t kTraceColor  = 0x00FF88;

/// The window's own peak is drawn at this fraction of the half-height, so a
/// quiet decay tail reads as well as a fresh hit — a scope's AUTO range.
constexpr float kAutoScaleTarget = 0.80f;
/**
 * @brief Smallest peak the auto-scale will normalise to.
 *
 * Below it the gain stops rising, so the noise floor between notes stays the
 * flat line it is instead of being amplified into a full-height mess. 0.01 is
 * -40 dBFS: under any note still worth looking at, over the residue of one
 * that has finished.
 */
constexpr float kAutoScaleFloor = 0.01f;
/**
 * @brief Per-frame release of the tracked peak, one pole.
 *
 * The peak *rises* instantly and falls at this rate — an AGC's fast attack,
 * slow release. Smoothing the rise as well looked reasonable and was not: the
 * gain left over from the silence before a hit is the largest the floor
 * allows, so for the first few frames of a note the trace was drawn eighty
 * times too big and clipped flat against both edges. Falling slowly is what
 * stops the picture flickering between hits; rising slowly only delays it.
 */
constexpr float kAutoScaleRelease = 0.3f;

struct ScopeState {
    crosspad_gui::ScopeSource source = nullptr;
    void*     ctx     = nullptr;
    lv_obj_t* canvas  = nullptr;
    lv_obj_t* label   = nullptr;
    lv_timer_t* timer = nullptr;
    float     peakEnv = 0.0f;   ///< tracked window peak the auto-scale divides by
    crosspad_gui::ScopeViewMode mode = crosspad_gui::ScopeViewMode::Live;
    bool      active  = true;
    uint8_t*  pixels  = nullptr;
    float*    history = nullptr; ///< one RMS value per pixel column
    uint32_t  historyHead  = 0;  ///< next slot to write
    uint32_t  historyCount = 0;
    int32_t   w = 0;
    int32_t   h = 0;
    float     samples[kSnapshotSamples];
    char      text[40];
};

lv_color_t color_from_hex(uint32_t hex) {
    return lv_color_make((hex >> 16) & 0xFF, (hex >> 8) & 0xFF, hex & 0xFF);
}

void draw_frame(ScopeState* st) {
    lv_obj_t* canvas = st->canvas;

    // A hidden view must not pull, analyse, or paint. The sampler also pauses
    // this timer explicitly, but keeping this guard makes the widget safe for
    // any other caller that only hides it.
    lv_obj_t* scope = lv_obj_get_parent(canvas);
    if (scope && lv_obj_has_flag(scope, LV_OBJ_FLAG_HIDDEN)) return;

    float hz = 0.0f;
    uint32_t rate = 0;
    uint32_t got = 0;
    if (st->source) {
        got = st->source(st->ctx, st->samples, kSnapshotSamples, &hz, &rate);
        if (got > kSnapshotSamples) got = kSnapshotSamples;
    }

    float rms = 0.0f;
    if (st->mode == crosspad_gui::ScopeViewMode::HistoryRms) {
        // Only RMS mode pays for the reduction and history write. At 30 Hz
        // and 150 px the complete graph spans about five seconds.
        if (got > 0) {
            double sumSquares = 0.0;
            for (uint32_t i = 0; i < got; ++i) {
                sumSquares += static_cast<double>(st->samples[i]) * st->samples[i];
            }
            rms = static_cast<float>(sqrt(sumSquares / static_cast<double>(got)));
            if (rms > 1.0f) rms = 1.0f;
        }
        if (st->history && st->w > 0) {
            st->history[st->historyHead] = rms;
            st->historyHead = (st->historyHead + 1) % static_cast<uint32_t>(st->w);
            if (st->historyCount < static_cast<uint32_t>(st->w)) ++st->historyCount;
        }
    }

    lv_canvas_fill_bg(canvas, color_from_hex(kBgColor), LV_OPA_COVER);

    const int32_t midY = st->h / 2;

    lv_layer_t layer;
    lv_canvas_init_layer(canvas, &layer);

    lv_draw_line_dsc_t dsc;
    lv_draw_line_dsc_init(&dsc);
    dsc.width = 1;
    dsc.opa   = LV_OPA_COVER;

    // Centre line first, so the trace draws over it.
    dsc.color = color_from_hex(kAxisColor);
    dsc.p1 = lv_point_precise_t{ 0, static_cast<lv_value_precise_t>(midY) };
    dsc.p2 = lv_point_precise_t{ static_cast<lv_value_precise_t>(st->w - 1),
                                 static_cast<lv_value_precise_t>(midY) };
    lv_draw_line(&layer, &dsc);

    crosspad::ScopeWindow win;
    if (st->mode == crosspad_gui::ScopeViewMode::Live && got >= 2 && rate > 0) {
        win = crosspad::scopeTrigger(st->samples, got,
                                     crosspad::scopeWindowSamples(hz, rate));
    }

    // Auto-range on the window's own peak, then approach it one pole at a
    // time — a step change in gain between two hits is more distracting than
    // the wrong scale for three frames.
    float peak = 0.0f;
    for (uint32_t i = 0; i < win.length; ++i) {
        const float a = st->samples[win.start + i] < 0 ? -st->samples[win.start + i]
                                                       : st->samples[win.start + i];
        if (a > peak) peak = a;
    }
    if (peak > st->peakEnv) st->peakEnv = peak;
    else st->peakEnv += (peak - st->peakEnv) * kAutoScaleRelease;
    const float gain = kAutoScaleTarget /
                       (st->peakEnv > kAutoScaleFloor ? st->peakEnv : kAutoScaleFloor);

    if (st->mode == crosspad_gui::ScopeViewMode::HistoryRms && st->historyCount > 0) {
        dsc.color = color_from_hex(kTraceColor);
        const uint32_t capacity = static_cast<uint32_t>(st->w);
        const uint32_t oldest = st->historyCount == capacity ? st->historyHead : 0;
        const int32_t firstX = st->w - static_cast<int32_t>(st->historyCount);

        for (uint32_t i = 0; i < st->historyCount; ++i) {
            const float value = st->history[(oldest + i) % capacity];
            // Loudness history is useful on a dB scale: a linear graph makes
            // every decay tail and quieter hit indistinguishable from zero.
            // -60 dB is the floor, 0 dBFS reaches the edge.
            float norm = 0.0f;
            if (value > 0.001f) {
                norm = (20.0f * log10f(value) + 60.0f) / 60.0f;
                if (norm < 0.0f) norm = 0.0f;
                if (norm > 1.0f) norm = 1.0f;
            }
            int32_t mag = static_cast<int32_t>(norm * static_cast<float>(midY));
            if (mag < 1) mag = 1;
            if (mag > midY) mag = midY;
            const int32_t x = firstX + static_cast<int32_t>(i);
            dsc.p1 = lv_point_precise_t{static_cast<lv_value_precise_t>(x),
                                        static_cast<lv_value_precise_t>(midY - mag)};
            dsc.p2 = lv_point_precise_t{static_cast<lv_value_precise_t>(x),
                                        static_cast<lv_value_precise_t>(midY + mag)};
            lv_draw_line(&layer, &dsc);
        }
    } else if (win.length >= 2) {
        // One point per pixel column, so the polyline is the window stretched
        // across the whole width whatever the note.
        dsc.color = color_from_hex(kTraceColor);
        const float scale = static_cast<float>(midY) * gain;

        int32_t prevY = 0;
        for (int32_t x = 0; x < st->w; ++x) {
            const uint32_t idx =
                win.start + static_cast<uint32_t>(
                    (static_cast<int64_t>(x) * (win.length - 1)) / (st->w > 1 ? st->w - 1 : 1));
            const float v = st->samples[idx];

            int32_t y = midY - static_cast<int32_t>(v * scale);
            if (y < 0) y = 0;
            if (y > st->h - 1) y = st->h - 1;

            if (x > 0) {
                dsc.p1 = lv_point_precise_t{ static_cast<lv_value_precise_t>(x - 1),
                                             static_cast<lv_value_precise_t>(prevY) };
                dsc.p2 = lv_point_precise_t{ static_cast<lv_value_precise_t>(x),
                                             static_cast<lv_value_precise_t>(y) };
                lv_draw_line(&layer, &dsc);
            }
            prevY = y;
        }
    }

    lv_canvas_finish_layer(canvas, &layer);

    if (!st->label) return;

    char next[sizeof(st->text)];
    if (st->mode == crosspad_gui::ScopeViewMode::HistoryRms) {
        const float seconds = static_cast<float>(st->historyCount * kRefreshMs) / 1000.0f;
        const int db = rms > 0.0f ? static_cast<int>(20.0f * log10f(rms) - 0.5f) : -99;
        snprintf(next, sizeof(next), "RMS %.1fs %ddB", static_cast<double>(seconds),
                 db < -99 ? -99 : db);
    } else if (rate > 0 && win.length > 0) {
        const float ms = 1000.0f * static_cast<float>(win.length) / static_cast<float>(rate);
        // Tight spacing, no separators: the widest reading has to fit a box
        // as narrow as the sampler's 150 px waveform slot.
        snprintf(next, sizeof(next), "LIVE %dHz %.1fms",
                 static_cast<int>(hz + 0.5f), static_cast<double>(ms));
    } else {
        snprintf(next, sizeof(next), "--Hz");
    }
    // lv_label_set_text() would copy into a fresh allocation every frame; the
    // static variant points at this buffer, so a redraw only costs the compare.
    if (strcmp(next, st->text) != 0) {
        memcpy(st->text, next, sizeof(next));
        lv_label_set_text_static(st->label, st->text);
    }
}

void scope_timer_cb(lv_timer_t* t) {
    auto* st = static_cast<ScopeState*>(lv_timer_get_user_data(t));
    if (st && st->canvas) draw_frame(st);
}

void scope_delete_cb(lv_event_t* e) {
    auto* st = static_cast<ScopeState*>(lv_event_get_user_data(e));
    if (!st) return;
    if (st->timer) lv_timer_delete(st->timer);
    free(st->history);
    free(st->pixels);
    lv_free(st);
}

ScopeState* state_of(lv_obj_t* scope) {
    return scope ? static_cast<ScopeState*>(lv_obj_get_user_data(scope)) : nullptr;
}

} // namespace

lv_obj_t* create_scope_view(lv_obj_t* parent, int32_t w, int32_t h) {
    if (w < 8) w = 8;
    if (h < 8) h = 8;

    auto* st = static_cast<ScopeState*>(lv_malloc(sizeof(ScopeState)));
    if (!st) return nullptr;
    new (st) ScopeState();
    st->w = w;
    st->h = h;
    st->text[0] = '\0';

    // A 120x60 RGB565 canvas is 14 kB — small, but on the S3 it is 14 kB of
    // internal RAM that the UI has none to spare of, so it goes to PSRAM.
    const uint32_t bytes = LV_CANVAS_BUF_SIZE(w, h, 16, 1);
#ifdef ESP_PLATFORM
    st->pixels = static_cast<uint8_t*>(heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
#endif
    if (!st->pixels) st->pixels = static_cast<uint8_t*>(malloc(bytes));
    if (!st->pixels) { lv_free(st); return nullptr; }

    lv_obj_t* cont = lv_obj_create(parent);
    lv_obj_set_size(cont, w, h);
    lv_obj_set_style_bg_color(cont, color_from_hex(kBgColor), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(cont, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(cont, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(cont, color_from_hex(kBorderColor), LV_PART_MAIN);
    lv_obj_set_style_radius(cont, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(cont, 0, LV_PART_MAIN);
    lv_obj_clear_flag(cont, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(cont, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_user_data(cont, st);

    st->canvas = lv_canvas_create(cont);
    lv_canvas_set_buffer(st->canvas, st->pixels, w, h, LV_COLOR_FORMAT_RGB565);
    lv_obj_set_size(st->canvas, w, h);
    lv_obj_align(st->canvas, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_clear_flag(st->canvas, LV_OBJ_FLAG_CLICKABLE);
    lv_canvas_fill_bg(st->canvas, color_from_hex(kBgColor), LV_OPA_COVER);

    st->label = lv_label_create(cont);
    lv_obj_set_style_text_color(st->label, color_from_hex(kTraceColor), 0);
    lv_obj_set_style_text_opa(st->label, LV_OPA_70, 0);
    lv_label_set_text_static(st->label, st->text);
    lv_obj_align(st->label, LV_ALIGN_TOP_LEFT, 2, 1);
    lv_obj_clear_flag(st->label, LV_OBJ_FLAG_CLICKABLE);

    st->timer = lv_timer_create(scope_timer_cb, kRefreshMs, st);
    lv_obj_add_event_cb(cont, scope_delete_cb, LV_EVENT_DELETE, st);

    return cont;
}

void scope_view_set_source(lv_obj_t* scope, crosspad_gui::ScopeSource src, void* ctx) {
    auto* st = state_of(scope);
    if (!st) return;
    if (st->source != src || st->ctx != ctx) scope_view_clear_history(scope);
    st->source = src;
    st->ctx    = ctx;
}

void scope_view_clear_source(lv_obj_t* scope) {
    scope_view_set_source(scope, nullptr, nullptr);
}

void scope_view_set_mode(lv_obj_t* scope, crosspad_gui::ScopeViewMode mode) {
    auto* st = state_of(scope);
    if (!st || st->mode == mode) return;
    st->mode = mode;
    st->peakEnv = 0.0f;
    if (mode == crosspad_gui::ScopeViewMode::HistoryRms) {
        if (!st->history) {
            st->history = static_cast<float*>(calloc(static_cast<size_t>(st->w), sizeof(float)));
        }
        scope_view_clear_history(scope);
    }
    if (st->timer) lv_timer_ready(st->timer);
}

crosspad_gui::ScopeViewMode scope_view_get_mode(lv_obj_t* scope) {
    auto* st = state_of(scope);
    return st ? st->mode : crosspad_gui::ScopeViewMode::Live;
}

void scope_view_clear_history(lv_obj_t* scope) {
    auto* st = state_of(scope);
    if (!st) return;
    if (st->history) {
        memset(st->history, 0, static_cast<size_t>(st->w) * sizeof(float));
    }
    st->historyHead = 0;
    st->historyCount = 0;
}

void scope_view_set_active(lv_obj_t* scope, bool active) {
    auto* st = state_of(scope);
    if (!st || st->active == active) return;
    st->active = active;
    if (!st->timer) return;
    if (active) {
        lv_timer_resume(st->timer);
        lv_timer_ready(st->timer);
    } else {
        lv_timer_pause(st->timer);
    }
}

#endif // USE_LVGL
