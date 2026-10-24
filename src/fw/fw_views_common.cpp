// SPDX-License-Identifier: GPL-3.0-or-later
#ifdef USE_LVGL

#include "crosspad-gui/fw/fw_views_common.h"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <ctime>

#include "crosspad-gui/fw/fw_actions.h"
#include "crosspad-gui/fw/fw_icons.h"
#include "crosspad-gui/fw/fw_kit_card.h"
#include "crosspad-gui/fw/fw_updates_view.h"
#include "crosspad-gui/theme/crosspad_theme.h"
#include "crosspad/net/WifiTypes.hpp"
#include "fw_text.h"

using crosspad_gui::FwNetTab;
using crosspad_gui::FwUpdaterState;
using crosspad_gui::fwViewService;
using crosspad_gui::kFwNetTabCount;
using crosspad_gui::kFwNetTabStore;
using crosspad_gui::kFwuErrorMax;

static constexpr uint8_t kCols = 3;
static constexpr int32_t kRowsShown = 2;     // tile rows at rest: what the budget is measured on
static constexpr int32_t kGap = 4;
static constexpr int32_t kHeaderH = 16;
static constexpr int32_t kIconSide = crosspad::kFwIconSide;
static constexpr int32_t kTilePad = 2;
static constexpr int32_t kLineH = 12;        // one line of montserrat_10
static constexpr int32_t kTileMinH = kTilePad * 3 + kIconSide + 2 * kLineH;
static constexpr int32_t kRadius = 6;
/* An outline drawn inside the tile: a border would move the content when it
 * thickens, and an outline outside is clipped by the grid at its edges. */
static constexpr int32_t kFocusRing = 2;
static constexpr uint64_t kMiB = 1024ull * 1024ull;
static constexpr uint64_t kTenths = 10;
static constexpr uint32_t kNoIndex = UINT32_MAX;
static constexpr unsigned char kUtf8TwoBytes = 0xC0;
static constexpr unsigned char kUtf8ThreeBytes = 0xE0;
static constexpr unsigned char kUtf8FourBytes = 0xF0;

struct TileState {
    uint32_t index;
    uint32_t ticket;
    const lv_image_dsc_t* icon;
    bool noIcon;   // this item has none: do not ask again until the tile shows another
    lv_obj_t* img;
    lv_obj_t* letter;
    lv_obj_t* name;
    lv_obj_t* meta;
};

static int s_users;
static lv_style_t s_tile;
static lv_style_t s_tileFocused;
static lv_style_t s_text;
static lv_style_t s_header;
static lv_style_t s_letter;
static bool s_stylesReady;
static crosspad::FwSlotInfo s_slots[2];
static bool s_slotsStale = true;
static uint32_t s_lastPollTick = UINT32_MAX;
static bool s_wasBusy;
static bool s_wasFlow;
static uint32_t s_updaterGen;

static constexpr const char* kLeaseReason = "fw_views";
/* A shown tab checks (C Revision 2); a minute between automatic checks keeps
 * tab flipping from hammering the Worker. Check now always checks. */
static constexpr int64_t kAutoCheckGapUs = 60LL * 1000 * 1000;
static constexpr size_t kLeaseFailMax = sizeof "not_network_mode";   // the longest WifiFail name
static constexpr char kBusy[] = "busy";

static bool s_holding[kFwNetTabCount];   // tabs that hold the lease
static bool s_lease;
static char s_leaseFail[kLeaseFailMax];   // the last refusal, "" while held
static char s_autoRefusal[kFwuErrorMax];   // why the automatic check was refused, shown until a press or a job
static int64_t s_lastAutoCheckUs = -1;
static bool s_autoCheckWanted;   // a tab was shown and its lease and check have not run yet
static bool s_offered;                    // network mode offered during this showing
static FwNetTab s_shownTab = kFwNetTabStore;
static lv_group_t* s_shownGroup;
static int s_storeUsers;

