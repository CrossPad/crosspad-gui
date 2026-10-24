// SPDX-License-Identifier: GPL-3.0-or-later
#ifdef USE_LVGL

#include "crosspad-gui/components/kit_loader.h"
#include "crosspad-gui/theme/crosspad_theme.h"

#include <cstdio>

namespace crosspad_gui {

namespace {

constexpr int32_t kX    = 4;
constexpr int32_t kY    = 34;
constexpr int32_t kW    = 312;
constexpr int32_t kH    = 22;
constexpr int32_t kPad  = 8;
constexpr int32_t kPctW = 40;
/// "KIT LOADED" stays this long, then fades out over kFadeMs.
constexpr uint32_t kDoneHoldMs = 600;
constexpr uint32_t kFadeMs     = 250;

const lv_color_t kPanel = lv_color_hex(0x20242A);
const lv_color_t kText  = lv_color_hex(0xE8EAED);
const lv_color_t kMint  = lv_color_hex(0x00FF88);
const lv_color_t kRed   = lv_color_hex(0xFF5C5C);
const lv_color_t kInk   = lv_color_hex(0x06120C);

struct Labels { lv_obj_t* name = nullptr; lv_obj_t* pct = nullptr; };

lv_obj_t*   s_root = nullptr;
lv_obj_t*   s_fill = nullptr;
Labels      s_top;            // light text, under the fill
Labels      s_ink;            // dark text, inside the fill (clipped by it)
lv_timer_t* s_hideTimer = nullptr;
/* What the bar says, so a progress or done meant for a load cannot write
 * over an error still on show. */
enum class State : uint8_t { Hidden, Loading, Done, Error };
State       s_state = State::Hidden;

void bare(lv_obj_t* o)
{
    lv_obj_remove_style_all(o);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE);
}

void make_labels(lv_obj_t* parent, Labels& l, lv_color_t col)
{
    l.name = lv_label_create(parent);
    lv_obj_set_style_text_font(l.name, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(l.name, col, 0);
    lv_label_set_long_mode(l.name, LV_LABEL_LONG_DOT);
    lv_obj_set_width(l.name, kW - 2 * kPad - kPctW);
    lv_obj_align(l.name, LV_ALIGN_LEFT_MID, kPad, 0);

    l.pct = lv_label_create(parent);
    lv_obj_set_style_text_font(l.pct, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(l.pct, col, 0);
    lv_obj_set_style_text_align(l.pct, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_width(l.pct, kPctW);
    lv_obj_align(l.pct, LV_ALIGN_RIGHT_MID, -kPad, 0);
}

void set_text(const char* name, const char* pct)
{
    lv_label_set_text(s_top.name, name);
    lv_label_set_text(s_ink.name, name);
    lv_label_set_text(s_top.pct, pct);
    lv_label_set_text(s_ink.pct, pct);
}

void set_fill(uint8_t pct)
{
    if (pct > 100) pct = 100;
    lv_obj_set_width(s_fill, kW * pct / 100);
}

void cancel_timer()
{
    if (s_hideTimer) {
        lv_timer_delete(s_hideTimer);
        s_hideTimer = nullptr;
    }
}

void opa_cb(void* o, int32_t v)
{
    lv_obj_set_style_opa(static_cast<lv_obj_t*>(o), static_cast<lv_opa_t>(v), 0);
}

void fade_done_cb(lv_anim_t*)
{
    if (!s_root) return;
    s_state = State::Hidden;
    lv_obj_add_flag(s_root, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_opa(s_root, LV_OPA_COVER, 0);
}

void hide_timer_cb(lv_timer_t*)
{
    s_hideTimer = nullptr;
    if (!s_root) return;
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_root);
    lv_anim_set_values(&a, LV_OPA_COVER, LV_OPA_TRANSP);
    lv_anim_set_duration(&a, kFadeMs);
    lv_anim_set_exec_cb(&a, opa_cb);
    lv_anim_set_completed_cb(&a, fade_done_cb);
    lv_anim_start(&a);
}

/* Only an error takes touches: it stays up, and a tap is the way to put the
 * context row under it back. Loading and loaded let them through. */
void set_clickable(bool on)
{
    if (on) lv_obj_add_flag(s_root, LV_OBJ_FLAG_CLICKABLE);
    else    lv_obj_remove_flag(s_root, LV_OBJ_FLAG_CLICKABLE);
}

void on_clicked(lv_event_t*)
{
    kit_loader_hide();
}

/* Whoever deletes the layer's children takes the bar with them; the next
 * call builds it again rather than writing to freed objects. */
void on_deleted(lv_event_t*)
{
    cancel_timer();
    s_state = State::Hidden;
    s_root = s_fill = nullptr;
    s_top = s_ink = Labels{};
}

bool ensure()
{
    if (s_root) return true;
    lv_obj_t* parent = getOverlayParent();
    if (!parent) return false;

    s_root = lv_obj_create(parent);
    bare(s_root);
    lv_obj_set_pos(s_root, kX, kY);
    lv_obj_set_size(s_root, kW, kH);
    lv_obj_set_style_radius(s_root, 4, 0);
    lv_obj_set_style_clip_corner(s_root, true, 0);
    lv_obj_set_style_bg_color(s_root, kPanel, 0);
    lv_obj_set_style_bg_opa(s_root, LV_OPA_COVER, 0);
    lv_obj_add_event_cb(s_root, on_clicked, LV_EVENT_CLICKED, nullptr);
    lv_obj_add_event_cb(s_root, on_deleted, LV_EVENT_DELETE, nullptr);

    make_labels(s_root, s_top, kText);

    /* The fill clips its children, so the ink copy only shows where it is. */
    s_fill = lv_obj_create(s_root);
    bare(s_fill);
    lv_obj_set_pos(s_fill, 0, 0);
    lv_obj_set_size(s_fill, 0, kH);
    lv_obj_set_style_bg_color(s_fill, kMint, 0);
    lv_obj_set_style_bg_opa(s_fill, LV_OPA_COVER, 0);

    /* The ink labels are laid out against a full-width box inside the fill. */
    lv_obj_t* inkBox = lv_obj_create(s_fill);
    bare(inkBox);
    lv_obj_set_pos(inkBox, 0, 0);
    lv_obj_set_size(inkBox, kW, kH);
    lv_obj_add_flag(inkBox, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
    make_labels(inkBox, s_ink, kInk);

    lv_obj_add_flag(s_root, LV_OBJ_FLAG_HIDDEN);
    return true;
}

/// Up, opaque, above whatever else is on the layer.
void show()
{
    cancel_timer();
    lv_anim_delete(s_root, opa_cb);
    lv_obj_set_style_opa(s_root, LV_OPA_COVER, 0);
    lv_obj_remove_flag(s_root, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_root);
}

} // namespace

void kit_loader_start(const char* kitName)
{
    if (!ensure()) return;
    char buf[64];
    snprintf(buf, sizeof(buf), "LOADING %s", (kitName && *kitName) ? kitName : "KIT");
    lv_obj_set_style_bg_color(s_fill, kMint, 0);
    set_fill(0);
    set_text(buf, "0%");
    set_clickable(false);
    show();
    s_state = State::Loading;
}

void kit_loader_set(uint8_t pct)
{
    if (!s_root || s_state != State::Loading) return;
    if (pct > 100) pct = 100;
    char p[8];
    snprintf(p, sizeof(p), "%u%%", static_cast<unsigned>(pct));
    lv_label_set_text(s_top.pct, p);
    lv_label_set_text(s_ink.pct, p);
    set_fill(pct);
}

void kit_loader_done(uint8_t loaded, uint8_t total)
{
    if (!s_root || s_state != State::Loading) return;
    char p[12];
    snprintf(p, sizeof(p), "%u/%u", static_cast<unsigned>(loaded), static_cast<unsigned>(total));
    lv_obj_set_style_bg_color(s_fill, kMint, 0);
    set_fill(100);
    set_text("KIT LOADED", p);
    set_clickable(false);
    s_state = State::Done;
    cancel_timer();
    s_hideTimer = lv_timer_create(hide_timer_cb, kDoneHoldMs, nullptr);
    lv_timer_set_repeat_count(s_hideTimer, 1);
}

void kit_loader_error(const char* msg)
{
    if (!ensure()) return;
    lv_obj_set_style_bg_color(s_fill, kRed, 0);
    set_fill(100);
    set_text((msg && *msg) ? msg : "LOAD FAILED", "!");
    set_clickable(true);
    show();
    s_state = State::Error;
}

void kit_loader_hide()
{
    if (!s_root) return;
    cancel_timer();
    lv_anim_delete(s_root, opa_cb);
    lv_obj_set_style_opa(s_root, LV_OPA_COVER, 0);
    lv_obj_add_flag(s_root, LV_OBJ_FLAG_HIDDEN);
    s_state = State::Hidden;
}

} // namespace crosspad_gui

#endif // USE_LVGL
