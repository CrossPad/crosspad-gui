// SPDX-License-Identifier: GPL-3.0-or-later
#ifdef USE_LVGL

#include "crosspad-gui/components/transport_bar.h"

#include "crosspad-gui/components/mode_overlay.h"
#include "crosspad-gui/design/cp_design.h"
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

/* ── Geometry and palette, 1:1 with the V5 export ──────────────────────── */

constexpr int32_t TB_H   = TRANSPORT_BAR_HEIGHT;   // 30
constexpr int32_t BTN_Y  = 3;
constexpr int32_t BTN_H  = 24;

#define INK     lv_color_hex(0x06120c)
#define C_MINT  lv_color_hex(0x00ff88)
#define C_AMB   lv_color_hex(0xffc857)
#define C_RED   lv_color_hex(0xff5c5c)

/* The correction narrows the substitutions to two. F_DUB was montserrat 10
 * all along and this firmware builds it, so DUB, the BPM value and the
 * position are 1:1. F_VAL now dresses only the PLAY glyph, which has to be a
 * montserrat (the pixel faces are ASCII and carry no LV_SYMBOL_*) -- 14 is
 * what we build either side of the 12 it asks for, and the key is 44x24 now,
 * so the larger triangle fits and reads better. F_LBL dresses only the voice
 * counter; there is no montserrat 8 here, and the design's own pixel face is
 * exactly the 8 px slot it was drawn in. */
#define F_LBL  CP_FONT_PIXEL
#define F_VAL  (&lv_font_montserrat_14)
#define F_DUB  (&lv_font_montserrat_10)

/* In the encoder's order: the five keys, the metronome, then the two values
 * it can turn. */
enum Btn : uint8_t { B_MODE = 0, B_REC, B_DUB, B_PLAY, B_STOP, B_CLICK, B_BPM, B_POS, B_COUNT };

/// The four the bar paints from, which is fewer than the transport's five:
/// an overdub is a take like a record, and the bar says so in red.
enum class BarState : uint8_t { Stop = 0, Pre, Play, Rec };

constexpr uint32_t kRefreshMs = 120;

struct {
    lv_obj_t * row = nullptr;         ///< the bar and the status bar side by side, a page each
    lv_obj_t * bar = nullptr;
    lv_obj_t * btn[B_COUNT] = {};
    lv_obj_t * ico[B_COUNT] = {};
    lv_obj_t * mode_sq[4] = {};
    lv_obj_t * click_rod = nullptr;   ///< the metronome's pendulum; T.ico[B_CLICK] is its case
    lv_obj_t * bpm_v = nullptr;
    lv_obj_t * pos_v = nullptr;
    lv_obj_t * vox = nullptr;
    lv_obj_t * bat_fill = nullptr;
    lv_obj_t * bat = nullptr;         ///< the battery's key: the encoder's way to the status icons
    lv_obj_t * loading = nullptr;
    lv_obj_t * status_bar = nullptr;
    BarState   st = BarState::Stop;
    bool       status_page = false;   ///< the status icons are the page on screen
    bool       en[B_COUNT] = {};
    bool       dub_on = false;
    bool       editing[B_COUNT] = {};   ///< BPM / POS: the encoder turns the value
} T;

void (*s_mode_handler)() = nullptr;
lv_timer_t * s_timer = nullptr;
/* The keys live in whatever group the navigation encoder currently points at,
 * so the transport is one step past the end of every screen: turning off an
 * app's last field reaches MODE, and on through the values to its first one.
 * LVGL does the rest for free: its group navigation skips hidden objects and
 * objects in LV_STATE_DISABLED, so a hidden bar drops out of the rotation and
 * so does a key the claim holder cannot honour. The power button's hold is a
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

/* What the last paint was for: the row repaints ten objects, and doing that
 * every 120 ms would also restyle the key the encoder has focused. */
