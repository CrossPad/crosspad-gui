#ifdef USE_LVGL

#include "crosspad-gui/components/transport_bar.h"

#include "crosspad-gui/components/mode_overlay.h"
#include "crosspad-gui/design/cp_design.h"
#include "crosspad-gui/platform/IGuiPlatform.h"
#include "crosspad-gui/widgets/gui_helpers.h"

#include "crosspad/status/CrosspadStatus.hpp"
#include "crosspad/transport/Transport.hpp"

#include <cstdio>

extern crosspad::CrosspadStatus status;

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

enum Btn : uint8_t { B_MODE = 0, B_REC, B_DUB, B_PLAY, B_STOP, B_BPM, B_COUNT };

/// The four the bar paints from, which is fewer than the transport's five:
/// an overdub is a take like a record, and the bar says so in red.
enum class BarState : uint8_t { Stop = 0, Pre, Play, Rec };

constexpr uint32_t kRefreshMs = 120;

struct {
    lv_obj_t * bar = nullptr;
    lv_obj_t * btn[B_COUNT] = {};
    lv_obj_t * ico[B_COUNT] = {};
    lv_obj_t * mode_sq[4] = {};
    lv_obj_t * bpm_v = nullptr;
    lv_obj_t * pos_v = nullptr;
    lv_obj_t * vox = nullptr;
    lv_obj_t * bat_fill = nullptr;
    lv_obj_t * status_bar = nullptr;
    BarState   st = BarState::Stop;
    bool       en[B_COUNT] = {};
    bool       dub_on = false;
} T;

void (*s_mode_handler)() = nullptr;
lv_timer_t * s_timer = nullptr;
/* The keys live in whatever group the navigation encoder currently points at,
 * so the transport is one step past the end of every screen rather than
 * behind a gesture nobody can discover. LVGL does the rest for free: its
 * group navigation skips hidden objects and objects in LV_STATE_DISABLED, so
 * a hidden bar drops out of the rotation and so does a key the claim holder
 * cannot honour. */
lv_group_t * s_joined = nullptr;
/* Where focus was before the power-button shortcut jumped to the transport. */
lv_obj_t * s_focus_return = nullptr;

/* What the last paint was for: the row repaints ten objects, and doing that
 * every 120 ms would also restyle the key the encoder has focused. */
BarState s_painted_st = BarState::Stop;
uint8_t  s_painted_caps = 0;
bool     s_painted_dub = false;
bool     s_painted_mode = false;
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
        /* BPM is a readout here. The tempo belongs to whoever holds the
         * transport and is edited on that app's own screen; a tap-tempo on
         * this key would need an API no target offers yet. */
        case B_BPM:  return;
        default:     return;
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
    if (id == B_REC)                      lv_obj_set_style_border_color(T.ico[id], fg, 0);
    else if (id == B_DUB || id == B_PLAY) lv_obj_set_style_text_color(T.ico[id], fg, 0);
    else                                  lv_obj_set_style_bg_color(T.ico[id], fg, 0);
}

void repaint()
{
    lv_obj_set_style_bg_color(T.bar, bar_color(), 0);
    paint_btn(B_MODE, mode_overlay_is_open());
    paint_btn(B_REC,  T.st == BarState::Rec && !T.dub_on);
    paint_btn(B_DUB,  T.dub_on);
    paint_btn(B_PLAY, T.st == BarState::Play || T.st == BarState::Rec);
    paint_btn(B_STOP, false);
    /* BPM is not painted like a key: the correction leaves its zone
     * transparent and says the state in the bar colour, so there is no state
     * word to crowd the counter and no fill to wash. Only the dim for a
     * target with no tempo of its own. */
    if (T.btn[B_BPM]) {
        lv_obj_set_style_opa(T.btn[B_BPM], T.en[B_BPM] ? LV_OPA_COVER : LV_OPA_40, 0);
    }
}

