// SPDX-License-Identifier: GPL-3.0-or-later
#ifdef USE_LVGL

#include "crosspad-gui/components/transport_bar.h"

#include "crosspad-gui/components/mode_overlay.h"
#include "crosspad-gui/components/status_bar.h"
#include "crosspad-gui/components/ui_toast.h"
#include "crosspad-gui/platform/IGuiPlatform.h"
#include "crosspad-gui/platform/GuiServices.h"
#include "crosspad-gui/widgets/gui_helpers.h"

#include "crosspad/status/CrosspadStatus.hpp"
#include "crosspad/transport/Transport.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>

namespace crosspad_gui {

namespace {

/* ── Geometry and palette, 1:1 with "CrossPad UX2000 Touch.html" (v2) ──── *
 *   BPM 2..50 | POS 52..99 | MODE 101..139 | REC 141..179 | PLAY 181..219 |
 *   METRO 221..259 | VOICES + BATTERY 261..318, every tile 36 tall at y 2. */

constexpr int32_t TB_H   = TRANSPORT_BAR_HEIGHT;   // 40
constexpr int32_t TILE_Y = 2;
constexpr int32_t TILE_H = 36;
constexpr int32_t TILE_R = 6;

#define INK     lv_color_hex(0x06120c)
#define C_MINT  lv_color_hex(0x00ff88)
#define C_AMB   lv_color_hex(0xffc857)
#define C_RED   lv_color_hex(0xff5c5c)

#define F10 (&lv_font_montserrat_10)
#define F14 (&lv_font_montserrat_14)
#define F18 (&lv_font_montserrat_18)

constexpr lv_opa_t kWash    = 36;   // an idle tile: 14 % ink
constexpr lv_opa_t kPressed = 72;   // a finger on it: 28 %

/* In the encoder's order, left to right; the VOICES + battery tile (T.bat)
 * comes last, the way to the status icons. */
enum Btn : uint8_t { B_BPM = 0, B_POS, B_MODE, B_REC, B_PLAY, B_METRO, B_COUNT };

/// What the bar paints from: an overdub is a take like a record, red, with
/// DUB in the REC key and the POS label.
enum class BarState : uint8_t { Stop = 0, Pre, Play, Rec };

constexpr uint32_t kRefreshMs = 120;
/* The metronome's beat flash: watched at this rate, lit 80 ms, 140 on the one. */
constexpr uint32_t kBeatPollMs  = 15;
constexpr uint32_t kFlashMs     = 80;
constexpr uint32_t kFlashDownMs = 140;

struct {
    lv_obj_t * row = nullptr;         ///< the bar and the status bar side by side, a page each
    lv_obj_t * bar = nullptr;
    lv_obj_t * btn[B_COUNT] = {};
    lv_obj_t * bpm_v = nullptr;
    lv_obj_t * pos_l = nullptr;       ///< the state word above the position
    lv_obj_t * pos_v = nullptr;
    lv_obj_t * mode_sq[4] = {};
    lv_obj_t * rec_dot = nullptr;
    lv_obj_t * rec_dub = nullptr;
    lv_obj_t * play_tri = nullptr;
    lv_obj_t * play_sq = nullptr;
    lv_obj_t * mt_case = nullptr;
    lv_obj_t * mt_arm = nullptr;
    lv_obj_t * mt_bob = nullptr;
    lv_obj_t * vox = nullptr;
    lv_obj_t * cpu = nullptr;          ///< the render's load over the voices ("VOICES" where none is measured)
    lv_obj_t * bat_fill = nullptr;
    lv_obj_t * bat = nullptr;         ///< VOICES + battery: the encoder's way to the status icons
    lv_obj_t * loading = nullptr;
    lv_obj_t * status_bar = nullptr;
    BarState   st = BarState::Stop;
    bool       status_page = false;   ///< the status icons are the page on screen
    bool       en[B_COUNT] = {};
    bool       dub_on = false;
    bool       click_on = false;
    bool       editing[B_COUNT] = {};   ///< BPM / POS: the encoder turns the value
} T;

void (*s_mode_handler)() = nullptr;
lv_timer_t * s_timer = nullptr;
lv_timer_t * s_beat_timer = nullptr;
/* The keys live in whatever group the navigation encoder currently points at,
 * so the transport is one step past the end of every screen: turning off an
 * app's last field reaches BPM, and on to the battery. LVGL's navigation
 * skips hidden and LV_STATE_DISABLED objects, so a key the claim holder
 * cannot honour drops out of the rotation. The power button's hold is a
 * shortcut to the keys, not the way in. */
lv_group_t * s_joined = nullptr;
/* Where focus was before the power-button shortcut jumped to the transport. */
lv_obj_t * s_focus_return = nullptr;

/* The tempo while BPM is turned: shown instead of the holder's, which only
 * the commit at the end of the turn reaches. After it, shown until the holder
 * has caught up (a song re-measures its clips on its worker first), so the
 * field does not flick back to the old number. */
float    s_edit_bpm = 0.0f;
float    s_held_bpm = 0.0f;
uint32_t s_held_since = 0;
constexpr uint32_t kHeldBpmMs = 3000;

/* The metronome flash: the beat last seen and when the flash ends (0 = off). */
uint8_t  s_last_beat = 0;
uint32_t s_flash_until = 0;
bool     s_flash_lit = false;

/* What the last paint was for: the row restyles a dozen objects, and doing
 * that every 120 ms would also restyle the key the encoder has focused. */
BarState s_painted_st = BarState::Stop;
uint8_t  s_painted_caps = 0;
bool     s_painted_dub = false;
bool     s_painted_mode = false;
bool     s_painted_start = false;
bool     s_painted_click = false;
bool     s_painted_valid = false;

/* The CPU readout: the render's average load smoothed over a few refreshes,
 * the overrun count last seen and when it last moved. */
uint32_t s_cpu_pct = 0;
uint32_t s_cpu_overruns = UINT32_MAX;
uint32_t s_cpu_late_at = 0;
constexpr uint32_t kCpuLateMs = 1000;

lv_color_t bar_color()
{
    return T.st == BarState::Rec ? C_RED
         : T.st == BarState::Pre ? C_AMB
                                 : C_MINT;
}

bool playing() { return T.st == BarState::Play || T.st == BarState::Rec; }

lv_obj_t * box(lv_obj_t * p, int32_t x, int32_t y, int32_t w, int32_t h, int32_t r)
{
    lv_obj_t * o = lv_obj_create(p);
    lv_obj_remove_style_all(o);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_radius(o, r, 0);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE);
    return o;
}

void fill(lv_obj_t * o, lv_color_t c, lv_opa_t opa)
{
    lv_obj_set_style_bg_color(o, c, 0);
    lv_obj_set_style_bg_opa(o, opa, 0);
}

lv_obj_t * label(lv_obj_t * p, const lv_font_t * f, const char * t)
{
    lv_obj_t * l = lv_label_create(p);
    lv_obj_set_style_text_font(l, f, 0);
    lv_obj_set_style_text_color(l, INK, 0);
    lv_label_set_text(l, t);
    lv_obj_remove_flag(l, LV_OBJ_FLAG_CLICKABLE);
    return l;
}

/// A drawn glyph: no font here has a metronome or the design's triangle.
lv_obj_t * line(lv_obj_t * p, const lv_point_precise_t * pts, uint32_t n, int32_t w)
{
    lv_obj_t * l = lv_line_create(p);
    lv_line_set_points(l, pts, n);
    lv_obj_set_style_line_width(l, w, 0);
    lv_obj_set_style_line_rounded(l, true, 0);
    lv_obj_set_style_line_color(l, INK, 0);
    lv_obj_remove_flag(l, LV_OBJ_FLAG_CLICKABLE);
    return l;
}

/// A two-line readout: a 10 px caption over a value, centred in the tile.
lv_obj_t * stack(lv_obj_t * tile, const char * caption, lv_obj_t ** caption_out)
{
    lv_obj_t * c = box(tile, 0, 0, LV_PCT(100), LV_PCT(100), 0);
    lv_obj_set_flex_flow(c, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(c, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_t * a = label(c, F10, caption);
    lv_obj_t * v = label(c, F18, "");
    if (caption_out) *caption_out = a;
    return v;
}

/// @p text in 18 px where it fits the tile, else 14: "123.4" and a three-digit
/// tempo with its tenth are wider than 18 px allows in 47-48.
void set_fitted(lv_obj_t * l, const char * text, int32_t room)
{
    lv_point_t sz;
    lv_text_get_size(&sz, text, F18, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    const lv_font_t * f = sz.x <= room ? F18 : F14;
    if (lv_obj_get_style_text_font(l, 0) != f) lv_obj_set_style_text_font(l, f, 0);
    if (strcmp(lv_label_get_text(l), text) != 0) lv_label_set_text(l, text);
}

void press(uint8_t b)
{
    if (b >= B_COUNT || !T.en[b]) return;
    switch (b) {
        case B_MODE:
            if (s_mode_handler) s_mode_handler();
            return;   // the overlay repaints us
        case B_REC:
            /* One key for a take: REC while stopped, DUB onto what plays, and
             * the same key again ends the overdub. */
            if (T.dub_on || T.st == BarState::Play) crosspad::transportOverdub();
            else                                     crosspad::transportRecord();
            break;
        case B_PLAY:
            if (playing() || T.st == BarState::Pre) crosspad::transportStop();
            else                                    crosspad::transportPlay();
            break;
        case B_METRO:
            crosspad::transportSetClick(!crosspad::transportClick());
            break;
        default:
            return;   // BPM and POS are turned, see value_cb
    }
    /* Paint from the target's own answer, not from what we just asked for: a
     * sequencer waiting for a sync point reports Waiting, not Playing. */
    transportbar_refresh();
}

void ev_cb(lv_event_t * e)
{
    const uint8_t b = (uint8_t)(uintptr_t)lv_event_get_user_data(e);
    const lv_event_code_t code = lv_event_get_code(e);
    if (b >= B_COUNT || !T.en[b]) return;

    if (code == LV_EVENT_LONG_PRESSED) {
        /* MODE held: the status bar. PLAY held: stop and back to 1.1 -- a
         * stop while stopped is the rewind. */
        if (b == B_MODE) transportbar_show(false);
        else if (b == B_PLAY) {
            crosspad::transportStop();
            crosspad::transportStop();
            transportbar_refresh();
        }
        return;
    }
    if (code == LV_EVENT_SHORT_CLICKED) press(b);
}

/// A key tile: an ink wash, darker under a finger, a ring under the encoder.
lv_obj_t * tile(uint8_t id, int32_t x, int32_t w)
{
    lv_obj_t * o = box(T.bar, x, TILE_Y, w, TILE_H, TILE_R);
    fill(o, INK, kWash);
    lv_obj_set_style_bg_opa(o, kPressed, LV_STATE_PRESSED);
    lv_obj_set_style_outline_color(o, INK, LV_STATE_FOCUS_KEY);
    lv_obj_set_style_outline_width(o, 2, LV_STATE_FOCUS_KEY);
    lv_obj_set_style_outline_pad(o, 0, LV_STATE_FOCUS_KEY);
    lv_obj_add_flag(o, LV_OBJ_FLAG_CLICKABLE);
    /* A finger on a key must not take the encoder off the screen: the next
     * power click would then only hand the focus back (powerBack()'s
     * Transport step) instead of leaving the app. */
    lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICK_FOCUSABLE);
    /* SHORT_CLICKED, not CLICKED: CLICKED also comes after a long press, and
     * a held PLAY or MODE must not act twice. */
    lv_obj_add_event_cb(o, ev_cb, LV_EVENT_SHORT_CLICKED, (void *)(uintptr_t)id);
    lv_obj_add_event_cb(o, ev_cb, LV_EVENT_LONG_PRESSED, (void *)(uintptr_t)id);
    T.btn[id] = o;
    return o;
}

// ── BPM and POS: values the encoder turns ──────────────────────────────────
//
// Standard LVGL editing: focus one, click to take the encoder into it, turn,
// click to let go (a long press lets go too). A tap does the same for a finger.
// BPM is the instrument's one tempo (crosspad::transportSetTempo): it is heard
// while it turns, and the open song takes it when the turn ends. POS moves
// where PLAY starts, a bar a detent, while stopped -- Arrange's playhead and
// grid follow it.

void paint_value(uint8_t id);
void paint_readouts();

bool begin_edit(uint8_t id)
{
    if (id == B_BPM) {
        if (crosspad::transportTempoFromHost()) {
            ui_toast_show("Tempo follows the host's MIDI clock");
            return false;
        }
        const float bpm = crosspad::transportBpm();
        s_edit_bpm = bpm > 0.0f ? bpm : crosspad::transportTempo();
        if (s_edit_bpm <= 0.0f) return false;
        s_edit_bpm = std::round(s_edit_bpm);
    } else if (id == B_POS) {
        if (!T.en[B_POS]) return false;
    }
    T.editing[id] = true;
    return true;
}

void end_edit(uint8_t id)
{
    if (!T.editing[id]) return;
    T.editing[id] = false;
    if (id == B_BPM) {
        crosspad::transportCommitTempo();
        s_held_bpm = s_edit_bpm;
        s_held_since = lv_tick_get();
    }
}

void turn(uint8_t id, int d)
{
    if (id == B_BPM) {
        float v = s_edit_bpm + (float)d;
        if (v < crosspad::kTransportTempoMin) v = crosspad::kTransportTempoMin;
        if (v > crosspad::kTransportTempoMax) v = crosspad::kTransportTempoMax;
        s_edit_bpm = v;
        crosspad::transportSetTempo(v);
    } else if (id == B_POS) {
        crosspad::transportLocate(d);
    }
}

void value_cb(lv_event_t * e)
{
    const uint8_t id = (uint8_t)(uintptr_t)lv_event_get_user_data(e);
    lv_obj_t * obj = (lv_obj_t *)lv_event_get_target(e);
    lv_group_t * g = lv_obj_get_group(obj);
    switch (lv_event_get_code(e)) {
        case LV_EVENT_FOCUSED: {
            /* Sent on focus and on every change of edit mode. */
            const bool ed = g && lv_group_get_editing(g);
            if (ed && !T.editing[id] && !begin_edit(id)) lv_group_set_editing(g, false);
            else if (!ed && T.editing[id]) end_edit(id);
            break;
        }
        case LV_EVENT_DEFOCUSED:
            end_edit(id);
            break;
        case LV_EVENT_KEY: {
            if (!T.editing[id]) return;
            const uint32_t k = lv_event_get_key(e);
            const int d = (k == LV_KEY_RIGHT || k == LV_KEY_UP) ? 1
                        : (k == LV_KEY_LEFT || k == LV_KEY_DOWN) ? -1 : 0;
            if (d) turn(id, d);
            break;
        }
        case LV_EVENT_CLICKED:
            /* The encoder's click into the value is LVGL's own and sends no
             * click; this is its click out of it, or a finger's tap. */
            if (!g) return;
            if (lv_group_get_editing(g) && lv_group_get_focused(g) == obj) {
                lv_group_set_editing(g, false);
            } else if (T.en[id]) {
                lv_group_focus_obj(obj);
                lv_group_set_editing(g, true);
            }
            break;
        default:
            return;
    }
    paint_value(id);
    paint_readouts();
}

lv_obj_t * value_tile(uint8_t id, int32_t x, int32_t w)
{
    lv_obj_t * o = lv_CreateEditableButton(T.bar);
    lv_obj_remove_style_all(o);
    lv_obj_set_pos(o, x, TILE_Y);
    lv_obj_set_size(o, w, TILE_H);
    lv_obj_set_style_radius(o, TILE_R, 0);
    fill(o, INK, kWash);
    lv_obj_set_style_bg_opa(o, kPressed, LV_STATE_PRESSED);
    lv_obj_set_style_outline_color(o, INK, LV_STATE_FOCUS_KEY);
    lv_obj_set_style_outline_width(o, 2, LV_STATE_FOCUS_KEY);
    lv_obj_set_style_outline_pad(o, 0, LV_STATE_FOCUS_KEY);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
    lv_obj_add_event_cb(o, value_cb, LV_EVENT_FOCUSED, (void *)(uintptr_t)id);
    lv_obj_add_event_cb(o, value_cb, LV_EVENT_DEFOCUSED, (void *)(uintptr_t)id);
    lv_obj_add_event_cb(o, value_cb, LV_EVENT_KEY, (void *)(uintptr_t)id);
    lv_obj_add_event_cb(o, value_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)id);
    T.btn[id] = o;
    return o;
}

/// A value being turned is an ink tile with its text in the bar colour, like
/// a key that is on; otherwise the wash of an idle key.
void paint_value(uint8_t id)
{
    lv_obj_t * o = T.btn[id];
    if (!o) return;
    const bool ed = T.editing[id];
    const lv_color_t fg = ed ? bar_color() : INK;
    lv_obj_set_style_bg_opa(o, ed ? (lv_opa_t)LV_OPA_COVER : kWash, 0);
    if (id == B_BPM) {
        lv_obj_set_style_text_color(T.bpm_v, fg, 0);
    } else {
        lv_obj_set_style_text_color(T.pos_v, fg, 0);
        lv_obj_set_style_text_color(T.pos_l, fg, 0);
    }
    /* Disabled only for the encoder's navigation: the readout stays legible.
     * BPM alone dims, with no tempo to show. */
    if (T.en[id]) lv_obj_remove_state(o, LV_STATE_DISABLED);
    else          lv_obj_add_state(o, LV_STATE_DISABLED);
    if (id == B_BPM) lv_obj_set_style_opa(o, T.en[id] ? LV_OPA_COVER : LV_OPA_40, 0);
}

/// Active = an ink tile with the glyph in the bar colour; idle = the wash with
/// an ink glyph. Disabled dims the key and takes it out of the rotation.
lv_color_t paint_key(uint8_t id, bool active)
{
    lv_obj_t * b = T.btn[id];
    lv_obj_set_style_bg_opa(b, active ? (lv_opa_t)LV_OPA_COVER : kWash, 0);
    lv_obj_set_style_opa(b, T.en[id] ? LV_OPA_COVER : LV_OPA_40, 0);
    /* LVGL's navigation skips LV_STATE_DISABLED, not low opacity: the state
     * carries the navigation, the opacity the look. */
    if (T.en[id]) lv_obj_remove_state(b, LV_STATE_DISABLED);
    else          lv_obj_add_state(b, LV_STATE_DISABLED);
    return active ? bar_color() : INK;
}

void paint_metro()
{
    /* On: ink tile, glyph in the bar colour. A beat flashes the whole tile in
     * the bar colour, glyph ink. */
    const bool on = T.click_on;
    const bool lit = on && s_flash_lit;
    lv_color_t fg = paint_key(B_METRO, on);
    if (lit) {
        fill(T.btn[B_METRO], bar_color(), LV_OPA_COVER);
        fg = INK;
    } else {
        lv_obj_set_style_bg_color(T.btn[B_METRO], INK, 0);
    }
    lv_obj_set_style_line_color(T.mt_case, fg, 0);
    lv_obj_set_style_line_color(T.mt_arm, fg, 0);
    lv_obj_set_style_bg_color(T.mt_bob, fg, 0);
}

void repaint()
{
    const lv_color_t bc = bar_color();
    lv_obj_set_style_bg_color(T.bar, bc, 0);

    /* MODE: four outline squares, in the bar colour while the map is up */
    const lv_color_t mfg = paint_key(B_MODE, mode_overlay_is_open());
    for (lv_obj_t * s : T.mode_sq) lv_obj_set_style_border_color(s, mfg, 0);

    /* REC: the dot, or DUB while overdubbing */
    const bool rec_on = (T.st == BarState::Rec && !T.dub_on) || T.st == BarState::Pre;
    const lv_color_t rfg = paint_key(B_REC, rec_on || T.dub_on);
    if (T.dub_on) {
        lv_obj_add_flag(T.rec_dot, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(T.rec_dub, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_text_color(T.rec_dub, rfg, 0);
    } else {
        lv_obj_remove_flag(T.rec_dot, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(T.rec_dub, LV_OBJ_FLAG_HIDDEN);
        fill(T.rec_dot, rec_on ? bc : C_RED, LV_OPA_COVER);
    }

    /* PLAY / STOP in one key: the triangle stopped, a square while it runs */
    const bool run = playing() || T.st == BarState::Pre;
    const lv_color_t pfg = paint_key(B_PLAY, run);
    if (run) {
        lv_obj_add_flag(T.play_tri, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(T.play_sq, LV_OBJ_FLAG_HIDDEN);
        fill(T.play_sq, pfg, LV_OPA_COVER);
    } else {
        lv_obj_remove_flag(T.play_tri, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(T.play_sq, LV_OBJ_FLAG_HIDDEN);
        uint32_t n = lv_obj_get_child_count(T.play_tri);
        for (uint32_t i = 0; i < n; i++) fill(lv_obj_get_child(T.play_tri, i), pfg, LV_OPA_COVER);
    }

    paint_metro();

    static const char * const kWord[] = {"STOP", "COUNT", "PLAY", "REC"};
    lv_label_set_text(T.pos_l, T.dub_on ? "DUB" : kWord[(uint8_t)T.st]);

    paint_value(B_BPM);
    paint_value(B_POS);
}

void paint_readouts()
{
    char buf[16];

    float bpm = crosspad::transportBpm();
    if (T.editing[B_BPM]) {
        bpm = s_edit_bpm;
    } else if (s_held_since) {
        if (std::fabs(bpm - s_held_bpm) < 0.05f || lv_tick_elaps(s_held_since) > kHeldBpmMs) s_held_since = 0;
        else bpm = s_held_bpm;
    }
    if (bpm > 0.0f) {
        const int t = (int)(bpm * 10.0f + 0.5f);
        snprintf(buf, sizeof(buf), "%d.%d", t / 10, t % 10);
    } else {
        /* No tempo of its own: a dash, not a stale number from the last
         * thing that had one. */
        snprintf(buf, sizeof(buf), "--");
    }
    set_fitted(T.bpm_v, buf, lv_obj_get_width(T.btn[B_BPM]) - 4);

    const crosspad::TransportPos p = crosspad::transportPosition();
    snprintf(buf, sizeof(buf), "%u.%u", (unsigned)p.bar, (unsigned)p.beat);
    set_fitted(T.pos_v, buf, lv_obj_get_width(T.btn[B_POS]) - 4);

    /* Sounding / the kit's ceiling. Dim with nothing sounding, and inverted
     * -- an ink chip, the number in the bar colour -- at the ceiling, because
     * the next hit steals a voice and the player should know before it is
     * heard. "--" with no kit in an engine. */
    const VoiceMeter vm = getGuiPlatform().getVoiceMeter();
    const bool full = vm.limit != 0 && vm.active >= vm.limit;
    if (vm.limit == 0) snprintf(buf, sizeof(buf), "--");
    else               snprintf(buf, sizeof(buf), "%u/%u", (unsigned)vm.active, (unsigned)vm.limit);
    if (strcmp(lv_label_get_text(T.vox), buf) != 0) lv_label_set_text(T.vox, buf);
    lv_obj_set_style_text_opa(T.vox, (vm.limit != 0 && vm.active == 0) ? LV_OPA_50 : LV_OPA_COVER, 0);
    lv_obj_set_style_bg_opa(T.vox, full ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
    lv_obj_set_style_text_color(T.vox, full ? bar_color() : INK, 0);

    /* The render's load over the voices: what a pass averages of its block.
     * An ink chip, amber from 75 % -- at 85 % the engines stop adding voices
     * -- and red from 90 %, or for a second after a pass ran over its block:
     * the DMA was late and a click may have been heard. */
    const DspLoad dl = getGuiPlatform().getDspLoad();
    if (dl.measured) {
        s_cpu_pct = (s_cpu_pct * 3 + dl.avgPct + 2) / 4;
        if (s_cpu_overruns != UINT32_MAX && dl.overruns != s_cpu_overruns) s_cpu_late_at = lv_tick_get() | 1;
        s_cpu_overruns = dl.overruns;
        const bool late = s_cpu_late_at && lv_tick_elaps(s_cpu_late_at) < kCpuLateMs;
        snprintf(buf, sizeof(buf), "CPU %u%%", (unsigned)s_cpu_pct);
        if (strcmp(lv_label_get_text(T.cpu), buf) != 0) lv_label_set_text(T.cpu, buf);
        const bool red = late || s_cpu_pct >= 90;
        const bool amber = !red && s_cpu_pct >= 75;
        lv_obj_set_style_bg_opa(T.cpu, red || amber ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
        lv_obj_set_style_text_color(T.cpu, red ? C_RED : amber ? C_AMB : INK, 0);
    }

    /* The battery: up to 9 px of fill inside a 13 x 8 outline, red when low. */
    uint8_t pct = platformStatus().batteryPct;
    if (pct > 100) pct = 100;
    int32_t w = (9 * (int32_t)pct) / 100;
    if (w < 1) w = 1;
    if (lv_obj_get_width(T.bat_fill) != w) lv_obj_set_width(T.bat_fill, w);
    lv_obj_set_style_bg_color(T.bat_fill, pct < 15 ? C_RED : INK, 0);
}

BarState bar_state_from(crosspad::TransportState s)
{
    switch (s) {
        case crosspad::TransportState::Stopped:     return BarState::Stop;
        case crosspad::TransportState::Waiting:     return BarState::Pre;
        case crosspad::TransportState::Playing:     return BarState::Play;
        case crosspad::TransportState::Recording:
        case crosspad::TransportState::Overdubbing: return BarState::Rec;
    }
    return BarState::Stop;
}

/// True while @p obj is one of the bar's keys.
bool is_ours(const lv_obj_t * obj)
{
    if (!obj) return false;
    for (uint8_t i = 0; i < B_COUNT; i++) if (T.btn[i] == obj) return true;
    return obj == T.bat;
}

/// The screen's own last control the encoder can land on, as LVGL's
/// navigation judges it: not disabled, nothing above it hidden.
lv_obj_t * last_not_ours(lv_group_t * g)
{
    lv_obj_t * last = nullptr;
    const uint32_t n = lv_group_get_obj_count(g);
    for (uint32_t i = 0; i < n; i++) {
        lv_obj_t * o = lv_group_get_obj_by_index(g, i);
        if (!o || is_ours(o) || o == statusbar_get_arrow()) continue;
        if (lv_obj_get_state(o) & LV_STATE_DISABLED) continue;
        bool hidden = false;
        for (lv_obj_t * p = o; p && !hidden; p = lv_obj_get_parent(p)) {
            hidden = lv_obj_has_flag(p, LV_OBJ_FLAG_HIDDEN);
        }
        if (!hidden) last = o;
    }
    return last;
}

lv_group_t * nav_group()
{
    lv_indev_t * nav = getGuiPlatform().getNavigationEncoder();
    return nav ? lv_indev_get_group(nav) : nullptr;
}

/// Put the keys in the group the encoder is on, appended, so every screen
/// keeps its own navigation order and gains the transport at its end. With
/// the status icons on screen the status bar's arrow takes their place: the
/// one thing on that page the encoder can do is come back.
void join_current_group()
{
    lv_group_t * g = nav_group();
    if (!g) return;
    if (T.status_page) {
        lv_obj_t * a = statusbar_get_arrow();
        if (!a || lv_obj_get_group(a) == g) return;
        if (lv_group_get_obj_count(g) == 0) return;   // as for the keys, below
        lv_group_add_obj(g, a);
        return;
    }
    /* Not only "a different group": an app that calls
     * lv_group_remove_all_objs() on its own group drops our keys without the
     * pointer ever changing. Arrange does exactly that every time it refills
     * the track list. So the keys themselves are the test. */
    lv_obj_t * probe = T.btn[B_BPM];
    if (g == s_joined && probe && lv_obj_get_group(probe) == g) return;
    /* Wait until the screen has put something of its own in: joining an
     * empty group makes the first key the only member, LVGL focuses it, and
     * an app would open with its focus on the transport instead of its own
     * first control. */
    if (lv_group_get_obj_count(g) == 0) return;

    for (uint8_t i = 0; i < B_COUNT; i++) {
        if (T.btn[i]) lv_group_add_obj(g, T.btn[i]);
    }
    if (T.bat) lv_group_add_obj(g, T.bat);
    s_joined = g;
    s_focus_return = nullptr;
}

void leave_group(lv_obj_t * o)
{
    if (o && lv_obj_get_group(o)) lv_group_remove_obj(o);
}

/// Make the status icons (or the transport) the page the encoder works on:
/// who is in its rotation, and where its focus goes. Moving the row there is
/// the caller's -- a finger's drag has already done it.
///
/// The battery's click lands on the arrow and the arrow's on the battery, so
/// two clicks of the encoder go there and back. Any other key the encoder sat
/// on hands it back to the screen, as the power-button jump does. The new
/// member is focused before the old ones leave, or LVGL would focus whatever
/// came next in the screen's own rotation in between.
void set_page(bool status)
{
    if (status == T.status_page) return;
    lv_group_t * g = nav_group();
    lv_obj_t * focused = g ? lv_group_get_focused(g) : nullptr;
    lv_obj_t * a = statusbar_get_arrow();

    if (status) {
        const bool from_bat = T.bat && focused == T.bat;
        if (!from_bat && is_ours(focused)) transportbar_toggle_focus();
        T.status_page = true;
        join_current_group();
        if (from_bat && a && g && lv_obj_get_group(a) == g) lv_group_focus_obj(a);
        for (uint8_t i = 0; i < B_COUNT; i++) leave_group(T.btn[i]);
        leave_group(T.bat);
        s_joined = nullptr;
        return;
    }

    const bool from_arrow = a && focused == a;
    T.status_page = false;
    join_current_group();
    if (from_arrow && T.bat && g && lv_obj_get_group(T.bat) == g) lv_group_focus_obj(T.bat);
    leave_group(a);
    s_painted_valid = false;
    transportbar_refresh();
    paint_readouts();
}

void refresh_timer_cb(lv_timer_t *)
{
    join_current_group();
    if (T.status_page) return;   // nothing of ours on screen to repaint
    transportbar_refresh();
    paint_readouts();
}

/// The metronome's flash: a beat seen on the transport's position lights the
/// key, the one longer; only while the click is on and something plays.
void beat_timer_cb(lv_timer_t *)
{
    if (!T.bar || T.status_page) return;
    bool lit = false;
    if (T.click_on && playing()) {
        const crosspad::TransportPos p = crosspad::transportPosition();
        if (p.beat != s_last_beat) {
            s_last_beat = p.beat;
            s_flash_until = lv_tick_get() + (p.beat == 1 ? kFlashDownMs : kFlashMs);
        }
        lit = s_flash_until && (int32_t)(s_flash_until - lv_tick_get()) > 0;
    } else {
        s_last_beat = 0;
        s_flash_until = 0;
    }
    if (lit != s_flash_lit) {
        s_flash_lit = lit;
        paint_metro();
    }
}

/// A tap on the voices and the battery, or the encoder's click on them: the
/// system status icons in place of the bar.
void battery_cb(lv_event_t *)
{
    transportbar_show(false);
}

/// The row is LVGL's own scroll, snapped a page at a time: a finger drags
/// either bar sideways and the other comes in behind it, a flick or a drag
/// past half way swaps them, anything less springs back. The page is read
/// where the scroll comes to rest, so a drag, the battery and the arrow all
/// end in the same state.
void row_scroll_cb(lv_event_t * e)
{
    if (lv_event_get_code(e) == LV_EVENT_SCROLL_BEGIN) {
        /* The transport's paint stops while the icons are up; bring it up
         * to date before it slides back into view. */
        if (T.status_page) {
            s_painted_valid = false;
            transportbar_refresh();
            paint_readouts();
        }
        return;
    }
    set_page(lv_obj_get_scroll_x(T.row) > lv_obj_get_width(T.row) / 2);
}

void bar_delete_cb(lv_event_t *)
{
    /* The bar dies with the screen on every rebuild. The keys go with it and
     * LVGL takes them out of the group it had them in, so there is nothing to
     * hand back -- only our own handles to drop. */
    s_joined = nullptr;
    s_focus_return = nullptr;
    s_held_since = 0;
    if (s_timer) { lv_timer_delete(s_timer); s_timer = nullptr; }
    if (s_beat_timer) { lv_timer_delete(s_beat_timer); s_beat_timer = nullptr; }
    T = {};
    s_flash_lit = false;
    s_painted_valid = false;
}

} // namespace

lv_obj_t * transportbar_create(lv_obj_t * parent, lv_obj_t * status_bar)
{
    if (!parent) return nullptr;
    if (T.bar) return T.bar;

    for (uint8_t i = 0; i < B_COUNT; i++) T.en[i] = true;
    T.st = BarState::Stop;
    T.status_page = false;
    T.status_bar = status_bar;

    /* One row, two pages: this bar, then the status bar moved in beside it,
     * in the place the status bar had in @p parent. The row is the scroll; a
     * press on either bar's background lands on it (neither bar is
     * clickable), and one on a key hands the drag up to it through LVGL's
     * scroll chain -- the key gets PRESS_LOST, not a click. It also vetoes
     * touch_gesture's sideways back/forward swipe, which it would otherwise
     * be: that veto is any horizontally scrollable ancestor with somewhere
     * to go. */
    T.row = lv_obj_create(parent);
    lv_obj_remove_style_all(T.row);
    lv_obj_set_size(T.row, 320, TB_H);
    lv_obj_set_flex_flow(T.row, LV_FLEX_FLOW_ROW);
    lv_obj_set_scroll_dir(T.row, LV_DIR_HOR);
    lv_obj_set_scroll_snap_x(T.row, LV_SCROLL_SNAP_START);
    lv_obj_add_flag(T.row, LV_OBJ_FLAG_SCROLL_ONE);
    lv_obj_set_scrollbar_mode(T.row, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_event_cb(T.row, row_scroll_cb, LV_EVENT_SCROLL_BEGIN, nullptr);
    lv_obj_add_event_cb(T.row, row_scroll_cb, LV_EVENT_SCROLL_END, nullptr);

    T.bar = box(T.row, 0, 0, 320, TB_H, 0);
    lv_obj_set_style_bg_opa(T.bar, LV_OPA_COVER, 0);
    lv_obj_add_event_cb(T.bar, bar_delete_cb, LV_EVENT_DELETE, nullptr);

    /* BPM */
    T.bpm_v = stack(value_tile(B_BPM, 2, 48), "BPM", nullptr);

    /* POS: the state word over bar.beat */
    T.pos_v = stack(value_tile(B_POS, 52, 47), "STOP", &T.pos_l);

    /* MODE: 2x2 outline squares */
    lv_obj_t * m = tile(B_MODE, 101, 38);
    lv_obj_t * grid = box(m, 0, 0, 21, 21, 0);
    lv_obj_center(grid);
    for (int i = 0; i < 4; i++) {
        lv_obj_t * s = box(grid, (i % 2) * 12, (i / 2) * 12, 9, 9, 2);
        lv_obj_set_style_border_width(s, 2, 0);
        lv_obj_set_style_border_color(s, INK, 0);
        T.mode_sq[i] = s;
    }

    /* REC: a ringed dot, or DUB */
    lv_obj_t * r = tile(B_REC, 141, 38);
    T.rec_dot = box(r, 0, 0, 16, 16, LV_RADIUS_CIRCLE);
    lv_obj_center(T.rec_dot);
    lv_obj_set_style_border_width(T.rec_dot, 2, 0);
    lv_obj_set_style_border_color(T.rec_dot, INK, 0);
    T.rec_dub = label(r, F14, "DUB");
    lv_obj_center(T.rec_dub);

    /* PLAY / STOP: a 14 x 16 triangle of 1 px columns, or a 13 px square */
    lv_obj_t * p = tile(B_PLAY, 181, 38);
    T.play_tri = box(p, 0, 0, 14, 16, 0);
    lv_obj_align(T.play_tri, LV_ALIGN_CENTER, 2, 0);
    for (int x = 0; x < 14; x++) {
        int32_t h = 16 - (x * 16) / 14;
        if (h < 2) h = 2;
        box(T.play_tri, x, (16 - h) / 2, 1, h, 0);
    }
    T.play_sq = box(p, 0, 0, 13, 13, 2);
    lv_obj_center(T.play_sq);

    /* METRO: a trapezoid case, its swinging arm and the bob on it. The points
     * are static: lv_line keeps the pointer, not a copy. */
    lv_obj_t * mt = tile(B_METRO, 221, 38);
    lv_obj_t * mg = box(mt, 0, 0, 18, 20, 0);
    lv_obj_center(mg);
    static const lv_point_precise_t kCase[] = { {6, 1}, {1, 19}, {17, 19}, {12, 1}, {6, 1} };
    static const lv_point_precise_t kArm[]  = { {9, 15}, {14, 4} };
    T.mt_case = line(mg, kCase, 5, 2);
    T.mt_arm  = line(mg, kArm, 2, 2);
    T.mt_bob  = box(mg, 7, 13, 4, 4, LV_RADIUS_CIRCLE);
    lv_obj_set_style_bg_opa(T.mt_bob, LV_OPA_COVER, 0);

    /* VOICES + battery, one tile: the caption over the count and a 15 x 8
     * battery. A tap, or the encoder's click -- it is the last key in the
     * rotation -- swaps in the status bar. A tap must not take the encoder's
     * focus off the screen with it. */
    T.bat = box(T.bar, 261, TILE_Y, 57, TILE_H, TILE_R);
    fill(T.bat, INK, kWash);
    lv_obj_set_style_bg_opa(T.bat, kPressed, LV_STATE_PRESSED);
    lv_obj_set_style_outline_color(T.bat, INK, LV_STATE_FOCUS_KEY);
    lv_obj_set_style_outline_width(T.bat, 2, LV_STATE_FOCUS_KEY);
    lv_obj_set_style_outline_pad(T.bat, 0, LV_STATE_FOCUS_KEY);
    lv_obj_add_flag(T.bat, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(T.bat, LV_OBJ_FLAG_CLICK_FOCUSABLE);
    lv_obj_add_event_cb(T.bat, battery_cb, LV_EVENT_CLICKED, nullptr);
    lv_obj_t * col = box(T.bat, 0, 0, LV_PCT(100), LV_PCT(100), 0);
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(col, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    T.cpu = label(col, F10, "VOICES");
    lv_obj_set_style_bg_color(T.cpu, INK, 0);
    lv_obj_set_style_radius(T.cpu, 3, 0);
    lv_obj_set_style_pad_hor(T.cpu, 1, 0);
    lv_obj_t * vrow = box(col, 0, 0, LV_SIZE_CONTENT, LV_SIZE_CONTENT, 0);
    lv_obj_set_flex_flow(vrow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(vrow, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(vrow, 3, 0);
    T.vox = label(vrow, F14, "--");
    lv_obj_set_style_bg_color(T.vox, INK, 0);
    lv_obj_set_style_radius(T.vox, 3, 0);
    lv_obj_set_style_pad_hor(T.vox, 1, 0);
    lv_obj_t * bw = box(vrow, 0, 0, 15, 8, 0);
    lv_obj_t * bat = box(bw, 0, 0, 13, 8, 2);
    lv_obj_set_style_border_width(bat, 1, 0);
    lv_obj_set_style_border_color(bat, INK, 0);
    T.bat_fill = box(bat, 2, 2, 9, 4, 1);
    fill(T.bat_fill, INK, LV_OPA_COVER);
    fill(box(bw, 13, 2, 2, 4, 1), INK, LV_OPA_COVER);

    /* What the status bar's loader shows, as 2 px along the bottom edge: a
     * kit loading is something the player waits for. */
    T.loading = box(T.bar, 0, TB_H - 2, 0, 2, 0);
    fill(T.loading, INK, LV_OPA_COVER);
    lv_obj_add_flag(T.loading, LV_OBJ_FLAG_HIDDEN);
    if (lv_subject_t * subj = statusbar_get_loading_subject()) {
        lv_subject_add_observer_obj(subj, [](lv_observer_t * o, lv_subject_t * sj) {
            lv_obj_t * ln = lv_observer_get_target_obj(o);
            const int32_t v = lv_subject_get_int(sj);
            if (v < 0) { lv_obj_add_flag(ln, LV_OBJ_FLAG_HIDDEN); return; }
            lv_obj_set_width(ln, (320 * (v > 100 ? 100 : v)) / 100);
            lv_obj_remove_flag(ln, LV_OBJ_FLAG_HIDDEN);
        }, T.loading, nullptr);
    }

    if (status_bar) {
        lv_obj_move_to_index(T.row, lv_obj_get_index(status_bar));
        lv_obj_set_parent(status_bar, T.row);
        lv_obj_set_height(status_bar, TB_H);
        lv_obj_remove_flag(status_bar, LV_OBJ_FLAG_HIDDEN);
        statusbar_set_drawer_anchor(T.row);
    }

    s_painted_valid = false;
    transportbar_refresh();
    paint_readouts();
    if (!s_timer) s_timer = lv_timer_create(refresh_timer_cb, kRefreshMs, nullptr);
    if (!s_beat_timer) s_beat_timer = lv_timer_create(beat_timer_cb, kBeatPollMs, nullptr);
    return T.bar;
}

void transportbar_set_mode_handler(void (*handler)()) { s_mode_handler = handler; }

void transportbar_refresh()
{
    if (!T.bar) return;

    const crosspad::TransportState ts = crosspad::transportState();
    const uint8_t caps = crosspad::transportCaps();
    const bool mode_on = mode_overlay_is_open();
    const BarState st = bar_state_from(ts);
    const bool dub = (ts == crosspad::TransportState::Overdubbing);
    const crosspad::TransportPos pos = crosspad::transportPosition();
    const bool at_start = pos.bar <= 1 && pos.beat <= 1 && pos.cent == 0;
    /* Also switched from the Sequencer's M: read, not remembered. */
    const bool click = crosspad::transportClick();

    if (s_painted_valid && st == s_painted_st && caps == s_painted_caps &&
        dub == s_painted_dub && mode_on == s_painted_mode && at_start == s_painted_start &&
        click == s_painted_click) {
        return;
    }
    s_painted_st = st;
    s_painted_caps = caps;
    s_painted_dub = dub;
    s_painted_mode = mode_on;
    s_painted_start = at_start;
    s_painted_click = click;
    s_painted_valid = true;

    T.st = st;
    T.dub_on = dub;
    T.click_on = click;

    /* MODE, the metronome and BPM are the bar's own; the rest are the
     * holder's, and a key it cannot honour dims without moving. REC is a
     * record while stopped and an overdub while playing; PLAY is a stop while
     * anything runs. */
    const bool run = st != BarState::Stop;
    T.en[B_MODE]  = true;
    T.en[B_METRO] = true;
    T.en[B_BPM]   = crosspad::transportBpm() > 0.0f;
    T.en[B_POS]   = (caps & crosspad::kTransportCanLocate) != 0 && st == BarState::Stop;
    T.en[B_PLAY]  = run ? (caps & crosspad::kTransportCanStop) != 0
                        : (caps & crosspad::kTransportCanPlay) != 0;
    T.en[B_REC]   = (dub || st == BarState::Play) ? (caps & crosspad::kTransportCanOverdub) != 0
                                                  : (caps & crosspad::kTransportCanRecord) != 0;

    /* The position is turned only while stopped: PLAY from a finger or
     * another screen ends the turn. */
    if (T.editing[B_POS] && !T.en[B_POS]) {
        if (lv_group_t * g = lv_obj_get_group(T.btn[B_POS])) lv_group_set_editing(g, false);
        end_edit(B_POS);
    }
    repaint();
}

bool transportbar_shown()
{
    return T.bar && !T.status_page;
}

void transportbar_show(bool show)
{
    if (!T.row || !T.status_bar) return;
    /* The page first: it may focus the arrow, a button LVGL scrolls into
     * view on focus, and the scroll below must be the last word. Also when
     * the page is already the one asked for -- a drag let go half way can
     * leave the row between the two. */
    set_page(!show);
    lv_obj_scroll_to_x(T.row, show ? 0 : lv_obj_get_width(T.row), LV_ANIM_ON);
}

uint8_t transportbar_probe(TransportBarReadout * out, uint8_t max)
{
    if (!out || !T.bar) return 0;
    struct { const char * id; lv_obj_t * o; } rows[] = {
        { "bpm",   T.bpm_v },
        { "pos",   T.pos_v },
        { "vox",   T.vox   },
    };
    uint8_t n = 0;
    for (auto & r : rows) {
        if (n >= max) break;
        if (!r.o) continue;
        /* The readouts sit in flex columns inside their tiles: the screen's
         * coordinates, not the parent's. */
        lv_area_t a;
        lv_obj_get_coords(r.o, &a);
        out[n].id   = r.id;
        out[n].x    = a.x1;
        out[n].y    = a.y1;
        out[n].w    = lv_area_get_width(&a);
        out[n].h    = lv_area_get_height(&a);
        out[n].text = lv_label_get_text(r.o);
        n++;
    }
    return n;
}

int32_t transportbar_row_scroll()
{
    return T.row ? lv_obj_get_scroll_x(T.row) : -1;
}

bool transportbar_has_focus()
{
    lv_group_t * g = nav_group();
    return g && is_ours(lv_group_get_focused(g));
}

bool transportbar_toggle_focus()
{
    if (!T.bar || !transportbar_shown()) return false;
    join_current_group();

    lv_group_t * g = nav_group();
    if (!g || g != s_joined) return false;

    lv_obj_t * focused = lv_group_get_focused(g);
    if (is_ours(focused)) {
        /* Back where the jump came from. The remembered object can have died
         * with a screen in between, so it is checked against this group
         * rather than trusted. A value being turned lets go first. */
        lv_group_set_editing(g, false);
        /* With no jump to go back to -- the encoder was turned past the
         * screen's last control onto the keys -- that control: the keys sit
         * at the group's end, so "next" from one of them was the next key,
         * one per click. */
        lv_obj_t * back = (s_focus_return && lv_obj_get_group(s_focus_return) == g)
                              ? s_focus_return : last_not_ours(g);
        if (back) lv_group_focus_obj(back);
        s_focus_return = nullptr;
        return back != nullptr;
    }

    /* Jump to PLAY -- the one key that starts and stops -- or MODE when the
     * holder cannot honour it. A disabled key is skipped by LVGL's
     * navigation and must not be the jump's destination either. */
    lv_obj_t * target = T.btn[B_PLAY];
    if (target && (lv_obj_get_state(target) & LV_STATE_DISABLED)) target = T.btn[B_MODE];
    if (!target) return false;
    lv_group_set_editing(g, false);
    s_focus_return = focused;
    lv_group_focus_obj(target);
    return true;
}

} // namespace crosspad_gui

#endif // USE_LVGL
