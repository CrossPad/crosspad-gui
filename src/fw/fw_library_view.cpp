// SPDX-License-Identifier: GPL-3.0-or-later
#ifdef USE_LVGL

#include "crosspad-gui/fw/fw_library_view.h"

#include <cstdio>
#include <cstring>

#include <strings.h>

#include "crosspad-gui/fw/fw_actions.h"
#include "crosspad-gui/fw/fw_kit_card.h"
#include "crosspad-gui/fw/fw_views_common.h"
#include "crosspad-gui/platform/psram_placement.h"
#include "crosspad/fw/Semver.hpp"

using crosspad_gui::FwIconSrc;
using crosspad_gui::FwLibItem;
using crosspad_gui::FwLibrarySnap;
using crosspad_gui::FwUpdaterState;
using crosspad_gui::fwViewService;
using crosspad_gui::kFwLibMax;
using crosspad_gui::kFwNetTabStore;
using crosspad_gui::kFwuKitMax;
using crosspad_gui::kFwuVersionMax;

static constexpr size_t kMetaMax = 64;
static constexpr size_t kFooterMax = 96;
static constexpr uint64_t kMiB = 1024ull * 1024ull;
static constexpr int32_t kFooterH = 14;
static constexpr int32_t kStatusH = 14;
static constexpr size_t kSlots = 2;
/* A first scan that found no memory (for its snapshot or the worker) is asked
 * again, waiting twice as long each time, then the tab says so. */
static constexpr uint8_t kScanTries = 4;
static constexpr uint32_t kScanRetryMs = 1000;
static constexpr uint8_t kSlotOnly = static_cast<uint8_t>(kFwLibMax);   // order[] values from here: slotOnly[]

namespace {
struct LibraryView {
    lv_obj_t* root;
    lv_obj_t* status;
    lv_obj_t* grid;
    lv_obj_t* footer;
    lv_group_t* group;
    lv_obj_t* focusAtBuild;   // where the encoder was when the tab opened (compared, never used)
    uint32_t libGen;
    bool selectedOnce;
    uint8_t scanTries;
    uint32_t lastScanTick;
    size_t count;
    uint8_t order[kFwLibMax + kSlots];   // tile index -> FwLibrarySnap item, or kSlotOnly + slot
    FwLibItem slotOnly[kSlots];          // slot images whose package is not on the card
};
}  // namespace

/* PSRAM: two items are ~300 B of internal RAM nothing on this screen needs fast. */
CROSSPAD_GUI_PSRAM_BSS static LibraryView s;

static const FwLibItem* item_at_order(uint8_t o) {
    if (o >= kSlotOnly) return &s.slotOnly[o - kSlotOnly];
    const FwLibrarySnap* lib = fwViewService().library();
    return lib ? &lib->items[o] : nullptr;
}

static const FwLibItem* item_at(uint32_t index) {
    if (index >= s.count) return nullptr;   // first: after a close there is no snapshot to look at
    return item_at_order(s.order[index]);
}

static char ascii_lower(char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c; }

static int compare_names(const char* a, const char* b) {
    for (;; ++a, ++b) {
        const char ca = ascii_lower(*a);
        const char cb = ascii_lower(*b);
        if (ca != cb || !ca) return static_cast<unsigned char>(ca) - static_cast<unsigned char>(cb);
    }
}

/* Slot rank: the running image first, the other slot second, then the rest. */
static int slot_rank(const FwLibItem& it) {
    if (!it.slot) return 2;
    return it.running ? 0 : 1;
}

static bool before(const FwLibItem& a, const FwLibItem& b) {
    const int ra = slot_rank(a);
    const int rb = slot_rank(b);
    if (ra != rb) return ra < rb;
    const int byName = compare_names(a.displayName, b.displayName);
    if (byName != 0) return byName < 0;
    const int byVersion = crosspad::compareVersionText(a.version, b.version);
    if (byVersion != 0) return byVersion > 0;   // newest first
    return strcmp(a.name, b.name) < 0;
}

