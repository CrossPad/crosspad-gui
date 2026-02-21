#ifdef USE_LVGL

#include "crosspad-gui/components/status_bar.h"
#include "crosspad-gui/styles/styles.h"
#include "crosspad-gui/widgets/gui_helpers.h"
#include "crosspad-gui/platform/IGuiPlatform.h"
#include "crosspad-gui/theme/crosspad_theme.h"

#include "crosspad/settings/CrosspadSettings.hpp"
#include "crosspad/status/CrosspadStatus.hpp"

using namespace crosspad;

#define LV_STATUS_BAR_OPENED_HEIGHT lv_obj_get_height(crosspad_gui::getOverlayParent()) * 0.85

extern CrosspadSettings * settings;
extern CrosspadStatus status;
extern lv_obj_t * status_c; // status bar container

static bool quickSettingsPanelVisible = false;

// subjects
static lv_subject_t s_loading;

// timers
static lv_timer_t * t_ramUpdate = NULL;
static lv_timer_t * t_check_status = NULL;

// objects
static lv_obj_t * openButton = NULL;
static lv_obj_t * closeButton = NULL;
static lv_obj_t * statusBarPanel = NULL;
static lv_obj_t * quickSettingsPanel = NULL;

// status icons
static lv_obj_t * status_logo;
static lv_obj_t * status_battery;
static lv_obj_t * status_charging;
static lv_obj_t * status_wifi;
static lv_obj_t * status_bluetooth;
static lv_obj_t * status_sd;
static lv_obj_t * status_loading_arc;

// animations
static lv_anim_t   anim_opening_template;
static lv_anim_t * bar_opening_anim;
static lv_anim_t * bar_closing_anim;
static lv_anim_t   anim_quick_opening_template;
static lv_anim_t * quick_opening_anim;
static lv_anim_t * quick_closing_anim;

// Asset path buffers (built once at creation time)
#ifdef PLATFORM_PC
#define ASSET_PATH_SIZE 256
#else
#define ASSET_PATH_SIZE 64
#endif
static char asset_logo[ASSET_PATH_SIZE];
static char asset_drawer_down[ASSET_PATH_SIZE];
static char asset_drawer_up[ASSET_PATH_SIZE];

static void build_asset_paths() {
    const char * prefix = crosspad_gui::getGuiPlatform().assetPathPrefix();
    snprintf(asset_logo, sizeof(asset_logo), "%sCrossPad_Logo_110w.png", prefix);
    snprintf(asset_drawer_down, sizeof(asset_drawer_down), "%sdrawerDownArrow.png", prefix);
    snprintf(asset_drawer_up, sizeof(asset_drawer_up), "%sdrawerUpArrow.png", prefix);
}

// ── Animation callbacks ─────────────────────────────────────────────────────

static void anim_y_cb(void * var, int32_t v) {
    lv_obj_set_height((lv_obj_t *)var, v);
}

// Forward declarations
static void drawer_open_event_cb(lv_event_t * e);
static void drawer_close_event_cb(lv_event_t * e);
static void drawer_open_end_event_cb(lv_anim_t * a);
static void drawer_close_end_event_cb(lv_anim_t * a);
static void quick_opening_anim_end_cb(lv_anim_t * a);
static void quick_closing_anim_end_cb(lv_anim_t * a);

static lv_obj_t * lv_CreateStatusBarTop(lv_obj_t * parent);
static lv_obj_t * lv_CreateStatusBarQuickSettings(lv_obj_t * parent);

// ── Loading API ─────────────────────────────────────────────────────────────

namespace crosspad_gui {

void statusbar_set_loading(int progress) {
    lv_subject_set_int(&s_loading, progress);
}

lv_subject_t * statusbar_get_loading_subject() {
    return &s_loading;
}

void statusbar_show_loader_message(const char * title, const char * message, bool isError) {
    lv_obj_t * mbox = lv_msgbox_create(crosspad_gui::getOverlayParent());
    lv_obj_add_style(mbox, &styleStatusBar, 0);
    lv_obj_set_style_border_width(mbox, 2, 0);
    lv_msgbox_add_title(mbox, title);
    lv_msgbox_add_text(mbox, message);
    lv_msgbox_add_close_button(mbox);
}

void statusbar_process_loader_status(uint8_t progress, bool done, bool error, const char * msg) {
    if (done) {
        statusbar_set_loading(-1);
        if (error) {
            statusbar_show_loader_message("Error", msg, true);
        }
    } else {
        statusbar_set_loading(progress);
    }
}

} // namespace crosspad_gui

