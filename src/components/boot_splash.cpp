// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file boot_splash.cpp
 * @brief The cold-boot splash -- see boot_splash.h.
 *
 * A step every kStepMs: pads 1..step-1 purple, pad `step` lime (the one being
 * lit, fast in, slow out, so the lit ones trail it), the glow behind them
 * growing with each; at kAllLime every pad flashes lime, the glow blooms and
 * the logo rises in; ~0.95 s later (kFadeStep) the backlight fades out. A
 * pad's change of colour is its own animation, since local styles do not run
 * LVGL's transitions.
 *
 * The real pads go along, pad for pad with the screen's: the one being lit
 * lime, the ones before it purple; yellow together at kAllLime, fading out
 * with the screen; their own colours once the screen after the splash is up
 * (boot_splash_reveal).
 */

#ifdef USE_LVGL

#include "crosspad-gui/components/boot_splash.h"
/* lv_image_cache_drop(): lvgl.h stopped exporting the cache in 9.4. */
#include "src/misc/cache/instance/lv_image_cache.h"

#include "crosspad/led/RgbColor.hpp"
#include "crosspad/pad/PadAnimator.hpp"
#include "crosspad/pad/PadLedController.hpp"
#include "crosspad/settings/CrosspadSettings.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <initializer_list>

extern "C" const lv_image_dsc_t boot_splash_logo;