BarState s_painted_st = BarState::Stop;
uint8_t  s_painted_caps = 0;
bool     s_painted_dub = false;
bool     s_painted_mode = false;
bool     s_painted_start = false;
bool     s_painted_click = false;
bool     s_painted_valid = false;

lv_color_t bar_color()
{
    return T.st == BarState::Rec ? C_RED
         : T.st == BarState::Pre ? C_AMB
                                 : C_MINT;
}

lv_obj_t * box(lv_obj_t * p, int32_t x, int32_t y, int32_t w, int32_t h, int32_t r)
{
    lv_obj_t * o = lv_obj_create(p);
    lv_obj_remove_style_all(o);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_radius(o, r, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE);
    return o;
}

lv_obj_t * label(lv_obj_t * p, const lv_font_t * f, int32_t x, int32_t y, const char * t)
{
    lv_obj_t * l = lv_label_create(p);
    lv_obj_set_style_text_font(l, f, 0);
    lv_obj_set_style_text_color(l, INK, 0);
    lv_label_set_text(l, t);
    lv_obj_set_pos(l, x, y);
    return l;
}

void press(uint8_t b)
{
    if (b >= B_COUNT || !T.en[b]) return;
    switch (b) {
        case B_MODE: if (s_mode_handler) s_mode_handler(); return;  // the overlay repaints us
        case B_REC:  crosspad::transportRecord();  break;
        case B_DUB:  crosspad::transportOverdub(); break;
        case B_PLAY: crosspad::transportPlay();    break;
        case B_STOP: crosspad::transportStop();    break;
        case B_CLICK: crosspad::transportSetClick(!crosspad::transportClick()); break;
        default:     return;   // BPM and POS are turned, see value_cb
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
        /* A long press on MODE is this design's way to the status bar: the
         * row is full to the last pixel, so there is no arrow to put there,
         * and wifi, Bluetooth, SD and the loader have no other home. */
        if (b == B_MODE) transportbar_show(false);
        return;
    }
    if (code == LV_EVENT_CLICKED) press(b);
}

