#ifdef USE_LVGL

#include "crosspad-gui/components/app_launcher.h"
#include "crosspad-gui/components/ui_modal.h"
#include "crosspad-gui/platform/IGuiPlatform.h"
#include "crosspad-gui/styles/styles.h"
#include "crosspad-gui/theme/crosspad_theme.h"
#include "crosspad-gui/widgets/gui_helpers.h"
#include "crosspad/app/AppRegistry.hpp"
#include <cstring>
#include <cstdio>

namespace crosspad_gui {

// ── Internal state for callbacks ────────────────────────────────────────────

// Stored config copy for the current launcher (set by launcher_create_button_grid).
// Copied by value to avoid dangling pointer if caller passes a stack-local config.
static LauncherConfig s_config = {};

// ── Power OFF dialog ────────────────────────────────────────────────────────

// Context for the Cancel button: the dialog is an app, closed by its host.
// One dialog at a time, so one context.
struct PopupCtx {
    void* app_ptr;
    launcher_close_popup_cb_t on_close;
};
static PopupCtx s_popup = {};

// Encoder group state for the active power off dialog
static lv_group_t* s_powerOffGroup = nullptr;
static lv_group_t* s_powerOffPrevGroup = nullptr;

static void RestorePowerOffEncoder() {
    lv_indev_t* nav = getGuiPlatform().getNavigationEncoder();
    if (nav && s_powerOffPrevGroup) {
        lv_indev_restore_group(nav, s_powerOffPrevGroup);
    }
    if (s_powerOffGroup) {
        lv_group_delete(s_powerOffGroup);
        s_powerOffGroup = nullptr;
    }
    s_powerOffPrevGroup = nullptr;
}

static void ClosePowerOffDialogEvent(lv_event_t* e) {
    if (lv_event_get_code(e) == LV_EVENT_DELETE) {
        RestorePowerOffEncoder();
    }
}

static void ClosePopUpApp(lv_event_t* e) {
    LV_LOG_INFO("Power off cancelled");
    if (s_popup.on_close) {
        s_popup.on_close(s_popup.app_ptr);   // the host deletes the dialog with the app
    } else {
        lv_obj_delete_async((lv_obj_t*)lv_event_get_user_data(e));
    }
}

lv_obj_t* launcher_create_power_off_dialog(
    lv_obj_t* parent,
    void* app_ptr,
    launcher_close_popup_cb_t on_close)
{
    s_popup = PopupCtx{app_ptr, on_close};
    lv_obj_t* row = nullptr;
    lv_obj_t* message = nullptr;
    lv_obj_t* dialog = ui_modal_frame(parent, "Power off?",
                                      "Hold the power button to start the CrossPad again.",
                                      &row, &message);
    lv_obj_t* cancel = ui_modal_button(row, "Cancel", UI_MODAL_BUTTON_NORMAL);
    lv_obj_t* off = ui_modal_button(row, LV_SYMBOL_POWER "  Power off", UI_MODAL_BUTTON_DANGER);
    lv_obj_add_event_cb(cancel, ClosePopUpApp, LV_EVENT_CLICKED, dialog);

    /* The buttons make way for what is happening; hidden, not deleted: this
     * runs in one of them. */
    lv_obj_add_event_cb(off, [](lv_event_t* e) {
        LV_LOG_INFO("Powering off...");
        lv_obj_t* msg = (lv_obj_t*)lv_event_get_user_data(e);
        lv_label_set_text(msg, "Powering off ...");
        lv_obj_add_flag(lv_obj_get_parent(lv_event_get_target_obj(e)), LV_OBJ_FLAG_HIDDEN);

        lv_async_call([](void*) {
            auto& platform = getGuiPlatform();
            platform.sendPowerOff();
            platform.delayMs(200);
            LV_LOG_WARN("Powering off...");
            platform.sendPowerOff();
            platform.delayMs(500);
            platform.sendPowerOff();
        }, nullptr);
    }, LV_EVENT_CLICKED, message);

    // Route encoder to the buttons, Cancel first; restore previous group on delete
    lv_indev_t* nav = getGuiPlatform().getNavigationEncoder();
    if (nav) {
        s_powerOffPrevGroup = lv_indev_get_group(nav);
        s_powerOffGroup = lv_group_create();
        lv_group_add_obj(s_powerOffGroup, cancel);
        lv_group_add_obj(s_powerOffGroup, off);
        lv_group_focus_obj(cancel);
        lv_indev_set_group(nav, s_powerOffGroup);
        lv_obj_add_event_cb(dialog, ClosePowerOffDialogEvent, LV_EVENT_DELETE, nullptr);
    }

    return dialog;
}

// ── Power OFF app registration ──────────────────────────────────────────────

// Internal: LVGL factory for Power OFF dialog, adapts to launcher_create_power_off_dialog
static lv_obj_t* PowerOffFactory(lv_obj_t* parent, App* a) {
    // We use on_close from the stored config if available
    launcher_close_popup_cb_t on_close = s_config.on_close;
    return launcher_create_power_off_dialog(parent, (void*)a, on_close);
}

void launcher_register_power_off() {
    auto& registry = crosspad::AppRegistry::getInstance();

    // Build icon path using platform asset prefix
    static char icon_path[64];
    snprintf(icon_path, sizeof(icon_path), "%spower.png",
             getGuiPlatform().assetPathPrefix());

    registry.registerApp({
        "Power OFF",
        icon_path,
        PowerOffFactory,
        nullptr,  // no destroy
        -100,     // keep the destructive Power OFF action at the end of the launcher
        nullptr,
        true      // power.png is a black glyph
    });
}

// ── Button grid ─────────────────────────────────────────────────────────────

// LVGL callback for app button click — forwards to config.on_select
static void button_click_cb(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;

    void* app_ptr = lv_event_get_user_data(e);
    if (!app_ptr || !s_config.on_select) return;

    // Walk up to find the app container (btn_wrap's parent)
    lv_obj_t* btn = (lv_obj_t*)lv_event_get_current_target(e);
    lv_obj_t* btn_wrap = lv_obj_get_parent(btn);
    lv_obj_t* app_container = lv_obj_get_parent(btn_wrap);

    s_config.on_select(app_ptr, app_container);
}

lv_obj_t* launcher_create_button_grid(
    lv_obj_t* parent,
    const LauncherAppInfo* apps,
    size_t app_count,
    const LauncherConfig* config)
{
    s_config = *config;  // copy by value — safe even if caller passes a stack-local config

    // Create button wrapper container
    lv_obj_t* btn_wrap = lv_obj_create(parent);
    lv_obj_set_size(btn_wrap, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_opa(btn_wrap, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(btn_wrap, 0, 0);
    lv_obj_set_style_pad_all(btn_wrap, 0, 0);
    // All tiles fit on one screen (small button_size from the platform) —
    // scrolling off keeps encoder focus from dragging the grid around.
    lv_obj_set_scrollbar_mode(btn_wrap, LV_SCROLLBAR_MODE_OFF);
    lv_obj_remove_flag(btn_wrap, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(btn_wrap, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(btn_wrap, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(btn_wrap, s_config.button_spacing, 0);
    lv_obj_set_style_pad_column(btn_wrap, s_config.button_spacing, 0);

    for (size_t i = 0; i < app_count; i++) {
        const auto& info = apps[i];
        if (!info.app_ptr) continue;

        lv_obj_t* btn = lv_button_create(btn_wrap);
        if (!btn) continue;

        if (s_config.encoder_group) {
            lv_group_add_obj(s_config.encoder_group, btn);
        }
        lv_obj_add_style(btn, &styleAppPicker, 0);
        lv_obj_add_style(btn, &styleAppPickerPressed, LV_STATE_PRESSED);
        lv_obj_add_style(btn, &styleAppPickerInactive, LV_STATE_DISABLED);

        lv_obj_t* img = lv_image_create(btn);
        if (img) {
            lv_image_set_src(img, info.icon);
            lv_image_set_inner_align(img, LV_IMAGE_ALIGN_CONTAIN);
            lv_obj_set_size(img, s_config.icon_size, s_config.icon_size);
            if (info.recolor_icon) {
                lv_obj_set_style_image_recolor(img, lv_color_bw_from_color(CROSSPAD_THEME_CARD_BG), 0);
                lv_obj_set_style_image_recolor_opa(img, LV_OPA_COVER, 0);
            }
            lv_obj_center(img);
        }

        lv_obj_set_size(btn, s_config.button_size, s_config.button_size);
        lv_obj_add_flag(btn, LV_OBJ_FLAG_EVENT_BUBBLE);
        lv_obj_add_event_cb(btn, button_click_cb, LV_EVENT_CLICKED, info.app_ptr);

        if (s_config.show_names) {
            lv_obj_t* label = lv_label_create(btn);
            lv_obj_set_style_text_font(label, &lv_font_montserrat_10, 0);
            lv_label_set_text(label, info.name);
            lv_obj_align(label, LV_ALIGN_BOTTOM_MID, 0, 5);
        }
    }

    return btn_wrap;
}

} // namespace crosspad_gui

#endif // USE_LVGL