namespace {

const lv_color_t kBg      = lv_color_hex(0x0B0B0D);
const lv_color_t kBgTop   = lv_color_hex(0x1C1233);
const lv_color_t kPadOff  = lv_color_hex(0x1B1B20);
const lv_color_t kPurple  = lv_color_hex(0x8B5CF6);
const lv_color_t kLime    = lv_color_hex(0xD0F009);
const crosspad::RgbColor kLedPurple(139, 92, 246);   // kPurple on the pads
const crosspad::RgbColor kLedLime(208, 240, 9);      // kLime
const crosspad::RgbColor kLedYellow(255, 200, 0);

constexpr int32_t  kW = 320, kH = 240;
constexpr int32_t  kPadSize   = 20;
constexpr int32_t  kPadGap    = 4;
constexpr int32_t  kGridX     = 112;
constexpr int32_t  kGridY     = 34;
constexpr int32_t  kPadGlow   = 12;      ///< how far a lit pad's glow reaches past its edge
constexpr int32_t  kGlowR     = 80;      ///< the glow behind the grid, radius
constexpr uint32_t kStepMs    = 45;
constexpr int      kAllLime   = 17;      ///< the step every pad flashes lime and the logo comes in
constexpr int      kFadeStep  = 38;      ///< the fade starts (kFadeStep - kAllLime) * kStepMs after kAllLime: ~0.95 s of lime and logo
constexpr uint32_t kHotMs     = 90;      ///< a pad lighting up
constexpr uint32_t kCoolMs    = 260;     ///< a pad settling back, so the lit ones trail the new one
constexpr uint32_t kBloomMs   = 500;
constexpr uint32_t kLogoMs    = 350;
constexpr uint32_t kFloorMs   = 33;
constexpr int32_t  kLogoY     = 152;
/* The STM fades the backlight itself (CrossPad_STM32_r2 backlight.c): a new
 * level eases in over 100 x 5 ms, ease-in-out quad in percent, and the duty
 * is the percent squared. A second level before the first is reached starts
 * over from where the first one began -- so a fade is one level written, and
 * the pads follow that same curve beside it, frame for frame. */
constexpr uint32_t kFadeMs     = 500;
constexpr uint32_t kFadeStepMs = 20;

constexpr int32_t kHorizonY = 172;
constexpr int32_t kFloorH   = kH - kHorizonY;
constexpr int     kHLines   = 7;
constexpr int     kVLines   = 15;

enum class PadMode : int8_t { Unset = -1, Off, On, Hot };

struct Pad {
    lv_obj_t*  obj = nullptr;
    lv_obj_t*  glow = nullptr;
    PadMode    mode = PadMode::Unset;
    lv_color_t from{}, to{}, cur{};
    uint8_t    glowFrom = 0, glowTo = 0;
};

lv_obj_t*      s_root = nullptr;
lv_obj_t*      s_glow = nullptr;
lv_obj_t*      s_logo = nullptr;
Pad            s_pads[17];                       // index = pad number 1..16
lv_obj_t*      s_hline[kHLines] = {};
/* A horizontal line is a one-pixel strip moved up and down, never new
 * points: a line object spans from (0,0) to its farthest point, and each new
 * set of points repainted most of the screen -- seven of them, 30 times a
 * second, was every frame a full one. */
const lv_point_precise_t kHLinePts[2] = {{0, 0}, {(lv_value_precise_t)kW, 0}};
lv_point_precise_t s_vpts[kVLines][2];
lv_draw_buf_t* s_padGlowBuf = nullptr;
lv_draw_buf_t* s_bigGlowBuf = nullptr;
lv_timer_t*    s_stepTimer = nullptr;
lv_timer_t*    s_floorTimer = nullptr;
int            s_step = 0;
uint32_t       s_limeTick = 0;                   // lv_tick_get() at kAllLime: the hold is timed from it
uint16_t       s_phase = 0;                      // the floor's scroll, 0..255
bool           s_standalone = false;
/* Read by the boot task without the LVGL lock. */
std::atomic<bool> s_active{false};

/* The fades, which outlive the objects: one brings the next screen up
 * after they are gone. */
enum class Fade : uint8_t {
    In,       ///< the splash appears: the screen only, the pads are its own
    Out,      ///< the splash goes: the screen and the pads' yellow together
    Reveal,   ///< the next screen appears: the screen and the pads' own colours
};
boot_splash_backlight_fn s_backlight = nullptr;
uint8_t     s_level = 100;
lv_timer_t* s_fadeTimer = nullptr;
uint32_t    s_fadeStart = 0;
Fade        s_fade = Fade::In;
uint8_t     s_padLevel = 0;       ///< the pads' brightness a Reveal brings them to
uint8_t     s_padCap = 0;         ///< the controller's own cap, back once it is there
uint8_t     s_padDim = 255;       ///< the splash's colours on the pads, as the user set them
void (*s_fadeDone)() = nullptr;

// ── the backlight ──────────────────────────────────────────────────────────

/* How bright the pads' own colours are, as PadLedController::applyBrightness
 * makes them: the pad brightness from Settings under the controller's cap.
 * The screen comes back at the level it was left at, and so do the pads. */
uint8_t pad_level()
{
    auto& leds = crosspad::getPadLedController();
    const auto* cfg = crosspad::CrosspadSettings::getInstance();
    return cfg ? std::min(leds.getBrightness(), cfg->RGBbrightness) : leds.getBrightness();
}

/* @p c at @p level of 255, as near its hue as a few steps of 8-bit PWM
 * allow: rounded, and dark once a channel the hue is made of would round
 * away. Pads are set as low as 3 of 255, where yellow truncated is (3,2,0)
 * and its fade's tail (1,0,0) -- red. A channel under an eighth of the
 * largest (lime's blue) is only a tint and may go. */
crosspad::RgbColor dim_keep_hue(crosspad::RgbColor c, float level)
{
    const uint8_t in[3] = {c.R, c.G, c.B};
    const int top = std::max({c.R, c.G, c.B});
    uint8_t out[3];
    for (int i = 0; i < 3; ++i) {
        out[i] = (uint8_t)std::lround((float)in[i] * level / 255.0f);
        if (out[i] == 0 && in[i] != 0 && in[i] * 8 >= top) return crosspad::RgbColor();
    }
    return crosspad::RgbColor(out[0], out[1], out[2]);
}

/* The pads' LEDs at step @p step, numbered as the screen's pads (1..16,
 * bottom left first); every one @p all when that is not black; at @p light
 * of the pads' own brightness. */
void show_pads(int step, crosspad::RgbColor all = crosspad::RgbColor(), float light = 1.0f)
{
    crosspad::RgbColor frame[crosspad::PadAnimator::kPadCount];
    const bool uniform = all.R || all.G || all.B || step < 0;
    for (int n = 1; n <= 16; ++n) {
        frame[n - 1] = dim_keep_hue(uniform ? all : n < step ? kLedPurple : n == step ? kLedLime : crosspad::RgbColor(),
                                    (float)s_padDim * light);
    }
    crosspad::getPadAnimator().showFrame(frame);
}

float ease_in_out_quad(float t)
{
    return t < 0.5f ? 2.0f * t * t : 1.0f - 2.0f * (1.0f - t) * (1.0f - t);
}

/* Where the STM's fade is, as the eye gets it: the eased fraction of the
 * level, squared as its duty is -- and the pads' PWM is linear, so the same
 * number is the same light. */
void fade_cb(lv_timer_t*)
{
    const uint32_t ms = lv_tick_elaps(s_fadeStart);
    const float t = ms >= kFadeMs ? 1.0f : (float)ms / (float)kFadeMs;
    const float e = s_fade == Fade::Out ? 1.0f - ease_in_out_quad(t) : ease_in_out_quad(t);
    const float light = e * e;
    if (s_fade == Fade::Out) {
        show_pads(-1, kLedYellow, light);
    } else if (s_fade == Fade::Reveal) {
        crosspad::getPadLedController().setBrightness(t < 1.0f ? (uint8_t)((float)s_padLevel * light) : s_padCap);
    }
    if (t < 1.0f) return;
    lv_timer_delete(s_fadeTimer);
    s_fadeTimer = nullptr;
    if (s_fadeDone) s_fadeDone();
}

void fade(Fade kind, void (*done)())
{
    if (s_fadeTimer) lv_timer_delete(s_fadeTimer);
    s_fade = kind;
    s_fadeDone = done;
    if (s_backlight) s_backlight(kind == Fade::Out ? 0 : s_level);
    s_fadeStart = lv_tick_get();
    s_fadeTimer = lv_timer_create(fade_cb, kFadeStepMs, nullptr);
}

// ── the glow sprites ───────────────────────────────────────────────────────

/* Alpha only, tinted by each image's recolour: one sprite serves every pad
 * in any colour, and drawing one is a blend, not a blur. */
lv_draw_buf_t* make_sprite(int32_t w, int32_t h, float (*alpha)(float x, float y))
{
    lv_draw_buf_t* b = lv_draw_buf_create((uint32_t)w, (uint32_t)h, LV_COLOR_FORMAT_A8, LV_STRIDE_AUTO);
    if (!b) return nullptr;
    for (int32_t y = 0; y < h; ++y) {
        uint8_t* row = b->data + y * b->header.stride;
        for (int32_t x = 0; x < w; ++x) {
            const float a = alpha(x + 0.5f - w * 0.5f, y + 0.5f - h * 0.5f);
            row[x] = (uint8_t)(255.0f * (a < 0.0f ? 0.0f : a > 1.0f ? 1.0f : a));
        }
    }
    return b;
}

/* Falling off from the pad's rounded edge. */
float pad_glow_alpha(float x, float y)
{
    constexpr float kHalf = kPadSize * 0.5f, kR = 4.0f;
    const float dx = std::fmax(std::fabs(x) - (kHalf - kR), 0.0f);
    const float dy = std::fmax(std::fabs(y) - (kHalf - kR), 0.0f);
    const float d = std::sqrt(dx * dx + dy * dy) - kR;
    const float t = 1.0f - d / (float)kPadGlow;
    return d <= 0.0f ? 1.0f : t * t;
}

float big_glow_alpha(float x, float y)
{
    const float t = 1.0f - std::sqrt(x * x + y * y) / (float)kGlowR;
    return t * t * t;
}

lv_obj_t* sprite(lv_obj_t* parent, lv_draw_buf_t* buf, lv_color_t c, lv_opa_t opa)
{
    lv_obj_t* img = lv_image_create(parent);
    if (buf) lv_image_set_src(img, buf);
    lv_obj_set_style_image_recolor(img, c, 0);
    lv_obj_set_style_image_recolor_opa(img, LV_OPA_COVER, 0);
    lv_obj_set_style_image_opa(img, opa, 0);
    lv_obj_remove_flag(img, LV_OBJ_FLAG_CLICKABLE);
    return img;
}

// ── the pads ───────────────────────────────────────────────────────────────

void pad_anim_cb(void* var, int32_t v)
{
    Pad& p = s_pads[(intptr_t)lv_obj_get_user_data(static_cast<lv_obj_t*>(var))];
    p.cur = lv_color_mix(p.to, p.from, (uint8_t)v);
    lv_obj_set_style_bg_color(p.obj, p.cur, 0);
    lv_obj_set_style_image_recolor(p.glow, p.cur, 0);
    lv_obj_set_style_image_opa(p.glow, (lv_opa_t)(p.glowFrom + ((p.glowTo - p.glowFrom) * v) / 255), 0);
}

void set_pad(int n, PadMode mode)
{
    Pad& p = s_pads[n];
    if (p.mode == mode) return;
    const bool first = p.mode == PadMode::Unset;
    p.mode = mode;
    p.from = first ? kPadOff : p.cur;
    p.to = mode == PadMode::Hot ? kLime : mode == PadMode::On ? kPurple : kPadOff;
    p.glowFrom = first ? 0 : p.glowTo;
    p.glowTo = mode == PadMode::Hot ? 230 : mode == PadMode::On ? 140 : 0;
    lv_anim_delete(p.obj, pad_anim_cb);
    if (first) {
        pad_anim_cb(p.obj, 255);
        return;
    }
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, p.obj);
    lv_anim_set_exec_cb(&a, pad_anim_cb);
    lv_anim_set_values(&a, 0, 255);
    lv_anim_set_duration(&a, mode == PadMode::Hot ? kHotMs : kCoolMs);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_start(&a);
}