lv_obj_t * button(uint8_t id, int32_t x, int32_t w)
{
    lv_obj_t * o = box(T.bar, x, BTN_Y, w, BTN_H, 4);
    lv_obj_add_flag(o, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(o, ev_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)id);
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

lv_obj_t * value_key(uint8_t id, int32_t x, int32_t w)
{
    lv_obj_t * o = lv_CreateEditableButton(T.bar);
    lv_obj_remove_style_all(o);
    lv_obj_set_pos(o, x, BTN_Y);
    lv_obj_set_size(o, w, BTN_H);
    lv_obj_set_style_radius(o, 4, 0);
    lv_obj_set_style_bg_color(o, INK, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_TRANSP, 0);
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

/// A value being turned is an ink pill with the number in the bar colour,
/// like a key that is on; otherwise it has no fill of its own.
void paint_value(uint8_t id)
{
    lv_obj_t * o = T.btn[id];
    lv_obj_t * l = id == B_BPM ? T.bpm_v : T.pos_v;
    if (!o || !l) return;
    const bool ed = T.editing[id];
    lv_obj_set_style_bg_opa(o, ed ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
    lv_obj_set_style_text_color(l, ed ? bar_color() : INK, 0);
    /* Disabled only for the encoder's navigation: the readout stays legible.
     * BPM alone dims, with no tempo to show. */
    if (T.en[id]) lv_obj_remove_state(o, LV_STATE_DISABLED);
    else          lv_obj_add_state(o, LV_STATE_DISABLED);
    if (id == B_BPM) lv_obj_set_style_opa(o, T.en[id] ? LV_OPA_COVER : LV_OPA_40, 0);
}

/// Active = ink tile with the glyph in the bar colour; inactive = a 36/255
/// wash with an ink glyph. Disabled dims the whole key and leaves it in place.
void paint_btn(uint8_t id, bool active)
{
    lv_obj_t * b = T.btn[id];
    if (!b) return;
    const lv_color_t fg = active ? bar_color() : INK;
    lv_obj_set_style_bg_color(b, active ? INK : lv_color_black(), 0);
    lv_obj_set_style_bg_opa(b, active ? LV_OPA_COVER : 36, 0);
    lv_obj_set_style_opa(b, T.en[id] ? LV_OPA_COVER : LV_OPA_40, 0);
    /* The export dims an unavailable key and leaves it at that, which was
     * enough when a key was only ever touched. In the encoder's rotation the
     * dim is invisible to navigation -- LVGL skips LV_STATE_DISABLED, not
     * low opacity -- so focus stopped dead on STOP while the transport was
     * stopped. Measured on the board: a rotate off PLAY landed on STOP. The
     * state carries the navigation, the opacity carries the look, and the
     * keys were built with lv_obj_remove_style_all(), so no theme style
     * rides in on the state. */
    if (T.en[id]) lv_obj_remove_state(b, LV_STATE_DISABLED);
    else          lv_obj_add_state(b, LV_STATE_DISABLED);
    /* Encoder focus is a ring in the bar colour: the fill already carries
     * on/off, so focus cannot borrow it. */
    lv_obj_set_style_outline_color(b, INK, LV_STATE_FOCUS_KEY);
    lv_obj_set_style_outline_width(b, 2, LV_STATE_FOCUS_KEY);
    lv_obj_set_style_outline_pad(b, 0, LV_STATE_FOCUS_KEY);

    if (id == B_MODE) {
        for (int i = 0; i < 4; i++) {
            if (T.mode_sq[i]) lv_obj_set_style_bg_color(T.mode_sq[i], fg, 0);
        }
        return;
    }
    if (!T.ico[id]) return;
    if (id == B_REC) {
        /* A red dot with an ink ring; on a red bar the dot itself goes ink. */
        lv_obj_set_style_border_color(T.ico[id], fg, 0);
        lv_obj_set_style_bg_color(T.ico[id], (T.st == BarState::Rec && !active) ? INK : C_RED, 0);
    }
    else if (id == B_DUB || id == B_PLAY) lv_obj_set_style_text_color(T.ico[id], fg, 0);
    else if (id == B_CLICK) {
        lv_obj_set_style_line_color(T.ico[id], fg, 0);
        if (T.click_rod) lv_obj_set_style_line_color(T.click_rod, fg, 0);
    }
    else                                  lv_obj_set_style_bg_color(T.ico[id], fg, 0);
}

/// A drawn icon: no font here has a metronome.
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

void repaint()
{
    lv_obj_set_style_bg_color(T.bar, bar_color(), 0);
    paint_btn(B_MODE, mode_overlay_is_open());
    paint_btn(B_REC,  (T.st == BarState::Rec && !T.dub_on) || T.st == BarState::Pre);
    paint_btn(B_DUB,  T.dub_on);
    paint_btn(B_PLAY, T.st == BarState::Play || T.st == BarState::Rec);
    paint_btn(B_STOP, false);
    paint_btn(B_CLICK, crosspad::transportClick());
    /* BPM and the position are not painted like keys: the correction leaves
     * their zone transparent and says the state in the bar colour, so there
     * is no state word to crowd the counter and no fill to wash. */
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
        /* The design writes "92.0", and 24 px of montserrat 10 holds exactly
         * that. Measured on the board, a three-digit tempo with its tenth is
         * 26 px and spills over the hairline -- and 120 is the default, so it
         * would be the first thing seen. The tenth is kept wherever it fits
         * and dropped where it does not, rather than clipping a digit or
         * moving the design's hairline. */
        const int t = (int)(bpm * 10.0f + 0.5f);
        if (bpm < 100.0f) snprintf(buf, sizeof(buf), "%d.%d", t / 10, t % 10);
        else              snprintf(buf, sizeof(buf), "%d", (t + 5) / 10);
    } else {
        /* No tempo of its own: a dash, not a stale number from the last
         * thing that had one. */
        snprintf(buf, sizeof(buf), "--");
    }
    if (strcmp(lv_label_get_text(T.bpm_v), buf) != 0) lv_label_set_text(T.bpm_v, buf);

    /* bar.beat, the way the correction writes it -- no zero padding and no
     * sub-beat field. TransportPos still carries cent because position()
     * falls out of one float and Arrange counts finer than a beat. */
    const crosspad::TransportPos p = crosspad::transportPosition();
    snprintf(buf, sizeof(buf), "%u.%u", (unsigned)p.bar, (unsigned)p.beat);
    if (strcmp(lv_label_get_text(T.pos_v), buf) != 0) lv_label_set_text(T.pos_v, buf);

    /* Sounding / the kit's ceiling. Dim with nothing sounding, and inverted
     * -- an ink pill, the number in the bar colour -- at the ceiling, because
     * the next hit steals a voice and the player should know before it is
     * heard. "--" with no kit in an engine. */
    const VoiceMeter vm = getGuiPlatform().getVoiceMeter();
    const bool full = vm.limit != 0 && vm.active >= vm.limit;
    if (vm.limit == 0) snprintf(buf, sizeof(buf), "--");
    else               snprintf(buf, sizeof(buf), "%u/%u",
                                (unsigned)vm.active, (unsigned)vm.limit);
    if (strcmp(lv_label_get_text(T.vox), buf) != 0) lv_label_set_text(T.vox, buf);
    lv_obj_set_style_text_opa(T.vox, (vm.limit != 0 && vm.active == 0) ? LV_OPA_50 : LV_OPA_COVER, 0);
    lv_obj_set_style_bg_opa(T.vox, full ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
    lv_obj_set_style_text_color(T.vox, full ? bar_color() : INK, 0);

    uint8_t pct = platformStatus().batteryPct;
    if (pct > 100) pct = 100;
    int32_t w = (14 * (int32_t)pct) / 100;
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
    lv_obj_t * probe = T.btn[B_MODE];
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

/// A tap on the battery, or the encoder's click on it: the system status
/// icons in place of the bar.
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
    T = {};
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
    lv_obj_add_event_cb(T.bar, bar_delete_cb, LV_EVENT_DELETE, nullptr);

    /* The five keys are 38 wide, not the export's 44: the metronome needed
     * the room and the row had none (BPM, the position and the voices are
     * already as narrow as their text). 42 apart, the glyphs centred in each. */
    constexpr int32_t KW = 38, KP = 42;

    /* MODE: 2x2 squares */
    lv_obj_t * m = button(B_MODE, 3, KW);
    static const int32_t sq[4][2] = { {11, 5}, {11, 13}, {19, 5}, {19, 13} };
    for (int i = 0; i < 4; i++) T.mode_sq[i] = box(m, sq[i][0], sq[i][1], 6, 6, 1);

    /* REC: a ring that fills red */
    lv_obj_t * r = button(B_REC, 3 + KP, KW);
    T.ico[B_REC] = box(r, 13, 6, 12, 12, LV_RADIUS_CIRCLE);
    lv_obj_set_style_bg_color(T.ico[B_REC], C_RED, 0);
    lv_obj_set_style_border_width(T.ico[B_REC], 2, 0);

    /* DUB */
    lv_obj_t * d = button(B_DUB, 3 + 2 * KP, KW);
    T.ico[B_DUB] = label(d, F_DUB, 0, 0, "DUB");
    lv_obj_center(T.ico[B_DUB]);

    /* PLAY */
    lv_obj_t * p = button(B_PLAY, 3 + 3 * KP, KW);
    T.ico[B_PLAY] = label(p, F_VAL, 0, 0, LV_SYMBOL_PLAY);
    lv_obj_center(T.ico[B_PLAY]);

    /* STOP: square */
    lv_obj_t * st = button(B_STOP, 3 + 4 * KP, KW);
    T.ico[B_STOP] = box(st, 13, 6, 12, 12, 1);

    /* CLICK: the metronome -- a case and its pendulum, lit while it clicks.
     * The points are static: lv_line keeps the pointer, not a copy. */
    lv_obj_t * ck = button(B_CLICK, 3 + 5 * KP, 26);
    static const lv_point_precise_t kCase[] = { {6, 0}, {11, 15}, {1, 15}, {6, 0} };
    static const lv_point_precise_t kRod[]  = { {6, 12}, {10, 2} };
    T.ico[B_CLICK] = line(ck, kCase, 4, 2);
    lv_obj_set_pos(T.ico[B_CLICK], 7, 4);
    T.click_rod = line(ck, kRod, 2, 1);
    lv_obj_set_pos(T.click_rod, 7, 4);

    /* BPM: the value alone, no label above it any more */
    lv_obj_t * bz = value_key(B_BPM, 242, 24);
    T.bpm_v = label(bz, F_DUB, 2, 7, "--");

    /* separator */
    lv_obj_t * sep = box(T.bar, 268, 8, 1, 14, 0);
    lv_obj_set_style_bg_color(sep, INK, 0);
    lv_obj_set_style_bg_opa(sep, 90, 0);

    /* Position, one line: the state is the bar colour now. y=11 rather than
     * the design's 10 for the same reason the counter below diverges -- see
     * there; at 6 px monospace a five-character count reaches back into this
     * label's last column, and one pixel of separation costs nothing. */
    /* Its key sits under the label rather than holding it: a key clips what
     * it holds, and "123.4" is wider than the zone the design gives it. */
    value_key(B_POS, 270, 22);
    T.pos_v = label(T.bar, F_DUB, 273, 11, "1.1");

    /* Voices above the battery. The design gives this 20 px centred, which
     * holds four characters of a proportional montserrat 8. The pixel face is
     * 6 px monospace, so "1/10" is 24 and wrapped onto a second line, and
     * "16/16" is 30 -- a width no font we have fits in 20 legibly. Content
     * width with the right edge flush to the battery's keeps the four-
     * character case within a pixel of where it was drawn and never wraps. */
    T.vox = label(T.bar, F_LBL, 294, 3, "--");
    lv_obj_set_style_bg_color(T.vox, INK, 0);
    lv_obj_set_style_radius(T.vox, 2, 0);
    lv_obj_set_style_pad_hor(T.vox, 1, 0);
    lv_obj_set_width(T.vox, LV_SIZE_CONTENT);
    lv_obj_set_style_text_align(T.vox, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_align(T.vox, LV_ALIGN_TOP_RIGHT, -5, 3);

    /* battery: outline, nub, fill */
    lv_obj_t * bat = box(T.bar, 295, 15, 18, 10, 2);
    lv_obj_set_style_bg_opa(bat, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_color(bat, INK, 0);
    lv_obj_set_style_border_width(bat, 1, 0);
    lv_obj_set_style_bg_color(box(T.bar, 313, 18, 2, 4, 0), INK, 0);
    T.bat_fill = box(T.bar, 297, 17, 12, 6, 0);
    lv_obj_set_style_bg_color(T.bat_fill, INK, 0);

    /* The battery and the counter above it are one tap target for the
     * status icons: 9 x 16 px alone is under any finger. It is also the last
     * key in the encoder's rotation, framed when focused (a border: an
     * outline would fall outside the bar and be clipped). A tap must not
     * take the encoder's focus off the screen with it. */
    T.bat = box(T.bar, 290, 0, 30, TB_H, 4);
    lv_obj_set_style_bg_opa(T.bat, LV_OPA_TRANSP, 0);
    lv_obj_add_flag(T.bat, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(T.bat, LV_OBJ_FLAG_CLICK_FOCUSABLE);
    lv_obj_set_style_border_color(T.bat, INK, LV_STATE_FOCUS_KEY);
    lv_obj_set_style_border_width(T.bat, 2, LV_STATE_FOCUS_KEY);
    lv_obj_add_event_cb(T.bat, battery_cb, LV_EVENT_CLICKED, nullptr);

    /* What the status bar's loader shows, as 2 px along the bottom edge: a
     * kit loading is something the player waits for. */
    T.loading = box(T.bar, 0, TB_H - 2, 0, 2, 0);
    lv_obj_set_style_bg_color(T.loading, INK, 0);
    lv_obj_add_flag(T.loading, LV_OBJ_FLAG_HIDDEN);
    if (lv_subject_t * subj = statusbar_get_loading_subject()) {
        lv_subject_add_observer_obj(subj, [](lv_observer_t * o, lv_subject_t * sj) {
            lv_obj_t * line = lv_observer_get_target_obj(o);
            const int32_t v = lv_subject_get_int(sj);
            if (v < 0) { lv_obj_add_flag(line, LV_OBJ_FLAG_HIDDEN); return; }
            lv_obj_set_width(line, (320 * (v > 100 ? 100 : v)) / 100);
            lv_obj_remove_flag(line, LV_OBJ_FLAG_HIDDEN);
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

    /* MODE and BPM are the bar's own; the four commands are the holder's, and
     * a key it cannot honour dims without moving. STOP while stopped takes
     * the position back to 1.1, so it is offered until it is there. */
    T.en[B_MODE] = true;
    T.en[B_CLICK] = true;
    T.en[B_BPM]  = crosspad::transportBpm() > 0.0f;
    T.en[B_POS]  = (caps & crosspad::kTransportCanLocate) != 0 && st == BarState::Stop;
    T.en[B_PLAY] = (caps & crosspad::kTransportCanPlay) != 0;
    T.en[B_STOP] = (caps & crosspad::kTransportCanStop) != 0 && (st != BarState::Stop || !at_start);
    T.en[B_REC]  = (caps & crosspad::kTransportCanRecord) != 0;
    T.en[B_DUB]  = (caps & crosspad::kTransportCanOverdub) != 0;

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
        out[n].id   = r.id;
        out[n].x    = lv_obj_get_x(r.o);
        out[n].y    = lv_obj_get_y(r.o);
        out[n].w    = lv_obj_get_width(r.o);
        out[n].h    = lv_obj_get_height(r.o);
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
    lv_indev_t * nav = getGuiPlatform().getNavigationEncoder();
    lv_group_t * g = nav ? lv_indev_get_group(nav) : nullptr;
    return g && is_ours(lv_group_get_focused(g));
}

bool transportbar_toggle_focus()
{
    if (!T.bar || !transportbar_shown()) return false;
    join_current_group();

    lv_indev_t * nav = getGuiPlatform().getNavigationEncoder();
    lv_group_t * g = nav ? lv_indev_get_group(nav) : nullptr;
    if (!g || g != s_joined) return false;

    lv_obj_t * focused = lv_group_get_focused(g);
    if (is_ours(focused)) {
        /* Back where the jump came from. The remembered object can have died
         * with a screen in between, so it is checked against this group
         * rather than trusted. A value being turned lets go first. */
        lv_group_set_editing(g, false);
        if (s_focus_return && lv_obj_get_group(s_focus_return) == g) {
            lv_group_focus_obj(s_focus_return);
        } else {
            lv_group_focus_next(g);   // anything that is not the transport
        }
        s_focus_return = nullptr;
        return true;
    }

    /* Jump to the key the state makes obvious, so one press does the expected
     * thing without rotating the whole screen first. A disabled key is
     * skipped by LVGL's navigation and must not be the jump's destination
     * either. */
    lv_obj_t * target = T.btn[T.st == BarState::Stop ? B_PLAY : B_STOP];
    if (target && (lv_obj_get_state(target) & LV_STATE_DISABLED)) target = T.btn[B_MODE];
    if (!target) return false;
    lv_group_set_editing(g, false);
    s_focus_return = focused;
    lv_group_focus_obj(target);
    return true;
}

} // namespace crosspad_gui

#endif // USE_LVGL
