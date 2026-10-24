// SPDX-License-Identifier: GPL-3.0-or-later
#ifdef USE_LVGL

#include "crosspad-gui/components/metronome_popup.h"

#include "crosspad-gui/components/power_gesture.h"
#include "crosspad-gui/components/ui_modal.h"
#include "crosspad-gui/platform/IGuiPlatform.h"
#include "crosspad-gui/theme/crosspad_theme.h"
#include "crosspad-gui/widgets/gui_helpers.h"

#include "crosspad/transport/Transport.hpp"

#include <cmath>

namespace crosspad_gui {

namespace {

const lv_color_t kCaption = lv_color_hex(0x9AA0A8);   // as a dialog's message
const lv_color_t kSegment = lv_color_make(45, 45, 45); // as a dialog's button
constexpr int32_t kCaptionW = 78;
constexpr int32_t kSegmentH = 28;
constexpr int32_t kRing     = 2;   // the focus ring, 2 px off the control
/* The click level in 5 % steps: a detent of the encoder is one. */
constexpr int32_t kLevelSteps = 20;

constexpr uint8_t kDivisions[] = {1, 2, 4};
const char * const kDivisionText[] = {"1/4", "1/8", "1/16"};
constexpr uint8_t kCountIns[] = {0, 1, 2};
const char * const kCountInText[] = {"Off", "1 bar", "2 bars"};
constexpr int kOptions = 3;

struct {
    lv_obj_t *   overlay = nullptr;
    lv_obj_t *   level_v = nullptr;
    lv_obj_t *   division[kOptions] = {};
    lv_obj_t *   count_in[kOptions] = {};
    lv_group_t * group = nullptr;
    lv_group_t * prev_group = nullptr;
    uint32_t     back_token = 0;
} P;

/// Only one of a row's segments is checked: the value the transport reports.
void paint()
{
    if (!P.overlay) return;
    const uint8_t div = crosspad::transportClickDivision();
    const uint8_t bars = crosspad::transportCountInBars();
    for (int i = 0; i < kOptions; i++) {
        lv_obj_set_state(P.division[i], LV_STATE_CHECKED, kDivisions[i] == div);
        lv_obj_set_state(P.count_in[i], LV_STATE_CHECKED, kCountIns[i] == bars);
    }
    const int pct = (int)std::lround(crosspad::transportClickLevel() * 100.0f);
    lv_label_set_text_fmt(P.level_v, "%d%%", pct);
}

/// The encoder back where it was and our handles dropped. The overlay itself
/// is the caller's: deleted after this, or already being deleted.
void teardown()
{
    ui_back_remove(P.back_token);
    lv_indev_t * nav = getGuiPlatform().getNavigationEncoder();
    if (nav && P.prev_group) lv_indev_restore_group(nav, P.prev_group);
    if (P.group) lv_group_delete_deferred(P.group);
    P = {};
}

bool back_cb(void *)
{
    metronome_popup_close();
    return true;
}

/// The overlay went with its parent (a screen rebuilt under it): the encoder
/// and the back handler still have to be given back.
void overlay_delete_cb(lv_event_t * e)
{
    if (P.overlay == lv_event_get_target(e)) teardown();
}

/// A tap beside the card closes, as on every other overlay.
void overlay_click_cb(lv_event_t * e)
{
    if (lv_event_get_target(e) == P.overlay) metronome_popup_close();
}

void level_cb(lv_event_t * e)
{
    lv_obj_t * s = (lv_obj_t *)lv_event_get_target(e);
    crosspad::transportSetClickLevel((float)lv_slider_get_value(s) / (float)kLevelSteps);
    paint();
}

void division_cb(lv_event_t * e)
{
    crosspad::transportSetClickDivision(kDivisions[(uintptr_t)lv_event_get_user_data(e)]);
    paint();
}

void count_in_cb(lv_event_t * e)
{
    crosspad::transportSetCountInBars(kCountIns[(uintptr_t)lv_event_get_user_data(e)]);
    paint();
}

void done_cb(lv_event_t *)
{
    metronome_popup_close();
}

/// A captioned line of the card, inserted above its button row.
lv_obj_t * line(lv_obj_t * card, lv_obj_t * buttons, const char * caption)
{
    lv_obj_t * r = lv_obj_create(card);
    lv_obj_remove_style_all(r);
    lv_obj_set_size(r, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(r, kRing * 2, 0);
    lv_obj_set_style_pad_column(r, 6, 0);
    lv_obj_set_flex_flow(r, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(r, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(r, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_move_to_index(r, lv_obj_get_index(buttons));

    lv_obj_t * l = lv_label_create(r);
    lv_obj_set_width(l, kCaptionW);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(l, kCaption, 0);
    lv_label_set_text(l, caption);
    return r;
}

/// One choice of a row: grey, the accent with black text when it is the value.
lv_obj_t * segment(lv_obj_t * row, const char * text, lv_event_cb_t cb, uintptr_t index)
{
    lv_obj_t * b = lv_button_create(row);
    lv_obj_remove_style_all(b);
    lv_obj_set_height(b, kSegmentH);
    lv_obj_set_flex_grow(b, 1);
    lv_obj_set_style_radius(b, 6, 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(b, kSegment, 0);
    lv_obj_set_style_bg_color(b, CROSSPAD_THEME_PRIMARY, LV_STATE_CHECKED);
    lv_obj_set_style_bg_opa(b, LV_OPA_70, LV_STATE_PRESSED);
    lv_obj_set_style_text_color(b, lv_color_white(), 0);
    lv_obj_set_style_text_color(b, lv_color_black(), LV_STATE_CHECKED);
    lv_obj_set_style_outline_color(b, lv_color_white(), LV_STATE_FOCUSED);
    lv_obj_set_style_outline_width(b, kRing, LV_STATE_FOCUSED);
    lv_obj_set_style_outline_pad(b, kRing, LV_STATE_FOCUSED);
    lv_obj_t * l = lv_label_create(b);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_14, 0);
    lv_label_set_text(l, text);
    lv_obj_center(l);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, (void *)index);
    lv_group_add_obj(P.group, b);
    return b;
}

} // namespace

void metronome_popup_open()
{
    if (P.overlay) return;

    lv_obj_t * buttons = nullptr;
    P.overlay = ui_modal_frame(getOverlayParent(), "Metronome", nullptr, &buttons, nullptr);
    lv_obj_t * card = lv_obj_get_parent(buttons);
    lv_obj_add_event_cb(P.overlay, overlay_delete_cb, LV_EVENT_DELETE, nullptr);
    lv_obj_add_event_cb(P.overlay, overlay_click_cb, LV_EVENT_CLICKED, nullptr);

    lv_indev_t * nav = getGuiPlatform().getNavigationEncoder();
    P.prev_group = nav ? lv_indev_get_group(nav) : nullptr;
    P.group = lv_group_create();
    P.back_token = ui_back_push(back_cb, nullptr);

    /* Volume: a slider the encoder edits (click in, turn, click out), and
     * the level it is at. */
    lv_obj_t * vol = line(card, buttons, "Volume");
    lv_obj_t * s = lv_slider_create(vol);
    lv_obj_set_flex_grow(s, 1);
    lv_obj_set_height(s, 8);
    lv_slider_set_range(s, 0, kLevelSteps);
    lv_slider_set_value(s, (int32_t)std::lround(crosspad::transportClickLevel() * kLevelSteps), LV_ANIM_OFF);
    lv_obj_add_flag(s, LV_OBJ_FLAG_ADV_HITTEST);
    lv_obj_set_style_outline_color(s, lv_color_white(), LV_STATE_FOCUS_KEY);
    lv_obj_set_style_outline_width(s, kRing, LV_STATE_FOCUS_KEY);
    lv_obj_set_style_outline_pad(s, kRing * 3, LV_STATE_FOCUS_KEY);
    lv_obj_add_event_cb(s, level_cb, LV_EVENT_VALUE_CHANGED, nullptr);
    lv_group_add_obj(P.group, s);
    P.level_v = lv_label_create(vol);
    lv_obj_set_width(P.level_v, 40);
    lv_obj_set_style_text_align(P.level_v, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_style_text_font(P.level_v, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(P.level_v, lv_color_white(), 0);

    lv_obj_t * den = line(card, buttons, "Density");
    for (int i = 0; i < kOptions; i++) P.division[i] = segment(den, kDivisionText[i], division_cb, i);

    lv_obj_t * pre = line(card, buttons, "Count-in");
    for (int i = 0; i < kOptions; i++) P.count_in[i] = segment(pre, kCountInText[i], count_in_cb, i);

    lv_obj_t * done = ui_modal_button(buttons, "Done", UI_MODAL_BUTTON_PRIMARY);
    lv_obj_add_event_cb(done, done_cb, LV_EVENT_CLICKED, nullptr);
    lv_group_add_obj(P.group, done);

    if (nav) lv_indev_set_group(nav, P.group);
    lv_group_focus_obj(s);
    paint();
}

void metronome_popup_close()
{
    lv_obj_t * o = P.overlay;
    if (!o) return;
    lv_obj_remove_event_cb(o, overlay_delete_cb);
    teardown();
    lv_obj_delete_async(o);
}

bool metronome_popup_is_open() { return P.overlay != nullptr; }

} // namespace crosspad_gui

#endif // USE_LVGL
