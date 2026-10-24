// SPDX-License-Identifier: GPL-3.0-or-later
#ifdef USE_LVGL

#include "crosspad-gui/fw/fw_store_view.h"

#include <cstdio>
#include <cstring>

#include "crosspad-gui/fw/fw_actions.h"
#include "crosspad-gui/fw/fw_kit_card.h"
#include "crosspad-gui/fw/fw_updates_view.h"
#include "crosspad-gui/fw/fw_views_common.h"
#include "crosspad-gui/platform/psram_placement.h"
#include "crosspad/fw/FwStore.hpp"
#include "crosspad/fw/Semver.hpp"
#include "fw_text.h"

using crosspad_gui::FwLibrarySnap;
using crosspad_gui::FwUpdaterState;
using crosspad_gui::fwViewService;
using crosspad_gui::kFwNetTabStore;
using crosspad_gui::kFwuErrorMax;
using crosspad_gui::kFwuKitMax;
using crosspad_gui::kFwuVersionMax;

static constexpr size_t kMetaMax = 64;
static constexpr size_t kStatusMax = 128;
static constexpr size_t kDateMax = 16;    // "2026-10-02"
static constexpr int32_t kStatusH = 26;   // two lines of montserrat_10
static constexpr uint8_t kSectionCount = 2;

namespace {
struct StoreView {
    lv_obj_t* root;
    lv_obj_t* status;
    lv_obj_t* grid;
    lv_group_t* group;
    lv_obj_t* focusAtBuild;   // where the encoder was when the tab opened (compared, never used)
    uint32_t storeGen;
    uint32_t libGen;
    bool built;
    bool selectedOnce;
    size_t count;
    size_t firstCommunity;
    uint8_t order[crosspad::kFwCatalogKits];   // tile index -> FwStore kit
    int8_t shown[crosspad::kFwCatalogKits];    // that kit's tile version
};
}  // namespace

/* PSRAM: nothing on this screen needs fast RAM. */
CROSSPAD_GUI_PSRAM_BSS static StoreView s;

static const crosspad::FwStoreKit* kit_at(uint32_t index, const crosspad::FwStoreVersion** v) {
    const crosspad::FwStore* store = fwViewService().store();
    if (!store || index >= s.count || s.shown[index] < 0 || s.order[index] >= store->kitCount) return nullptr;
    const crosspad::FwStoreKit& k = store->kits[s.order[index]];
    if (s.shown[index] >= k.count) return nullptr;
    if (v) *v = &k.versions[s.shown[index]];
    return &k;
}

static bool on_card(const char* kit, const char* version) {
    const FwLibrarySnap* lib = fwViewService().library();
    if (!lib) return false;
    char name[crosspad::kFwPackageNameMax];
    crosspad::fwPackageName(kit, version, name, sizeof name);
    for (size_t i = 0; i < lib->count; ++i)
        if (lib->items[i].complete && strcmp(lib->items[i].name, name) == 0) return true;
    return false;
}

static bool downloading(const char* kit, const char* version) {
    if (fwViewService().updaterState() != FwUpdaterState::Downloading && fwViewService().updaterState() != FwUpdaterState::Installing) return false;
    char tk[kFwuKitMax];
    char tv[kFwuVersionMax];
    fwViewService().updaterTargetKit(tk, sizeof tk);
    fwViewService().updaterTarget(tv, sizeof tv);
    return strcmp(tk, kit) == 0 && crosspad::compareVersionText(tv, version) == 0;
}

static void fill_cb(lv_obj_t* tile, uint32_t index, void*) {
    const crosspad::FwStoreVersion* v = nullptr;
    const crosspad::FwStoreKit* k = kit_at(index, &v);
    if (!k) return;
    char meta[kMetaMax];
    if (downloading(k->kit, v->version)) {
        snprintf(meta, sizeof meta, LV_SYMBOL_DOWNLOAD " %u %%", static_cast<unsigned>(fwViewService().updaterProgress()));
    } else {
        snprintf(meta, sizeof meta, "%s%s%s%s", v->version, v->channel == crosspad::FwChannel::Beta ? " beta" : "",
                 on_card(k->kit, v->version) ? " " LV_SYMBOL_OK : "",
                 v->tier == crosspad::FwTier::Community ? " " LV_SYMBOL_WARNING : "");
    }
    fw_tile_show(tile, index, FwTileView{k->displayName, meta, false});
}