// ── the sequence ───────────────────────────────────────────────────────────

void stop_timers()
{
    if (s_stepTimer) { lv_timer_delete(s_stepTimer); s_stepTimer = nullptr; }
    if (s_floorTimer) { lv_timer_delete(s_floorTimer); s_floorTimer = nullptr; }
}

/* Dark: nothing more to draw. Standalone, it goes and the screen under it
 * comes back up; at boot, the launcher is built first (boot_splash_reveal). */
void faded_out()
{
    stop_timers();
    s_active = false;
    if (!s_standalone) return;
    if (s_root) lv_obj_delete_async(s_root);
    boot_splash_reveal();
}

void opa_cb(void* o, int32_t v) { lv_obj_set_style_image_opa(static_cast<lv_obj_t*>(o), (lv_opa_t)v, 0); }
void y_cb(void* o, int32_t v) { lv_obj_set_y(static_cast<lv_obj_t*>(o), v); }

void animate(lv_obj_t* o, lv_anim_exec_xcb_t cb, int32_t from, int32_t to, uint32_t ms)
{
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, o);
    lv_anim_set_exec_cb(&a, cb);
    lv_anim_set_values(&a, from, to);
    lv_anim_set_duration(&a, ms);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_start(&a);
}

void step_cb(lv_timer_t*)
{
    ++s_step;
    if (s_step > kAllLime) {
        /* Timed, not counted: lv_timer runs a late tick once and drops the
         * rest, so the 21 steps of the hold took 21 frames -- ~1.7 s on the
         * 05.10 boot, frames being slower since the blend left IRAM -- and
         * the launcher waits for the fade. */
        if (lv_tick_elaps(s_limeTick) >= (kFadeStep - kAllLime) * kStepMs) {
            lv_timer_pause(s_stepTimer);
            fade(Fade::Out, faded_out);
        }
        return;
    }
    const bool allLime = s_step == kAllLime;
    if (allLime) s_limeTick = lv_tick_get();
    for (int n = 1; n <= 16; ++n) {
        set_pad(n, allLime ? PadMode::Hot : n < s_step ? PadMode::On : n == s_step ? PadMode::Hot : PadMode::Off);
    }
    if (allLime) show_pads(-1, kLedYellow);
    else         show_pads(s_step);
    if (allLime) {
        /* A bloom that settles, and the logo rising into it. */
        lv_obj_set_style_image_recolor(s_glow, kLime, 0);
        animate(s_glow, opa_cb, 255, 150, kBloomMs);
        animate(s_logo, opa_cb, LV_OPA_TRANSP, LV_OPA_COVER, kLogoMs);
        animate(s_logo, y_cb, kLogoY + 8, kLogoY, kLogoMs);
    } else {
        lv_obj_set_style_image_opa(s_glow, (lv_opa_t)(40 + s_step * 5), 0);
    }
}

