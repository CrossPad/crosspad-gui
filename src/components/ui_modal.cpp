// SPDX-License-Identifier: GPL-3.0-or-later
#ifdef USE_LVGL

#include "crosspad-gui/components/ui_modal.h"
#include "crosspad-gui/widgets/gui_helpers.h"
#include "crosspad-gui/theme/crosspad_theme.h"
#include "crosspad-gui/platform/IGuiPlatform.h"
#include "crosspad-gui/components/power_gesture.h"

// ---- Internal types ----

struct ChoiceCtx {
    modal_choice_cb_t cb;           // ui_modal_choice
    modal_confirm_cb_t confirm_cb;  // ui_modal_confirm
    void * user_data;
    lv_obj_t * overlay;
    lv_group_t * prev_group;
    lv_group_t * modal_group;
    uint32_t back_token;   // power-button "back" registration
};

struct SelectCtx {
    modal_select_cb_t cb;
    void * user_data;
    lv_obj_t * overlay;
    lv_group_t * prev_group;
    lv_group_t * modal_group;
    int selected;
    uint32_t back_token;   // power-button "back" registration
};

// ---- The look ----

namespace {

const lv_color_t kCard     = lv_color_make(20, 20, 20);   // as the Settings rows
const lv_color_t kCardEdge = lv_color_make(45, 45, 45);
const lv_color_t kMessage  = lv_color_hex(0x9AA0A8);
const lv_color_t kButton   = lv_color_make(45, 45, 45);
const lv_color_t kDanger   = lv_color_hex(0xFF5C5C);
constexpr int32_t kCardW   = 272;
constexpr int32_t kButtonH = 32;
constexpr int32_t kRing    = 4;    // the focus ring: 2 px, 2 px off the button

} // namespace

