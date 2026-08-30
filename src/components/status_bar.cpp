#include "crosspad-gui/components/status_bar.h"
#include "crosspad-gui/styles/styles.h"
#include "crosspad-gui/widgets/gui_helpers.h"
#include "crosspad-gui/platform/IGuiPlatform.h"
#include "crosspad-gui/theme/crosspad_theme.h"

#include "crosspad/settings/CrosspadSettings.hpp"
#include "crosspad/settings/ISettingsUI.hpp"
#include "crosspad/status/CrosspadStatus.hpp"
#include "crosspad/pad/PadLedController.hpp"

#if LV_USE_PERF_MONITOR && LV_VERSION_CHECK(9,4,0)
#include "debugging/sysmon/lv_sysmon.h"
#elif LV_USE_PERF_MONITOR
#include "others/sysmon/lv_sysmon.h" 
#else
#pragma message("Performance monitor is disabled. Enable LV_USE_PERF_MONITOR in lv_conf.h to show FPS/CPU/MEM stats in the status bar.")
#endif

#include <cstring>

using namespace crosspad;

// Maximum drawer height = parent content height minus status bar (cap so it doesn't exceed screen)
#define LV_DRAWER_MAX_HEIGHT (int32_t)(lv_obj_get_content_height(lv_obj_get_parent(status_c)) - lv_obj_get_height(status_c))

extern CrosspadSettings * settings;
extern CrosspadStatus status;
extern lv_obj_t * status_c; // status bar container

static bool quickSettingsPanelVisible = false;

// subjects
static lv_subject_t s_loading;
static lv_subject_t s_ramUsage;
static lv_subject_t s_psRamUsage;
static bool s_ram_subjects_initialized = false;

// timers
static lv_timer_t * t_ramUpdate = NULL;
static lv_timer_t * t_check_status = NULL;

// objects
static lv_obj_t * openButton = NULL;
static lv_obj_t * closeButton = NULL;
static lv_obj_t * statusBarPanel = NULL;
static lv_obj_t * s_drawer_overlay = NULL; // floating quick settings overlay
static lv_obj_t * s_drawer_backdrop = NULL; // click catcher behind the overlay
static lv_group_t * s_drawer_group = NULL;       // encoder group owned by the drawer
static lv_group_t * s_drawer_saved_group = NULL; // group to hand back when it closes

// status icons
static lv_obj_t * status_logo;
static lv_obj_t * status_battery;
static lv_obj_t * status_charging;
static lv_obj_t * status_wifi;
static lv_obj_t * status_bluetooth;
static lv_obj_t * status_sd;
static lv_obj_t * status_loading_arc;
// perf stats: 3 independent vertical columns
static lv_obj_t * s_perf_fps_col = NULL;   // FPS column
static lv_obj_t * s_perf_cpu_col = NULL;   // CPU column
static lv_obj_t * s_perf_mem_col = NULL;   // MEM column
static lv_timer_t * t_perf_stats = NULL;
static uint8_t s_perf_flags = 0; // bitfield: bit0=FPS, bit1=CPU, bit2=MEM

// dynamic app icon slots
struct StatusBarAppIcon {
    const char * id;
    const char * content;
    crosspad_gui::StatusIconSide side;
    lv_color_t color;
    lv_obj_t * obj;
    bool active;
    bool hidden;   ///< remembered so visibility survives a status-bar rebuild
};

static StatusBarAppIcon s_app_icons[STATUSBAR_MAX_APP_ICONS] = {};
static lv_obj_t * s_app_icon_zone_left = nullptr;
static lv_obj_t * s_app_icon_zone_right = nullptr;

// animation
static lv_anim_t   anim_drawer_template;

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

// ── Animation callback ──────────────────────────────────────────────────────

static void anim_y_cb(void * var, int32_t v) {
    lv_obj_set_height((lv_obj_t *)var, v);
}

// Forward declarations
static void drawer_open_event_cb(lv_event_t * e);
static void drawer_close_event_cb(lv_event_t * e);
static void drawer_open_end_cb(lv_anim_t * a);
static void drawer_release_encoder();
static void drawer_close_end_cb(lv_anim_t * a);

static lv_obj_t * lv_CreateStatusBarTop(lv_obj_t * parent);
static void lv_CreateStatusBarQuickSettings(lv_obj_t * cont);
static void create_open_button();
static void delete_open_button();

// ── Loading API ─────────────────────────────────────────────────────────────