/* The package the updater is downloading into right now. */
static bool downloading_into(const char* name) {
    if (fwViewService().updaterState() != FwUpdaterState::Downloading) return false;
    char kit[kFwuKitMax];
    char version[kFwuVersionMax];
    char target[crosspad::kFwPackageNameMax];
    fwViewService().updaterTargetKit(kit, sizeof kit);
    fwViewService().updaterTarget(version, sizeof version);
    crosspad::fwPackageName(kit, version, target, sizeof target);
    return strcmp(target, name) == 0;
}

static void meta_of(const FwLibItem& it, char* out, size_t n, bool* dim) {
    *dim = false;
    if (it.onCard && !it.complete) {
        if (downloading_into(it.name)) snprintf(out, n, LV_SYMBOL_DOWNLOAD " %u %%", static_cast<unsigned>(fwViewService().updaterProgress()));
        else snprintf(out, n, "incomplete");
        return;
    }
    if (it.verdict != crosspad::FwPkgVerdict::Ok && it.verdict != crosspad::FwPkgVerdict::UnknownBuild) {
        snprintf(out, n, "%s", crosspad::fwPkgVerdictText(it.verdict));
        *dim = true;
        return;
    }
    if (it.slot && it.slotState == crosspad::FwSlotState::RolledBack) {
        snprintf(out, n, "%c " LV_SYMBOL_WARNING " rolled back", it.slot);   // it did not start last time
        *dim = true;
        return;
    }
    char slotText[16] = "";
    if (it.slot) snprintf(slotText, sizeof slotText, " %s%c", it.running ? LV_SYMBOL_PLAY : "", it.slot);
    snprintf(out, n, "%s%s%s%s", it.version[0] ? it.version : "-", slotText,
             it.community ? " " LV_SYMBOL_WARNING : "", it.pinned ? " " LV_SYMBOL_SAVE : "");
}

static void fill_cb(lv_obj_t* tile, uint32_t index, void*) {
    const FwLibItem* it = item_at(index);
    if (!it) return;
    char meta[kMetaMax];
    bool dim = false;
    meta_of(*it, meta, sizeof meta, &dim);
    fw_tile_show(tile, index, FwTileView{it->displayName, meta, dim});
}

static bool icon_for(uint32_t index, FwIconKey* key) {
    const FwLibItem* it = item_at(index);
    if (!it || it->icon == FwIconSrc::None) return false;
    if (it->icon == FwIconSrc::Package) strlcpy(key->pkg, it->name, sizeof key->pkg);
    strlcpy(key->kit, it->kit, sizeof key->kit);
    strlcpy(key->version, it->version, sizeof key->version);
    return true;
}

static void selected_cb(uint32_t index, void*) {
    const FwLibItem* it = item_at(index);
    fwViewService().uiSel(it ? static_cast<int32_t>(index) : -1, it ? it->kit : nullptr, it ? it->version : nullptr);
}

/* Slots from fw_slots onto the card's items, and an item of its own for each
 * slot image whose package is not on the card (a board flashed over USB). */
static void merge_slots(FwLibrarySnap& lib) {
    for (size_t i = 0; i < lib.count; ++i) {
        lib.items[i].slot = 0;
        lib.items[i].running = false;
        lib.items[i].slotState = crosspad::FwSlotState::Empty;
    }
    size_t n = 0;
    const crosspad::FwSlotInfo* slots = fw_views_slots();
    for (size_t k = 0; k < kSlots; ++k) {
        const crosspad::FwSlotInfo& sl = slots[k];
        if (sl.state == crosspad::FwSlotState::Empty) continue;
        bool onCard = false;
        // FATFS names compare case-insensitively, as the cleanup compares them.
        for (size_t i = 0; i < lib.count && sl.package[0]; ++i) {
            if (strcasecmp(lib.items[i].name, sl.package) != 0) continue;
            lib.items[i].slot = sl.label;
            lib.items[i].running = sl.running;
            lib.items[i].slotState = sl.state;
            onCard = true;
        }
        if (onCard) continue;
        FwLibItem& it = s.slotOnly[n];
        it = FwLibItem{};
        s.order[s.count++] = static_cast<uint8_t>(kSlotOnly + n);
        ++n;
        strlcpy(it.kit, sl.kit, sizeof it.kit);
        strlcpy(it.version, fw_strip_v(sl.version), sizeof it.version);
        if (it.kit[0]) {
            crosspad::fwCopyText(it.displayName, sizeof it.displayName, it.kit);
            it.icon = FwIconSrc::Cache;   // the Store's cached icon of that version, if the card has it
        } else {
            snprintf(it.displayName, sizeof it.displayName, "Slot %c", sl.label);
        }
        it.verdict = crosspad::FwPkgVerdict::Ok;
        it.complete = true;
        it.slot = sl.label;
        it.running = sl.running;
        it.slotState = sl.state;
    }
}

