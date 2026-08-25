#ifdef USE_LVGL

#include "crosspad-gui/components/app_launcher.h"
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

// Context for ClosePopUpApp callback
struct PopupCtx {
    void* app_ptr;
    launcher_close_popup_cb_t on_close;
};

// Encoder group state for the active power off dialog
static lv_group_t* s_powerOffGroup = nullptr;
static lv_group_t* s_powerOffPrevGroup = nullptr;

static void RestorePowerOffEncoder() {
    lv_indev_t* nav = getGuiPlatform().getNavigationEncoder();
    if (nav && s_powerOffPrevGroup) {
        lv_indev_set_group(nav, s_powerOffPrevGroup);
    }
    if (s_powerOffGroup) {
        lv_group_delete(s_powerOffGroup);
        s_powerOffGroup = nullptr;
    }
    s_powerOffPrevGroup = nullptr;
}

static void ClosePowerOffMsgboxEvent(lv_event_t* e) {
    if (lv_event_get_code(e) == LV_EVENT_DELETE) {
        RestorePowerOffEncoder();
    }
}

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

    // Route encoder to msgbox buttons; restore previous group on delete
    lv_indev_t* nav = getGuiPlatform().getNavigationEncoder();
    if (nav) {
        s_powerOffPrevGroup = lv_indev_get_group(nav);
        s_powerOffGroup = lv_group_create();
        lv_group_add_obj(s_powerOffGroup, no);
        lv_group_add_obj(s_powerOffGroup, yes);
        lv_group_add_obj(s_powerOffGroup, closeBtn);
        lv_group_focus_obj(no);
        lv_indev_set_group(nav, s_powerOffGroup);
        lv_obj_add_event_cb(msgbox, ClosePowerOffMsgboxEvent, LV_EVENT_DELETE, nullptr);
    }

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
        -100      // keep the destructive Power OFF action at the end of the launcher
    });
}

// ── Button grid ─────────────────────────────────────────────────────────────

// LVGL callback for app button click — forwards to config.on_select
static void button_click_cb(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;

    void* app_ptr = lv_event_get_user_data(e);
    if (!app_ptr || !s_config.on_select) return;

    /* row -> list -> body -> app container: the launcher root is the two-column
     * body, so the row sits three levels below the container the app must
     * build into. */
    lv_obj_t* row  = (lv_obj_t*)lv_event_get_current_target(e);
    lv_obj_t* list = lv_obj_get_parent(row);
    lv_obj_t* body = lv_obj_get_parent(list);
    lv_obj_t* app_container = lv_obj_get_parent(body);

    s_config.on_select(app_ptr, app_container);
}

/* The launcher borrows the kit browser's layout wholesale: a preview panel on
 * the left, a scrollable list on the right, same palette and same row geometry.
 * Two screens that do the same job — pick one of many things — now look like
 * one device rather than two designs.
 *
 * The palette is the browser's, not CROSSPAD_THEME_*, for exactly that reason.
 */