// ── Battery icon helper ─────────────────────────────────────────────────────

static void PickCorrectBatteryIcon(uint8_t batteryPercent) {
    if (batteryPercent < 5) lv_obj_add_flag(status_battery, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_remove_flag(status_battery, LV_OBJ_FLAG_HIDDEN);
    if (batteryPercent < 25) lv_image_set_src(status_battery, LV_SYMBOL_BATTERY_EMPTY);
    else if (batteryPercent < 40) lv_image_set_src(status_battery, LV_SYMBOL_BATTERY_1);
    else if (batteryPercent < 55) lv_image_set_src(status_battery, LV_SYMBOL_BATTERY_2);
    else if (batteryPercent < 70) lv_image_set_src(status_battery, LV_SYMBOL_BATTERY_3);
    else lv_image_set_src(status_battery, LV_SYMBOL_BATTERY_FULL);
}

// ── Status check timer ──────────────────────────────────────────────────────

static void check_status_timer_cb(lv_timer_t * timer) {
    // SD card
    bool sd = status.stm32.sdDetected;
    static bool lastSd = false;
    if (sd != lastSd) {
        lastSd = sd;
        lv_obj_set_hidden(status_sd, !sd);
    }
    // Bluetooth
    bool bt = status.bluetoothConnected;
    static bool lastBt = false;
    if (bt != lastBt) {
        lastBt = bt;
        lv_obj_set_hidden(status_bluetooth, !bt);
    }
    // WiFi
    bool wifi = status.wifiConnected;
    static bool lastWifi = false;
    if (wifi != lastWifi) {
        lastWifi = wifi;
        lv_obj_set_hidden(status_wifi, !wifi);
    }
    // Charging
    bool charging = status.stm32.chargingStatus;
    static bool lastCharging = false;
    if (charging != lastCharging) {
        lastCharging = charging;
        lv_obj_set_hidden(status_charging, !charging);
    }
    // Battery level
    static uint8_t batteryPercent = 0;
    const int bat_hist = 2;
    if (batteryPercent > status.batteryPct + bat_hist || batteryPercent < status.batteryPct - bat_hist) {
        batteryPercent = status.batteryPct;
        PickCorrectBatteryIcon(batteryPercent);
        lv_obj_invalidate(status_battery);
    }
}

// ── Drawer open/close animations ────────────────────────────────────────────

static void drawer_open_event_cb(lv_event_t * e) {
    if (quickSettingsPanelVisible) return;
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_PRESSED) {
        lv_anim_set_values(&anim_opening_template, LV_STATUS_BAR_HEIGHT, LV_STATUS_BAR_OPENED_HEIGHT);
        lv_anim_set_completed_cb(&anim_opening_template, drawer_open_end_event_cb);
        lv_async_call([](void *) {
            lv_obj_add_flag(openButton, LV_OBJ_FLAG_HIDDEN);
        }, NULL);
        bar_opening_anim = lv_anim_start(&anim_opening_template);
    }
}

static void drawer_open_end_event_cb(lv_anim_t * a) {
    lv_obj_set_height(status_c, LV_STATUS_BAR_OPENED_HEIGHT);
    lv_anim_set_values(&anim_quick_opening_template, 0, LV_STATUS_BAR_OPENED_HEIGHT - LV_STATUS_BAR_HEIGHT - 2);
    lv_anim_set_completed_cb(&anim_quick_opening_template, quick_opening_anim_end_cb);
    quick_opening_anim = lv_anim_start(&anim_quick_opening_template);
}

static void quick_opening_anim_end_cb(lv_anim_t * a) {
    quickSettingsPanelVisible = true;
    lv_obj_set_height(quickSettingsPanel, LV_STATUS_BAR_OPENED_HEIGHT - LV_STATUS_BAR_HEIGHT - 2);
    lv_obj_remove_flag(closeButton, LV_OBJ_FLAG_HIDDEN);
    lv_timer_resume(t_ramUpdate);
    lv_timer_ready(t_ramUpdate);
}

