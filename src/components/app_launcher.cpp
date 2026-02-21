#ifdef USE_LVGL

#include "crosspad-gui/components/app_launcher.h"
#include "crosspad-gui/platform/IGuiPlatform.h"
#include "crosspad-gui/styles/styles.h"
#include "crosspad/app/AppRegistry.hpp"
#include <cstring>
#include <cstdio>

namespace crosspad_gui {

// ── Internal state for callbacks ────────────────────────────────────────────

// Stored config copy for the current launcher (set by launcher_create_button_grid).
// Copied by value to avoid dangling pointer if caller passes a stack-local config.
static LauncherConfig s_config = {};

// ── Power OFF dialog ────────────────────────────────────────────────────────

// Context for ClosePopUpApp callback
struct PopupCtx {
    void* app_ptr;
    launcher_close_popup_cb_t on_close;
};

static void ClosePopUpApp(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;

    LV_LOG_INFO("Power off cancelled");
    auto* ctx = (PopupCtx*)lv_event_get_user_data(e);
    if (ctx && ctx->on_close) {
        ctx->on_close(ctx->app_ptr);
    }
    lv_obj_t* btn = (lv_obj_t*)lv_event_get_current_target(e);
    lv_obj_t* mbox = lv_obj_get_parent(lv_obj_get_parent(btn));
    lv_msgbox_close(mbox);
    // Note: ctx is leaked (small, one-shot allocation per dialog)
}

lv_obj_t* launcher_create_power_off_dialog(
    lv_obj_t* parent,
    void* app_ptr,
    launcher_close_popup_cb_t on_close)
{
    lv_obj_t* msgbox = lv_msgbox_create(parent);
    lv_obj_t* closeBtn = lv_msgbox_add_close_button(msgbox);
    lv_obj_remove_event(closeBtn, 0);  // remove default close event
    lv_msgbox_add_title(msgbox, "Power OFF");
    lv_msgbox_add_text(msgbox, "Are you sure you want to power off?");
    lv_obj_t* yes = lv_msgbox_add_footer_button(msgbox, "Yes");
    lv_obj_t* no  = lv_msgbox_add_footer_button(msgbox, "No");
    lv_obj_add_style(yes, &styleButton, 0);
    lv_obj_add_style(no, &styleButton, 0);

    // "Yes" sends power off via platform
    lv_obj_add_event_cb(yes, [](lv_event_t* e) {
        if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;

        LV_LOG_INFO("Powering off...");
        lv_obj_t* obj = (lv_obj_t*)lv_event_get_target(e);
        lv_obj_t* footer = lv_obj_get_parent(obj);
        lv_obj_clean(footer);
        lv_obj_t* label = lv_label_create(footer);
        lv_label_set_text(label, "Powering off...");
        lv_obj_align(label, LV_ALIGN_CENTER, 0, 0);

        lv_async_call([](void*) {
            auto& platform = getGuiPlatform();
            platform.sendPowerOff();
            platform.delayMs(200);
            LV_LOG_WARN("Powering off...");
            platform.sendPowerOff();
            platform.delayMs(500);
            platform.sendPowerOff();
        }, nullptr);
    }, LV_EVENT_CLICKED, msgbox);

    // "No" and close button dismiss
    auto* ctx = new PopupCtx{app_ptr, on_close};
    lv_obj_add_event_cb(no, ClosePopUpApp, LV_EVENT_CLICKED, ctx);
    lv_obj_add_event_cb(closeBtn, ClosePopUpApp, LV_EVENT_CLICKED, ctx);

    return msgbox;
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
        nullptr,  // no app logic
        nullptr,  // no simple GUI
        nullptr,  // no refresh
        nullptr,  // no cleanup
        100       // high priority -> last on app list
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
    lv_obj_set_scrollbar_mode(btn_wrap, LV_SCROLLBAR_MODE_AUTO);
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