namespace {

constexpr int LR_COVER     = 122;  ///< Preview panel, square, as in the browser
constexpr int LR_PREV_ICON = 76;   ///< Icon inside it — app icons are line art,
                                   ///< so filling the panel would look coarse
constexpr int LR_THUMB     = 20;
constexpr int LR_ROW_H     = 30;
constexpr int LR_MAX_ROWS  = 24;

#define LR_PRIMARY    lv_color_hex(0x00FF88)
#define LR_BG_DARK    lv_color_hex(0x0B0B18)
#define LR_CARD_BG    lv_color_hex(0x16162E)
#define LR_TEXT       lv_color_hex(0xE6E4F0)
#define LR_ON_PRIMARY lv_color_hex(0x001A0E)

lv_obj_t* s_rows[LR_MAX_ROWS];
lv_obj_t* s_rowIcon[LR_MAX_ROWS];
lv_obj_t* s_rowName[LR_MAX_ROWS];
/* Hidden carrier for the optional subtitle: LVGL owns the string, so nothing
 * from the caller's descriptor array has to stay alive. Null when the app has
 * no subtitle. Hidden children are skipped by the flex layout. */
lv_obj_t* s_rowDesc[LR_MAX_ROWS];
size_t    s_row_count = 0;
uint16_t  s_sel       = 0;

/* lv_group_add_obj() fires LV_EVENT_FOCUSED synchronously for the first object
 * it receives, which lands here while the row is still half-built. Selection is
 * applied explicitly once the list is complete. */
bool s_building = false;

lv_obj_t* s_prevIcon = nullptr;
lv_obj_t* s_prevName = nullptr;
lv_obj_t* s_prevDesc = nullptr;

lv_style_t st_row, st_row_sel, st_cover;
bool s_styles_ready = false;

void lr_styles_init() {
    if (s_styles_ready) return;
    s_styles_ready = true;

    lv_style_init(&st_row);
    lv_style_set_bg_color(&st_row, LR_CARD_BG);
    lv_style_set_bg_opa(&st_row, LV_OPA_COVER);
    lv_style_set_radius(&st_row, 9);
    lv_style_set_border_width(&st_row, 0);
    lv_style_set_pad_hor(&st_row, 7);
    lv_style_set_pad_ver(&st_row, 0);
    lv_style_set_pad_column(&st_row, 7);

    lv_style_init(&st_row_sel);
    lv_style_set_bg_color(&st_row_sel, LR_PRIMARY);

    lv_style_init(&st_cover);
    lv_style_set_radius(&st_cover, 14);
    lv_style_set_clip_corner(&st_cover, true);
    lv_style_set_bg_color(&st_cover, LR_CARD_BG);
    lv_style_set_bg_opa(&st_cover, LV_OPA_COVER);
}

void lr_select(size_t i) {
    if (s_building || i >= s_row_count || !s_rows[i]) return;

    if (s_sel < s_row_count && s_rows[s_sel]) {
        lv_obj_remove_style(s_rows[s_sel], &st_row_sel, 0);
        lv_obj_set_style_text_color(s_rowName[s_sel], LR_TEXT, 0);
        lv_obj_set_style_image_recolor(s_rowIcon[s_sel], LR_TEXT, 0);
    }

    s_sel = (uint16_t)i;

    lv_obj_add_style(s_rows[s_sel], &st_row_sel, 0);
    lv_obj_set_style_text_color(s_rowName[s_sel], LR_ON_PRIMARY, 0);
    lv_obj_set_style_image_recolor(s_rowIcon[s_sel], LR_ON_PRIMARY, 0);

    /* The row's own image owns a copy of the path (lv_image_set_src strdups),
     * so the preview can reuse it without the caller's string staying alive. */
    const void* src = lv_image_get_src(s_rowIcon[s_sel]);
    if (src && s_prevIcon) lv_image_set_src(s_prevIcon, src);

    if (s_prevName) lv_label_set_text(s_prevName, lv_label_get_text(s_rowName[s_sel]));
    if (s_prevDesc) {
        lv_label_set_text(s_prevDesc,
            s_rowDesc[s_sel] ? lv_label_get_text(s_rowDesc[s_sel]) : "");
    }

    lv_obj_scroll_to_view(s_rows[s_sel], LV_ANIM_OFF);
}

void lr_focus_cb(lv_event_t* e) {
    lr_select((size_t)(uintptr_t)lv_event_get_user_data(e));
}

} // namespace