void paint_readouts()
{
    char buf[16];

    const float bpm = crosspad::transportBpm();
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
    lv_label_set_text(T.bpm_v, buf);

    /* bar.beat, the way the correction writes it -- no zero padding and no
     * sub-beat field. TransportPos still carries cent because position()
     * falls out of one float and Arrange counts finer than a beat. */
    const crosspad::TransportPos p = crosspad::transportPosition();
    snprintf(buf, sizeof(buf), "%u.%u", (unsigned)p.bar, (unsigned)p.beat);
    lv_label_set_text(T.pos_v, buf);

    const VoiceMeter vm = getGuiPlatform().getVoiceMeter();
    if (vm.limit == 0) snprintf(buf, sizeof(buf), "-/-");
    else               snprintf(buf, sizeof(buf), "%u/%u",
                                (unsigned)vm.active, (unsigned)vm.limit);
    lv_label_set_text(T.vox, buf);

    uint8_t pct = status.batteryPct;
    if (pct > 100) pct = 100;
    int32_t w = (14 * (int32_t)pct) / 100;
    lv_obj_set_width(T.bat_fill, w < 1 ? 1 : w);
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
    return false;
}

/// Put the keys in the group the encoder is on, appended, so every screen
/// keeps its own navigation order and gains the transport at its end.
void join_current_group()
{
    lv_indev_t * nav = getGuiPlatform().getNavigationEncoder();
    if (!nav) return;
    lv_group_t * g = lv_indev_get_group(nav);
    if (!g) return;
    /* Not only "a different group": an app that calls
     * lv_group_remove_all_objs() on its own group drops our keys without the
     * pointer ever changing. Arrange does exactly that every time it refills
     * the track list, and the transport became unreachable on that screen
     * while the launcher and the Sequencer were fine -- which is what made it
     * look like Arrange was broken. So the keys themselves are the test. */
    lv_obj_t * probe = T.btn[B_MODE];
    if (g == s_joined && probe && lv_obj_get_group(probe) == g) return;
    /* Wait until the screen has put something of its own in: joining an
     * empty group makes the first key the only member, LVGL focuses it, and
     * an app would open with its focus on the transport instead of its own
     * first control. */
    if (lv_group_get_obj_count(g) == 0) return;

    for (uint8_t i = 0; i < B_COUNT; i++) {
        /* BPM is a readout, not a control: nothing happens on a press, so it
         * stays out of the rotation. Wiring an encoder edit or a tap tempo
         * there is the obvious next thing. */
        if (i == B_BPM) continue;
        if (T.btn[i]) lv_group_add_obj(g, T.btn[i]);
    }
    s_joined = g;
    s_focus_return = nullptr;
}

void refresh_timer_cb(lv_timer_t *)
{
    join_current_group();
    transportbar_refresh();
    paint_readouts();
}