static void drawer_close_event_cb(lv_event_t * e) {
    if (!quickSettingsPanelVisible) return;
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_PRESSED) {
        lv_async_call([](void *) {
            lv_obj_add_flag(closeButton, LV_OBJ_FLAG_HIDDEN);
        }, NULL);
        lv_anim_set_values(&anim_quick_opening_template, LV_STATUS_BAR_OPENED_HEIGHT - LV_STATUS_BAR_HEIGHT - 2, 0);
        lv_anim_set_completed_cb(&anim_quick_opening_template, quick_closing_anim_end_cb);
        quick_closing_anim = lv_anim_start(&anim_quick_opening_template);
    }
}

static void quick_closing_anim_end_cb(lv_anim_t * a) {
    lv_obj_set_height(quickSettingsPanel, 0);
    lv_anim_set_values(&anim_opening_template, LV_STATUS_BAR_OPENED_HEIGHT, LV_STATUS_BAR_HEIGHT + 2);
    lv_anim_set_completed_cb(&anim_opening_template, drawer_close_end_event_cb);
    bar_closing_anim = lv_anim_start(&anim_opening_template);
}

static void drawer_close_end_event_cb(lv_anim_t * a) {
    quickSettingsPanelVisible = false;
    lv_obj_set_height(status_c, LV_STATUS_BAR_HEIGHT);
    lv_obj_remove_flag(openButton, LV_OBJ_FLAG_HIDDEN);
}

// ── Main creation ───────────────────────────────────────────────────────────