lv_obj_t* launcher_create_button_grid(
    lv_obj_t* parent,
    const LauncherAppInfo* apps,
    size_t app_count,
    const LauncherConfig* config)
{
    s_config = *config;  // copy by value — safe even if caller passes a stack-local config
    lr_styles_init();

    lv_obj_t* body = lv_obj_create(parent);
    lv_obj_remove_style_all(body);
    lv_obj_set_size(body, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(body, LR_BG_DARK, 0);
    lv_obj_set_style_bg_opa(body, LV_OPA_COVER, 0);
    lv_obj_remove_flag(body, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(body, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_all(body, 10, 0);
    lv_obj_set_style_pad_column(body, 10, 0);

    // ── Left: preview panel ─────────────────────────────────────────────
    lv_obj_t* left = lv_obj_create(body);
    lv_obj_remove_style_all(left);
    lv_obj_set_size(left, LR_COVER, LV_PCT(100));
    lv_obj_set_flex_flow(left, LV_FLEX_FLOW_COLUMN);
    /* Panel plus its labels are shorter than the body: centre the stack on the
     * main axis, keep the cross axis at START so the captions line up with the
     * panel's left edge. */
    lv_obj_set_flex_align(left, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(left, 7, 0);
    lv_obj_remove_flag(left, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* coverBox = lv_obj_create(left);
    lv_obj_remove_style_all(coverBox);
    lv_obj_add_style(coverBox, &st_cover, 0);
    lv_obj_set_size(coverBox, LR_COVER, LR_COVER);
    lv_obj_remove_flag(coverBox, LV_OBJ_FLAG_SCROLLABLE);

    s_prevIcon = lv_image_create(coverBox);
    lv_image_set_inner_align(s_prevIcon, LV_IMAGE_ALIGN_CONTAIN);
    lv_obj_set_size(s_prevIcon, LR_PREV_ICON, LR_PREV_ICON);
    lv_obj_set_style_image_recolor_opa(s_prevIcon, LV_OPA_COVER, 0);
    lv_obj_set_style_image_recolor(s_prevIcon, LR_PRIMARY, 0);
    lv_obj_center(s_prevIcon);

    s_prevName = lv_label_create(left);
    lv_obj_set_style_text_font(s_prevName, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_prevName, lv_color_white(), 0);
    lv_label_set_long_mode(s_prevName, LV_LABEL_LONG_DOT);
    lv_obj_set_width(s_prevName, LR_COVER);
    lv_label_set_text(s_prevName, "");

    s_prevDesc = lv_label_create(left);
    lv_obj_set_style_text_font(s_prevDesc, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(s_prevDesc, LR_PRIMARY, 0);
    lv_label_set_long_mode(s_prevDesc, LV_LABEL_LONG_DOT);
    lv_obj_set_width(s_prevDesc, LR_COVER);
    lv_label_set_text(s_prevDesc, "");

    // ── Right: scrollable app list ──────────────────────────────────────
    lv_obj_t* list = lv_obj_create(body);
    lv_obj_remove_style_all(list);
    lv_obj_set_flex_grow(list, 1);
    lv_obj_set_height(list, LV_PCT(100));
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(list, 5, 0);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_OFF);

    s_row_count = 0;
    s_sel       = 0;
    s_building  = true;

    for (size_t i = 0; i < app_count && s_row_count < LR_MAX_ROWS; i++) {
        const auto& info = apps[i];
        if (!info.app_ptr) continue;

        const size_t idx = s_row_count;
        void* tag = (void*)(uintptr_t)idx;

        lv_obj_t* row = lv_obj_create(list);
        lv_obj_remove_style_all(row);
        lv_obj_add_style(row, &st_row, 0);
        lv_obj_set_size(row, LV_PCT(100), LR_ROW_H);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START,
                                   LV_FLEX_ALIGN_CENTER,
                                   LV_FLEX_ALIGN_CENTER);
        lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(row, button_click_cb, LV_EVENT_CLICKED, info.app_ptr);
        lv_obj_add_event_cb(row, lr_focus_cb, LV_EVENT_FOCUSED, tag);
        if (s_config.encoder_group) lv_group_add_obj(s_config.encoder_group, row);
        s_rows[idx] = row;

        lv_obj_t* img = lv_image_create(row);
        lv_image_set_src(img, info.icon);
        lv_image_set_inner_align(img, LV_IMAGE_ALIGN_CONTAIN);
        lv_obj_set_size(img, LR_THUMB, LR_THUMB);
        lv_obj_set_style_image_recolor_opa(img, LV_OPA_COVER, 0);
        lv_obj_set_style_image_recolor(img, LR_TEXT, 0);
        s_rowIcon[idx] = img;

        lv_obj_t* name = lv_label_create(row);
        lv_obj_set_flex_grow(name, 1);
        lv_obj_set_style_text_font(name, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(name, LR_TEXT, 0);
        lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
        lv_label_set_text(name, info.name ? info.name : "");
        s_rowName[idx] = name;

        /* Subtitle only when the app supplies one — inventing text here would
         * put made-up status on the screen. */
        if (info.desc && info.desc[0]) {
            lv_obj_t* desc = lv_label_create(row);
            lv_label_set_text(desc, info.desc);
            lv_obj_add_flag(desc, LV_OBJ_FLAG_HIDDEN);
            s_rowDesc[idx] = desc;
        } else {
            s_rowDesc[idx] = nullptr;
        }

        s_row_count++;
    }

    s_building = false;

    if (s_row_count > 0) {
        if (s_config.encoder_group) lv_group_focus_obj(s_rows[0]);
        /* s_sel already points at row 0, and lr_select() early-outs on a
         * no-op move, so drive the first paint through the same path with the
         * selection parked one past the end. */
        s_sel = (uint16_t)s_row_count;
        lr_select(0);
    }

    return body;
}

} // namespace crosspad_gui

#endif // USE_LVGL
