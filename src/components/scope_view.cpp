// SPDX-License-Identifier: GPL-3.0-or-later

#ifdef USE_LVGL

#include "crosspad-gui/components/scope_view.h"

#include "crosspad-gui/components/spectrum_math.h"

#include <crosspad/dsp/FftFactory.hpp>
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

/// A held peak tick stays this many frames (~0.5 s) before it falls.
constexpr uint8_t kSpectrumPeakHoldFrames = 15;
/// Bar fall per frame, as a fraction of the distance to the new level: the
/// rise is instant, the fall takes ~0.3 s, so a hit reads and a tail decays.
constexpr float kSpectrumBarRelease = 0.25f;
/// Peak tick fall per frame once the hold is over, of the full scale.
constexpr float kSpectrumPeakFall = 0.04f;
/// A bar this far above the floor counts as signal (silence check, label).
constexpr float kSpectrumSignal = 0.02f;
/// A block whose loudest sample is below this (-80 dBFS, under the -72 dB floor
/// of the scale) is drawn as silence without being transformed.
constexpr float kSpectrumSilence = 1e-4f;
/// Pixels per bar, gap included. Bars are as many as fit, up to kSpectrumMaxBars.
constexpr int32_t kSpectrumBarPx = 3;

constexpr uint32_t kPeakColor = 0xE8FFF4;
/// Widest scope SPECTRUM draws: the display's own width.
constexpr int32_t kSpectrumMaxWidth = 320;

/**
 * @brief Everything SPECTRUM needs, allocated when that mode is first chosen.
 *
 * ~13 kB with the transform buffer, so a scope that stays on Live or RMS never
 * pays for it. The buffer is 16-byte aligned: esp-dsp's kernels move complex
 * pairs with 64-bit loads.
 */
struct SpectrumState {
    alignas(16) float fftBuf[2 * crosspad_gui::kSpectrumFftSize];
    float window[crosspad_gui::kSpectrumFftSize];
    crosspad_gui::SpectrumBar map[crosspad_gui::kSpectrumMaxBars];
    int8_t  colBar[kSpectrumMaxWidth];               ///< bar a pixel column belongs to, -1 = the gap
    bool    colGrid[kSpectrumMaxWidth];              ///< column of a decade line (100 Hz, 1 kHz, 10 kHz)
    int16_t gridRow[2] = { -1, -1 };                 ///< rows of the -24 / -48 dB lines
    int16_t colTop[kSpectrumMaxWidth];               ///< first row of each column's bar as painted; h = none
    int16_t colTick[kSpectrumMaxWidth];              ///< row of its peak tick as painted; -1 = none
    bool    painted = false;                         ///< the canvas holds this state's last frame
    float   level[crosspad_gui::kSpectrumMaxBars];   ///< bar height, 0..1
    float   peak[crosspad_gui::kSpectrumMaxBars];    ///< held tick, 0..1
    uint8_t hold[crosspad_gui::kSpectrumMaxBars];    ///< frames the tick still holds
    crosspad::IFft* fft = nullptr;
    uint32_t rate = 0;    ///< sample rate `map` was built for
    uint32_t bars = 0;
};