static bool icon_for(uint32_t index, FwIconKey* key) {
    const crosspad::FwStoreVersion* v = nullptr;
    const crosspad::FwStoreKit* k = kit_at(index, &v);
    if (!k) return false;
    const bool card = on_card(k->kit, v->version);
    if (!v->hasIcon && !card) return false;
    if (card) crosspad::fwPackageName(k->kit, v->version, key->pkg, sizeof key->pkg);
    crosspad_gui::fw_text::copy(key->kit, k->kit, sizeof key->kit);
    crosspad_gui::fw_text::copy(key->version, v->version, sizeof key->version);
    return true;
}

static constexpr char kTab[] = "store";

static void publish_sel(int32_t index) {
    if (!fwViewService().uiTabIs(kTab)) return;
    const crosspad::FwStoreVersion* v = nullptr;
    const crosspad::FwStoreKit* k = index >= 0 ? kit_at(static_cast<uint32_t>(index), &v) : nullptr;
    fwViewService().uiSel(k ? index : -1, k ? k->kit : nullptr, k ? v->version : nullptr);
}

/* FW_UI's items, store and selection, while this is the tab on screen. */
static void publish(void) {
    if (!fwViewService().uiTabIs(kTab)) return;
    fwViewService().uiItems(static_cast<uint32_t>(s.count));
    fwViewService().uiStore(s.root && fwViewService().storeLoaded() ? crosspad::fwStoreLoadName(fwViewService().storeState())
                                                                : nullptr);
    publish_sel(s.grid && s.count ? crosspad_gui::ui_tile_grid_selected(s.grid) : -1);
}

static void selected_cb(uint32_t index, void*) { publish_sel(static_cast<int32_t>(index)); }

static void clicked_cb(uint32_t index, void*) {
    const crosspad::FwStoreVersion* v = nullptr;
    const crosspad::FwStoreKit* k = kit_at(index, &v);
    if (!k) return;
    FwTarget t{};
    crosspad_gui::fw_text::copy(t.kit, k->kit, sizeof t.kit);
    crosspad_gui::fw_text::copy(t.version, v->version, sizeof t.version);
    t.community = v->tier == crosspad::FwTier::Community;   // the version's tier, not the kit's newest
    fw_kit_card_open(t, kFwNetTabStore);
}

static void window_cb(void*) {
    if (s.grid) fw_tile_icons_sync(s.grid, icon_for);
}

static void status_text(char* out, size_t n) {
    char date[kDateMax];
    const char* wifi = fw_views_wifi_reason();
    const crosspad::FwStore* store = fwViewService().store();
    int o = 0;
    if (fwViewService().updaterState() == FwUpdaterState::Checking) {
        o = snprintf(out, n, "Checking the store...");
    } else if (!fwViewService().storeLoaded()) {
        o = snprintf(out, n, "Reading the card...");
    } else if (store) {
        fw_format_date(store->fetched, date, sizeof date);
        o = snprintf(out, n, "Catalog of %s", date);
    } else if (fwViewService().storeState() == crosspad::FwStoreLoad::Missing) {
        o = snprintf(out, n, "Never fetched");
    } else {
        o = snprintf(out, n, "Store unavailable - %s",
                     fwViewService().storeState() == crosspad::FwStoreLoad::OtherBoard ? "catalog of another board"
                                                                                    : "catalog unreadable");
    }
    char err[kFwuErrorMax];
    fwViewService().updaterError(err, sizeof err);
    const char* why = wifi[0] ? wifi : (fwViewService().updaterState() == FwUpdaterState::Failed ? err : "");
    if (why[0] && o > 0 && static_cast<size_t>(o) < n)
        snprintf(out + o, n - static_cast<size_t>(o), "\n%s", fw_updates_error_text(why));
}