namespace crosspad_gui {

void statusbar_set_loading(int progress) {
    lv_subject_set_int(&s_loading, progress);
}

lv_subject_t * statusbar_get_loading_subject() {
    return &s_loading;
}

void statusbar_show_loader_message(const char * title, const char * message, bool isError) {
    lv_obj_t * mbox = lv_msgbox_create(lv_layer_top());
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

// ── Dynamic App Icon API ────────────────────────────────────────────────────

static StatusBarAppIcon * find_app_icon(const char * id) {
    if (!id) return nullptr;
    for (int i = 0; i < STATUSBAR_MAX_APP_ICONS; i++) {
        if (s_app_icons[i].active && strcmp(s_app_icons[i].id, id) == 0) {
            return &s_app_icons[i];
        }
    }
    return nullptr;
}

static StatusBarAppIcon * find_free_slot() {
    for (int i = 0; i < STATUSBAR_MAX_APP_ICONS; i++) {
        if (!s_app_icons[i].active) return &s_app_icons[i];
    }
    return nullptr;
}

bool statusbar_add_icon(const char * id, const char * content,
                        lv_color_t color, StatusIconSide side) {
    if (!id || !content) return false;

    // Update existing icon
    StatusBarAppIcon * slot = find_app_icon(id);
    if (slot) {
        slot->content = content;
        slot->color = color;
        if (slot->obj) {
            lv_label_set_text(slot->obj, content);
            lv_obj_set_style_text_color(slot->obj, color, 0);
        }
        // If side changed, reparent
        if (slot->side != side && slot->obj) {
            lv_obj_t * zone = (side == StatusIconSide::Left) ? s_app_icon_zone_left : s_app_icon_zone_right;
            if (zone) {
                lv_obj_set_parent(slot->obj, zone);
                slot->side = side;
            }
        }
        return true;
    }

    // Find free slot
    slot = find_free_slot();
    if (!slot) return false;

    lv_obj_t * zone = (side == StatusIconSide::Left) ? s_app_icon_zone_left : s_app_icon_zone_right;
    if (!zone) return false;

    lv_obj_t * lbl = lv_label_create(zone);
    lv_label_set_text(lbl, content);
    lv_obj_set_style_text_color(lbl, color, 0);
    lv_obj_set_height(lbl, LV_SIZE_CONTENT);

    slot->id = id;
    slot->content = content;
    slot->side = side;
    slot->color = color;
    slot->obj = lbl;
    slot->active = true;
    return true;
}

void statusbar_remove_icon(const char * id) {
    StatusBarAppIcon * slot = find_app_icon(id);
    if (!slot) return;
    if (slot->obj) lv_obj_delete(slot->obj);
    memset(slot, 0, sizeof(*slot));
}

void statusbar_set_icon_color(const char * id, lv_color_t color) {
    StatusBarAppIcon * slot = find_app_icon(id);
    if (!slot) return;
    slot->color = color;
    if (slot->obj) lv_obj_set_style_text_color(slot->obj, color, 0);
}

void statusbar_set_icon_content(const char * id, const char * content) {
    StatusBarAppIcon * slot = find_app_icon(id);
    if (!slot || !content) return;
    slot->content = content;
    if (slot->obj) lv_label_set_text(slot->obj, content);
}

void statusbar_set_icon_font(const char * id, const lv_font_t * font) {
    StatusBarAppIcon * slot = find_app_icon(id);
    if (!slot || !slot->obj) return;
    lv_obj_set_style_text_font(slot->obj, font, 0);
}

void statusbar_set_icon_visible(const char * id, bool visible) {
    StatusBarAppIcon * slot = find_app_icon(id);
    if (!slot) return;
    slot->hidden = !visible;
    if (!slot->obj) return;
    if (visible) lv_obj_remove_flag(slot->obj, LV_OBJ_FLAG_HIDDEN);
    else         lv_obj_add_flag(slot->obj, LV_OBJ_FLAG_HIDDEN);
}

void statusbar_clear_all_icons() {
    for (int i = 0; i < STATUSBAR_MAX_APP_ICONS; i++) {
        if (s_app_icons[i].active) {
            if (s_app_icons[i].obj) lv_obj_delete(s_app_icons[i].obj);
            memset(&s_app_icons[i], 0, sizeof(s_app_icons[i]));
        }
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

// ── Perf stats ──────────────────────────────────────────────────────────────

static void perf_stats_timer_cb(lv_timer_t * timer) {
    if (s_perf_flags == 0) return;

    crosspad_gui::PerfStats ps = crosspad_gui::getGuiPlatform().getPerfStats();

    if (s_perf_fps_col && (s_perf_flags & PERF_FLAG_FPS)) {
        lv_label_set_text_fmt(s_perf_fps_col,
            "fps\n%" LV_PRIu32, ps.fps);
    }
    if (s_perf_cpu_col && (s_perf_flags & PERF_FLAG_CPU)) {
        lv_label_set_text_fmt(s_perf_cpu_col,
            "%02" LV_PRIu32 "%%\n%02" LV_PRIu32 "%%",
            ps.cpu0_pct, ps.cpu1_pct);
    }
    if (s_perf_mem_col && (s_perf_flags & PERF_FLAG_MEM)) {
        lv_label_set_text_fmt(s_perf_mem_col,
            "S%" LV_PRIu32 "\nP%" LV_PRIu32,
            ps.sram_pct, ps.psram_pct);
    }
}

static void apply_perf_column_visibility() {
    if (s_perf_fps_col) {
        if (s_perf_flags & PERF_FLAG_FPS) lv_obj_remove_flag(s_perf_fps_col, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(s_perf_fps_col, LV_OBJ_FLAG_HIDDEN);
    }
    if (s_perf_cpu_col) {
        if (s_perf_flags & PERF_FLAG_CPU) lv_obj_remove_flag(s_perf_cpu_col, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(s_perf_cpu_col, LV_OBJ_FLAG_HIDDEN);
    }
    if (s_perf_mem_col) {
        if (s_perf_flags & PERF_FLAG_MEM) lv_obj_remove_flag(s_perf_mem_col, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(s_perf_mem_col, LV_OBJ_FLAG_HIDDEN);
    }

    if (s_perf_flags && t_perf_stats) {
        lv_timer_resume(t_perf_stats);
        lv_timer_ready(t_perf_stats);
    } else if (!s_perf_flags && t_perf_stats) {
        lv_timer_pause(t_perf_stats);
    }

#if LV_USE_SYSMON && LV_USE_PERF_MONITOR
    lv_sysmon_hide_performance(NULL);
#endif
}

namespace crosspad_gui {

void statusbar_set_perf_level(uint8_t flags) {
    s_perf_flags = flags;
    apply_perf_column_visibility();
}

void statusbar_set_drawer_enabled(bool enabled) {
#if LV_HIDE_QUICK_SETTINGS
    enabled = false;
#endif
    if (enabled) {
        if (!openButton) create_open_button();
    } else {
        // If drawer open, leave deletion to drawer_close_end_cb.
        if (!quickSettingsPanelVisible) delete_open_button();
    }
}

} // namespace crosspad_gui

// ── Status check timer ──────────────────────────────────────────────────────

// Cached previous values — file-scoped so they can be reset on status bar re-creation
static bool s_lastSd = false;
static bool s_lastBt = false;
static bool s_lastBtActive = false;
static bool s_lastWifi = false;
static bool s_lastCharging = false;
static uint8_t s_lastBatteryPct = 0;

static void check_status_timer_cb(lv_timer_t * timer) {
    // SD card — ESP filesystem mount state is authoritative; STM32 detect is fallback
    bool sd = status.sdCardDetected;
    if (sd != s_lastSd) {
        s_lastSd = sd;
        lv_obj_set_hidden(status_sd, !sd);
    }
    // Bluetooth: hidden when the radio is off, dimmed while it is up but
    // unconnected, solid once a peer is on the other end.
    bool bt = status.bluetoothConnected;
    bool btActive = status.bluetoothActive || bt;
    if (bt != s_lastBt || btActive != s_lastBtActive) {
        s_lastBt = bt;
        s_lastBtActive = btActive;
        lv_obj_set_hidden(status_bluetooth, !btActive);
        /* The glyph is a symbol, which LVGL draws through the text path, so
         * dimming it needs text_opa; image_opa is set too in case the source is
         * ever swapped for a real image. */
        lv_opa_t opa = bt ? LV_OPA_COVER : LV_OPA_40;
        lv_obj_set_style_image_opa(status_bluetooth, opa, 0);
        lv_obj_set_style_text_opa(status_bluetooth, opa, 0);
    }
    // WiFi
    bool wifi = status.wifiConnected;
    if (wifi != s_lastWifi) {
        s_lastWifi = wifi;
        lv_obj_set_hidden(status_wifi, !wifi);
    }
    // Charging
    bool charging = status.stm32.chargingStatus;
    if (charging != s_lastCharging) {
        s_lastCharging = charging;
        lv_obj_set_hidden(status_charging, !charging);
    }
    // Battery level — negative pct means STM32 reports no battery (VBAT < 3200 mV).
    // Blink empty/full to signal it.
    static bool s_batMissing = false;
    static uint8_t s_batBlinkPhase = 0xFF;
    if (status.batteryPct < 0) {
        uint8_t phase = (lv_tick_get() / 500) & 1;
        if (!s_batMissing || phase != s_batBlinkPhase) {
            s_batMissing = true;
            s_batBlinkPhase = phase;
            lv_obj_remove_flag(status_battery, LV_OBJ_FLAG_HIDDEN);
            lv_image_set_src(status_battery,
                phase ? LV_SYMBOL_BATTERY_FULL : LV_SYMBOL_BATTERY_EMPTY);
            lv_obj_invalidate(status_battery);
        }
        s_lastBatteryPct = 0; // force re-pick when battery returns
    } else {
        const int bat_hist = 2;
        if (s_batMissing ||
            s_lastBatteryPct > status.batteryPct + bat_hist ||
            s_lastBatteryPct < status.batteryPct - bat_hist) {
            s_batMissing = false;
            s_batBlinkPhase = 0xFF;
            s_lastBatteryPct = status.batteryPct;
            PickCorrectBatteryIcon(s_lastBatteryPct);
            lv_obj_invalidate(status_battery);
        }
    }
}

// ── Drawer open/close ───────────────────────────────────────────────────────

/* Both handlers fire on LV_EVENT_CLICKED, i.e. on release. Acting on press
 * swaps the focus group mid-click, and the release half lands on whatever the
 * other group had focused — closing the drawer launched an app that way. */

static void drawer_open_event_cb(lv_event_t * e) {
    if (quickSettingsPanelVisible) return;
    if (s_drawer_overlay) return;
    lv_event_code_t code = lv_event_get_code(e);
    if (code != LV_EVENT_CLICKED) return;

    lv_obj_t * parent = lv_obj_get_parent(status_c);

    /* Click catcher under the drawer, so a tap that misses the panel closes it
     * instead of reaching what is below. The close arrow sits in the corner,
     * where half a fingertip lands outside the panel. */
    s_drawer_backdrop = lv_obj_create(parent);
    lv_obj_remove_style_all(s_drawer_backdrop);
    lv_obj_add_flag(s_drawer_backdrop, LV_OBJ_FLAG_FLOATING);
    lv_obj_add_flag(s_drawer_backdrop, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_add_flag(s_drawer_backdrop, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(s_drawer_backdrop, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(s_drawer_backdrop, lv_obj_get_width(parent), lv_obj_get_height(parent));
    lv_obj_set_pos(s_drawer_backdrop, 0, 0);
    lv_obj_add_event_cb(s_drawer_backdrop, drawer_close_event_cb, LV_EVENT_CLICKED, NULL);

    // Create floating overlay on the parent screen
    s_drawer_overlay = lv_obj_create(parent);
    lv_obj_add_flag(s_drawer_overlay, LV_OBJ_FLAG_FLOATING);
    lv_obj_set_width(s_drawer_overlay, lv_obj_get_width(status_c));
    lv_obj_add_style(s_drawer_overlay, &styleStatusBar, 0);
    lv_obj_set_style_bg_opa(s_drawer_overlay, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(s_drawer_overlay, 0, 0);
    lv_obj_set_style_radius(s_drawer_overlay, 0, 0);
    lv_obj_remove_flag(s_drawer_overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(s_drawer_overlay, LV_SCROLLBAR_MODE_OFF);

    // Populate quick settings content (before measuring height)
    lv_CreateStatusBarQuickSettings(s_drawer_overlay);

    // Measure content height, cap at available space
    lv_obj_set_height(s_drawer_overlay, LV_SIZE_CONTENT);
    lv_obj_update_layout(parent);
    int32_t content_h = lv_obj_get_height(s_drawer_overlay);
    int32_t max_h = LV_DRAWER_MAX_HEIGHT;
    int32_t target_h = content_h < max_h ? content_h : max_h;

    // Set initial height to 0 for animation
    lv_obj_set_height(s_drawer_overlay, 0);

    // Position directly below the status bar (relative alignment)
    lv_obj_align_to(s_drawer_overlay, status_c, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 0);

    // Ensure drawn on top of siblings
    lv_obj_move_to_index(s_drawer_overlay, -1);

    // Hide open button
    lv_obj_add_flag(openButton, LV_OBJ_FLAG_HIDDEN);

    // Animate height from 0 to content height
    lv_anim_init(&anim_drawer_template);
    lv_anim_set_exec_cb(&anim_drawer_template, anim_y_cb);
    lv_anim_set_var(&anim_drawer_template, s_drawer_overlay);
    lv_anim_set_values(&anim_drawer_template, 0, target_h);
    lv_anim_set_duration(&anim_drawer_template, 400);
    lv_anim_set_path_cb(&anim_drawer_template, lv_anim_path_ease_in_out);
    lv_anim_set_completed_cb(&anim_drawer_template, drawer_open_end_cb);
    lv_anim_start(&anim_drawer_template);
}

static void drawer_open_end_cb(lv_anim_t * a) {
    quickSettingsPanelVisible = true;
    if (closeButton) {
        lv_obj_remove_flag(closeButton, LV_OBJ_FLAG_HIDDEN);
    }
    if (t_ramUpdate) {
        lv_timer_resume(t_ramUpdate);
        lv_timer_ready(t_ramUpdate);
    }
}

static void drawer_close_event_cb(lv_event_t * e) {
    if (!quickSettingsPanelVisible) return;
    lv_event_code_t code = lv_event_get_code(e);
    if (code != LV_EVENT_CLICKED) return;

    if (closeButton) {
        lv_obj_add_flag(closeButton, LV_OBJ_FLAG_HIDDEN);
    }
    // One close per drawer: a second tap during the animation must not restart it.
    if (s_drawer_backdrop) lv_obj_remove_flag(s_drawer_backdrop, LV_OBJ_FLAG_CLICKABLE);

    int32_t current_h = s_drawer_overlay ? lv_obj_get_height(s_drawer_overlay) : 0;

    lv_anim_init(&anim_drawer_template);
    lv_anim_set_exec_cb(&anim_drawer_template, anim_y_cb);
    lv_anim_set_var(&anim_drawer_template, s_drawer_overlay);
    lv_anim_set_values(&anim_drawer_template, current_h, 0);
    lv_anim_set_duration(&anim_drawer_template, 400);
    lv_anim_set_path_cb(&anim_drawer_template, lv_anim_path_ease_in_out);
    lv_anim_set_completed_cb(&anim_drawer_template, drawer_close_end_cb);
    lv_anim_start(&anim_drawer_template);
}

static void drawer_close_end_cb(lv_anim_t * a) {
    quickSettingsPanelVisible = false;
    if (t_ramUpdate) {
        lv_timer_delete(t_ramUpdate);
        t_ramUpdate = NULL;
    }
    if (s_drawer_overlay) {
        lv_obj_delete(s_drawer_overlay);
        s_drawer_overlay = NULL;
    }
    if (s_drawer_backdrop) {
        lv_obj_delete(s_drawer_backdrop);
        s_drawer_backdrop = NULL;
    }
    /* Only now, once the panel is gone: hand the encoder back any earlier and
     * the release half of the press that closed the drawer lands on whatever
     * the launcher had focused — which pressed it. */
    drawer_release_encoder();
    closeButton = NULL;
    bool drawer_enabled = settings ? settings->quickSettingsEnabled : true;
#if LV_HIDE_QUICK_SETTINGS
    drawer_enabled = false;
#endif
    if (drawer_enabled) {
        if (openButton) lv_obj_remove_flag(openButton, LV_OBJ_FLAG_HIDDEN);
    } else {
        delete_open_button();
    }
}

// ── Drawer encoder handover ─────────────────────────────────────────────────

/* Give the navigation encoder back to whoever had it, also from the rebuild
 * path in case the drawer died with its tree. The guard matters there: the
 * rebuild's own binding to the new launcher group must survive. */
static void drawer_release_encoder() {
    if (!s_drawer_group) return;
    lv_indev_t * nav = crosspad_gui::getGuiPlatform().getNavigationEncoder();
    if (nav && lv_indev_get_group(nav) == s_drawer_group) {
        lv_indev_set_group(nav, s_drawer_saved_group);
    }
    lv_group_delete(s_drawer_group);
    s_drawer_group = NULL;
    s_drawer_saved_group = NULL;
}

// ── Open button (drawer arrow) lifecycle ────────────────────────────────────

static void create_open_button() {
    if (openButton || !statusBarPanel) return;
    openButton = lv_button_create(statusBarPanel);
    lv_obj_add_style(openButton, &styleStatusBar, 0);
    lv_obj_t * img_open = lv_image_create(openButton);
    lv_image_set_src(img_open, asset_drawer_down);
    lv_obj_center(img_open);
    lv_obj_set_size(openButton, QUICK_BUTTON_SIZE * 1.5, LV_STATUS_BAR_HEIGHT);
    lv_obj_align(openButton, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_add_event_cb(openButton, drawer_open_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_set_ext_click_area(openButton, 8);
    lv_obj_add_flag(openButton, LV_OBJ_FLAG_IGNORE_LAYOUT);
    if (s_app_icon_zone_right) {
        lv_obj_set_style_pad_right(s_app_icon_zone_right, (QUICK_BUTTON_SIZE * 3 / 2) + 4, 0);
    }
}

static void delete_open_button() {
    if (!openButton) return;
    lv_obj_delete(openButton);
    openButton = NULL;
    if (s_app_icon_zone_right) {
        lv_obj_set_style_pad_right(s_app_icon_zone_right, 4, 0);
    }
}

// ── Main creation ───────────────────────────────────────────────────────────

lv_obj_t * lv_CreateStatusBar(lv_obj_t * parent) {
    build_asset_paths();

    /* The previous bar's widgets died with its tree, but the file-static
     * handles still point at them. create_open_button() returns early on a
     * non-NULL openButton, so the drawer arrow was never re-created after the
     * first rebuild, and unhiding it wrote through freed memory. */
    drawer_release_encoder();
    openButton = NULL;
    closeButton = NULL;
    s_drawer_overlay = NULL;
    s_drawer_backdrop = NULL;
    quickSettingsPanelVisible = false;
    if (t_ramUpdate) {
        lv_timer_delete(t_ramUpdate);
        t_ramUpdate = NULL;
    }

    lv_obj_t * cont = lv_obj_create(parent);
    lv_obj_set_width(cont, LV_PCT(100));
    lv_obj_set_height(cont, LV_STATUS_BAR_HEIGHT);
    lv_obj_add_style(cont, &styleStatusBar, 0);
    lv_obj_set_style_bg_opa(cont, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(cont, 0, 0);
    lv_obj_set_style_radius(cont, 0, 0);
    lv_obj_remove_flag(cont, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(cont, LV_SCROLLBAR_MODE_OFF);

    // Status bar panel (icons row)
    statusBarPanel = lv_obj_create(cont);
    lv_obj_set_width(statusBarPanel, LV_PCT(100));
    lv_obj_set_height(statusBarPanel, LV_STATUS_BAR_HEIGHT);
    lv_obj_add_style(statusBarPanel, &styleStatusBar, 0);
    lv_obj_set_style_pad_left(statusBarPanel, 4, 0);
    lv_obj_set_style_pad_right(statusBarPanel, 4, 0);
    lv_obj_remove_flag(statusBarPanel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(statusBarPanel, LV_SCROLLBAR_MODE_OFF);
    lv_CreateStatusBarTop(statusBarPanel);

    // Open button (drawer arrow) — only built when drawer enabled
    bool drawer_enabled = settings ? settings->quickSettingsEnabled : true;
#if LV_HIDE_QUICK_SETTINGS
    drawer_enabled = false;
#endif
    if (drawer_enabled) {
        create_open_button();
    }

    // Init RAM subjects once (reused across open/close cycles)
    if (!s_ram_subjects_initialized) {
        lv_subject_init_int(&s_ramUsage, 1);
        lv_subject_init_int(&s_psRamUsage, 1);
        s_ram_subjects_initialized = true;
    }

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

    // App icon zone — left (after system icons, grows with content)
    s_app_icon_zone_left = lv_obj_create(cont);
    lv_obj_set_height(s_app_icon_zone_left, LV_PCT(100));
    lv_obj_set_width(s_app_icon_zone_left, LV_SIZE_CONTENT);
    lv_obj_set_layout(s_app_icon_zone_left, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(s_app_icon_zone_left, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(s_app_icon_zone_left, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_flex_grow(s_app_icon_zone_left, 0);
    lv_obj_set_style_pad_all(s_app_icon_zone_left, 0, 0);
    lv_obj_set_style_pad_column(s_app_icon_zone_left, 3, 0);
    lv_obj_set_style_bg_opa(s_app_icon_zone_left, LV_OPA_0, 0);
    lv_obj_set_style_border_width(s_app_icon_zone_left, 0, 0);
    lv_obj_remove_flag(s_app_icon_zone_left, LV_OBJ_FLAG_SCROLLABLE);

    // App icon zone — right (fills remaining space, icons align to end)
    s_app_icon_zone_right = lv_obj_create(cont);
    lv_obj_set_height(s_app_icon_zone_right, LV_PCT(100));
    lv_obj_set_width(s_app_icon_zone_right, LV_SIZE_CONTENT);
    lv_obj_set_layout(s_app_icon_zone_right, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(s_app_icon_zone_right, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(s_app_icon_zone_right, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_flex_grow(s_app_icon_zone_right, 1);
    lv_obj_set_style_pad_all(s_app_icon_zone_right, 0, 0);
    // Default padding assumes no drawer arrow; create_open_button() bumps it.
    lv_obj_set_style_pad_right(s_app_icon_zone_right, 4, 0);
    lv_obj_set_style_pad_column(s_app_icon_zone_right, 3, 0);
    lv_obj_set_style_bg_opa(s_app_icon_zone_right, LV_OPA_0, 0);
    lv_obj_set_style_border_width(s_app_icon_zone_right, 0, 0);

    // Icons registered against the destroyed tree lost their labels, but their
    // slots still say what to show. Re-create them, so a long-lived icon needs
    // no re-registration and nothing writes through a dangling label.
    for (int i = 0; i < STATUSBAR_MAX_APP_ICONS; i++) {
        StatusBarAppIcon * slot = &s_app_icons[i];
        if (!slot->active || !slot->content) continue;
        lv_obj_t * zone = (slot->side == crosspad_gui::StatusIconSide::Left) ? s_app_icon_zone_left
                                                               : s_app_icon_zone_right;
        lv_obj_t * lbl = lv_label_create(zone);
        lv_label_set_text(lbl, slot->content);
        lv_obj_set_style_text_color(lbl, slot->color, 0);
        lv_obj_set_height(lbl, LV_SIZE_CONTENT);
        if (slot->hidden) lv_obj_add_flag(lbl, LV_OBJ_FLAG_HIDDEN);
        slot->obj = lbl;
    }
    lv_obj_remove_flag(s_app_icon_zone_right, LV_OBJ_FLAG_SCROLLABLE);

    // Clear all app icon slots
    memset(s_app_icons, 0, sizeof(s_app_icons));

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

    // Performance stats: 3 vertical columns in right icon zone
    // Each column is a label with 2 lines, narrow and tall
    auto make_perf_col = [](lv_obj_t * parent) -> lv_obj_t * {
        lv_obj_t * lbl = lv_label_create(parent);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_10, 0);
        // Match status bar text color (styleStatusBar uses lv_color_bw_from_color)
        lv_obj_set_style_text_color(lbl, lv_color_bw_from_color(CROSSPAD_THEME_PRIMARY), 0);
        lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_line_space(lbl, -2, 0);
        lv_label_set_text(lbl, "");
        return lbl;
    };
    s_perf_fps_col = make_perf_col(s_app_icon_zone_right);
    s_perf_cpu_col = make_perf_col(s_app_icon_zone_right);
    s_perf_mem_col = make_perf_col(s_app_icon_zone_right);

    // Perf stats timer (500ms update)
    if (t_perf_stats) {
        lv_timer_delete(t_perf_stats);
        t_perf_stats = NULL;
    }
    t_perf_stats = lv_timer_create(perf_stats_timer_cb, 500, NULL);

    // Apply initial flags from settings
    uint8_t perf_flags = settings ? settings->perfStatsFlags : 0;
    crosspad_gui::statusbar_set_perf_level(perf_flags);

    // Status check timer — delete previous if status bar is being re-created
    if (t_check_status) {
        lv_timer_delete(t_check_status);
        t_check_status = NULL;
    }
    // Reset cached values so icons are refreshed on first tick
    s_lastSd = !status.stm32.sdDetected;
    s_lastBt = !status.bluetoothConnected;
    s_lastBtActive = !(status.bluetoothActive || status.bluetoothConnected);
    s_lastWifi = !status.wifiConnected;
    s_lastCharging = !status.stm32.chargingStatus;
    s_lastBatteryPct = 0;
    t_check_status = lv_timer_create(check_status_timer_cb, LV_DEF_REFR_PERIOD, NULL);
    return cont;
}

// ── Quick settings panel ────────────────────────────────────────────────────

// Drawer slider context — holds setting pointer plus optional hardware-apply hook.
struct DrawerSliderCtx {
    uint8_t * setting;
    lv_event_cb_t hw_apply_cb; // optional
};

static void drawer_slider_event_cb(lv_event_t * e) {
    auto * ctx = (DrawerSliderCtx *)lv_event_get_user_data(e);
    if (!ctx || !ctx->setting) return;
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_VALUE_CHANGED) {
        int value = lv_slider_get_value((lv_obj_t *)lv_event_get_target(e));
        *(ctx->setting) = (uint8_t)value;
        // Live hardware apply on every drag tick — keep slider feeling responsive.
        if (ctx->hw_apply_cb) ctx->hw_apply_cb(e);
    } else if (code == LV_EVENT_RELEASED) {
        // Persist only after the user lets go: NVS writes are slow.
        auto * ui = crosspad::getSettingsUI();
        if (ui) ui->saveSettings();
    }
}

static void drawer_slider_delete_cb(lv_event_t * e) {
    delete (DrawerSliderCtx *)lv_event_get_user_data(e);
}

static void check_ram_timer_cb(lv_timer_t * timer) {
    crosspad_gui::HeapStats heap = crosspad_gui::getGuiPlatform().getHeapStats();
    uint8_t ram = heap.sram_total ? (uint8_t)((heap.sram_total - heap.sram_free) * 100 / heap.sram_total) : 0;
    uint8_t psRam = heap.psram_total ? (uint8_t)((heap.psram_total - heap.psram_free) * 100 / heap.psram_total) : 0;
    lv_subject_set_int(&s_ramUsage, ram);
    lv_subject_set_int(&s_psRamUsage, psRam);
}

static lv_style_t style_main;
static lv_style_t style_indicator;
static lv_style_t style_knob;
static lv_style_t style_pressed_color;
static lv_style_t style_focused;   // encoder focus ring — the drawer has no other cue

static lv_obj_t * lv_create_my_slider(lv_obj_t * parent, const char * txt, int min, int max,
                                        uint8_t * setting, lv_event_cb_t hw_apply_cb)
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
        auto * ctx = new DrawerSliderCtx{setting, hw_apply_cb};
        lv_obj_add_event_cb(slider, drawer_slider_event_cb, LV_EVENT_VALUE_CHANGED, ctx);
        lv_obj_add_event_cb(slider, drawer_slider_event_cb, LV_EVENT_RELEASED, ctx);
        lv_obj_add_event_cb(slider, drawer_slider_delete_cb, LV_EVENT_DELETE, ctx);
    } else {
        lv_slider_set_value(slider, 50, LV_ANIM_OFF);
        lv_obj_set_state(slider, LV_STATE_DISABLED, 0);
    }
    /* ADV_HITTEST makes the knob the only grabbable part of a slider, and the
     * knob is ~20 px on a 320 px screen. Grow the hit area, not the graphic. */
    lv_obj_add_flag(slider, LV_OBJ_FLAG_ADV_HITTEST);
    lv_obj_set_ext_click_area(slider, 14);

    lv_obj_add_style(slider, &style_main, LV_PART_MAIN);
    lv_obj_add_style(slider, &style_indicator, LV_PART_INDICATOR);
    lv_obj_add_style(slider, &style_pressed_color,
                     (lv_style_selector_t)LV_PART_INDICATOR | LV_STATE_PRESSED);
    lv_obj_add_style(slider, &style_knob, LV_PART_KNOB);
    lv_obj_add_style(slider, &style_pressed_color,
                     (lv_style_selector_t)LV_PART_KNOB | LV_STATE_PRESSED);
    lv_obj_add_style(slider, &style_focused,
                     (lv_style_selector_t)LV_PART_KNOB | LV_STATE_FOCUSED);
    lv_obj_add_style(slider, &style_pressed_color,
                     (lv_style_selector_t)LV_PART_KNOB | LV_STATE_EDITED);

    if (s_drawer_group && setting != NULL) lv_group_add_obj(s_drawer_group, slider);

    lv_obj_update_layout(cont);
    /* +20, not +15: the vertical half of the slider's extended hit area is
     * clipped by this container, and the grid row is 35 px tall anyway. */
    unsigned int h = lv_obj_get_height(slider) > lv_obj_get_height(label)
                     ? (lv_obj_get_height(slider) + 20)
                     : (lv_obj_get_height(label) + 20);
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

    lv_style_init(&style_focused);
    lv_style_set_border_color(&style_focused, lv_color_white());
    lv_style_set_border_width(&style_focused, 3);
}

static void lv_CreateStatusBarQuickSettings(lv_obj_t * cont) {
    lv_obj_set_layout(cont, LV_LAYOUT_GRID);
    lv_obj_set_scrollbar_mode(cont, LV_SCROLLBAR_MODE_OFF);
    static int32_t column_dsc[] = {70, 190, 35, LV_GRID_TEMPLATE_LAST};
    static int32_t row_dsc[]    = {35, 35, 35, 40, LV_GRID_TEMPLATE_LAST};
    lv_obj_set_grid_dsc_array(cont, column_dsc, row_dsc);

    initSliderStyle();

    /* The drawer takes the navigation encoder while it is open: the sliders and
     * the close arrow are all small targets, and the encoder is the way to hit
     * them without aiming. Handed back by drawer_release_encoder(). */
    s_drawer_group = lv_group_create();
    lv_indev_t * nav = crosspad_gui::getGuiPlatform().getNavigationEncoder();
    if (nav) {
        s_drawer_saved_group = lv_indev_get_group(nav);
        lv_indev_set_group(nav, s_drawer_group);
    }

    auto spk_apply = [](lv_event_t * e) {
        (void)e;
        crosspad_gui::getGuiPlatform().setSpeakerVolume(settings->masterFX.outVolumeCodec[0][0]);
    };
    auto rgb_apply = [](lv_event_t * e) {
        (void)e;
        crosspad::getPadLedController().setBrightness(settings->RGBbrightness);
    };
    auto lcd_apply = [](lv_event_t * e) {
        (void)e;
        crosspad_gui::getGuiPlatform().setLcdBrightness(settings->LCDbrightness);
    };

    // Codec volume range is 0..100 — a 0..127 slider wasted its top third on a clamp.
    lv_obj_t * slider1 = lv_create_my_slider(cont, "SPK", 0, 100,
                                              &(settings->masterFX.outVolumeCodec[0][0]), spk_apply);
    lv_obj_t * slider2 = lv_create_my_slider(cont, "PAD Brightness", 0, 255,
                                              &(settings->RGBbrightness), rgb_apply);
    lv_obj_t * slider3 = lv_create_my_slider(cont, "SCR Brightness", 5, 100,
                                              &(settings->LCDbrightness), lcd_apply);
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
    lv_label_bind_text(ram, &s_ramUsage, "Stack: %d%%");
    lv_label_bind_text(psRam, &s_psRamUsage, "PSRAM: %d%%");
    lv_obj_set_content_height(footer, 32);
    lv_obj_set_content_width(footer, 220);
    lv_obj_align(ram, LV_ALIGN_CENTER, -45, 6);
    lv_obj_align(psRam, LV_ALIGN_CENTER, 57, 6);

    t_ramUpdate = lv_timer_create(check_ram_timer_cb, 1000, NULL);

    // Close button
    closeButton = lv_button_create(cont);
    lv_obj_add_style(closeButton, &styleStatusBar, 0);
    lv_obj_t * img_close = lv_image_create(closeButton);
    lv_image_set_src(img_close, asset_drawer_up);
    lv_obj_center(img_close);
    lv_obj_set_size(closeButton, QUICK_BUTTON_SIZE * 1.5, QUICK_BUTTON_SIZE);
    lv_obj_align(closeButton, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
    lv_obj_add_event_cb(closeButton, drawer_close_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_flag(closeButton, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_ext_click_area(closeButton, 30);
    lv_obj_set_style_outline_color(closeButton, lv_color_white(), LV_STATE_FOCUSED);
    lv_obj_set_style_outline_width(closeButton, 2, LV_STATE_FOCUSED);
    lv_obj_set_grid_cell(closeButton, LV_GRID_ALIGN_END, 2, 1, LV_GRID_ALIGN_END, 3, 1);

    if (s_drawer_group) {
        lv_group_add_obj(s_drawer_group, closeButton);
        lv_group_focus_obj(lv_group_get_obj_by_index(s_drawer_group, 0));
    }
}
