// SPDX-License-Identifier: GPL-3.0-or-later
#ifdef USE_LVGL

#include "crosspad-gui/fw/fw_updates_view.h"

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "crosspad-gui/components/app_orchestrator.h"
#include "crosspad-gui/components/settings_ui.h"
#include "crosspad-gui/fw/fw_actions.h"
#include "crosspad-gui/fw/fw_app.h"
#include "crosspad-gui/fw/fw_views_common.h"
#include "crosspad-gui/platform/psram_placement.h"
#include "crosspad/fw/FwStore.hpp"
#include "fw_text.h"

using crosspad_gui::FwLibItem;
using crosspad_gui::FwLibrarySnap;
using crosspad_gui::FwUpdaterState;
using crosspad_gui::fwViewService;
using crosspad_gui::kFwLibMax;
using crosspad_gui::kFwNetTabUpdates;
using crosspad_gui::kFwuErrorMax;
using crosspad_gui::kFwuVersionMax;

static constexpr size_t kStatusMax = 112;
static constexpr size_t kRowTextMax = 96;
static constexpr size_t kDateMax = 16;     // "2026-10-02"
static constexpr size_t kCheckedMax = sizeof "checked " + kDateMax;   // longer than "not checked yet"
static constexpr size_t kTailMax = 4;
static constexpr size_t kRowsMax = crosspad::kFwCatalogKits;
static constexpr size_t kSlotCount = 2;
static constexpr size_t kOwnedMax = kFwLibMax + kSlotCount;   // the library and both slots
static constexpr int32_t kStatusPad = 4;   // as the settings rows around it
static const lv_color_t kCaptionColor = lv_color_make(160, 160, 160);   // as the settings rows' labels
static constexpr char kSettingsApp[] = "Settings";
static constexpr char kSettingsCategory[] = "Updates";   // B's Settings > Updates page
/* A prefix: the radio gate says "no_mem", a job that could not get its heap
 * "no_mem:<where>" -- either way a restart in network mode frees the memory. */
static constexpr char kNoMem[] = "no_mem";

namespace {
struct UpdatesView {
    lv_obj_t* root;
    lv_obj_t* status;   // the full-width value under the Status caption
    lv_obj_t* runRow;
    lv_obj_t* list;
    lv_group_t* group;
    lv_obj_t* tail[kTailMax];
    size_t tailCount;
    uint32_t storeGen;
    uint32_t libGen;
    uint32_t updaterGen;
    bool built;
    size_t rowCount;
    crosspad::FwUpdateRow rows[kRowsMax];
    crosspad::FwOwned owned[kOwnedMax];   // scratch for rebuild_rows(): too big for the LVGL task's stack
};
}  // namespace

/* PSRAM: nothing on this screen needs fast RAM. */
CROSSPAD_GUI_PSRAM_BSS static UpdatesView s;

bool fw_updates_is_no_mem(const char* err) {
    return strncmp(err, kNoMem, strlen(kNoMem)) == 0;
}

/* What the user can do about it, in words. */
const char* fw_updates_error_text(const char* err) {
    static const struct { const char* token; const char* text; } kTexts[] = {
        {"disabled", "WiFi is off - see Settings > WiFi"},
        {"no_network", "no WiFi network in reach"},
        {"auth", "wrong WiFi password"},
        {"timeout", "no answer from the WiFi network"},
        {"wifi_timeout", "no answer from the WiFi network"},
        {"lost", "WiFi connection lost"},
        {"card_full", "the SD card is full"},
        {"no_card", "no SD card"},
        {"signature", "the package is not signed by CrossPad"},
        {"mismatch", "the package is not for this CrossPad"},
        {"sha", "the download was damaged"},
        {"network", "the connection was interrupted - try again"},
        {"blocked", "this version is not offered for this CrossPad"},
        {"bad_args", "no such update"},
        {"not_in_catalog", "no such update"},
        {"transfer_mode", "not available while the SD card is a USB drive"},
        {"not_network_mode", "WiFi is not available here"},
        {"http", "crosspad.app refused the request"},
        {"parse", "crosspad.app sent an answer this CrossPad cannot read"},
        {"io", "the SD card could not be written"},
    };
    if (strcmp(err, kNoMem) == 0) return "not enough memory for WiFi";   // the radio gate
    if (fw_updates_is_no_mem(err)) return "not enough memory";           // a job's own heap
    for (const auto& t : kTexts)
        if (strcmp(err, t.token) == 0) return t.text;
    if (strncmp(err, "catalog", strlen("catalog")) == 0) return "crosspad.app did not answer";
    return err;   // install:<reason> and anything new read as they are
}