static void window_cb(void*) {
    if (s.grid) fw_tile_icons_sync(s.grid, icon_for);
}

static void footer_text(const FwLibrarySnap& lib, char* out, size_t n) {
    const crosspad::FwSlotInfo* slots = fw_views_slots();
    snprintf(out, n, "SD: %u/%u MB  \xC2\xB7  A%s  \xC2\xB7  B%s", static_cast<unsigned>(lib.bytes / kMiB),
             static_cast<unsigned>(fwViewService().libraryLimitMb()), slots[0].running ? " " LV_SYMBOL_PLAY : "",
             slots[1].running ? " " LV_SYMBOL_PLAY : "");
}

static void rebuild(void) {
    FwLibrarySnap* lib = fwViewService().library();
    if (!lib) return;
    s.count = lib->count;
    for (size_t i = 0; i < s.count; ++i) s.order[i] = static_cast<uint8_t>(i);
    merge_slots(*lib);
    for (size_t i = 1; i < s.count; ++i) {   // insertion sort: 34 items at most, stable
        const uint8_t v = s.order[i];
        size_t j = i;
        for (; j > 0 && before(*item_at_order(v), *item_at_order(s.order[j - 1])); --j) s.order[j] = s.order[j - 1];
        s.order[j] = v;
    }
    lv_obj_add_flag(s.status, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(s.grid, LV_OBJ_FLAG_HIDDEN);
    lv_obj_t* focused = lv_group_get_focused(s.group);
    const bool onGrid = focused && lv_obj_get_parent(focused) == s.grid;
    const crosspad_gui::TileGridSection all{nullptr, static_cast<uint32_t>(s.count)};
    crosspad_gui::ui_tile_grid_set_sections(s.grid, &all, 1);
    char footer[kFooterMax];
    footer_text(*lib, footer, sizeof footer);
    lv_label_set_text(s.footer, footer);
    fwViewService().uiItems(static_cast<uint32_t>(s.count));
    const int32_t sel = crosspad_gui::ui_tile_grid_selected(s.grid);
    const bool firstLanding = !s.selectedOnce;
    s.selectedOnce = true;
    if (s.count == 0) {
        fwViewService().uiSel(-1, nullptr, nullptr);
    } else if (firstLanding) {
        // The tab was just opened: the encoder lands on the first tile, unless
        // the user has turned it elsewhere (the tab bar) while the card was read.
        lv_obj_t* now = lv_group_get_focused(s.group);
        const bool untouched = !now || now == s.focusAtBuild || onGrid;
        crosspad_gui::ui_tile_grid_select(s.grid, 0, untouched);
    } else {
        // The same index may now be another package; an encoder on the grid stays
        // there, one elsewhere is not taken (items arriving in an empty Library).
        crosspad_gui::ui_tile_grid_select(s.grid, sel >= 0 ? static_cast<uint32_t>(sel) : 0, onGrid);
    }
    window_cb(nullptr);
}

static void clicked_cb(uint32_t index, void*) {
    const FwLibItem* it = item_at(index);
    if (!it) return;
    FwTarget t{};
    strlcpy(t.kit, it->kit, sizeof t.kit);
    strlcpy(t.version, it->version, sizeof t.version);
    strlcpy(t.pkg, it->name, sizeof t.pkg);
    if (!it->onCard) t.slot = it->slot;   // a slot image only: the card finds it by its slot
    t.community = it->community;
    t.unknownBuild = it->verdict == crosspad::FwPkgVerdict::UnknownBuild;
    t.stmProtoOlder = it->stmProtoOlder;
    fw_kit_card_open(t, kFwNetTabStore);   // a refused download from here offers network mode on the Store
}

/* The view lets go of everything it holds: icons, the selection, the lease. */
static void forget(void) {
    fw_tile_icons_release(s.grid);
    s = LibraryView{};
    fwViewService().uiSel(-1, nullptr, nullptr);
    fwViewService().uiItems(0);
    fw_views_release();
}

/* A host that deletes the parent without fw_library_view_close() (the
 * Firmware app's root deleted with the screen) leaves no dangling root. */
static void root_deleted_cb(lv_event_t* e) {
    if (lv_event_get_target(e) != s.root) return;   // close() let it go already
    forget();
}

void fw_library_view_build(lv_obj_t* parent, lv_group_t* group) {
    if (s.root) return;
    fw_views_acquire();
    s.group = group;
    s.focusAtBuild = lv_group_get_focused(group);
    s.root = lv_obj_create(parent);
    lv_obj_remove_style_all(s.root);
    lv_obj_set_size(s.root, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_flow(s.root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(s.root, kFwPad, 0);
    lv_obj_remove_flag(s.root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(s.root, root_deleted_cb, LV_EVENT_DELETE, nullptr);
    s.status = lv_label_create(s.root);
    lv_obj_add_style(s.status, fw_views_text_style(), 0);
    lv_obj_set_height(s.status, kStatusH);
    lv_label_set_text(s.status, "Scanning the card...");
    lv_obj_update_layout(parent);
    const int32_t gridH = lv_obj_get_content_height(s.root) - kFooterH - kFwPad;
    crosspad_gui::TileGridConfig cfg;
    cfg.fill = fill_cb;
    cfg.clicked = clicked_cb;
    cfg.selected = selected_cb;
    cfg.window = window_cb;
    cfg.emptyText = "No firmware on the card yet";
    s.grid = fw_grid_create(s.root, group, cfg, gridH);
    lv_obj_add_flag(s.grid, LV_OBJ_FLAG_HIDDEN);   // until the first scan: "empty" would be a lie
    s.footer = lv_label_create(s.root);
    lv_obj_add_style(s.footer, fw_views_text_style(), 0);
    lv_obj_set_size(s.footer, LV_PCT(100), kFooterH);
    lv_label_set_long_mode(s.footer, LV_LABEL_LONG_DOT);
    lv_label_set_text(s.footer, "");
    fwViewService().uiTab("library");
    fwViewService().uiItems(0);
    s.libGen = fwViewService().libraryGen();
    s.scanTries = 1;
    s.lastScanTick = lv_tick_get();
    fwViewService().scanLibrary();
}

static void set_status(const char* text) {
    if (strcmp(lv_label_get_text(s.status), text) != 0) lv_label_set_text(s.status, text);   // same text: no repaint
}

/* No snapshot and nothing on its way: the scan failed or was never queued. */
static void retry_first_scan(void) {
    if (fwViewService().library() || fwViewService().dataBusy()) return;
    if (s.scanTries >= kScanTries) {
        set_status("Could not read the SD card");
        return;
    }
    if (lv_tick_elaps(s.lastScanTick) < (kScanRetryMs << (s.scanTries - 1))) return;
    set_status("Could not read the card - trying again...");
    ++s.scanTries;
    s.lastScanTick = lv_tick_get();
    fwViewService().scanLibrary();
}

void fw_library_view_refresh(void) {
    if (!s.root) return;
    fw_views_poll();
    const uint32_t gen = fwViewService().libraryGen();
    if (gen != s.libGen) {
        s.libGen = gen;
        rebuild();
        return;
    }
    if (!fwViewService().library()) {
        retry_first_scan();
        return;
    }
    if (fwViewService().updaterState() == FwUpdaterState::Downloading) crosspad_gui::ui_tile_grid_refill_all(s.grid);   // a tile's progress
    window_cb(nullptr);   // icons the worker's full queue refused before are asked again
}

void fw_library_view_close(void) {
    if (!s.root) return;
    lv_obj_t* root = s.root;
    forget();
    lv_obj_add_flag(root, LV_OBJ_FLAG_HIDDEN);   // nothing draws it again before it goes
    lv_obj_delete_async(root);
}

#endif // USE_LVGL