static void rebuild(void) {
    const crosspad::FwStore* store = fwViewService().store();
    s.count = 0;
    s.firstCommunity = 0;
    if (store) {
        s.count = crosspad::fwStoreOrder(*store, s.order, crosspad::kFwCatalogKits, &s.firstCommunity);
        for (size_t i = 0; i < s.count; ++i) {
            const crosspad::FwStoreKit& k = store->kits[s.order[i]];
            const auto channel = fwViewService().channelBeta(k.kit) ? crosspad::FwChannel::Beta : crosspad::FwChannel::Stable;
            s.shown[i] = static_cast<int8_t>(crosspad::fwShownVersion(k, channel));
        }
    }
    lv_obj_t* focused = lv_group_get_focused(s.group);
    const bool onGrid = focused && lv_obj_get_parent(focused) == s.grid;
    const crosspad_gui::TileGridSection sections[kSectionCount] = {
        {"Official", static_cast<uint32_t>(s.firstCommunity)},
        {"Community", static_cast<uint32_t>(s.count - s.firstCommunity)},
    };
    crosspad_gui::ui_tile_grid_set_sections(s.grid, sections, kSectionCount);
    publish();
    const int32_t sel = crosspad_gui::ui_tile_grid_selected(s.grid);
    if (s.count > 0 && !s.selectedOnce) {
        // The first catalog: the encoder lands on the first tile, unless the
        // user has turned it elsewhere (the tab bar) while the card was read.
        // Network mode builds every tab at once: a Store page scrolled out of
        // view must not take the encoder, or its scroll-on-focus would slide
        // the tab view over from the tab the user is on.
        s.selectedOnce = true;
        lv_obj_t* now = lv_group_get_focused(s.group);
        crosspad_gui::ui_tile_grid_select(s.grid, 0,
                                          lv_obj_is_visible(s.grid) && (!now || now == s.focusAtBuild || onGrid));
    } else if (s.count > 0) {
        // The same index may now be another kit; an encoder on the grid stays there.
        crosspad_gui::ui_tile_grid_select(s.grid, sel >= 0 ? static_cast<uint32_t>(sel) : 0, onGrid);
    }
    window_cb(nullptr);
}

/* The view lets go of everything it holds: icons, the selection, the lease. */
static void forget(void) {
    fw_views_tab_closed(kFwNetTabStore);
    fw_tile_icons_release(s.grid);
    s = StoreView{};
    publish();
    fw_views_want_store(false);
    fw_views_release();
}

/* A host that deletes the parent without fw_store_view_close() (the Firmware
 * app's root deleted with the screen) leaves no dangling root. */
static void root_deleted_cb(lv_event_t* e) {
    if (lv_event_get_target(e) != s.root) return;   // close() let it go already
    forget();
}

void fw_store_view_build(lv_obj_t* parent, lv_group_t* group) {
    if (s.root) return;
    fw_views_acquire();
    fw_views_want_store(true);
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
    lv_obj_set_size(s.status, LV_PCT(100), kStatusH);
    lv_label_set_long_mode(s.status, LV_LABEL_LONG_DOT);
    lv_label_set_text(s.status, "");
    lv_obj_update_layout(parent);
    crosspad_gui::TileGridConfig cfg;
    cfg.fill = fill_cb;
    cfg.clicked = clicked_cb;
    cfg.selected = selected_cb;
    cfg.window = window_cb;
    cfg.emptyText = "No firmware in the store";
    s.grid = fw_grid_create(s.root, group, cfg, lv_obj_get_content_height(s.root) - kStatusH - kFwPad);
    s.storeGen = fwViewService().storeGen();
    s.libGen = fwViewService().libraryGen();
    publish();
    fwViewService().scanLibrary();   // for the check marks
}

void fw_store_view_shown(void) {
    if (!s.root) return;
    // Network mode builds the tab long before it is shown: where the encoder
    // was when the tab opened is where it is now.
    if (!s.selectedOnce) s.focusAtBuild = lv_group_get_focused(s.group);
    fwViewService().uiTab(kTab);
    publish();
    fw_views_tab_shown(kFwNetTabStore, s.group);
}

void fw_store_view_refresh(void) {
    if (!s.root) return;
    fw_views_poll();
    char text[kStatusMax];
    status_text(text, sizeof text);
    if (strcmp(lv_label_get_text(s.status), text) != 0) lv_label_set_text(s.status, text);
    const uint32_t storeGen = fwViewService().storeGen();
    const uint32_t libGen = fwViewService().libraryGen();
    if (!s.built || storeGen != s.storeGen || libGen != s.libGen) {
        if (!fwViewService().storeLoaded()) return;   // a grid of nothing before the card answered would be a lie
        s.built = true;
        s.storeGen = storeGen;
        s.libGen = libGen;
        rebuild();
        return;
    }
    if (fwViewService().updaterBusy()) crosspad_gui::ui_tile_grid_refill_all(s.grid);   // a tile's progress
    window_cb(nullptr);   // icons the worker's full queue refused before are asked again
}

void fw_store_view_close(void) {
    if (!s.root) return;
    lv_obj_t* root = s.root;
    forget();
    lv_obj_add_flag(root, LV_OBJ_FLAG_HIDDEN);   // nothing draws it again before it goes
    lv_obj_delete_async(root);
}

#endif // USE_LVGL