/* "checked 2026-10-02"; "checked" when the catalog's answer carried no usable
 * Date; "not checked yet" before the first check that succeeded. */
static void checked_text(char* out, size_t n) {
    if (!fwViewService().updaterChecked()) {
        snprintf(out, n, "not checked yet");
        return;
    }
    const int64_t at = fwViewService().updaterLastCheck();
    if (at <= 0) {
        snprintf(out, n, "checked");
        return;
    }
    char date[kDateMax];
    fw_format_date(at, date, sizeof date);
    snprintf(out, n, "checked %s", date);
}

static void status_text(char* out, size_t n) {
    char ver[kFwuVersionMax];
    char err[kFwuErrorMax];
    char checked[kCheckedMax];
    const char* wifi = fw_views_wifi_reason();
    switch (fwViewService().updaterState()) {
        case FwUpdaterState::Checking:
            snprintf(out, n, "Checking...");
            return;
        case FwUpdaterState::Downloading:
            fwViewService().updaterTarget(ver, sizeof ver);
            snprintf(out, n, "Downloading %s... %u %%", ver, static_cast<unsigned>(fwViewService().updaterProgress()));
            return;
        case FwUpdaterState::Installing:
            fwViewService().updaterTarget(ver, sizeof ver);
            snprintf(out, n, "Installing %s... %u %%", ver, static_cast<unsigned>(fwViewService().updaterProgress()));
            return;
        case FwUpdaterState::ReadyToRestart:
            fwViewService().updaterReadyVersion(ver, sizeof ver);
            snprintf(out, n, "%s installed in the other slot", ver);
            return;
        case FwUpdaterState::Failed:
            fwViewService().updaterError(err, sizeof err);
            snprintf(out, n, "Error: %s", fw_updates_error_text(err));
            return;
        default:
            break;
    }
    checked_text(checked, sizeof checked);
    if (wifi[0]) snprintf(out, n, "%s (%s)", fw_updates_error_text(wifi), checked);
    else snprintf(out, n, "Running %s, %s", fwViewService().runningVersion(), checked);
}

void fw_updates_keep_focus_in_view(lv_group_t * group) {
    lv_obj_t * focused = group ? lv_group_get_focused(group) : nullptr;
    if (!focused) return;
    lv_obj_update_layout(focused);
    lv_obj_scroll_to_view_recursive(focused, LV_ANIM_OFF);
}