static void styles_init(void) {
    if (s_stylesReady) {
        lv_style_reset(&s_tile);
        lv_style_reset(&s_tileFocused);
        lv_style_reset(&s_text);
        lv_style_reset(&s_header);
        lv_style_reset(&s_letter);
    }
    // Rebuilt at every first use: the theme's colours can change in Settings.
    lv_style_init(&s_tile);
    lv_style_set_bg_color(&s_tile, CROSSPAD_THEME_CARD_BG);
    lv_style_set_bg_opa(&s_tile, LV_OPA_COVER);
    lv_style_set_radius(&s_tile, kRadius);
    lv_style_set_pad_hor(&s_tile, kTilePad);
    lv_style_init(&s_tileFocused);
    lv_style_set_outline_width(&s_tileFocused, kFocusRing);
    lv_style_set_outline_pad(&s_tileFocused, -kFocusRing);
    lv_style_set_outline_color(&s_tileFocused, CROSSPAD_THEME_PRIMARY);
    lv_style_init(&s_text);
    lv_style_set_text_font(&s_text, &lv_font_montserrat_10);
    lv_style_set_text_color(&s_text, CROSSPAD_THEME_TEXT);
    lv_style_init(&s_header);
    lv_style_set_text_font(&s_header, &lv_font_montserrat_10);
    lv_style_set_text_color(&s_header, CROSSPAD_THEME_PRIMARY);
    lv_style_init(&s_letter);
    lv_style_set_text_font(&s_letter, &lv_font_montserrat_24);
    lv_style_set_text_color(&s_letter, CROSSPAD_THEME_PRIMARY);
    lv_style_set_text_align(&s_letter, LV_TEXT_ALIGN_CENTER);
    lv_style_set_pad_top(&s_letter, (kIconSide - lv_font_get_line_height(&lv_font_montserrat_24)) / 2);
    s_stylesReady = true;
}

const lv_style_t* fw_views_text_style(void) { return &s_text; }

static TileState* state_of(lv_obj_t* tile) {
    lv_obj_t* img = lv_obj_get_child(tile, 0);
    return img ? static_cast<TileState*>(lv_obj_get_user_data(img)) : nullptr;
}

static void set_text(lv_obj_t* label, const char* text) {
    if (strcmp(lv_label_get_text(label), text) != 0) lv_label_set_text(label, text);   // same text: no repaint
}