/* The floor's lines scroll towards the viewer, closer together near the
 * horizon, fainter there too. */
void floor_cb(lv_timer_t*)
{
    s_phase = (uint16_t)((s_phase + 9) & 0xFF);
    for (int i = 0; i < kHLines; ++i) {
        const int32_t u = (i * 256 + s_phase) / kHLines;                 // 0..255
        lv_obj_set_y(s_hline[i], kHorizonY + (kFloorH * u * u) / (256 * 256));
        lv_obj_set_style_line_opa(s_hline[i], (lv_opa_t)(u * 120 / 256), 0);
    }
}

lv_obj_t* floor_line(lv_obj_t* parent)
{
    lv_obj_t* l = lv_line_create(parent);
    lv_obj_set_style_line_color(l, kPurple, 0);
    lv_obj_set_style_line_width(l, 1, 0);
    lv_obj_set_pos(l, 0, 0);
    return l;
}

lv_obj_t* plain(lv_obj_t* parent)
{
    lv_obj_t* o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_remove_flag(o, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
    return o;
}

/* The sprites once nothing can draw them any more: the images still point
 * at them while the tree around them is being deleted. */
void free_sprites(void*)
{
    for (lv_draw_buf_t** b : {&s_padGlowBuf, &s_bigGlowBuf}) {
        if (!*b) continue;
        lv_image_cache_drop(*b);
        lv_draw_buf_destroy(*b);
        *b = nullptr;
    }
}

/* Whoever deletes the splash -- itself, or the next screen built over it,
 * early or not -- leaves nothing running on objects gone. */
void root_deleted_cb(lv_event_t*)
{
    stop_timers();
    /* Never with a null var: that deletes every animation there is. */
    if (s_glow) lv_anim_delete(s_glow, nullptr);
    if (s_logo) lv_anim_delete(s_logo, nullptr);
    for (int n = 1; n <= 16; ++n) {
        if (s_pads[n].obj) lv_anim_delete(s_pads[n].obj, nullptr);
    }
    s_root = s_glow = s_logo = nullptr;
    for (auto& p : s_pads) p = Pad{};
    for (auto& l : s_hline) l = nullptr;
    s_active = false;
    lv_async_call(free_sprites, nullptr);
}

} // namespace