lv_obj_t * lv_CreateStatusBar() {
    build_asset_paths();

    lv_obj_t * overlayParent = crosspad_gui::getOverlayParent();
    lv_obj_t * cont = lv_obj_create(overlayParent);
    lv_obj_set_size(cont, lv_obj_get_content_width(overlayParent), LV_STATUS_BAR_HEIGHT + 2);
    lv_obj_add_style(cont, &styleStatusBar, 0);
    lv_obj_set_style_bg_opa(cont, LV_OPA_0, 0);
    lv_obj_align(cont, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_remove_flag(cont, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(cont, LV_SCROLLBAR_MODE_OFF);

    // Status bar panel (icons row)
    statusBarPanel = lv_obj_create(cont);
    lv_obj_update_layout(cont);
    lv_obj_set_size(statusBarPanel, lv_obj_get_content_width(cont), LV_STATUS_BAR_HEIGHT);
    lv_obj_add_style(statusBarPanel, &styleStatusBar, 0);
    lv_obj_set_style_border_width(statusBarPanel, 1, 0);
    lv_obj_align(statusBarPanel, LV_ALIGN_TOP_MID, 0, 0);
    lv_CreateStatusBarTop(statusBarPanel);

    // Quick settings panel
    quickSettingsPanel = lv_obj_create(cont);
    lv_obj_set_size(quickSettingsPanel, lv_obj_get_content_width(cont), 0);
    lv_obj_add_style(quickSettingsPanel, &styleStatusBar, 0);
    lv_obj_align(quickSettingsPanel, LV_ALIGN_TOP_MID, 0, LV_STATUS_BAR_HEIGHT);
    lv_obj_set_style_border_width(quickSettingsPanel, 1, 0);
    lv_CreateStatusBarQuickSettings(quickSettingsPanel);

    // Open button (drawer arrow)
    openButton = lv_button_create(statusBarPanel);
    lv_obj_add_style(openButton, &styleStatusBar, 0);
    lv_obj_t * img_open = lv_image_create(openButton);
    lv_image_set_src(img_open, asset_drawer_down);
    lv_obj_center(img_open);
    lv_obj_set_size(openButton, QUICK_BUTTON_SIZE * 1.5, QUICK_BUTTON_SIZE);
    lv_obj_align(openButton, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_add_event_cb(openButton, drawer_open_event_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_set_ext_click_area(openButton, 8);
    lv_obj_add_flag(openButton, LV_OBJ_FLAG_IGNORE_LAYOUT);
#if LV_HIDE_QUICK_SETTINGS
    lv_obj_add_flag(openButton, LV_OBJ_FLAG_HIDDEN);
#endif

    // Setup animations
    lv_anim_init(&anim_opening_template);
    lv_anim_set_exec_cb(&anim_opening_template, anim_y_cb);
    lv_anim_set_var(&anim_opening_template, cont);
    lv_anim_set_duration(&anim_opening_template, 250);
    lv_anim_set_path_cb(&anim_opening_template, lv_anim_path_ease_in_out);
    lv_anim_set_completed_cb(&anim_opening_template, drawer_open_end_event_cb);

    lv_anim_init(&anim_quick_opening_template);
    lv_anim_set_exec_cb(&anim_quick_opening_template, anim_y_cb);
    lv_anim_set_var(&anim_quick_opening_template, quickSettingsPanel);
    lv_anim_set_duration(&anim_quick_opening_template, 500);
    lv_anim_set_path_cb(&anim_quick_opening_template, lv_anim_path_ease_in_out);
    lv_anim_set_completed_cb(&anim_quick_opening_template, quick_opening_anim_end_cb);

    return cont;
}

// ── Status bar top (icons) ──────────────────────────────────────────────────

static lv_obj_t * lv_CreateStatusBarTop(lv_obj_t * cont) {
    lv_obj_set_layout(cont, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(cont, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // Logo (centered, outside flex)
    status_logo = lv_image_create(cont);
    lv_image_set_src(status_logo, asset_logo);
    lv_obj_add_style(status_logo, &styleStatusBar, 0);
    lv_obj_center(status_logo);
    lv_obj_add_flag(status_logo, LV_OBJ_FLAG_IGNORE_LAYOUT);

    // Battery
    status_battery = lv_img_create(cont);
    lv_image_set_src(status_battery, LV_SYMBOL_BATTERY_FULL);
    lv_obj_set_style_pad_ver(status_battery, 2, 0);
    lv_obj_set_flex_grow(status_battery, 0);
    lv_obj_set_height(status_battery, LV_SIZE_CONTENT);

    // Charging
    status_charging = lv_img_create(cont);
    lv_image_set_src(status_charging, LV_SYMBOL_CHARGE);
    lv_obj_add_flag(status_charging, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_flex_grow(status_charging, 0);
    lv_obj_set_height(status_charging, LV_SIZE_CONTENT);

    // WiFi
    status_wifi = lv_img_create(cont);
    lv_image_set_src(status_wifi, LV_SYMBOL_WIFI);
    lv_obj_add_flag(status_wifi, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_image_recolor(status_wifi, lv_color_white(), 0);
    lv_obj_set_style_image_recolor_opa(status_wifi, LV_OPA_100, 0);
    lv_obj_set_flex_grow(status_wifi, 0);
    lv_obj_set_height(status_wifi, LV_SIZE_CONTENT);

    // Bluetooth
    status_bluetooth = lv_img_create(cont);
    lv_image_set_src(status_bluetooth, LV_SYMBOL_BLUETOOTH);
    lv_obj_set_flex_grow(status_bluetooth, 0);
    lv_obj_add_flag(status_bluetooth, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_height(status_bluetooth, LV_SIZE_CONTENT);

    // SD card
    status_sd = lv_img_create(cont);
    lv_image_set_src(status_sd, LV_SYMBOL_SD_CARD);
    lv_obj_add_flag(status_sd, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_flex_grow(status_sd, 0);
    lv_obj_set_height(status_sd, LV_SIZE_CONTENT);

    // Loading arc
    status_loading_arc = lv_arc_create(cont);
    lv_subject_init_int(&s_loading, -1);
    lv_obj_bind_flag_if_eq(status_loading_arc, &s_loading, LV_OBJ_FLAG_HIDDEN, -1);
    lv_arc_bind_value(status_loading_arc, &s_loading);
    lv_obj_set_size(status_loading_arc, 22, 22);
    lv_arc_set_rotation(status_loading_arc, 270);
    lv_arc_set_bg_angles(status_loading_arc, 0, 360);
    lv_obj_remove_style(status_loading_arc, NULL, LV_PART_KNOB);
    lv_obj_remove_flag(status_loading_arc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(status_loading_arc, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_align_to(status_loading_arc, status_logo, LV_ALIGN_OUT_RIGHT_MID, 5, 0);
    extern lv_style_t styleLoadingArc;
    lv_obj_add_style(status_loading_arc, &styleLoadingArc, LV_PART_INDICATOR);

    // Status check timer
    t_check_status = lv_timer_create(check_status_timer_cb, LV_DEF_REFR_PERIOD, NULL);
    return cont;
}

// ── Quick settings panel ────────────────────────────────────────────────────

static void slider_event_cb(lv_event_t * e) {
    int value = lv_slider_get_value((lv_obj_t *)lv_event_get_target(e));
    uint8_t * setting = (uint8_t *)lv_event_get_user_data(e);
    *setting = value;
}

static lv_subject_t s_ramUsage;
static lv_subject_t s_psRamUsage;

static void check_ram_timer_cb(lv_timer_t * timer) {
    crosspad_gui::HeapStats heap = crosspad_gui::getGuiPlatform().getHeapStats();
    uint8_t ram = heap.sram_total ? (uint8_t)((heap.sram_total - heap.sram_free) * 100 / heap.sram_total) : 0;
    uint8_t psRam = heap.psram_total ? (uint8_t)((heap.psram_total - heap.psram_free) * 100 / heap.psram_total) : 0;
    lv_subject_set_int(&s_ramUsage, ram);
    lv_subject_set_int(&s_psRamUsage, psRam);
    if (!quickSettingsPanelVisible) {
        lv_timer_pause(timer);
    }
}

static lv_style_t style_main;
static lv_style_t style_indicator;
static lv_style_t style_knob;
static lv_style_t style_pressed_color;

static lv_obj_t * lv_create_my_slider(lv_obj_t * parent, const char * txt, int min, int max,
                                        uint8_t * setting, lv_event_cb_t cb, lv_event_code_t cb_event_type)
{
    lv_obj_t * cont = lv_obj_create(parent);
    lv_obj_set_style_border_width(cont, 0, 0);
    lv_obj_set_style_bg_opa(cont, LV_OPA_0, 0);
    lv_obj_set_scrollbar_mode(cont, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_scroll_snap_y(cont, LV_SCROLL_SNAP_CENTER);

    lv_obj_t * label = lv_label_create(cont);
    lv_label_set_text(label, txt);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(label, lv_color_black(), 0);
    lv_obj_align(label, LV_ALIGN_LEFT_MID, 5, 0);

    lv_obj_t * slider = lv_slider_create(cont);
    lv_obj_remove_style_all(slider);
    lv_obj_set_size(slider, 150, 15);
    lv_obj_align(slider, LV_ALIGN_CENTER, 40, 0);
    lv_slider_set_range(slider, min, max);
    if (setting != NULL) {
        lv_slider_set_value(slider, *setting, LV_ANIM_OFF);
        if (cb != NULL) {
            lv_obj_add_event_cb(slider, cb, cb_event_type, setting);
        }
        lv_obj_add_event_cb(slider, slider_event_cb, LV_EVENT_VALUE_CHANGED, setting);
    } else {
        lv_slider_set_value(slider, 50, LV_ANIM_OFF);
        lv_obj_set_state(slider, LV_STATE_DISABLED, 0);
    }
    lv_obj_add_flag(slider, LV_OBJ_FLAG_ADV_HITTEST);

    lv_obj_add_style(slider, &style_main, LV_PART_MAIN);
    lv_obj_add_style(slider, &style_indicator, LV_PART_INDICATOR);
    lv_obj_add_style(slider, &style_pressed_color, LV_PART_INDICATOR | LV_STATE_PRESSED);
    lv_obj_add_style(slider, &style_knob, LV_PART_KNOB);
    lv_obj_add_style(slider, &style_pressed_color, LV_PART_KNOB | LV_STATE_PRESSED);

    lv_obj_update_layout(cont);
    unsigned int h = lv_obj_get_height(slider) > lv_obj_get_height(label)
                     ? (lv_obj_get_height(slider) + 15)
                     : (lv_obj_get_height(label) + 15);
    unsigned int w = lv_obj_get_content_width(parent);
    lv_obj_set_size(cont, w, h);
    return cont;
}

static void initSliderStyle() {
    static const lv_style_prop_t props[] = {LV_STYLE_BG_COLOR, 0};
    static lv_style_transition_dsc_t transition_dsc;
    lv_style_transition_dsc_init(&transition_dsc, props, lv_anim_path_linear, 300, 0, NULL);

    lv_style_init(&style_main);
    lv_style_set_bg_opa(&style_main, LV_OPA_COVER);
    lv_style_set_bg_color(&style_main, lv_color_black());
    lv_style_set_radius(&style_main, LV_RADIUS_CIRCLE);
    lv_style_set_pad_ver(&style_main, -2);

    lv_style_init(&style_indicator);
    lv_style_set_bg_opa(&style_indicator, LV_OPA_0);
    lv_style_set_bg_color(&style_indicator, lv_palette_main(LV_PALETTE_CYAN));
    lv_style_set_radius(&style_indicator, LV_RADIUS_CIRCLE);
    lv_style_set_transition(&style_indicator, &transition_dsc);

    lv_style_init(&style_knob);
    lv_style_set_bg_opa(&style_knob, LV_OPA_COVER);
    lv_style_set_bg_color(&style_knob, lv_palette_main(LV_PALETTE_CYAN));
    lv_style_set_border_color(&style_knob, lv_palette_darken(LV_PALETTE_CYAN, 3));
    lv_style_set_border_width(&style_knob, 2);
    lv_style_set_radius(&style_knob, LV_RADIUS_CIRCLE);
    lv_style_set_pad_all(&style_knob, 6);
    lv_style_set_transition(&style_knob, &transition_dsc);

    lv_style_init(&style_pressed_color);
    lv_style_set_bg_color(&style_pressed_color, lv_palette_darken(LV_PALETTE_CYAN, 2));
    lv_style_set_bg_opa(&style_indicator, LV_OPA_100);
}

static lv_obj_t * lv_CreateStatusBarQuickSettings(lv_obj_t * cont) {
    lv_obj_set_layout(cont, LV_LAYOUT_GRID);
    lv_obj_set_scrollbar_mode(cont, LV_SCROLLBAR_MODE_OFF);
    static int32_t column_dsc[] = {70, 190, 35, LV_GRID_TEMPLATE_LAST};
    static int32_t row_dsc[]    = {35, 35, 35, 40, LV_GRID_TEMPLATE_LAST};
    lv_obj_set_grid_dsc_array(cont, column_dsc, row_dsc);

    initSliderStyle();
    lv_obj_t * slider1 = lv_create_my_slider(cont, "SPK", 0, 100, NULL, NULL, LV_EVENT_ALL);
    lv_obj_t * slider2 = lv_create_my_slider(cont, "PAD Brightness", 0, 255, &(settings->RGBbrightness), NULL, LV_EVENT_VALUE_CHANGED);
    lv_obj_t * slider3 = lv_create_my_slider(cont, "SCR Brightness", 5, 100, &(settings->LCDbrightness), NULL, LV_EVENT_VALUE_CHANGED);
    lv_obj_t * footer = lv_obj_create(cont);

    lv_obj_set_grid_cell(slider1, LV_GRID_ALIGN_CENTER, 0, 3, LV_GRID_ALIGN_CENTER, 0, 1);
    lv_obj_set_grid_cell(slider2, LV_GRID_ALIGN_CENTER, 0, 3, LV_GRID_ALIGN_CENTER, 1, 1);
    lv_obj_set_grid_cell(slider3, LV_GRID_ALIGN_CENTER, 0, 3, LV_GRID_ALIGN_CENTER, 2, 1);
    lv_obj_set_grid_cell(footer,  LV_GRID_ALIGN_CENTER, 1, 1, LV_GRID_ALIGN_CENTER, 3, 1);

    // RAM usage labels
    lv_obj_add_style(footer, &styleStatusBar, 0);
    lv_obj_set_style_bg_opa(footer, LV_OPA_0, 0);
    lv_obj_t * ram = lv_label_create(footer);
    lv_obj_t * psRam = lv_label_create(footer);
    lv_subject_init_int(&s_ramUsage, 1);
    lv_subject_init_int(&s_psRamUsage, 1);
    lv_label_bind_text(ram, &s_ramUsage, "Stack: %d%%");
    lv_label_bind_text(psRam, &s_psRamUsage, "PSRAM: %d%%");
    lv_obj_set_content_height(footer, 32);
    lv_obj_set_content_width(footer, 220);
    lv_obj_align(ram, LV_ALIGN_CENTER, -45, 6);
    lv_obj_align(psRam, LV_ALIGN_CENTER, 57, 6);

    t_ramUpdate = lv_timer_create(check_ram_timer_cb, 1000, NULL);
    lv_timer_ready(t_ramUpdate);

    // Close button
    closeButton = lv_button_create(cont);
    lv_obj_add_style(closeButton, &styleStatusBar, 0);
    lv_obj_t * img_close = lv_image_create(closeButton);
    lv_image_set_src(img_close, asset_drawer_up);
    lv_obj_center(img_close);
    lv_obj_set_size(closeButton, QUICK_BUTTON_SIZE * 1.5, QUICK_BUTTON_SIZE);
    lv_obj_align(closeButton, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
    lv_obj_add_event_cb(closeButton, drawer_close_event_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_flag(closeButton, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_ext_click_area(closeButton, 30);
    lv_obj_set_grid_cell(closeButton, LV_GRID_ALIGN_END, 2, 1, LV_GRID_ALIGN_END, 3, 1);
    return cont;
}

#endif // USE_LVGL
