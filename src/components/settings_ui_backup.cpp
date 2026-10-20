// SPDX-License-Identifier: GPL-3.0-or-later
//
// Settings: the Backup (SD) category and the backup export/import API.

#ifdef USE_LVGL

#include "settings_ui_internal.h"
#include "crosspad/config/crosspad_config.h"
#include "crosspad-gui/components/ui_toast.h"
#include "crosspad-gui/platform/IGuiPlatform.h"
#include "crosspad-gui/styles/styles.h"
#include "crosspad/settings/CrosspadSettings.hpp"
#include "crosspad/settings/ISettingsUI.hpp"
#include "crosspad/settings/SettingsIo.hpp"
#include "crosspad/pad/PadLedController.hpp"
#include "crosspad/stm32/reg_map.h"
#include "crosspad/platform/IVibration.hpp"

#include <cstdio>

using namespace crosspad;

namespace crosspad_gui {

using namespace settings_detail;

#if FEAT_SETTINGS_BACKUP
// ---- Backup (SD) ----
//
// Two independent files: the full settings snapshot and a pad-tuning-only file.
// Pads are separate on purpose — pad calibration belongs to a physical unit and
// is worth carrying between firmware profiles without dragging along someone
// else's brightness, routing and audio setup.

static const char* kBackupAllName  = "crosspad-settings.json";
static const char* kBackupPadsName = "crosspad-pads.json";

static bool backup_path(char* buf, size_t len, const char* file) {
    auto* ui = getSettingsUI();
    const char* dir = ui ? ui->getBackupDir() : nullptr;
    if (!dir) return false;
    snprintf(buf, len, "%s/%s", dir, file);
    return true;
}

static void backup_report(const crosspad::SettingsIoResult& r, const char* okMsg) {
    if (r.ok) {
        ui_toast_show(okMsg, 2000, UI_TOAST_INFO);
    } else {
        char msg[96];
        snprintf(msg, sizeof(msg), "%s", r.error ? r.error : "failed");
        ui_toast_show(msg, 3000, UI_TOAST_WARNING);
    }
}

static crosspad::SettingsIoResult backup_import_all(const char* p) {
    auto* s = get_settings();
    if (!s) return { false, 0, "no settings" };
    auto r = crosspad::settingsImportAll(*s, p);
    if (r.ok) {
        // Imported values only live in RAM until something persists and applies
        // them — do both, then redraw so the page shows what was loaded.
        save_settings();
        auto& plat = getGuiPlatform();
        plat.setLcdBrightness(s->LCDbrightness);
        plat.setTouchSensitivity(s->touchSensitivity);
        apply_output_volumes(s);
        crosspad::getPadLedController().setBrightness(s->RGBbrightness);
        crosspad::getVibration().applySettings(s->vibration);
        if (stm_config_available()) stm_push_config(s);
        reinitStyles();
        // Only a page that is open can be redrawn; the companion imports
        // from the launcher too.
        if (s_content && s_current_category >= 0) show_detail(s_current_category);
    }
    return r;
}

static void backup_export_all_cb(lv_event_t *) {
    auto* s = get_settings(); char p[128];
    if (!s || !backup_path(p, sizeof(p), kBackupAllName)) return;
    backup_report(crosspad::settingsExportAll(*s, p), "Settings exported to SD");
}

static void backup_import_all_cb(lv_event_t *) {
    char p[128];
    if (!backup_path(p, sizeof(p), kBackupAllName)) return;
    backup_report(backup_import_all(p), "Settings imported");
}

bool settings_ui_backup_export(const char* path, const char** error) {
    auto* s = get_settings();
    if (!s) { if (error) *error = "no settings"; return false; }
    auto r = crosspad::settingsExportAll(*s, path);
    if (error) *error = r.ok ? nullptr : r.error;
    return r.ok;
}

bool settings_ui_backup_import(const char* path, const char** error) {
    auto r = backup_import_all(path);
    if (error) *error = r.ok ? nullptr : r.error;
    return r.ok;
}

const char* settings_ui_backup_default_path() {
    static char p[128];
    return backup_path(p, sizeof(p), kBackupAllName) ? p : nullptr;
}

static void backup_export_pads_cb(lv_event_t *) {
    auto* s = get_settings(); char p[128];
    if (!s || !backup_path(p, sizeof(p), kBackupPadsName)) return;
    backup_report(crosspad::settingsExportPads(*s, p), "Pad tuning exported to SD");
}

static void backup_import_pads_cb(lv_event_t *) {
    auto* s = get_settings(); char p[128];
    if (!s || !backup_path(p, sizeof(p), kBackupPadsName)) return;
    auto r = crosspad::settingsImportPads(*s, p);
    backup_report(r, "Pad tuning imported");
    if (r.ok) {
        save_settings();
        if (stm_config_available()) {
            auto& plat = getGuiPlatform();
            for (uint8_t i = 0; i < crosspad::CROSSPAD_MAX_PADS; ++i) plat.writeStmConfigPad(i);
            plat.writeStmConfig8(REG_CTL_VEL_CURVE_DEF, s->stm.velCurveDef);
            plat.writeStmConfig8(REG_CTL_VEL_FLOOR_DEF, s->stm.velFloorDef);
            plat.writeStmConfig8(REG_CTL_CC_PRESS_BASE, s->stm.ccPressBase);
            plat.writeStmConfig8(REG_CTL_PAN_CC_EN,     s->stm.panCcEn);
            plat.writeStmConfig8(REG_CTL_CC_PAN_BASE,   s->stm.ccPanBase);
            plat.writeStmConfig8(REG_CTL_ANALOG_MIN_MS, s->stm.analogMinMs);
            plat.writeStmConfig8(REG_CTL_ANALOG_DELTA,  s->stm.analogDelta);
            plat.writeStmConfig8(REG_CTL_BTN_PRESS_DB_MS, s->stm.pressDebounceMs);
        }
    }
}

void settings_detail::cat_build_backup(lv_obj_t * parent, lv_group_t * group) {
    char path[128];
    if (!backup_path(path, sizeof(path), kBackupAllName)) {
        settings_row_info(parent, "Storage", "no SD card support");
        return;
    }

    settings_section_header(parent, "All settings");
    settings_row_action(parent, group, kBackupAllName, "Export",
                        backup_export_all_cb, nullptr);
    settings_row_action(parent, group, nullptr, "Import",
                        backup_import_all_cb, nullptr);

    settings_section_header(parent, "Pad tuning only");
    settings_row_action(parent, group, kBackupPadsName, "Export",
                        backup_export_pads_cb, nullptr);
    settings_row_action(parent, group, nullptr, "Import",
                        backup_import_pads_cb, nullptr);

    char ver[32];
    snprintf(ver, sizeof(ver), "v%u", (unsigned)crosspad::kSettingsFileVersion);
    settings_row_info(parent, "File format", ver);
}
#else
bool settings_ui_backup_export(const char*, const char** error) {
    if (error) *error = "backup not built";
    return false;
}
bool settings_ui_backup_import(const char*, const char** error) {
    if (error) *error = "backup not built";
    return false;
}
const char* settings_ui_backup_default_path() { return nullptr; }
#endif  // FEAT_SETTINGS_BACKUP

} // namespace crosspad_gui

#endif // USE_LVGL