lv_obj_t * ui_modal_frame(lv_obj_t * parent, const char * title, const char * message,
                          lv_obj_t ** button_row, lv_obj_t ** message_label) {
    /* Clickable, so a tap beside the card reaches nothing under it. */
    lv_obj_t * overlay = lv_obj_create(parent);
    lv_obj_remove_style_all(overlay);
    lv_obj_set_size(overlay, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(overlay, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(overlay, LV_OPA_60, 0);
    lv_obj_remove_flag(overlay, LV_OBJ_FLAG_SCROLLABLE);

    /* A Settings row grown into a card: dark, the accent down its left edge. */
    lv_obj_t * card = lv_obj_create(overlay);
    lv_obj_remove_style_all(card);
    lv_obj_set_size(card, kCardW, LV_SIZE_CONTENT);
    lv_obj_center(card);
    lv_obj_set_style_bg_color(card, kCard, 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(card, 10, 0);
    lv_obj_set_style_border_color(card, CROSSPAD_THEME_PRIMARY, 0);
    lv_obj_set_style_border_side(card, LV_BORDER_SIDE_LEFT, 0);
    lv_obj_set_style_border_width(card, 3, 0);
    lv_obj_set_style_outline_color(card, kCardEdge, 0);
    lv_obj_set_style_outline_width(card, 1, 0);
    /* kRing less than the look wants: the button row's own padding makes it
     * up, and the texts are set in by as much. */
    lv_obj_set_style_pad_hor(card, 14 - kRing, 0);
    lv_obj_set_style_pad_top(card, 12, 0);
    lv_obj_set_style_pad_bottom(card, 12 - kRing, 0);
    lv_obj_set_style_pad_row(card, 6, 0);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    if (title) {
        lv_obj_t * t = lv_label_create(card);
        lv_obj_set_width(t, LV_PCT(100));
        lv_label_set_long_mode(t, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_pad_hor(t, kRing, 0);
        lv_obj_set_style_text_font(t, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(t, lv_color_white(), 0);
        lv_label_set_text(t, title);
    }

    lv_obj_t * m = lv_label_create(card);
    lv_obj_set_width(m, LV_PCT(100));
    lv_label_set_long_mode(m, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_pad_hor(m, kRing, 0);
    lv_obj_set_style_text_font(m, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(m, kMessage, 0);
    lv_label_set_text(m, message ? message : "");
    if (!message) lv_obj_add_flag(m, LV_OBJ_FLAG_HIDDEN);
    if (message_label) *message_label = m;

    /* The focus ring inside the row's own padding: drawn past the row, its
     * lower half was cut off, and which button the encoder is on with it. */
    lv_obj_t * row = lv_obj_create(card);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(row, kRing, 0);
    lv_obj_set_style_pad_top(row, 8, 0);
    lv_obj_set_style_pad_column(row, 8, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    if (button_row) *button_row = row;

    return overlay;
}

lv_obj_t * ui_modal_button(lv_obj_t * button_row, const char * text, ui_modal_button_kind_t kind) {
    const bool loud = kind != UI_MODAL_BUTTON_NORMAL;
    lv_obj_t * b = lv_button_create(button_row);
    lv_obj_remove_style_all(b);
    lv_obj_set_height(b, kButtonH);
    lv_obj_set_flex_grow(b, 1);
    lv_obj_set_style_radius(b, 8, 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(b, kind == UI_MODAL_BUTTON_PRIMARY ? CROSSPAD_THEME_PRIMARY
                                 : kind == UI_MODAL_BUTTON_DANGER ? kDanger : kButton, 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_70, LV_STATE_PRESSED);
    lv_obj_set_style_text_color(b, loud ? lv_color_black() : lv_color_white(), 0);
    lv_obj_set_style_outline_color(b, lv_color_white(), LV_STATE_FOCUSED);
    lv_obj_set_style_outline_width(b, kRing / 2, LV_STATE_FOCUSED);
    lv_obj_set_style_outline_pad(b, kRing / 2, LV_STATE_FOCUSED);
    lv_obj_t * l = lv_label_create(b);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_14, 0);
    lv_label_set_text(l, text);
    lv_obj_center(l);
    return b;
}

// ---- Choice dialog ----

/* One teardown for every way this dialog can close: a button, or the power
 * button's contextual back (index -1, which a confirm reads as "no"). */
static void choice_close(ChoiceCtx * ctx, int index) {
    crosspad_gui::ui_back_remove(ctx->back_token);
    ctx->back_token = 0;

    if (ctx->cb) ctx->cb(index, ctx->user_data);
    if (ctx->confirm_cb) ctx->confirm_cb(index == 0, ctx->user_data);

    lv_indev_t * nav = crosspad_gui::getGuiPlatform().getNavigationEncoder();
    if (ctx->prev_group && nav) {
        lv_indev_restore_group(nav, ctx->prev_group);
    }
    if (ctx->modal_group) {
        lv_group_del(ctx->modal_group);
    }

    lv_obj_t * overlay = ctx->overlay;
    delete ctx;
    lv_obj_delete(overlay);
}

static bool choice_back_cb(void * user_data) {
    choice_close((ChoiceCtx *)user_data, -1);
    return true;
}

void ui_modal_confirm_dismiss(lv_obj_t * overlay, bool confirmed) {
    if (!overlay) return;
    ChoiceCtx * ctx = (ChoiceCtx *)lv_obj_get_user_data(overlay);
    if (ctx) choice_close(ctx, confirmed ? 0 : -1);
}

static void choice_btn_cb(lv_event_t * e) {
    ChoiceCtx * ctx = (ChoiceCtx *)lv_event_get_user_data(e);
    lv_obj_t * btn = (lv_obj_t *)lv_event_get_target(e);
    choice_close(ctx, (int)lv_obj_get_index(btn));
}

static lv_obj_t * choice_open(const char * title, const char * message,
                              const ui_modal_button_t * buttons, int count, int focus,
                              ChoiceCtx * ctx, lv_group_t * encoder_group) {
    lv_obj_t * row = nullptr;
    lv_obj_t * overlay = ui_modal_frame(crosspad_gui::getOverlayParent(), title, message, &row, nullptr);

    lv_indev_t * nav = crosspad_gui::getGuiPlatform().getNavigationEncoder();
    ctx->overlay = overlay;
    ctx->prev_group = encoder_group ? encoder_group : (nav ? lv_indev_get_group(nav) : nullptr);
    ctx->back_token = crosspad_gui::ui_back_push(choice_back_cb, ctx);
    lv_obj_set_user_data(overlay, ctx);   // ui_modal_confirm_dismiss() finds it here

    ctx->modal_group = lv_group_create();
    if (nav) lv_indev_set_group(nav, ctx->modal_group);

    lv_obj_t * focused = nullptr;
    for (int i = 0; i < count && i < 4; i++) {
        lv_obj_t * b = ui_modal_button(row, buttons[i].text, buttons[i].kind);
        lv_obj_add_event_cb(b, choice_btn_cb, LV_EVENT_CLICKED, ctx);
        lv_group_add_obj(ctx->modal_group, b);
        if (i == focus) focused = b;
    }
    if (focused) lv_group_focus_obj(focused);
    return overlay;
}

lv_obj_t * ui_modal_choice(
    const char * title,
    const char * message,
    const ui_modal_button_t * buttons,
    int count,
    int focus,
    modal_choice_cb_t callback,
    void * user_data,
    lv_group_t * encoder_group
) {
    ChoiceCtx * ctx = new ChoiceCtx();
    ctx->cb = callback;
    ctx->user_data = user_data;
    return choice_open(title, message, buttons, count, focus, ctx, encoder_group);
}

lv_obj_t * ui_modal_confirm(
    const char * title,
    const char * message,
    const char * yes_text,
    const char * no_text,
    modal_confirm_cb_t callback,
    void * user_data,
    lv_group_t * encoder_group
) {
    const ui_modal_button_t buttons[] = {
        {yes_text, UI_MODAL_BUTTON_PRIMARY},
        {no_text, UI_MODAL_BUTTON_NORMAL},
    };
    ChoiceCtx * ctx = new ChoiceCtx();
    ctx->confirm_cb = callback;
    ctx->user_data = user_data;
    return choice_open(title, message, buttons, 2, 0, ctx, encoder_group);
}

// ---- Selection dialog ----

static void select_close(SelectCtx * ctx, int idx, bool cancelled) {
    crosspad_gui::ui_back_remove(ctx->back_token);
    ctx->back_token = 0;

    ctx->selected = idx;
    if (ctx->cb) ctx->cb(idx, cancelled, ctx->user_data);

    lv_indev_t * nav = crosspad_gui::getGuiPlatform().getNavigationEncoder();
    if (ctx->prev_group && nav) {
        lv_indev_restore_group(nav, ctx->prev_group);
    }
    if (ctx->modal_group) {
        lv_group_del(ctx->modal_group);
    }

    lv_obj_t * overlay = ctx->overlay;
    delete ctx;
    lv_obj_delete(overlay);
}

static bool select_back_cb(void * user_data) {
    select_close((SelectCtx *)user_data, -1, true);
    return true;
}

static void select_item_cb(lv_event_t * e) {
    SelectCtx * ctx = (SelectCtx *)lv_event_get_user_data(e);
    lv_obj_t * btn = (lv_obj_t *)lv_event_get_target(e);

    // The option's own index: the panel's children also hold the title.
    select_close(ctx, (int)(intptr_t)lv_obj_get_user_data(btn), false);
}

static void select_cancel_cb(lv_event_t * e) {
    select_close((SelectCtx *)lv_event_get_user_data(e), -1, true);
}

lv_obj_t * ui_modal_selection(
    const char * title,
    const char * options[],
    int current_selection,
    modal_select_cb_t callback,
    void * user_data,
    lv_group_t * encoder_group
) {
    // Overlay
    lv_obj_t * overlay = lv_obj_create(crosspad_gui::getOverlayParent());
    lv_obj_set_size(overlay, CP_LCD_HOR_RES, CP_LCD_VER_RES);
    lv_obj_set_style_bg_color(overlay, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(overlay, LV_OPA_70, 0);
    lv_obj_set_style_border_width(overlay, 0, 0);
    lv_obj_set_style_radius(overlay, 0, 0);
    lv_obj_set_style_pad_all(overlay, 0, 0);
    lv_obj_center(overlay);

    // Panel
    lv_obj_t * panel = lv_obj_create(overlay);
    lv_obj_set_size(panel, 260, LV_SIZE_CONTENT);
    lv_obj_set_style_max_height(panel, 200, 0);
    lv_obj_center(panel);
    lv_obj_set_style_bg_color(panel, lv_color_make(30, 30, 30), 0);
    lv_obj_set_style_border_color(panel, CROSSPAD_THEME_PRIMARY, 0);
    lv_obj_set_style_border_width(panel, 2, 0);
    lv_obj_set_style_radius(panel, 12, 0);
    lv_obj_set_style_pad_all(panel, 10, 0);
    lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(panel, 4, 0);

    SelectCtx * ctx = new SelectCtx();
    ctx->cb = callback;
    ctx->user_data = user_data;
    ctx->overlay = overlay;
    ctx->prev_group = encoder_group;
    ctx->selected = current_selection;
    ctx->back_token = crosspad_gui::ui_back_push(select_back_cb, ctx);

    lv_indev_t * nav2 = crosspad_gui::getGuiPlatform().getNavigationEncoder();
    ctx->modal_group = lv_group_create();
    if (nav2) lv_indev_set_group(nav2, ctx->modal_group);

    // Title
    if (title) {
        lv_obj_t * lbl_title = lv_label_create(panel);
        lv_label_set_text(lbl_title, title);
        lv_obj_set_style_text_color(lbl_title, CROSSPAD_THEME_PRIMARY, 0);
        lv_obj_set_style_text_font(lbl_title, &lv_font_montserrat_14, 0);
    }

    // Option buttons
    lv_obj_t * focus_obj = NULL;
    for (int i = 0; options[i] != NULL; i++) {
        lv_obj_t * btn = lv_button_create(panel);
        lv_obj_set_user_data(btn, (void *)(intptr_t)i);
        lv_obj_set_size(btn, LV_PCT(100), 30);
        lv_obj_set_style_radius(btn, 6, 0);
        lv_obj_set_style_pad_hor(btn, 8, 0);

        if (i == current_selection) {
            lv_obj_set_style_bg_color(btn, CROSSPAD_THEME_PRIMARY, 0);
            lv_obj_set_style_outline_width(btn, 2, LV_STATE_FOCUSED);
            lv_obj_set_style_outline_color(btn, lv_color_white(), LV_STATE_FOCUSED);
        } else {
            lv_obj_set_style_bg_color(btn, lv_color_make(50, 50, 50), 0);
            lv_obj_set_style_outline_width(btn, 2, LV_STATE_FOCUSED);
            lv_obj_set_style_outline_color(btn, CROSSPAD_THEME_PRIMARY, LV_STATE_FOCUSED);
        }

        lv_obj_t * lbl = lv_label_create(btn);
        lv_label_set_text(lbl, options[i]);
        lv_obj_set_style_text_color(lbl, (i == current_selection) ? lv_color_black() : lv_color_white(), 0);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
        lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 0, 0);

        lv_obj_add_event_cb(btn, select_item_cb, LV_EVENT_CLICKED, ctx);
        lv_group_add_obj(ctx->modal_group, btn);

        if (i == current_selection) focus_obj = btn;
    }

    // Cancel button
    lv_obj_t * btn_cancel = lv_button_create(panel);
    lv_obj_set_size(btn_cancel, LV_PCT(100), 30);
    lv_obj_set_style_bg_color(btn_cancel, lv_color_make(60, 60, 60), 0);
    lv_obj_set_style_radius(btn_cancel, 6, 0);
    lv_obj_t * lbl_cancel = lv_label_create(btn_cancel);
    lv_label_set_text(lbl_cancel, "Cancel");
    lv_obj_set_style_text_color(lbl_cancel, lv_color_white(), 0);
    lv_obj_set_style_text_font(lbl_cancel, &lv_font_montserrat_14, 0);
    lv_obj_center(lbl_cancel);
    lv_obj_add_event_cb(btn_cancel, select_cancel_cb, LV_EVENT_CLICKED, ctx);
    lv_group_add_obj(ctx->modal_group, btn_cancel);

    if (focus_obj) lv_group_focus_obj(focus_obj);

    return overlay;
}

#endif // USE_LVGL