static void set_shown(lv_obj_t* obj, bool shown) {
    if (!obj || lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN) == !shown) return;   // unchanged: no repaint
    if (shown) {
        lv_obj_remove_flag(obj, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    /* The press that hid this row left the encoder on it: step on to the next
     * control that can still be reached. */
    lv_obj_t* focused = lv_group_get_focused(s.group);
    const bool hadFocus = focused && lv_obj_get_parent(focused) == obj;
    lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
    if (hadFocus) lv_group_focus_next(s.group);
}

static bool is_beta(const char* kit, void*) { return fwViewService().channelBeta(kit); }

/* Every kit@version the card or a slot holds. */
static size_t owned_kits(crosspad::FwOwned* out, size_t cap) {
    size_t n = 0;
    if (const FwLibrarySnap* lib = fwViewService().library()) {
        for (size_t i = 0; i < lib->count && n < cap; ++i) {
            const FwLibItem& it = lib->items[i];
            if (!it.complete || !it.kit[0]) continue;
            crosspad_gui::fw_text::copy(out[n].kit, it.kit, sizeof out[n].kit);
            crosspad_gui::fw_text::copy(out[n].version, it.version, sizeof out[n].version);
            ++n;
        }
    }
    const crosspad::FwSlotInfo* slots = fw_views_slots();   // a board flashed over USB has its kit in a slot only
    for (size_t i = 0; i < kSlotCount && n < cap; ++i) {
        if (slots[i].state == crosspad::FwSlotState::Empty || !slots[i].kit[0]) continue;
        crosspad_gui::fw_text::copy(out[n].kit, slots[i].kit, sizeof out[n].kit);
        crosspad_gui::fw_text::copy(out[n].version, fw_strip_v(slots[i].version), sizeof out[n].version);
        ++n;
    }
    return n;
}

static void update_cb(lv_event_t* e) {
    const auto i = static_cast<size_t>(reinterpret_cast<uintptr_t>(lv_event_get_user_data(e)));
    const crosspad::FwStore* store = fwViewService().store();
    // Rows name indices of the catalog they were built from: a newer one waits for the next rebuild.
    if (!store || i >= s.rowCount || fwViewService().storeGen() != s.storeGen) return;
    const crosspad::FwUpdateRow& row = s.rows[i];
    if (row.kit >= store->kitCount || row.version >= store->kits[row.kit].count) return;
    const crosspad::FwStoreKit& k = store->kits[row.kit];
    FwTarget t{};
    crosspad_gui::fw_text::copy(t.kit, k.kit, sizeof t.kit);
    crosspad_gui::fw_text::copy(t.version, k.versions[row.version].version, sizeof t.version);
    t.community = k.versions[row.version].tier == crosspad::FwTier::Community;
    fw_action_start(crosspad_gui::kFwActInstall, t, kFwNetTabUpdates, s.group);
}

/* The row the encoder is on, -1 when it is elsewhere. */
static int32_t focused_row(void) {
    for (lv_obj_t* o = lv_group_get_focused(s.group); o; o = lv_obj_get_parent(o))
        if (lv_obj_get_parent(o) == s.list) return lv_obj_get_index(o);
    return -1;
}

/* New rows join the group after the host's tail: the tail goes back behind
 * them, the encoder staying where it was. */
static void retail(void) {
    lv_obj_t* focused = lv_group_get_focused(s.group);
    for (size_t i = 0; i < s.tailCount; ++i) {
        if (lv_obj_get_group(s.tail[i]) != s.group) continue;
        lv_group_remove_obj(s.tail[i]);
        lv_group_add_obj(s.group, s.tail[i]);
    }
    if (focused) lv_group_focus_obj(focused);
}

static constexpr char kTab[] = "updates";

/* FW_UI's items while this is the tab on screen: network mode keeps the Store built beside it. */
static void publish_items(void) {
    if (fwViewService().uiTabIs(kTab)) fwViewService().uiItems(static_cast<uint32_t>(s.rowCount));
}

static void rebuild_rows(void) {
    const crosspad::FwStore* store = fwViewService().store();
    const int32_t wasRow = focused_row();
    s.rowCount = 0;
    if (store) {
        const size_t n = owned_kits(s.owned, kOwnedMax);
        s.rowCount = crosspad::fwStoreUpdates(*store, s.owned, n, is_beta, nullptr, s.rows, kRowsMax);
    }
    lv_obj_clean(s.list);   // from the host's timer, never an event of these rows: deleting them now is safe
    if (s.rowCount == 0) {
        lv_obj_t* l = lv_label_create(s.list);
        lv_obj_add_style(l, fw_views_text_style(), 0);
        lv_label_set_text(l, store ? "Everything on the card is up to date." : "No catalog on the card yet.");
    }
    for (size_t i = 0; i < s.rowCount; ++i) {
        const crosspad::FwStoreKit& k = store->kits[s.rows[i].kit];
        char text[kRowTextMax];
        snprintf(text, sizeof text, "%s %s -> %s", k.displayName, s.rows[i].have, k.versions[s.rows[i].version].version);
        crosspad_gui::settings_row_action(s.list, s.group, text, "Update", update_cb,
                                          reinterpret_cast<void*>(static_cast<uintptr_t>(i)));
    }
    if (s.rowCount > 0) retail();
    if (wasRow >= 0) {
        // The encoder was on a row that is gone now: the same place in the new list, else Check now.
        const auto last = static_cast<int32_t>(s.rowCount) - 1;
        if (last < 0) fw_views_focus_first(s.root, s.group);
        else fw_views_focus_first(lv_obj_get_child(s.list, wasRow < last ? wasRow : last), s.group);
    }
    publish_items();
}

/* The view lets go of everything it holds: the lease, the catalog, the views. */
static void forget(void) {
    fw_views_tab_closed(kFwNetTabUpdates);
    memset(&s, 0, sizeof s);   // in place: a value-initialised temporary would be ~2 kB of the LVGL task's stack
    publish_items();
    fw_views_want_store(false);
    fw_views_release();
}

/* A host that deletes the parent without fw_updates_view_close() leaves no
 * dangling root behind and still gives the lease back. */
static void root_deleted_cb(lv_event_t* e) {
    if (lv_event_get_target(e) != s.root) return;   // close() let it go already
    forget();
}

static void check_cb(lv_event_t*) { fw_views_check_now(kFwNetTabUpdates, s.group); }

static void run_cb(lv_event_t*) {
    FwTarget t{};
    fwViewService().updaterTargetKit(t.kit, sizeof t.kit);
    fwViewService().updaterReadyVersion(t.version, sizeof t.version);
    fw_action_start(crosspad_gui::kFwActRunNew, t, kFwNetTabUpdates, s.group);
}

/* A cycle after the press, not inside it: opening Settings destroys the
 * Firmware app, this button with it. */
static void open_settings_async(void*) {
    lv_obj_t* container = crosspad_gui::fw_app_container();
    if (!s.root || !container) return;   // the app closed since the press
    for (const auto& app : crosspad_gui::AppOrchestrator::getInstance().getApps()) {
        if (app && strcmp(app->getName(), kSettingsApp) == 0) {
            crosspad_gui::settings_ui_open_next(kSettingsCategory);
            crosspad_gui::AppOrchestrator::onAppSelected(app.get(), container);   // closes the Firmware app first
            return;
        }
    }
}

static void settings_cb(lv_event_t*) { lv_async_call(open_settings_async, nullptr); }

void fw_updates_view_build(lv_obj_t* parent, lv_group_t* group) {
    if (s.root) return;
    fw_views_acquire();
    fw_views_want_store(true);
    s.group = group;
    s.root = lv_obj_create(parent);
    lv_obj_remove_style_all(s.root);
    lv_obj_set_size(s.root, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(s.root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(s.root, kFwPad, 0);
    lv_obj_add_event_cb(s.root, root_deleted_cb, LV_EVENT_DELETE, nullptr);
    /* A caption over a full-width wrapped value: the version being offered and
     * the error texts are too long for half a row. */
    lv_obj_t* row = lv_obj_create(s.root);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_hor(row, kStatusPad, 0);
    lv_obj_t* caption = lv_label_create(row);
    lv_label_set_text(caption, "Status");
    lv_obj_set_style_text_color(caption, kCaptionColor, 0);
    lv_obj_set_style_text_font(caption, &lv_font_montserrat_14, 0);
    s.status = lv_label_create(row);
    lv_label_set_text(s.status, "");
    lv_obj_set_width(s.status, LV_PCT(100));
    lv_label_set_long_mode(s.status, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(s.status, lv_color_white(), 0);
    lv_obj_set_style_text_font(s.status, &lv_font_montserrat_14, 0);
    crosspad_gui::settings_row_action(s.root, group, "Check for updates", "Check now", check_cb);
    s.runRow = crosspad_gui::settings_row_action(s.root, group, "Run the new version", "Run", run_cb);
    lv_obj_add_flag(s.runRow, LV_OBJ_FLAG_HIDDEN);
    if (!fwViewService().networkMode()) {   // Settings is not there in network mode
        crosspad_gui::settings_row_action(s.root, group, "Channel, check at startup", "Settings", settings_cb);
    }
    crosspad_gui::settings_section_header(s.root, "For the kits on the card");
    s.list = lv_obj_create(s.root);
    lv_obj_remove_style_all(s.list);
    lv_obj_set_size(s.list, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(s.list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(s.list, kFwPad, 0);
    s.storeGen = fwViewService().storeGen();
    s.libGen = fwViewService().libraryGen();
    s.updaterGen = fw_views_updater_gen();
    publish_items();
    fwViewService().scanLibrary();
    fw_updates_view_refresh();
}

void fw_updates_view_set_tail(lv_obj_t* const* objs, size_t n) {
    s.tailCount = n < kTailMax ? n : kTailMax;
    for (size_t i = 0; i < s.tailCount; ++i) s.tail[i] = objs[i];
}

void fw_updates_view_shown(void) {
    if (!s.root) return;
    fwViewService().uiTab(kTab);
    fwViewService().uiSel(-1, nullptr, nullptr);
    publish_items();
    fw_views_tab_shown(kFwNetTabUpdates, s.group);
}

void fw_updates_view_refresh(void) {
    if (!s.root) return;
    fw_views_poll();   // a press made on another tab settles here too
    char text[kStatusMax];
    status_text(text, sizeof text);
    if (strcmp(lv_label_get_text(s.status), text) != 0) {
        lv_label_set_text(s.status, text);
        fw_updates_keep_focus_in_view(s.group);
    }
    set_shown(s.runRow, fwViewService().updaterState() == FwUpdaterState::ReadyToRestart);
    const uint32_t storeGen = fwViewService().storeGen();
    const uint32_t libGen = fwViewService().libraryGen();
    const uint32_t updaterGen = fw_views_updater_gen();
    if (s.built && storeGen == s.storeGen && libGen == s.libGen && updaterGen == s.updaterGen) return;
    if (!fwViewService().storeLoaded() || !fwViewService().library()) return;   // rows from half the facts would mislead
    s.built = true;
    s.storeGen = storeGen;
    s.libGen = libGen;
    s.updaterGen = updaterGen;
    rebuild_rows();
}

void fw_updates_view_close(void) {
    if (!s.root) return;
    lv_obj_t* root = s.root;
    forget();
    lv_obj_add_flag(root, LV_OBJ_FLAG_HIDDEN);   // nothing draws it again before it goes
    lv_obj_delete_async(root);
}

#endif // USE_LVGL
