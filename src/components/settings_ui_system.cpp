// SPDX-License-Identifier: GPL-3.0-or-later
//
// Settings: the Programming, Info and System categories, and the STM power/input config.

#ifdef USE_LVGL

#include "settings_ui_internal.h"
#include "crosspad-gui/components/status_bar.h"
#include "crosspad-gui/components/ui_modal.h"
#include "crosspad-gui/components/ui_toast.h"
#include "crosspad-gui/components/dfu_panel.h"
#include "crosspad-gui/platform/IGuiPlatform.h"
#include "crosspad-gui/styles/styles.h"
#include "crosspad-gui/theme/crosspad_theme.h"
#include "crosspad/settings/CrosspadSettings.hpp"
#include "crosspad/settings/ISettingsUI.hpp"
#include "crosspad/pad/PadLedController.hpp"
#include "crosspad/stm32/reg_map.h"
#include "crosspad/platform/IVibration.hpp"

using namespace crosspad;

namespace crosspad_gui {

using namespace settings_detail;

// ---- System / STM Power+Input ----

static constexpr uint8_t idx_for_u16(const uint16_t* arr, uint8_t n, uint16_t val, uint8_t dflt) {
    for (uint8_t i = 0; i < n; ++i) if (arr[i] == val) return i;
    return dflt;
}

static const uint16_t kTimeoutSecs[] = { 0, 30, 60, 300, 600, 1800, 3600 }; // 0 = never
static const char* kTimeoutOpts = "Never\n30s\n1m\n5m\n10m\n30m\n1h";
static uint8_t s_dimIdx = 0, s_sleepIdx = 0;

static const uint8_t kUsbSuspSecs[] = { 0, 15, 30, 60, 120 };   // 0 = never
static const char* kUsbSuspOpts = "Never\n15s\n30s\n1m\n2m";
static uint8_t s_usbSuspIdx = 2;   // default 30 s

// The STM clamps REG_CTL_LONG_PRESS to 1000..6500 (reg_get_long_press_ms), so
// a 0.5 s option was shown but never applied — the hold was always 1 s.
static constexpr uint16_t kLongPressMs[] = { 1000, 1500, 2000, 3000, 4000 };
static const char* kLongPressOpts = "1s\n1.5s\n2s\n3s\n4s";
static constexpr uint8_t kLongPressN = sizeof(kLongPressMs) / sizeof(kLongPressMs[0]);
static constexpr uint16_t kLongPressDefaultMs = 4000;
static constexpr uint8_t kLongPressDefaultIdx =
    idx_for_u16(kLongPressMs, kLongPressN, kLongPressDefaultMs, 0);
static uint8_t s_longPressIdx = kLongPressDefaultIdx;

// A stored timeout that is not one of the presets (older firmware defaulted the
// dim timeout to 1000 s) used to fall back to index 0 = "Never" while the STM
// happily kept counting — the panel said one thing, the hardware did another.
// Snap to the closest preset instead and write the snapped value back.
static uint8_t nearest_idx_u16(const uint16_t* arr, uint8_t n, uint16_t val) {
    uint8_t best = 0;
    uint32_t bestDist = 0xFFFFFFFFu;
    for (uint8_t i = 0; i < n; ++i) {
        uint32_t d = (arr[i] > val) ? (uint32_t)(arr[i] - val) : (uint32_t)(val - arr[i]);
        if (d < bestDist) { bestDist = d; best = i; }
    }
    return best;
}

// autoSleep is uint8_t in Stm32Settings; use a bool proxy like s_panCcEnProxy
static bool s_autoSleepProxy = true;

static void stm_auto_sleep_cb(lv_event_t*) {
    auto* s = get_settings(); if (!s) return;
    s->stm.autoSleep = s_autoSleepProxy ? 1 : 0;
    crosspad_gui::getGuiPlatform().writeStmConfig8(REG_CTL_AUTO_SLEEP, s->stm.autoSleep);
}
static void stm_keypad_en_cb(lv_event_t*) {
    auto* s = get_settings(); if (!s) return;
    // REG_CTL_KEYPAD_DIS is the INVERSE of keypadEnabled
    crosspad_gui::getGuiPlatform().writeStmConfig8(REG_CTL_KEYPAD_DIS, s->stm.keypadEnabled ? 0 : 1);
}
// The STM only promotes LIGHT → DEEP while sleep_to > dim_to (sleep.c
// light_sleep_tick). Picking sleep <= dim therefore disables sleep silently, so
// the UI repairs the pair instead of storing a combination that does nothing.
static void stm_push_timeouts(crosspad::CrosspadSettings* s) {
    auto& plat = getGuiPlatform();
    plat.writeStmConfig16(REG_CTL_DIM_TO_S_HI, s->stm.dimTimeoutS);
    plat.writeStmConfig16(REG_CTL_SLEEP_TO_S_HI, s->stm.sleepTimeoutS);
}

// Everything the STM keeps from Stm32Settings, for paths that replace the whole
// struct at once (reset to defaults, backup import).
void settings_detail::stm_push_config(crosspad::CrosspadSettings* s) {
    auto& plat = getGuiPlatform();
    for (uint8_t i = 0; i < crosspad::CROSSPAD_MAX_PADS; ++i) plat.writeStmConfigPad(i);
    plat.writeStmConfig8(REG_CTL_MIDI_CHAN,      s->stm.midiChannel);
    plat.writeStmConfig8(REG_CTL_NOTE_OFFSET,    (uint8_t)s->stm.noteOffset);
    plat.writeStmConfig8(REG_CTL_VEL_CURVE_DEF,  s->stm.velCurveDef);
    plat.writeStmConfig8(REG_CTL_VEL_FLOOR_DEF,  s->stm.velFloorDef);
    plat.writeStmConfig8(REG_CTL_CC_PRESS_BASE,  s->stm.ccPressBase);
    plat.writeStmConfig8(REG_CTL_PAN_CC_EN,      s->stm.panCcEn);
    plat.writeStmConfig8(REG_CTL_CC_PAN_BASE,    s->stm.ccPanBase);
    plat.writeStmConfig8(REG_CTL_ANALOG_MIN_MS,  s->stm.analogMinMs);
    plat.writeStmConfig8(REG_CTL_ANALOG_DELTA,   s->stm.analogDelta);
    plat.writeStmConfig8(REG_CTL_BTN_PRESS_DB_MS, s->stm.pressDebounceMs);
    plat.writeStmConfig8(REG_CTL_BTN_HIT_MODE,   s->stm.hitMode);
    plat.writeStmConfig8(REG_CTL_BTN_HIT_MIN_MS, s->stm.hitMinMs);
    plat.writeStmConfig8(REG_CTL_BTN_HIT_PEAK_DROP,    s->stm.hitPeakDrop);
    plat.writeStmConfig8(REG_CTL_BTN_HIT_PEAK_SAMPLES, s->stm.hitPeakSamples);
    plat.writeStmConfig8(REG_CTL_AUTO_SLEEP,     s->stm.autoSleep);
    plat.writeStmConfig8(REG_CTL_KEYPAD_DIS,     s->stm.keypadEnabled ? 0 : 1);
    plat.writeStmConfig8(REG_CTL_WAKE_BRIGHT,    s->stm.wakeBrightness);
    stm_push_timeouts(s);
    plat.writeStmConfig16(REG_CTL_LONG_PRESS_HI, s->stm.longPressMs);
}

static void stm_dim_to_cb(lv_event_t*) {
    auto* s = get_settings(); if (!s) return;
    if (s_dimIdx >= sizeof(kTimeoutSecs)/sizeof(kTimeoutSecs[0])) s_dimIdx = 0;
    s->stm.dimTimeoutS = kTimeoutSecs[s_dimIdx];
    if (s->stm.sleepTimeoutS > 0 && s->stm.sleepTimeoutS <= s->stm.dimTimeoutS) {
        s->stm.sleepTimeoutS = 0;   // "Never" — the honest reading of an unreachable sleep
        s_sleepIdx = 0;
        ui_toast_show("Sleep must be longer than Dim — sleep set to Never",
                      2500, UI_TOAST_WARNING);
        save_settings();
        stm_push_timeouts(s);
        show_detail(s_current_category);
        return;
    }
    stm_push_timeouts(s);
}
static void stm_sleep_to_cb(lv_event_t*) {
    auto* s = get_settings(); if (!s) return;
    if (s_sleepIdx >= sizeof(kTimeoutSecs)/sizeof(kTimeoutSecs[0])) s_sleepIdx = 0;
    s->stm.sleepTimeoutS = kTimeoutSecs[s_sleepIdx];
    if (s->stm.sleepTimeoutS > 0 && s->stm.dimTimeoutS >= s->stm.sleepTimeoutS) {
        // Pull dim down to the next preset below sleep so both stages stay live.
        uint16_t newDim = 0;
        uint8_t newIdx = 0;
        for (uint8_t i = 0; i < sizeof(kTimeoutSecs)/sizeof(kTimeoutSecs[0]); ++i) {
            if (kTimeoutSecs[i] > 0 && kTimeoutSecs[i] < s->stm.sleepTimeoutS &&
                kTimeoutSecs[i] > newDim) { newDim = kTimeoutSecs[i]; newIdx = i; }
        }
        s->stm.dimTimeoutS = newDim;   // 0 → dim disabled, STM goes straight to DEEP
        s_dimIdx = newIdx;
        ui_toast_show("Dim shortened to stay below Sleep", 2500, UI_TOAST_INFO);
        save_settings();
        stm_push_timeouts(s);
        show_detail(s_current_category);
        return;
    }
    stm_push_timeouts(s);
}
static void stm_usb_susp_cb(lv_event_t*) {
    auto* s = get_settings(); if (!s) return;
    if (s_usbSuspIdx >= sizeof(kUsbSuspSecs)/sizeof(kUsbSuspSecs[0])) s_usbSuspIdx = 2;
    s->stm.usbSuspendS = kUsbSuspSecs[s_usbSuspIdx];
    getGuiPlatform().writeStmConfig8(REG_CTL_USB_SUSP_S, s->stm.usbSuspendS);
}
static void stm_long_press_cb(lv_event_t*) {
    auto* s = get_settings(); if (!s) return;
    if (s_longPressIdx >= kLongPressN) s_longPressIdx = kLongPressDefaultIdx;
    s->stm.longPressMs = kLongPressMs[s_longPressIdx];
    crosspad_gui::getGuiPlatform().writeStmConfig16(REG_CTL_LONG_PRESS_HI, s->stm.longPressMs);
}



// ---- Programming ----

static void stm32_dfu_cb(lv_event_t * e);

static void coproc_update_cb(lv_event_t * e) {
    (void)e;
    auto* ui = getSettingsUI();
    if (ui) ui->requestCoprocFwUpdate();
}

void settings_detail::cat_build_programming(lv_obj_t * parent, lv_group_t * group) {
    auto* ui = getSettingsUI();
    settings_section_header(parent, "Co-processor");

    // The platform may carry an STM32 image and flash it itself; then the
    // running and bundled versions sit side by side with the button that
    // starts the platform's own update flow. Same version = "Reflash", the
    // recovery for a co-processor that misbehaves.
    crosspad::CoprocFwInfo fw{};
    if (ui && ui->getCoprocFwInfo(fw)) {
        settings_row_info(parent, "STM32 running", fw.running[0] ? fw.running : "no link");
        if (fw.canUpdate) {
            settings_row_action(parent, group, fw.bundled, fw.due ? "Update" : "Reflash",
                                coproc_update_cb, nullptr);
        } else {
            settings_row_info(parent, "STM32 bundled", fw.bundled);
        }
    }
    settings_row_action(parent, group, "STM32 firmware", "DFU Mode",
                        stm32_dfu_cb, nullptr);
    settings_row_info(parent, "ESP32 firmware", "OTA over USB CDC");
}

// ---- Info ----

/* The notices GPL-3.0 section 5(d) asks an interactive program to show: the
 * copyright and the licence as rows, the missing warranty and where the
 * licence text is in a dialog of one button. */
static lv_obj_t * s_legal_dialog = nullptr;

static void legal_notice_answer_cb(int index, void * user_data) {
    (void)index;
    (void)user_data;
    s_legal_dialog = nullptr;
}

static void legal_notice_cb(lv_event_t * e) {
    (void)e;
    if (s_legal_dialog) return;
    static const ui_modal_button_t buttons[] = {
        {"OK", UI_MODAL_BUTTON_PRIMARY},
    };
    s_legal_dialog = ui_modal_choice(
        "No warranty",
        "Provided as is, with absolutely no warranty. CrossPad is a musical "
        "instrument, not designed or certified for medical, military, vehicle "
        "or other safety-critical use. License: gnu.org/licenses/gpl-3.0",
        buttons, 1, 0, legal_notice_answer_cb, nullptr, s_group);
}

void settings_detail::cat_build_info(lv_obj_t * parent, lv_group_t * group) {
    auto* ui = getSettingsUI();

    settings_section_header(parent, "Device");
    settings_row_info(parent, "Platform", ui ? ui->getPlatformName() : "CrossPad");
    if (ui) {
        const char* profile = ui->getPlatformInfo();
        if (profile) settings_row_info(parent, "Profile", profile);
    }

    // Everything else is platform-reported (firmware/library revisions, STM32
    // identity, serial) so this page grows without touching the shared UI.
    if (ui) {
        int n = ui->getInfoRowCount();
        for (int i = 0; i < n; ++i) {
            const char* label = nullptr;
            const char* value = nullptr;
            if (ui->getInfoRow(i, &label, &value) && label && value) {
                settings_row_info(parent, label, value);
            }
        }
    }

    settings_section_header(parent, "Legal");
    settings_row_info(parent, "Copyright", "\xC2\xA9 CrossPad contributors");
    settings_row_info(parent, "License", "GPL-3.0-or-later");
    settings_row_action(parent, group, "No warranty", "Read", legal_notice_cb, nullptr);
}

// ---- System ----

static void factory_reset_apply() {
    auto * s = CrosspadSettings::getInstance();
    if (!s) return;
    // Reset to defaults by constructing fresh sub-structs
    s->keypad = crosspad::KeypadSettings();
    s->stmRouting = crosspad::STMRoutingSettings();
    s->esp32Routing = crosspad::ESPRoutingSettings();
    s->wireless = crosspad::WirelessSettings();
    s->vibration = crosspad::VibrationSettings();
    s->masterFX = crosspad::AudioSettings();
    s->previewAudio = crosspad::AudioSettings();
    s->stm = crosspad::Stm32Settings();
    s->audioEngine = crosspad::AudioEngineSettings();
    s->LCDbrightness = 100;
    s->themeColorIndex = CROSSPAD_THEME_DEFAULT_INDEX;
    s->perfStatsFlags = 0;
    s->RGBbrightness = 100;
    s->quickSettingsEnabled = true;
    s->launcherStyle = crosspad::LauncherStyle::Pads;
    s->touchSensitivity = crosspad::TouchSensitivity::Low;
    s->AudioEngineEnabled = true;
    s->Kit = 0;
    for (int i = 0; i < crosspad::CROSSPAD_MAX_PADS; i++) {
        s->audio[i] = crosspad::AudioSettings();
    }
    save_settings();

    // Re-apply everything that lives in hardware — a reset that only rewrites
    // NVS leaves the device running the old brightness / LEDs / haptics until
    // the next reboot.
    auto& plat = getGuiPlatform();
    plat.setLcdBrightness(s->LCDbrightness);
    plat.setTouchSensitivity(s->touchSensitivity);
    apply_output_volumes(s);
    crosspad::getPadLedController().setBrightness(s->RGBbrightness);
    crosspad::getVibration().applySettings(s->vibration);
    if (stm_config_available()) stm_push_config(s);
    statusbar_set_drawer_enabled(s->quickSettingsEnabled);
    statusbar_set_perf_level(s->perfStatsFlags);
    reinitStyles();
    plat.onFactoryReset();

    ui_toast_show("Settings reset to defaults", 2000, UI_TOAST_WARNING);
    // The page under the dialog, rebuilt on the new values -- if it is still open.
    if (s_content) show_detail(s_current_category);
}

/* Factory Reset asks first, in the look of the Power off dialog: Cancel first
 * and where the encoder starts, the action in red. */
enum { kResetCancel = 0, kResetConfirm = 1 };
static lv_obj_t * s_reset_dialog = nullptr;

static void factory_reset_answer_cb(int index, void * user_data) {
    (void)user_data;
    s_reset_dialog = nullptr;
    if (index == kResetConfirm) factory_reset_apply();
    // Cancel or the power button's back (-1): nothing changes.
}

static void factory_reset_cb(lv_event_t * e) {
    (void)e;
    if (s_reset_dialog) return;
    static const ui_modal_button_t buttons[] = {
        {"Cancel", UI_MODAL_BUTTON_NORMAL},
        {"Reset",  UI_MODAL_BUTTON_DANGER},
    };
    s_reset_dialog = ui_modal_choice(
        "Factory reset?",
        "Settings go back to their defaults. USB settings, Motion MIDI and "
        "the SD card are kept.",
        buttons, 2, kResetCancel, factory_reset_answer_cb, nullptr, s_group);
}

void settings_detail::system_dialogs_dismiss() {
    // Leaving Settings answers an open question with Cancel.
    if (s_reset_dialog) ui_modal_confirm_dismiss(s_reset_dialog, false);
    if (s_legal_dialog) ui_modal_confirm_dismiss(s_legal_dialog, false);
}

static void stm32_dfu_cb(lv_event_t * e) {
    (void)e;
    auto* ui = getSettingsUI();
    if (ui) ui->enterStm32Dfu();
    lv_CreateDfuScreen(lv_scr_act());
}

void settings_detail::cat_build_system(lv_obj_t * parent, lv_group_t * group) {
    // Versions live under Info and firmware flashing under Programming — System
    // is the runtime behaviour of this unit, nothing else.
    settings_section_header(parent, "Maintenance");

    settings_row_action(parent, group, nullptr, "Factory Reset",
                        factory_reset_cb, nullptr);

    auto* s = get_settings();
    if (s && stm_config_available()) {
        settings_section_header(parent, "STM Power / Input");
        s_autoSleepProxy = s->stm.autoSleep != 0;
        settings_row_switch(parent, group, "Auto Sleep", &s_autoSleepProxy, stm_auto_sleep_cb);
        // STM-side pad scanning (distinct from keypad.enableKeypad, which gates
        // pad→MIDI in PadManager — that one lives under Pads & LEDs).
        settings_row_switch(parent, group, "STM Keypad Scan", &s->stm.keypadEnabled, stm_keypad_en_cb);
        constexpr uint8_t kTimeoutCount = sizeof(kTimeoutSecs)/sizeof(kTimeoutSecs[0]);
        s_dimIdx   = nearest_idx_u16(kTimeoutSecs, kTimeoutCount, s->stm.dimTimeoutS);
        s_sleepIdx = nearest_idx_u16(kTimeoutSecs, kTimeoutCount, s->stm.sleepTimeoutS);
        if (kTimeoutSecs[s_dimIdx] != s->stm.dimTimeoutS ||
            kTimeoutSecs[s_sleepIdx] != s->stm.sleepTimeoutS) {
            s->stm.dimTimeoutS   = kTimeoutSecs[s_dimIdx];
            s->stm.sleepTimeoutS = kTimeoutSecs[s_sleepIdx];
            save_settings();
            stm_push_timeouts(s);
        }
        settings_row_dropdown(parent, group, "Dim After", kTimeoutOpts, &s_dimIdx, stm_dim_to_cb);
        settings_row_dropdown(parent, group, "Sleep After", kTimeoutOpts, &s_sleepIdx, stm_sleep_to_cb);
        // A saved 0.5 s behaved as 1 s on the STM; show it as what it was.
        const uint16_t lpMs = s->stm.longPressMs < 1000 ? 1000 : s->stm.longPressMs;
        s_longPressIdx = idx_for_u16(kLongPressMs, kLongPressN, lpMs, kLongPressDefaultIdx);
        settings_row_dropdown(parent, group, "Power Long-Press", kLongPressOpts, &s_longPressIdx, stm_long_press_cb);
        // Host-suspend grace before the STM drops to soft-off. Was reachable only
        // over I2C — a device on a sleeping host powered itself down after 30 s
        // with no way to change or disable that from the UI.
        s_usbSuspIdx = 2;
        for (uint8_t i = 0; i < sizeof(kUsbSuspSecs)/sizeof(kUsbSuspSecs[0]); ++i)
            if (kUsbSuspSecs[i] == s->stm.usbSuspendS) { s_usbSuspIdx = i; break; }
        settings_row_dropdown(parent, group, "USB Suspend Off", kUsbSuspOpts,
                              &s_usbSuspIdx, stm_usb_susp_cb);
    }
}

} // namespace crosspad_gui

#endif // USE_LVGL