void bar_delete_cb(lv_event_t *)
{
    /* The bar dies with the screen on every launcher rebuild. The keys go
     * with it and LVGL takes them out of the group it had them in, so there
     * is nothing to hand back -- only our own handles to drop. */
    s_joined = nullptr;
    s_focus_return = nullptr;
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
    T.status_bar = status_bar;

    T.bar = box(parent, 0, 0, 320, TB_H, 0);
    lv_obj_add_event_cb(T.bar, bar_delete_cb, LV_EVENT_DELETE, nullptr);

    /* MODE: 2x2 squares */
    lv_obj_t * m = button(B_MODE, 3, 44);
    static const int32_t sq[4][2] = { {14, 5}, {14, 13}, {22, 5}, {22, 13} };
    for (int i = 0; i < 4; i++) T.mode_sq[i] = box(m, sq[i][0], sq[i][1], 6, 6, 1);

    /* REC: a ring that fills red */
    lv_obj_t * r = button(B_REC, 51, 44);
    T.ico[B_REC] = box(r, 16, 6, 12, 12, LV_RADIUS_CIRCLE);
    lv_obj_set_style_bg_color(T.ico[B_REC], C_RED, 0);
    lv_obj_set_style_border_width(T.ico[B_REC], 2, 0);

    /* DUB */
    lv_obj_t * d = button(B_DUB, 99, 44);
    T.ico[B_DUB] = label(d, F_DUB, 0, 0, "DUB");
    lv_obj_center(T.ico[B_DUB]);

    /* PLAY */
    lv_obj_t * p = button(B_PLAY, 147, 44);
    T.ico[B_PLAY] = label(p, F_VAL, 0, 0, LV_SYMBOL_PLAY);
    lv_obj_center(T.ico[B_PLAY]);

    /* STOP: square */
    lv_obj_t * st = button(B_STOP, 195, 44);
    T.ico[B_STOP] = box(st, 16, 6, 12, 12, 1);

    /* BPM: the value alone, no label above it any more */
    lv_obj_t * bz = button(B_BPM, 242, 24);
    lv_obj_set_style_bg_opa(bz, LV_OPA_TRANSP, 0);
    T.bpm_v = label(bz, F_DUB, 2, 7, "--");

    /* separator */
    lv_obj_t * sep = box(T.bar, 268, 8, 1, 14, 0);
    lv_obj_set_style_bg_color(sep, INK, 0);
    lv_obj_set_style_bg_opa(sep, 90, 0);

    /* Position, one line: the state is the bar colour now. y=11 rather than
     * the design's 10 for the same reason the counter below diverges -- see
     * there; at 6 px monospace a five-character count reaches back into this
     * label's last column, and one pixel of separation costs nothing. */
    T.pos_v = label(T.bar, F_DUB, 273, 11, "1.1");

    /* Voices above the battery. The design gives this 20 px centred, which
     * holds four characters of a proportional montserrat 8. The pixel face is
     * 6 px monospace, so "1/10" is 24 and wrapped onto a second line, and
     * "16/16" is 30 -- a width no font we have fits in 20 legibly. Content
     * width with the right edge flush to the battery's keeps the four-
     * character case within a pixel of where it was drawn and never wraps. */
    T.vox = label(T.bar, F_LBL, 294, 3, "-/-");
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

    if (s_painted_valid && st == s_painted_st && caps == s_painted_caps &&
        dub == s_painted_dub && mode_on == s_painted_mode) {
        return;
    }
    s_painted_st = st;
    s_painted_caps = caps;
    s_painted_dub = dub;
    s_painted_mode = mode_on;
    s_painted_valid = true;

    T.st = st;
    T.dub_on = dub;

    /* MODE and BPM are the bar's own; the four commands are the holder's, and
     * a key it cannot honour dims without moving. STOP while stopped is the
     * one that would be offered and do nothing. */
    T.en[B_MODE] = true;
    T.en[B_BPM]  = crosspad::transportBpm() > 0.0f;
    T.en[B_PLAY] = (caps & crosspad::kTransportCanPlay) != 0;
    T.en[B_STOP] = (caps & crosspad::kTransportCanStop) != 0 && st != BarState::Stop;
    T.en[B_REC]  = (caps & crosspad::kTransportCanRecord) != 0;
    T.en[B_DUB]  = (caps & crosspad::kTransportCanOverdub) != 0;

    repaint();
}

bool transportbar_shown()
{
    return T.bar && !lv_obj_has_flag(T.bar, LV_OBJ_FLAG_HIDDEN);
}

void transportbar_show(bool show)
{
    if (!T.bar || !T.status_bar) return;
    if (show == transportbar_shown()) return;
    if (show) {
        lv_obj_add_flag(T.status_bar, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(T.bar, LV_OBJ_FLAG_HIDDEN);
        s_painted_valid = false;
        transportbar_refresh();
        paint_readouts();
        if (!s_timer) s_timer = lv_timer_create(refresh_timer_cb, kRefreshMs, nullptr);
    } else {
        /* Nothing to drive while the icons are up. The keys stay in the group
         * but go hidden with the bar, and LVGL's navigation skips them. */
        if (s_timer) { lv_timer_delete(s_timer); s_timer = nullptr; }
        lv_obj_add_flag(T.bar, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(T.status_bar, LV_OBJ_FLAG_HIDDEN);
    }
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
         * rather than trusted. */
        if (s_focus_return && lv_obj_get_group(s_focus_return) == g) {
            lv_group_focus_obj(s_focus_return);
        } else {
            lv_group_focus_next(g);   // anything that is not the transport
        }
        s_focus_return = nullptr;
        return true;
    }

    /* Jump to the key the state makes obvious, so one press does the expected
     * thing without rotating the whole screen first. */
    s_focus_return = focused;
    lv_obj_t * target = T.btn[T.st == BarState::Stop ? B_PLAY : B_STOP];
    /* A disabled key is skipped by LVGL's navigation and must not be the
     * jump's destination either. */
    if (target && (lv_obj_get_state(target) & LV_STATE_DISABLED)) target = T.btn[B_MODE];
    if (!target) return false;
    lv_group_focus_obj(target);
    return true;
}

} // namespace crosspad_gui

#endif // USE_LVGL
