#ifdef USE_LVGL

#include "crosspad-gui/components/ui_modal.h"
#include "crosspad-gui/widgets/gui_helpers.h"
#include "crosspad-gui/theme/crosspad_theme.h"
#include "crosspad-gui/platform/IGuiPlatform.h"
#include "crosspad-gui/components/power_gesture.h"

// ---- Internal types ----

struct ConfirmCtx {
    modal_confirm_cb_t cb;
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

// ---- Confirm dialog ----

/* One teardown for every way this dialog can close: a button, or the power
 * button's contextual back (which answers "no", like the close button). */
static void confirm_close(ConfirmCtx * ctx, bool confirmed) {
    crosspad_gui::ui_back_remove(ctx->back_token);
    ctx->back_token = 0;

    if (ctx->cb) ctx->cb(confirmed, ctx->user_data);

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

static bool confirm_back_cb(void * user_data) {
    confirm_close((ConfirmCtx *)user_data, false);
    return true;
}

static void confirm_btn_cb(lv_event_t * e) {
    ConfirmCtx * ctx = (ConfirmCtx *)lv_event_get_user_data(e);
    lv_obj_t * btn = (lv_obj_t *)lv_event_get_target(e);

    lv_obj_t * btn_row = lv_obj_get_parent(btn);
    bool confirmed = (lv_obj_get_child(btn_row, 0) == btn);

    confirm_close(ctx, confirmed);
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
    // Semi-transparent overlay
    lv_obj_t * overlay = lv_obj_create(crosspad_gui::getOverlayParent());
    lv_obj_set_size(overlay, CP_LCD_HOR_RES, CP_LCD_VER_RES);
    lv_obj_set_style_bg_color(overlay, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(overlay, LV_OPA_70, 0);
    lv_obj_set_style_border_width(overlay, 0, 0);
    lv_obj_set_style_radius(overlay, 0, 0);
    lv_obj_set_style_pad_all(overlay, 0, 0);
    lv_obj_center(overlay);

    // Dialog panel
    lv_obj_t * panel = lv_obj_create(overlay);
    lv_obj_set_size(panel, 280, LV_SIZE_CONTENT);
    lv_obj_center(panel);
    lv_obj_set_style_bg_color(panel, lv_color_make(30, 30, 30), 0);
    lv_obj_set_style_border_color(panel, CROSSPAD_THEME_PRIMARY, 0);
    lv_obj_set_style_border_width(panel, 2, 0);
    lv_obj_set_style_radius(panel, 12, 0);
    lv_obj_set_style_pad_all(panel, 12, 0);
    lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(panel, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(panel, 8, 0);

    // Title
    if (title) {
        lv_obj_t * lbl_title = lv_label_create(panel);
        lv_label_set_text(lbl_title, title);
        lv_obj_set_style_text_color(lbl_title, CROSSPAD_THEME_PRIMARY, 0);
        lv_obj_set_style_text_font(lbl_title, &lv_font_montserrat_18, 0);
    }

    // Message
    if (message) {
        lv_obj_t * lbl_msg = lv_label_create(panel);
        lv_label_set_text(lbl_msg, message);
        lv_obj_set_style_text_color(lbl_msg, lv_color_white(), 0);
        lv_obj_set_style_text_font(lbl_msg, &lv_font_montserrat_14, 0);
        lv_label_set_long_mode(lbl_msg, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(lbl_msg, 250);
    }

    // Button row
    lv_obj_t * btn_row = lv_obj_create(panel);
    lv_obj_set_size(btn_row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(btn_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btn_row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(btn_row, 12, 0);
    lv_obj_set_style_bg_opa(btn_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(btn_row, 0, 0);
    lv_obj_set_style_pad_all(btn_row, 4, 0);

    // Create context
    ConfirmCtx * ctx = new ConfirmCtx();
    ctx->cb = callback;
    ctx->user_data = user_data;
    ctx->overlay = overlay;
    ctx->prev_group = encoder_group;
    ctx->back_token = crosspad_gui::ui_back_push(confirm_back_cb, ctx);

    // Modal encoder group
    lv_indev_t * nav = crosspad_gui::getGuiPlatform().getNavigationEncoder();
    ctx->modal_group = lv_group_create();
    if (nav) lv_indev_set_group(nav, ctx->modal_group);

    // Yes button
    lv_obj_t * btn_yes = lv_button_create(btn_row);
    lv_obj_set_size(btn_yes, 110, 36);
    lv_obj_set_style_bg_color(btn_yes, CROSSPAD_THEME_PRIMARY, 0);
    lv_obj_set_style_radius(btn_yes, 8, 0);
    lv_obj_t * lbl_yes = lv_label_create(btn_yes);
    lv_label_set_text(lbl_yes, yes_text);
    lv_obj_set_style_text_color(lbl_yes, lv_color_black(), 0);
    lv_obj_set_style_text_font(lbl_yes, &lv_font_montserrat_14, 0);
    lv_obj_center(lbl_yes);
    lv_obj_add_event_cb(btn_yes, confirm_btn_cb, LV_EVENT_CLICKED, ctx);
    lv_group_add_obj(ctx->modal_group, btn_yes);

    // No button
    lv_obj_t * btn_no = lv_button_create(btn_row);
    lv_obj_set_size(btn_no, 110, 36);
    lv_obj_set_style_bg_color(btn_no, lv_color_make(60, 60, 60), 0);
    lv_obj_set_style_radius(btn_no, 8, 0);
    lv_obj_t * lbl_no = lv_label_create(btn_no);
    lv_label_set_text(lbl_no, no_text);
    lv_obj_set_style_text_color(lbl_no, lv_color_white(), 0);
    lv_obj_set_style_text_font(lbl_no, &lv_font_montserrat_14, 0);
    lv_obj_center(lbl_no);
    lv_obj_add_event_cb(btn_no, confirm_btn_cb, LV_EVENT_CLICKED, ctx);
    lv_group_add_obj(ctx->modal_group, btn_no);

    lv_group_focus_obj(btn_yes);

    return overlay;
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

    lv_obj_t * list = lv_obj_get_parent(btn);
    int idx = 0;
    uint32_t count = lv_obj_get_child_count(list);
    for (uint32_t i = 0; i < count; i++) {
        if (lv_obj_get_child(list, i) == btn) {
            idx = (int)i;
            break;
        }
    }

    select_close(ctx, idx, false);
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