static void drop_icon(TileState* st) {
    if (st->ticket) fw_icons_cancel(st->ticket);
    st->ticket = 0;
    if (st->icon) {
        lv_image_set_src(st->img, nullptr);   // detached before the buffer can go
        fw_icons_release(st->icon);
        st->icon = nullptr;
    }
    lv_obj_add_flag(st->img, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(st->letter, LV_OBJ_FLAG_HIDDEN);
}

static void tile_deleted_cb(lv_event_t* e) {
    lv_obj_t* tile = lv_event_get_target_obj(e);
    TileState* st = state_of(tile);
    if (!st) return;
    drop_icon(st);
    lv_obj_set_user_data(st->img, nullptr);
    lv_free(st);
}

/* Children aligned, not placed: the grid resizes its tiles when it is resized. */
static void tile_create(lv_obj_t* tile, void*) {
    auto* st = static_cast<TileState*>(lv_malloc_zeroed(sizeof(TileState)));
    if (!st) return;   // state_of() finds nothing: the tile stays blank
    lv_obj_add_style(tile, &s_tile, 0);
    lv_obj_add_style(tile, &s_tileFocused, LV_STATE_FOCUSED);
    st->index = kNoIndex;
    st->img = lv_image_create(tile);   // child 0: state_of() finds the state here
    lv_obj_set_size(st->img, kIconSide, kIconSide);
    lv_obj_align(st->img, LV_ALIGN_TOP_MID, 0, kTilePad);
    lv_obj_add_flag(st->img, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_user_data(st->img, st);
    st->letter = lv_label_create(tile);
    lv_obj_add_style(st->letter, &s_letter, 0);
    lv_obj_set_size(st->letter, kIconSide, kIconSide);
    lv_obj_align(st->letter, LV_ALIGN_TOP_MID, 0, kTilePad);
    lv_label_set_text(st->letter, "");
    st->name = lv_label_create(tile);
    st->meta = lv_label_create(tile);
    lv_obj_t* lines[] = {st->name, st->meta};
    for (int i = 0; i < 2; ++i) {
        lv_obj_add_style(lines[i], &s_text, 0);
        lv_obj_set_style_text_align(lines[i], LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_long_mode(lines[i], LV_LABEL_LONG_DOT);
        lv_obj_set_size(lines[i], LV_PCT(100), kLineH);
        lv_obj_align(lines[i], LV_ALIGN_TOP_MID, 0, 2 * kTilePad + kIconSide + i * kLineH);
        lv_label_set_text(lines[i], "");
    }
    lv_obj_add_event_cb(tile, tile_deleted_cb, LV_EVENT_DELETE, nullptr);
}

static void tile_unused(lv_obj_t* tile, void*) {
    TileState* st = state_of(tile);
    if (!st) return;
    drop_icon(st);
    st->index = kNoIndex;
}

lv_obj_t* fw_grid_create(lv_obj_t* parent, lv_group_t* group, crosspad_gui::TileGridConfig cfg, int32_t height) {
    int32_t tileH = (height + kGap) / kRowsShown - kGap;
    if (tileH < kTileMinH) tileH = kTileMinH;
    cfg.cols = kCols;
    cfg.tileH = tileH;
    cfg.gap = kGap;
    cfg.headerH = kHeaderH;
    cfg.headerStyle = &s_header;
    cfg.create = tile_create;
    cfg.unused = tile_unused;
    lv_obj_t* grid = crosspad_gui::ui_tile_grid_create(parent, group, cfg);
    lv_obj_set_size(grid, LV_PCT(100), kRowsShown * (tileH + kGap) - kGap);
    return grid;
}

void fw_tile_show(lv_obj_t* tile, uint32_t index, const FwTileView& v) {
    TileState* st = state_of(tile);
    if (!st) return;
    if (st->index != index) {
        drop_icon(st);
        st->noIcon = false;
        st->index = index;
    }
    char letter[8];
    fw_letter_of(v.name, letter, sizeof letter);
    set_text(st->letter, letter);
    set_text(st->name, v.name);
    set_text(st->meta, v.meta);
    const lv_opa_t opa = v.dim ? LV_OPA_50 : LV_OPA_COVER;
    if (lv_obj_get_style_opa(tile, LV_PART_MAIN) != opa) lv_obj_set_style_opa(tile, opa, 0);   // a refill repaints nothing that stayed
}

static void icon_done(const lv_image_dsc_t* icon, void* user) {
    TileState* st = state_of(static_cast<lv_obj_t*>(user));
    if (!st) {
        if (icon) fw_icons_release(icon);
        return;
    }
    st->ticket = 0;
    if (!icon) {
        st->noIcon = true;
        return;
    }
    st->icon = icon;
    lv_image_set_src(st->img, icon);
    lv_obj_remove_flag(st->img, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(st->letter, LV_OBJ_FLAG_HIDDEN);
}

struct SyncCtx {
    lv_obj_t* grid;
    FwIconFor iconFor;
};

static void sync_tile(lv_obj_t* tile, uint32_t index, void* user) {
    const auto* c = static_cast<const SyncCtx*>(user);
    TileState* st = state_of(tile);
    if (!st || st->index != index) return;
    if (!crosspad_gui::ui_tile_grid_index_visible(c->grid, index)) {
        drop_icon(st);
        return;
    }
    if (st->icon || st->ticket || st->noIcon) return;
    FwIconKey key{};
    if (!c->iconFor(index, &key)) {
        st->noIcon = true;
        return;
    }
    if (const lv_image_dsc_t* held = fw_icons_acquire(key.pkg, key.kit, key.version)) {
        icon_done(held, tile);   // shown a moment ago (a rescan, a row scrolled back): no card read
        return;
    }
    st->ticket = fw_icons_request(key.pkg, key.kit, key.version, icon_done, tile);   // 0: queue full, next sync retries
}

void fw_tile_icons_sync(lv_obj_t* grid, FwIconFor iconFor) {
    SyncCtx c{grid, iconFor};
    crosspad_gui::ui_tile_grid_for_each(grid, sync_tile, &c);
    fw_icons_trim();   // after the tiles took back what they show again
}

static void release_tile(lv_obj_t* tile, uint32_t, void*) {
    if (TileState* st = state_of(tile)) drop_icon(st);
}

void fw_tile_icons_release(lv_obj_t* grid) {
    crosspad_gui::ui_tile_grid_for_each(grid, release_tile, nullptr);
    fw_icons_trim();
}

void fw_letter_of(const char* name, char* out, size_t n) {
    if (n == 0) return;
    out[0] = '\0';
    if (!name || !name[0] || n < 2) return;
    const unsigned char c = static_cast<unsigned char>(name[0]);
    size_t len = c < kUtf8TwoBytes ? 1 : c < kUtf8ThreeBytes ? 2 : c < kUtf8FourBytes ? 3 : 4;
    if (len >= n) len = n - 1;
    if (strnlen(name, len) < len) len = 1;   // a sequence cut short: just its first byte
    memcpy(out, name, len);
    out[len] = '\0';
    if (len == 1 && out[0] >= 'a' && out[0] <= 'z') out[0] = static_cast<char>(out[0] - 'a' + 'A');
}

void fw_format_mb(uint64_t bytes, char* out, size_t n) {
    const uint64_t tenths = (bytes * kTenths + kMiB / 2) / kMiB;
    snprintf(out, n, "%u.%u MB", static_cast<unsigned>(tenths / kTenths), static_cast<unsigned>(tenths % kTenths));
}

void fw_format_date(int64_t unixSeconds, char* out, size_t n) {
    if (unixSeconds <= 0) {
        snprintf(out, n, "-");
        return;
    }
    const time_t t = static_cast<time_t>(unixSeconds);
    struct tm tm;
    if (!crosspad_gui::fw_text::utc(t, &tm)) {
        snprintf(out, n, "-");
        return;
    }
    strftime(out, n, "%Y-%m-%d", &tm);
}

const char* fw_strip_v(const char* version) { return version && version[0] == 'v' ? version + 1 : (version ? version : ""); }

const crosspad::FwSlotInfo* fw_views_slots(void) {
    if (s_slotsStale) {
        fwViewService().readSlots(s_slots);
        s_slotsStale = false;
    }
    return s_slots;
}

void fw_views_slots_invalidate(void) { s_slotsStale = true; }

char fw_views_slot_of(const char* pkg, bool* running) {
    if (running) *running = false;
    if (!pkg || !pkg[0]) return 0;
    const crosspad::FwSlotInfo* slots = fw_views_slots();
    for (int i = 0; i < 2; ++i) {
        if (crosspad_gui::fw_text::casecmp(slots[i].package, pkg) != 0) continue;   // FATFS names: case-insensitive
        if (running) *running = slots[i].running;
        return slots[i].label;
    }
    return 0;
}

bool fw_views_focus_first(lv_obj_t* scope, lv_group_t* group) {
    if (!scope || !group || lv_obj_has_flag(scope, LV_OBJ_FLAG_HIDDEN)) return false;   // nor anything under it
    if (lv_obj_get_group(scope) == group) {
        lv_group_focus_obj(scope);
        return true;
    }
    for (uint32_t i = 0; i < lv_obj_get_child_count(scope); ++i)
        if (fw_views_focus_first(lv_obj_get_child(scope, static_cast<int32_t>(i)), group)) return true;
    return false;
}

static void offer_network_mode(FwNetTab tab, lv_group_t* group) {
    if (s_offered || fwViewService().networkMode()) return;
    s_offered = true;
    fwViewService().offerNetworkMode(tab, group);
}

/* The views' lease, taken if not held; a refusal is kept as the reason shown. */
static bool take_lease(void) {
    if (s_lease) return true;
    s_lease = fwViewService().wifiAcquire(kLeaseReason);
    if (s_lease) {
        s_leaseFail[0] = '\0';
    } else {
        crosspad_gui::fw_text::copy(s_leaseFail, crosspad::wifiFailName(fwViewService().wifiFail()),
                sizeof s_leaseFail);
    }
    return s_lease;
}

/* The tab's lease and its own check, once per showing and gap. A trial boot holds both
 * (a crash in the WiFi connect, the radio or TLS would roll a good image back); the poll
 * goes on once the confirm has landed. A Check press is not held and takes the lease itself. */
static void lease_and_check_if_due(FwNetTab tab, lv_group_t* group) {
    if (!s_autoCheckWanted || fwViewService().runningOnTrial()) return;
    s_autoCheckWanted = false;
    if (!take_lease() && fw_updates_is_no_mem(s_leaseFail)) offer_network_mode(tab, group);
    // A refused lease is the reason already shown; a check would only be refused
    // the same way and overwrite the updater's last result with it.
    if (!s_lease) return;
    const int64_t now = fwViewService().nowUs();
    if (s_lastAutoCheckUs >= 0 && now - s_lastAutoCheckUs < kAutoCheckGapUs) return;
    if (fwViewService().updaterBusy() || fwViewService().updaterState() == FwUpdaterState::ReadyToRestart) return;
    s_lastAutoCheckUs = now;
    /* A refusal is shown passively, as the reason: only no_mem leads to a
     * question, nothing else is offered unasked. "busy" is not news. */
    if (fwViewService().checkStart(s_autoRefusal, sizeof s_autoRefusal) || strcmp(s_autoRefusal, kBusy) == 0)
        s_autoRefusal[0] = '\0';
    else if (fw_updates_is_no_mem(s_autoRefusal))
        offer_network_mode(tab, group);
}

void fw_views_tab_shown(FwNetTab tab, lv_group_t* group) {
    s_holding[tab] = true;
    s_shownTab = tab;
    s_shownGroup = group;
    s_offered = false;   // a new showing: one offer at most
    s_autoCheckWanted = true;
    lease_and_check_if_due(tab, group);
}

void fw_views_tab_closed(FwNetTab tab) {
    s_holding[tab] = false;
    for (bool h : s_holding)
        if (h) return;
    if (s_lease) fwViewService().wifiRelease();
    s_lease = false;
    s_autoCheckWanted = false;
    s_autoRefusal[0] = '\0';
    s_shownGroup = nullptr;
}

bool fw_views_check_now(FwNetTab tab, lv_group_t* group) {
    s_offered = false;   // a press is a request of its own
    s_autoRefusal[0] = '\0';
    // What refused the lease when the tab was shown (WiFi off, memory) may be
    // gone now. The check's own lease is its job's: refused, it ends failed with
    // the reason in the status row, and no_mem is offered by fw_actions_tick().
    if (s_holding[tab]) take_lease();
    s_lastAutoCheckUs = fwViewService().nowUs();
    char why[kFwuErrorMax];
    if (fwViewService().checkStart(why, sizeof why)) {
        fw_actions_track_check(tab);
        return true;
    }
    fw_action_refused(why, tab, group);
    return false;
}

const char* fw_views_wifi_reason(void) {
    if (!s_lease) return s_leaseFail;
    if (s_autoRefusal[0] && !fwViewService().updaterBusy()) return s_autoRefusal;
    if (fwViewService().wifiState() != crosspad::WifiLinkState::Failed) return "";
    return crosspad::wifiFailName(fwViewService().wifiFail());
}

void fw_views_want_store(bool want) {
    if (want) {
        if (s_storeUsers++ == 0) fwViewService().loadStore();
    } else if (s_storeUsers > 0 && --s_storeUsers == 0) {
        fwViewService().dropStore();
    }
}

void fw_views_acquire(void) {
    if (s_users++ == 0) {
        styles_init();
        s_slotsStale = true;
        s_wasBusy = fwViewService().updaterBusy();
        s_wasFlow = fwViewService().flowActive();
    }
    fwViewService().dataOpen();
}

void fw_views_release(void) {
    if (s_users > 0 && --s_users == 0) fw_icons_drop_all();
    fwViewService().dataClose();
}

void fw_views_poll(void) {
    const uint32_t now = lv_tick_get();
    if (now == s_lastPollTick) return;
    s_lastPollTick = now;
    fwViewService().dataPoll();
    const bool busy = fwViewService().updaterBusy();
    const bool flow = fwViewService().flowActive();
    if ((s_wasBusy && !busy) || (s_wasFlow && !flow)) {
        // A check rewrote the cache, a download added a package, an install moved a slot.
        s_slotsStale = true;
        ++s_updaterGen;
        fwViewService().scanLibrary();
        if (s_storeUsers > 0) fwViewService().loadStore();
    }
    s_wasBusy = busy;
    s_wasFlow = flow;
    // The radio's own memory gate can refuse after the lease was granted. With
    // a kit card open the offer gives the encoder back to the card (nullptr:
    // the group it had), not to the tab under it.
    if (s_lease && s_shownGroup && fw_updates_is_no_mem(fw_views_wifi_reason()))
        offer_network_mode(s_shownTab, fw_kit_card_is_open() ? nullptr : s_shownGroup);
    if (s_shownGroup) lease_and_check_if_due(s_shownTab, s_shownGroup);
    fw_actions_tick();
    fw_kit_card_refresh();
}

uint32_t fw_views_updater_gen(void) { return s_updaterGen; }

namespace crosspad_gui {

static IFwViewService* s_service = nullptr;

void setFwViewService(IFwViewService* service) { s_service = service; }

IFwViewService& fwViewService() {
    assert(s_service != nullptr && "Firmware view service not set! Call setFwViewService() first.");
    return *s_service;
}

} // namespace crosspad_gui

void fw_views_job_started(void) {
    s_wasBusy = true;
    s_autoRefusal[0] = '\0';
}

#endif // USE_LVGL