struct ScopeState {
    crosspad_gui::ScopeSource source = nullptr;
    void*     ctx     = nullptr;
    lv_obj_t* canvas  = nullptr;
    lv_obj_t* label   = nullptr;
    lv_timer_t* timer = nullptr;
    float     peakEnv = 0.0f;   ///< tracked window peak the auto-scale divides by
    crosspad_gui::ScopeViewMode mode = crosspad_gui::ScopeViewMode::Live;
    bool      active  = true;
    bool      wasSilent = false; ///< the last frame drawn was a silent window
    uint8_t*  pixels  = nullptr;
    float*    history = nullptr; ///< one RMS value per pixel column
    SpectrumState* spectrum = nullptr;
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

/**
 * @brief The canvas' own RGB565 pixels, written directly.
 *
 * The trace used to go through the canvas layer, one lv_draw_line() per pixel
 * column: 150 draw tasks a frame, 34 ms of software rendering at 30 Hz. LVGL
 * never idled, and under a pitched kit — whose tap always has samples — the
 * CDC drain loop below it got no CPU for as long as the sampler was on screen.
 * A vertical span per column into the buffer is the same picture in ~50 us.
 */
struct Pixels {
    uint8_t* data;
    uint32_t stride;   ///< bytes per row, as the draw buffer says
    int32_t  w;
    int32_t  h;
};

Pixels pixels_of(lv_obj_t* canvas) {
    lv_draw_buf_t* db = lv_canvas_get_draw_buf(canvas);
    return Pixels{ db->data, db->header.stride,
                   static_cast<int32_t>(db->header.w), static_cast<int32_t>(db->header.h) };
}

void fill(const Pixels& p, uint16_t c) {
    for (int32_t y = 0; y < p.h; ++y) {
        auto* row = reinterpret_cast<uint16_t*>(p.data + static_cast<uint32_t>(y) * p.stride);
        for (int32_t x = 0; x < p.w; ++x) row[x] = c;
    }
}

void hline(const Pixels& p, int32_t y, uint16_t c) {
    if (y < 0 || y >= p.h) return;
    auto* row = reinterpret_cast<uint16_t*>(p.data + static_cast<uint32_t>(y) * p.stride);
    for (int32_t x = 0; x < p.w; ++x) row[x] = c;
}

/// Column x from y0 to y1 inclusive, either order, clipped to the buffer.
void vspan(const Pixels& p, int32_t x, int32_t y0, int32_t y1, uint16_t c) {
    if (x < 0 || x >= p.w) return;
    if (y0 > y1) { const int32_t t = y0; y0 = y1; y1 = t; }
    if (y0 < 0) y0 = 0;
    if (y1 > p.h - 1) y1 = p.h - 1;
    uint8_t* px = p.data + static_cast<uint32_t>(y0) * p.stride + static_cast<uint32_t>(x) * 2;
    for (int32_t y = y0; y <= y1; ++y, px += p.stride) {
        *reinterpret_cast<uint16_t*>(px) = c;
    }
}

void* spectrum_alloc(size_t bytes, size_t align) {
    if (align < 16) align = 16;
    const size_t rounded = (bytes + align - 1) & ~(align - 1);
#ifdef ESP_PLATFORM
    return heap_caps_aligned_alloc(align, rounded, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#else
    return aligned_alloc(align, rounded);
#endif
}

void spectrum_free(void* p) {
#ifdef ESP_PLATFORM
    heap_caps_free(p);
#else
    free(p);
#endif
}

void spectrum_reset(SpectrumState* sp) {
    memset(sp->level, 0, sizeof(sp->level));
    memset(sp->peak, 0, sizeof(sp->peak));
    memset(sp->hold, 0, sizeof(sp->hold));
    sp->painted = false;
}

/// The bars and their held ticks over a faint grid of -24 / -48 dB and the
/// decades 100 Hz, 1 kHz, 10 kHz. Paints nothing when the frame is as silent as
/// the one before.
void draw_spectrum(ScopeState* st, uint32_t got, uint32_t rate) {
    SpectrumState* sp = st->spectrum;
    constexpr uint32_t n = crosspad_gui::kSpectrumFftSize;

    float fresh[crosspad_gui::kSpectrumMaxBars];
    float loudestSample = 0.0f;
    for (uint32_t i = 0; i < got; ++i) {
        const float a = st->samples[i] < 0 ? -st->samples[i] : st->samples[i];
        if (a > loudestSample) loudestSample = a;
    }
    // Between hits the engine hands over digital silence: nothing to transform.
    if (loudestSample >= kSpectrumSilence && rate > 0 && sp->fft) {
        if (rate != sp->rate) {
            crosspad_gui::spectrum_map_bars(sp->map, sp->bars, n, rate);
            sp->rate = rate;
        }
        // Newest n samples, windowed, real part only; short pulls are zero padded.
        const uint32_t used = got < n ? got : n;
        const float* src = st->samples + (got - used);
        for (uint32_t i = 0; i < used; ++i) {
            sp->fftBuf[2 * i]     = src[i] * sp->window[i];
            sp->fftBuf[2 * i + 1] = 0.0f;
        }
        for (uint32_t i = used; i < n; ++i) {
            sp->fftBuf[2 * i]     = 0.0f;
            sp->fftBuf[2 * i + 1] = 0.0f;
        }
        sp->fft->forward(sp->fftBuf, n);
        crosspad_gui::spectrum_levels(sp->fftBuf, n, sp->map, sp->bars, fresh);
    } else {
        memset(fresh, 0, sizeof(fresh));
    }

    bool signal = false;
    uint32_t loudest = 0;
    for (uint32_t i = 0; i < sp->bars; ++i) {
        float& lv = sp->level[i];
        lv = fresh[i] > lv ? fresh[i] : lv + (fresh[i] - lv) * kSpectrumBarRelease;
        if (lv < 0.002f) lv = 0.0f;

        if (lv >= sp->peak[i]) {
            sp->peak[i] = lv;
            sp->hold[i] = kSpectrumPeakHoldFrames;
        } else if (sp->hold[i] > 0) {
            --sp->hold[i];
        } else {
            sp->peak[i] -= kSpectrumPeakFall;
            if (sp->peak[i] < lv) sp->peak[i] = lv;
        }
        if (sp->peak[i] >= kSpectrumSignal) signal = true;
        if (fresh[i] > fresh[loudest]) loudest = i;
    }

    // Rest like the Live trace: a decayed, empty spectrum after an empty
    // spectrum is the picture already on screen.
    if (!signal && st->wasSilent) return;
    st->wasSilent = !signal;

    // The canvas is PSRAM: repainting all of it every frame cost ~2.4 ms on the
    // board. A column is repainted only where its bar or tick moved since the
    // last frame; the first frame after a mode change paints everything.
    const Pixels px = pixels_of(st->canvas);
    const uint16_t bg   = lv_color_to_u16(color_from_hex(kBgColor));
    const uint16_t axis = lv_color_to_u16(color_from_hex(kAxisColor));
    const uint16_t bar  = lv_color_to_u16(color_from_hex(kTraceColor));
    const uint16_t tick = lv_color_to_u16(color_from_hex(kPeakColor));
    const int32_t base = st->h - 1;
    const int32_t span = base - 1;   // bar height at 0 dBFS

    auto at = [&](int32_t x, int32_t y) {
        return reinterpret_cast<uint16_t*>(px.data + static_cast<uint32_t>(y) * px.stride) + x;
    };
    auto behind = [&](int32_t x, int32_t y, int32_t top) {
        if (y >= top) return bar;
        return (y == sp->gridRow[0] || y == sp->gridRow[1] || sp->colGrid[x]) ? axis : bg;
    };

    for (int32_t x = 0; x < st->w; ++x) {
        const int32_t i = sp->colBar[x];
        int32_t top = st->h;   // none
        int32_t mark = -1;
        if (i >= 0) {
            const int32_t hBar = static_cast<int32_t>(sp->level[i] * static_cast<float>(span));
            if (hBar > 0) top = base - hBar + 1;
            if (sp->peak[i] >= kSpectrumSignal) {
                mark = base - static_cast<int32_t>(sp->peak[i] * static_cast<float>(span));
            }
        }
        if (sp->painted && top == sp->colTop[x] && mark == sp->colTick[x]) continue;

        const int32_t oldTop = sp->painted ? sp->colTop[x] : 0;
        const int32_t y0 = sp->painted ? (top < oldTop ? top : oldTop) : 0;
        const int32_t y1 = sp->painted ? (top < oldTop ? oldTop : top) : st->h;
        for (int32_t y = y0; y < y1; ++y) *at(x, y) = behind(x, y, top);
        const int32_t oldMark = sp->painted ? sp->colTick[x] : -1;
        if (oldMark >= 0 && oldMark != mark) *at(x, oldMark) = behind(x, oldMark, top);
        if (mark >= 0) *at(x, mark) = tick;
        sp->colTop[x] = static_cast<int16_t>(top);
        sp->colTick[x] = static_cast<int16_t>(mark);
    }
    sp->painted = true;

    lv_obj_invalidate(st->canvas);

    if (!st->label) return;
    char next[sizeof(st->text)];
    if (signal && fresh[loudest] >= kSpectrumSignal) {
        snprintf(next, sizeof(next), "FFT %dHz",
                 static_cast<int>(crosspad_gui::spectrum_bar_hz(loudest, sp->bars) + 0.5f));
    } else {
        snprintf(next, sizeof(next), "FFT");
    }
    if (strcmp(next, st->text) != 0) {
        memcpy(st->text, next, sizeof(next));
        lv_label_set_text_static(st->label, st->text);
    }
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

    if (st->mode == crosspad_gui::ScopeViewMode::Spectrum) {
        draw_spectrum(st, got, rate);
        return;
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

    const int32_t midY = st->h / 2;

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

    // A silent window after a silent window is the flat line already on
    // screen: no pixels, no invalidation, nothing for the renderer. The RMS
    // history scrolls every frame, so only the live trace can rest.
    const bool silent = st->mode == crosspad_gui::ScopeViewMode::Live && peak <= kAutoScaleFloor;
    if (silent && st->wasSilent) return;
    st->wasSilent = silent;

    const Pixels px = pixels_of(canvas);
    fill(px, lv_color_to_u16(color_from_hex(kBgColor)));
    // Centre line first, so the trace draws over it.
    hline(px, midY, lv_color_to_u16(color_from_hex(kAxisColor)));
    const uint16_t trace = lv_color_to_u16(color_from_hex(kTraceColor));

    if (st->mode == crosspad_gui::ScopeViewMode::HistoryRms && st->historyCount > 0) {
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
            vspan(px, firstX + static_cast<int32_t>(i), midY - mag, midY + mag, trace);
        }
    } else if (win.length >= 2) {
        // One point per pixel column, so the polyline is the window stretched
        // across the whole width whatever the note. Each column carries the
        // span from the previous sample's row to its own, so a steep edge is
        // a continuous line, not a scatter of dots.
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

            vspan(px, x, x > 0 ? prevY : y, y, trace);
            prevY = y;
        }
    }

    lv_obj_invalidate(canvas);

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
    if (st->spectrum) spectrum_free(st->spectrum);
    free(st->pixels);
    lv_free(st);
}

/// Allocate the spectrum's buffers and fetch the FFT on first use. False leaves
/// the scope on the mode it had. The FFT is asked for again on every selection:
/// core retries an esp-dsp table that failed for want of memory.
bool spectrum_prepare(ScopeState* st) {
    if (!st->spectrum && st->w > kSpectrumMaxWidth) return false;
    if (!st->spectrum) {
        auto* sp = static_cast<SpectrumState*>(spectrum_alloc(sizeof(SpectrumState), alignof(SpectrumState)));
        if (!sp) return false;
        new (sp) SpectrumState();
        crosspad_gui::spectrum_hann(sp->window, crosspad_gui::kSpectrumFftSize);
        const int32_t fit = st->w / kSpectrumBarPx;
        sp->bars = static_cast<uint32_t>(fit < 8 ? 8 : (fit > static_cast<int32_t>(crosspad_gui::kSpectrumMaxBars)
                                                        ? static_cast<int32_t>(crosspad_gui::kSpectrumMaxBars) : fit));
        spectrum_reset(sp);
        // A bar is kSpectrumBarPx wide less one column of gap, however many fit.
        for (int32_t x = 0; x < st->w; ++x) {
            const int32_t i = x * static_cast<int32_t>(sp->bars) / st->w;
            const int32_t next = (i + 1) * st->w / static_cast<int32_t>(sp->bars);
            sp->colBar[x] = static_cast<int8_t>(x == next - 1 && next - 1 > i * st->w / static_cast<int32_t>(sp->bars) ? -1 : i);
            sp->colGrid[x] = false;
        }
        const float octaves = log2f(crosspad_gui::kSpectrumHighHz / crosspad_gui::kSpectrumLowHz);
        for (float hz = 100.0f; hz < crosspad_gui::kSpectrumHighHz; hz *= 10.0f) {
            sp->colGrid[static_cast<int32_t>(static_cast<float>(st->w) *
                                             log2f(hz / crosspad_gui::kSpectrumLowHz) / octaves)] = true;
        }
        for (int32_t k = 0; k < 2; ++k) {
            sp->gridRow[k] = static_cast<int16_t>(1 + (st->h - 2) * 24 * (k + 1) / 72);
        }
        st->spectrum = sp;
    }
    crosspad::FftMemory mem;
    mem.alloc = spectrum_alloc;
    mem.free  = spectrum_free;
    st->spectrum->fft = &crosspad::createFft(mem);
    return true;
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
    if (mode == crosspad_gui::ScopeViewMode::Spectrum && !spectrum_prepare(st)) return;
    st->mode = mode;
    st->peakEnv = 0.0f;
    st->wasSilent = false;
    if (mode == crosspad_gui::ScopeViewMode::Spectrum) spectrum_reset(st->spectrum);
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
    if (st->spectrum) spectrum_reset(st->spectrum);
    st->wasSilent = false;
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
