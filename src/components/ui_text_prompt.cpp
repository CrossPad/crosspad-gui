#ifdef USE_LVGL

#include "crosspad-gui/components/ui_text_prompt.h"

#include <cstdio>

#include "lvgl.h"
#include "crosspad-gui/components/power_gesture.h"
#include "crosspad-gui/platform/IGuiPlatform.h"
#include "crosspad-gui/theme/crosspad_theme.h"
#include "crosspad-gui/widgets/gui_helpers.h"

namespace crosspad_gui {

namespace {

constexpr int32_t kPad = 4;
constexpr int32_t kFieldH = 32;
constexpr int32_t kEyeW = 40;
constexpr int32_t kKeyGap = 3;
constexpr int32_t kKeyRadius = 4;
constexpr int32_t kFocusRingW = 2;
constexpr size_t kTextMax = 128;   // longest entry this widget holds (a WiFi password is 64)
const lv_color_t kKey        = lv_color_make(45, 45, 45);
const lv_color_t kKeyMode    = lv_color_make(28, 28, 28);   // shift, symbols, OK, close
const lv_color_t kKeyPressed = lv_color_make(90, 90, 90);

struct PromptCtx {
    TextPromptDone done;
    void* user;
    lv_obj_t* overlay;
    lv_obj_t* field;
    lv_obj_t* eye_label;
    lv_group_t* group;
    lv_group_t* prev_group;
    uint32_t back_token;
    bool finished;
    char text[kTextMax + 1];
};

// Everything a prompt owes its caller, once: shared by finish() and by an
// overlay deleted from outside (overlay_delete_cb).
void release(PromptCtx* ctx, bool ok) {
    ctx->finished = true;
    ui_back_remove(ctx->back_token);
    // Copied before the overlay goes: the field dies with it.
    snprintf(ctx->text, sizeof(ctx->text), "%s", ok ? lv_textarea_get_text(ctx->field) : "");
    lv_indev_t* nav = getGuiPlatform().getNavigationEncoder();
    if (nav) lv_indev_restore_group(nav, ctx->prev_group);
    lv_group_delete_deferred(ctx->group);
    ctx->done(ok, ctx->text, ctx->user);
}

void finish(PromptCtx* ctx, bool ok) {
    if (ctx->finished) return;   // READY and a back can both arrive in one frame
    release(ctx, ok);
    lv_obj_delete_async(ctx->overlay);   // ctx is freed by the overlay's LV_EVENT_DELETE
}

void keyboard_event_cb(lv_event_t* e) {
    auto* ctx = static_cast<PromptCtx*>(lv_event_get_user_data(e));
    finish(ctx, lv_event_get_code(e) == LV_EVENT_READY);
}

bool back_cb(void* user) {
    finish(static_cast<PromptCtx*>(user), false);
    return true;
}

void eye_clicked_cb(lv_event_t* e) {
    auto* ctx = static_cast<PromptCtx*>(lv_event_get_user_data(e));
    const bool hidden = lv_textarea_get_password_mode(ctx->field);
    lv_textarea_set_password_mode(ctx->field, !hidden);
    lv_label_set_text(ctx->eye_label, hidden ? LV_SYMBOL_EYE_CLOSE : LV_SYMBOL_EYE_OPEN);
}

void overlay_delete_cb(lv_event_t* e) {
    auto* ctx = static_cast<PromptCtx*>(lv_event_get_user_data(e));
    // Deleted from outside (an lv_obj_clean of the parent, a screen rebuild)
    // before finish() ran: the caller still gets its one "cancelled", and the
    // object is already going, so no second delete.
    if (!ctx->finished) release(ctx, false);
    delete ctx;
}

// The theme's key look animates every press into a full-object redraw; plain
// styles repaint one key.
void style_keyboard(lv_obj_t* kb) {
    lv_obj_remove_style_all(kb);
    lv_obj_set_style_bg_color(kb, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(kb, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(kb, kKeyGap, 0);
    lv_obj_set_style_pad_gap(kb, kKeyGap, 0);
    lv_obj_set_style_bg_color(kb, kKey, LV_PART_ITEMS);
    lv_obj_set_style_bg_opa(kb, LV_OPA_COVER, LV_PART_ITEMS);
    lv_obj_set_style_radius(kb, kKeyRadius, LV_PART_ITEMS);
    lv_obj_set_style_text_color(kb, lv_color_white(), LV_PART_ITEMS);
    lv_obj_set_style_text_font(kb, &lv_font_montserrat_14, LV_PART_ITEMS);
    lv_obj_set_style_bg_color(kb, kKeyMode, LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(kb, kKeyPressed, LV_PART_ITEMS | LV_STATE_PRESSED);
    // The key under the encoder: a ring, as on the modal's buttons.
    lv_obj_set_style_outline_color(kb, CROSSPAD_THEME_PRIMARY, LV_PART_ITEMS | LV_STATE_FOCUSED);
    lv_obj_set_style_outline_width(kb, kFocusRingW, LV_PART_ITEMS | LV_STATE_FOCUSED);
}

} // namespace

void ui_text_prompt(const char* title, const char* initial, uint32_t flags,
                    size_t max_len, TextPromptDone done, void* user) {
    auto* ctx = new PromptCtx{};
    ctx->done = done;
    ctx->user = user;
    const bool password = (flags & UI_TEXT_PASSWORD) != 0;

    lv_obj_t* overlay = lv_obj_create(getOverlayParent());
    lv_obj_remove_style_all(overlay);
    lv_obj_set_size(overlay, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(overlay, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(overlay, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(overlay, kPad, 0);
    lv_obj_set_style_pad_row(overlay, kPad, 0);
    lv_obj_set_flex_flow(overlay, LV_FLEX_FLOW_COLUMN);
    lv_obj_add_flag(overlay, LV_OBJ_FLAG_CLICKABLE);   // touches stop here; nothing under it reacts
    lv_obj_remove_flag(overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(overlay, overlay_delete_cb, LV_EVENT_DELETE, ctx);
    ctx->overlay = overlay;

    lv_obj_t* label = lv_label_create(overlay);
    lv_label_set_text(label, title ? title : "");
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    lv_obj_set_width(label, LV_PCT(100));
    lv_obj_set_style_text_font(label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(label, lv_color_white(), 0);

    lv_obj_t* row = lv_obj_create(overlay);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_PCT(100), kFieldH);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(row, kPad, 0);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* field = lv_textarea_create(row);
    lv_textarea_set_one_line(field, true);
    lv_textarea_set_max_length(field, (uint32_t)(max_len < kTextMax ? max_len : kTextMax));
    lv_textarea_set_password_mode(field, password);
    lv_textarea_set_text(field, initial ? initial : "");
    lv_obj_set_height(field, kFieldH);
    lv_obj_set_flex_grow(field, 1);
    ctx->field = field;

    lv_obj_t* eye = nullptr;
    if (password) {
        eye = lv_button_create(row);
        lv_obj_set_size(eye, kEyeW, kFieldH);
        lv_obj_add_event_cb(eye, eye_clicked_cb, LV_EVENT_CLICKED, ctx);
        ctx->eye_label = lv_label_create(eye);
        lv_label_set_text(ctx->eye_label, LV_SYMBOL_EYE_OPEN);
        lv_obj_center(ctx->eye_label);
    }

    lv_obj_t* kb = lv_keyboard_create(overlay);
    style_keyboard(kb);
    lv_obj_set_width(kb, LV_PCT(100));
    lv_obj_set_flex_grow(kb, 1);
    lv_keyboard_set_mode(kb, LV_KEYBOARD_MODE_TEXT_LOWER);
    lv_keyboard_set_textarea(kb, field);
    // lv_keyboard's own handler sends READY for its OK key, CANCEL for its close key.
    lv_obj_add_event_cb(kb, keyboard_event_cb, LV_EVENT_READY, ctx);
    lv_obj_add_event_cb(kb, keyboard_event_cb, LV_EVENT_CANCEL, ctx);

    lv_indev_t* nav = getGuiPlatform().getNavigationEncoder();
    ctx->prev_group = nav ? lv_indev_get_group(nav) : nullptr;
    ctx->group = lv_group_create();
    if (eye) lv_group_add_obj(ctx->group, eye);
    lv_group_add_obj(ctx->group, kb);
    lv_group_focus_obj(kb);
    lv_group_set_editing(ctx->group, true);   // rotation walks the keys, a press presses
    if (nav) lv_indev_set_group(nav, ctx->group);
    ctx->back_token = ui_back_push(back_cb, ctx);
}

} // namespace crosspad_gui

#endif // USE_LVGL