void boot_splash_start(lv_obj_t* parent, uint8_t level, boot_splash_backlight_fn backlight, bool standalone)
{
    if (s_root) return;
    s_step = 0;
    s_phase = 0;
    s_standalone = standalone;
    s_backlight = backlight;
    s_level = level;
    s_padDim = pad_level();
    if (s_backlight) s_backlight(0);
    free_sprites(nullptr);
    s_padGlowBuf = make_sprite(kPadSize + 2 * kPadGlow, kPadSize + 2 * kPadGlow, pad_glow_alpha);
    s_bigGlowBuf = make_sprite(2 * kGlowR, 2 * kGlowR, big_glow_alpha);

    s_root = plain(parent);
    lv_obj_add_flag(s_root, LV_OBJ_FLAG_FLOATING);
    lv_obj_set_size(s_root, kW, kH);
    lv_obj_set_style_bg_opa(s_root, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(s_root, kBgTop, 0);
    lv_obj_set_style_bg_grad_color(s_root, kBg, 0);
    lv_obj_set_style_bg_grad_dir(s_root, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_main_stop(s_root, 40, 0);
    lv_obj_set_style_bg_grad_stop(s_root, 220, 0);
    lv_obj_add_event_cb(s_root, root_deleted_cb, LV_EVENT_DELETE, nullptr);

    lv_obj_t* floor = plain(s_root);
    lv_obj_set_size(floor, kW, kH);
    for (int k = 0; k < kVLines; ++k) {
        const int32_t off = k - kVLines / 2;
        s_vpts[k][0] = {(lv_value_precise_t)(160 + off * 12), (lv_value_precise_t)kHorizonY};
        s_vpts[k][1] = {(lv_value_precise_t)(160 + off * 44), (lv_value_precise_t)kH};
        lv_obj_t* l = floor_line(floor);
        lv_line_set_points(l, s_vpts[k], 2);
        lv_obj_set_style_line_opa(l, 60, 0);
    }
    for (int i = 0; i < kHLines; ++i) {
        s_hline[i] = floor_line(floor);
        lv_line_set_points(s_hline[i], kHLinePts, 2);
    }

    const int32_t gridW = 4 * kPadSize + 3 * kPadGap;
    s_glow = sprite(s_root, s_bigGlowBuf, kPurple, 40);
    lv_obj_set_pos(s_glow, kGridX + gridW / 2 - kGlowR, kGridY + gridW / 2 - kGlowR);

    /* 13 14 15 16 / 9..12 / 5..8 / 1..4, as the pads are numbered; every glow
     * first, so none is drawn over a neighbouring pad. */
    for (int n = 1; n <= 16; ++n) {
        const int col = (n - 1) % 4, row = 3 - (n - 1) / 4;
        s_pads[n] = Pad{};
        s_pads[n].glow = sprite(s_root, s_padGlowBuf, kPadOff, LV_OPA_TRANSP);
        lv_obj_set_pos(s_pads[n].glow, kGridX + col * (kPadSize + kPadGap) - kPadGlow,
                       kGridY + row * (kPadSize + kPadGap) - kPadGlow);
    }
    for (int n = 1; n <= 16; ++n) {
        const int col = (n - 1) % 4, row = 3 - (n - 1) / 4;
        lv_obj_t* p = plain(s_root);
        lv_obj_set_size(p, kPadSize, kPadSize);
        lv_obj_set_pos(p, kGridX + col * (kPadSize + kPadGap), kGridY + row * (kPadSize + kPadGap));
        lv_obj_set_style_radius(p, 4, 0);
        lv_obj_set_style_bg_opa(p, LV_OPA_COVER, 0);
        lv_obj_set_user_data(p, (void*)(intptr_t)n);
        s_pads[n].obj = p;
        set_pad(n, PadMode::Off);
    }

    s_logo = sprite(s_root, nullptr, kLime, LV_OPA_TRANSP);
    lv_image_set_src(s_logo, &boot_splash_logo);
    lv_obj_set_pos(s_logo, (kW - (int32_t)boot_splash_logo.header.w) / 2, kLogoY + 8);

    s_active = true;
    show_pads(0);
    fade(Fade::In, nullptr);
    s_stepTimer = lv_timer_create(step_cb, kStepMs, nullptr);
    s_floorTimer = lv_timer_create(floor_cb, kFloorMs, nullptr);
    floor_cb(nullptr);
}

bool boot_splash_active(void)
{
    return s_active;
}

void boot_splash_reveal(void)
{
    /* The pads hand back to their own colours, from dark, rising with the
     * screen -- nothing in between: a green flash here only blinked before
     * them. */
    auto& leds = crosspad::getPadLedController();
    s_padCap = leds.getBrightness();
    s_padLevel = pad_level();
    leds.setBrightness(0);
    crosspad::getPadAnimator().stop();
    fade(Fade::Reveal, nullptr);
}

#endif // USE_LVGL
