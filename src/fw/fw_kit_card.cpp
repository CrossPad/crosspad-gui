// SPDX-License-Identifier: GPL-3.0-or-later
#ifdef USE_LVGL

#include "crosspad-gui/fw/fw_kit_card.h"

#include <cstdio>
#include <cstring>

#include "crosspad-gui/components/app_orchestrator.h"
#include "crosspad-gui/components/power_gesture.h"
#include "crosspad-gui/fw/FwCardActions.hpp"
#include "crosspad-gui/fw/fw_icons.h"
#include "crosspad-gui/fw/fw_updates_view.h"
#include "crosspad-gui/fw/fw_views_common.h"
#include "crosspad-gui/platform/IGuiPlatform.h"
#include "crosspad-gui/platform/psram_placement.h"
#include "crosspad-gui/theme/crosspad_theme.h"
#include "crosspad-gui/widgets/gui_helpers.h"
#include "crosspad/fw/FwStore.hpp"
#include "crosspad/fw/Semver.hpp"
#include "src/misc/cache/instance/lv_image_cache.h"   // lv_image_cache_drop(): lvgl.h stopped exporting it in 9.4
#include "fw_text.h"

using crosspad_gui::FwLibItem;
using crosspad_gui::FwLibrarySnap;
using crosspad_gui::FwNetTab;
using crosspad_gui::FwUpdaterState;
using crosspad_gui::fwViewService;
using crosspad_gui::kFwNetTabStore;
using crosspad_gui::kFwuErrorMax;
using crosspad_gui::kFwuKitMax;
using crosspad_gui::kFwuVersionMax;

static constexpr int32_t kPad = 6;
static constexpr int32_t kIconSide = 48;
static constexpr int32_t kButtonH = 28;
static constexpr int32_t kChipH = 24;
static constexpr int32_t kChipIconSide = 20;
static constexpr int32_t kRadius = 6;
/* Launcher PNGs decode into the shared image cache (48x48 ARGB8888 ~9 kB, 64x64
 * ~16 kB each): a few, dropped again when the card closes. */
static constexpr size_t kChipIconsMax = 4;
static constexpr size_t kTextMax = 128;
static constexpr size_t kNormMax = 32;
static constexpr size_t kNameMax = 16;        // "Slot A" for an image without a kit
static constexpr size_t kSizeTextMax = 16;    // "123.4 MB"
static constexpr size_t kLetterMax = 8;       // one UTF-8 character
static constexpr size_t kSlotTextMax = 8;     // "  " LV_SYMBOL_PLAY "A"
static constexpr size_t kSlots = 2;
static constexpr int kActionCount = 9;
static constexpr const crosspad_gui::FwCardAct kActions[kActionCount] = {
    crosspad_gui::kFwActRunNew, crosspad_gui::kFwActRun,   crosspad_gui::kFwActInstallRun,
    crosspad_gui::kFwActInstall, crosspad_gui::kFwActDownload, crosspad_gui::kFwActResume,
    crosspad_gui::kFwActPin,    crosspad_gui::kFwActUnpin, crosspad_gui::kFwActDelete,
};

namespace {
struct Card {
    lv_obj_t * overlay;
    lv_obj_t * img;
    lv_obj_t * letter;
    lv_obj_t * name;
    lv_obj_t * version;
    lv_obj_t * description;
    lv_obj_t * chips;
    lv_obj_t * meta;
    lv_obj_t * notesHead;
    lv_obj_t * notes;
    lv_obj_t * status;
    lv_obj_t * buttons[kActionCount];
    lv_obj_t * olderBtn;
    lv_obj_t * olderBox;
    lv_group_t * group;
    lv_group_t * prevGroup;
    uint32_t backToken;
    uint32_t iconTicket;
    const lv_image_dsc_t * icon;
    FwTarget target;
    char pkg[crosspad::kFwPackageNameMax];   // the package directory this version has, or would have
    FwNetTab tab;
    uint32_t detailGen;
    uint32_t storeGen;
    bool chipsBuilt;
    lv_obj_t * chipImg[kChipIconsMax];    // their sources leave the image cache with the card
    const char * chipSrc[kChipIconsMax];
};
}  // namespace

/* PSRAM: nothing on this screen needs fast RAM. */
CROSSPAD_GUI_PSRAM_BSS static Card s;
CROSSPAD_GUI_PSRAM_BSS static FwTarget s_opening;   // what the deferred open shows
static FwNetTab s_openingTab = kFwNetTabStore;
static bool s_openPending;                    // an open waits for its LVGL cycle; a close cancels it

static void set_text(lv_obj_t * label, const char * text) {
    if (label && strcmp(lv_label_get_text(label), text) != 0) lv_label_set_text(label, text);
}

static void set_hidden(lv_obj_t * obj, bool hidden) {
    if (!obj || lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN) == hidden) return;
    if (!hidden) {
        lv_obj_remove_flag(obj, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    // The press that hid this button left the encoder on it: step on to one still there.
    const bool hadFocus = s.group && lv_group_get_focused(s.group) == obj;
    lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
    if (hadFocus) lv_group_focus_next(s.group);
}

static lv_obj_t * text_label(lv_obj_t * parent, bool wrap) {
    lv_obj_t * l = lv_label_create(parent);
    lv_obj_add_style(l, fw_views_text_style(), 0);
    lv_obj_set_width(l, LV_PCT(100));
    lv_label_set_long_mode(l, wrap ? LV_LABEL_LONG_WRAP : LV_LABEL_LONG_DOT);
    lv_label_set_text(l, "");
    return l;
}

static const FwLibItem * library_item(void) {
    const FwLibrarySnap * lib = fwViewService().library();
    if (!lib || !s.pkg[0]) return nullptr;
    for (size_t i = 0; i < lib->count; ++i)
        if (strcmp(lib->items[i].name, s.pkg) == 0) return &lib->items[i];
    return nullptr;
}

static const crosspad::FwStoreKit * store_kit(int * version) {
    const crosspad::FwStore * store = fwViewService().store();
    const crosspad::FwStoreKit * k = store ? crosspad::fwStoreFind(*store, s.target.kit) : nullptr;
    if (version) *version = k ? crosspad::fwStoreVersionIndex(*k, s.target.version) : -1;
    return k;
}

/* The updater's running (or last) job is this version. An image without a kit
 * is never one: two unparseable versions would compare equal. */
static bool is_this_job(void) {
    if (!s.target.kit[0]) return false;
    char kit[kFwuKitMax];
    char version[kFwuVersionMax];
    fwViewService().updaterTargetKit(kit, sizeof kit);
    fwViewService().updaterTarget(version, sizeof version);
    return strcmp(kit, s.target.kit) == 0 && crosspad::compareVersionText(version, s.target.version) == 0;
}

/* The slot holding this version: by its package, or -- an image whose package
 * is not on the card -- the slot its tile named, while that slot still holds it. */
static char slot_of_target(bool * running) {
    const char byPackage = fw_views_slot_of(s.pkg, running);
    if (byPackage || !s.target.slot) return byPackage;
    const crosspad::FwSlotInfo * slots = fw_views_slots();
    for (size_t i = 0; i < kSlots; ++i) {
        const crosspad::FwSlotInfo & sl = slots[i];
        if (sl.label != s.target.slot || sl.state == crosspad::FwSlotState::Empty) continue;
        if (strcmp(sl.kit, s.target.kit) != 0 || strcmp(fw_strip_v(sl.version), s.target.version) != 0) continue;
        *running = sl.running;
        return sl.label;
    }
    return 0;
}

static const crosspad::FwSlotInfo & slot_info(char label) {
    const crosspad::FwSlotInfo * slots = fw_views_slots();
    return slots[slots[0].label == label ? 0 : 1];
}

static crosspad_gui::FwCardFacts facts(void) {
    crosspad_gui::FwCardFacts f{};
    int vi = -1;
    store_kit(&vi);
    f.inCatalog = vi >= 0;
    if (const FwLibItem * it = library_item()) {
        f.onCard = true;
        f.complete = it->complete;
        f.installable = it->verdict == crosspad::FwPkgVerdict::Ok || it->verdict == crosspad::FwPkgVerdict::UnknownBuild;
        f.pinned = it->pinned;
    }
    bool running = false;
    f.slotLabel = slot_of_target(&running);
    f.slot = !f.slotLabel ? crosspad_gui::FwSlotRole::None : running ? crosspad_gui::FwSlotRole::Running
                                                                 : crosspad_gui::FwSlotRole::Other;
    f.slotRolledBack = f.slot == crosspad_gui::FwSlotRole::Other && slot_info(f.slotLabel).state == crosspad::FwSlotState::RolledBack;
    f.updaterBusy = fwViewService().updaterBusy();
    const bool mine = is_this_job();
    f.busyThis = f.updaterBusy && mine;
    char ready[kFwuVersionMax];
    fwViewService().updaterReadyVersion(ready, sizeof ready);
    f.readyThis = fwViewService().updaterState() == FwUpdaterState::ReadyToRestart && mine && ready[0] &&
                  crosspad::compareVersionText(ready, s.target.version) == 0;
    return f;
}

static void status_text(const crosspad_gui::FwCardFacts & f, const crosspad_gui::FwCardPlan & p, char * out, size_t n) {
    out[0] = '\0';
    char err[kFwuErrorMax];
    if (f.busyThis) {
        snprintf(out, n, "%s... %u %%", fwViewService().updaterState() == FwUpdaterState::Installing ? "Installing" : "Downloading",
                 static_cast<unsigned>(fwViewService().updaterProgress()));
    } else if (f.slot == crosspad_gui::FwSlotRole::Running) {
        snprintf(out, n, "Running now, in slot %c", f.slotLabel);
    } else if (f.slotRolledBack) {
        const int len = snprintf(out, n, "In slot %c - it did not start last time", f.slotLabel);
        const FwLibItem * it = library_item();
        // Reinstall is disabled then: say why, not only that the slot failed.
        if (p.installBlock == crosspad_gui::FwCardBlock::NotInstallable && it && len > 0 && static_cast<size_t>(len) < n)
            snprintf(out + len, n - static_cast<size_t>(len), ". Cannot be reinstalled: %s", crosspad::fwPkgVerdictText(it->verdict));
    } else if (p.deleteBlock == crosspad_gui::FwCardBlock::InSlot) {
        snprintf(out, n, "Installed in slot %c - it cannot be deleted", f.slotLabel);
    } else if (f.slot == crosspad_gui::FwSlotRole::Other) {
        snprintf(out, n, "Installed in slot %c", f.slotLabel);
    } else if (p.installBlock == crosspad_gui::FwCardBlock::NotInstallable) {
        const FwLibItem * it = library_item();
        snprintf(out, n, "Cannot be installed: %s", it ? crosspad::fwPkgVerdictText(it->verdict) : "-");
    } else if (!f.updaterBusy && fwViewService().updaterState() == FwUpdaterState::Failed && is_this_job()) {
        fwViewService().updaterError(err, sizeof err);
        snprintf(out, n, "Failed: %s", fw_updates_error_text(err));
    } else if (f.onCard && !f.complete) {
        snprintf(out, n, "Incomplete download");
    } else if (f.updaterBusy) {
        snprintf(out, n, "Busy with another download");
    }
}

/* A rolled-back other slot is offered both ways: run once more, or written afresh. */
static const char * card_label(crosspad_gui::FwCardAct act, bool rolledBack) {
    if (rolledBack && act == crosspad_gui::kFwActRun) return "Try this slot again";
    if (rolledBack && (act == crosspad_gui::kFwActInstallRun || act == crosspad_gui::kFwActInstall)) return "Reinstall";
    return fw_action_label(act);
}

static void update_actions(void) {
    const crosspad_gui::FwCardFacts f = facts();
    const crosspad_gui::FwCardPlan p = crosspad_gui::fwCardPlan(f);
    for (int i = 0; i < kActionCount; ++i) {
        lv_obj_t * b = s.buttons[i];
        set_hidden(b, !(p.shown & kActions[i]));
        const bool enabled = (p.enabled & kActions[i]) != 0;
        if (lv_obj_has_state(b, LV_STATE_DISABLED) == enabled) {
            if (enabled) lv_obj_remove_state(b, LV_STATE_DISABLED);
            else lv_obj_add_state(b, LV_STATE_DISABLED);
        }
        set_text(lv_obj_get_child(b, 0), card_label(kActions[i], f.slotRolledBack));
    }
    char text[kTextMax];
    status_text(f, p, text, sizeof text);
    set_text(s.status, text);
    set_hidden(s.status, !text[0]);
    // Version line: the version and what the card knows about it.
    char line[kTextMax];
    char slot[kSlotTextMax] = "";
    if (f.slotLabel) snprintf(slot, sizeof slot, "  %s%c", f.slot == crosspad_gui::FwSlotRole::Running ? LV_SYMBOL_PLAY : "", f.slotLabel);
    snprintf(line, sizeof line, "%s%s%s%s", s.target.version[0] ? s.target.version : "-", slot,
             f.onCard && f.complete ? "  " LV_SYMBOL_OK : "", s.target.community ? "  " LV_SYMBOL_WARNING : "");
    set_text(s.version, line);
}

static void normalize(const char * in, char * out, size_t n) {   // "Pad Mixer", "pad-mixer" -> "padmixer"
    size_t o = 0;
    for (; *in && o + 1 < n; ++in) {
        char c = *in;
        if (c == ' ' || c == '-' || c == '_') continue;
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
        out[o++] = c;
    }
    out[o] = '\0';
}

static crosspad_gui::ILvglApp * running_app(const char * id) {
    char want[kNormMax];
    normalize(id, want, sizeof want);
    for (const auto & app : crosspad_gui::AppOrchestrator::getInstance().getApps()) {
        if (!app) continue;
        char have[kNormMax];
        normalize(app->getName(), have, sizeof have);
        if (strcmp(want, have) == 0) return app.get();
    }
    return nullptr;
}

static void build_chips(const crosspad::FwKitDetail & d) {
    size_t icons = 0;
    for (uint8_t i = 0; i < d.appCount; ++i) {
        lv_obj_t * chip = lv_obj_create(s.chips);
        lv_obj_remove_style_all(chip);
        lv_obj_set_size(chip, LV_SIZE_CONTENT, kChipH);
        lv_obj_set_style_bg_color(chip, CROSSPAD_THEME_CARD_BG, 0);
        lv_obj_set_style_bg_opa(chip, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(chip, kRadius, 0);
        lv_obj_set_style_pad_hor(chip, kPad / 2, 0);
        lv_obj_set_flex_flow(chip, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(chip, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_remove_flag(chip, static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
        crosspad_gui::ILvglApp * app = running_app(d.apps[i]);
        if (app && app->getIcon() && icons < kChipIconsMax) {
            lv_obj_t * img = lv_image_create(chip);
            lv_image_set_src(img, app->getIcon());
            s.chipImg[icons] = img;
            s.chipSrc[icons] = app->getIcon();
            ++icons;
            lv_image_set_inner_align(img, LV_IMAGE_ALIGN_CONTAIN);
            lv_obj_set_size(img, kChipIconSide, kChipIconSide);
        } else {
            lv_obj_t * l = lv_label_create(chip);
            lv_obj_add_style(l, fw_views_text_style(), 0);
            lv_label_set_text(l, app ? app->getName() : d.apps[i]);
        }
    }
    s.chipsBuilt = true;
}

static void show_detail(void) {
    const crosspad::FwKitDetail * d = fwViewService().detail();
    if (!d) {
        set_text(s.description, "...");
        return;
    }
    if (d->displayName[0]) set_text(s.name, d->displayName);
    set_text(s.description, d->description[0] ? d->description : "No description.");
    if (!s.chipsBuilt) build_chips(*d);
    set_text(s.notes, d->notes);
    set_hidden(s.notesHead, !d->notes[0]);
    set_hidden(s.notes, !d->notes[0]);
    // size - channel - tier - author
    int vi = -1;
    const crosspad::FwStoreKit * k = store_kit(&vi);
    char size[kSizeTextMax] = "-";
    if (k && vi >= 0) fw_format_mb(k->versions[vi].bytes, size, sizeof size);
    else if (const FwLibItem * it = library_item()) fw_format_mb(it->bytes, size, sizeof size);
    const char * channel = k && vi >= 0 ? (k->versions[vi].channel == crosspad::FwChannel::Beta ? "beta" : "stable") : "-";
    char meta[kTextMax];
    const int n = snprintf(meta, sizeof meta, "%s  \xC2\xB7  %s  \xC2\xB7  %s", size, channel,
                           d->tier == crosspad::FwTier::Community ? "community" : "official");
    if (d->author[0] && n > 0 && n < static_cast<int>(sizeof meta))
        snprintf(meta + n, sizeof meta - static_cast<size_t>(n), "  \xC2\xB7  by %s", d->author);
    set_text(s.meta, meta);
}

static void icon_done(const lv_image_dsc_t * icon, void *) {
    s.iconTicket = 0;
    if (!icon) return;
    if (!s.overlay) {
        fw_icons_release(icon);
        return;
    }
    s.icon = icon;
    lv_image_set_src(s.img, icon);
    set_hidden(s.img, false);
    set_hidden(s.letter, true);
}

static void action_cb(lv_event_t * e) {
    const auto act = static_cast<crosspad_gui::FwCardAct>(reinterpret_cast<uintptr_t>(lv_event_get_user_data(e)));
    FwTarget t = s.target;
    crosspad_gui::fw_text::copy(t.pkg, s.pkg, sizeof t.pkg);
    if (const FwLibItem * it = library_item()) {   // what the card holds now, not what the tile knew
        t.unknownBuild = it->verdict == crosspad::FwPkgVerdict::UnknownBuild;
        t.stmProtoOlder = it->stmProtoOlder;
    }
    fw_action_start(act, t, s.tab, s.group);
}

static void older_row_cb(lv_event_t * e) {
    const auto vi = static_cast<int>(reinterpret_cast<uintptr_t>(lv_event_get_user_data(e)));
    const crosspad::FwStoreKit * k = store_kit(nullptr);
    if (!k || vi >= k->count) return;
    FwTarget t{};
    crosspad_gui::fw_text::copy(t.kit, k->kit, sizeof t.kit);
    crosspad_gui::fw_text::copy(t.version, k->versions[vi].version, sizeof t.version);
    t.community = k->versions[vi].tier == crosspad::FwTier::Community;
    fw_kit_card_open(t, s.tab);
}

static void older_toggle_cb(lv_event_t *) {
    if (!lv_obj_has_flag(s.olderBox, LV_OBJ_FLAG_HIDDEN)) {
        set_hidden(s.olderBox, true);
        return;
    }
    if (lv_obj_get_child_count(s.olderBox) == 0) {
        const crosspad::FwStoreKit * k = store_kit(nullptr);
        uint8_t older[crosspad::kFwCatalogVersions];
        const size_t n = k ? crosspad::fwStoreOlder(*k, older, crosspad::kFwCatalogVersions) : 0;
        for (size_t i = 0; i < n; ++i) {
            const crosspad::FwStoreVersion & v = k->versions[older[i]];
            char size[kSizeTextMax];
            fw_format_mb(v.bytes, size, sizeof size);
            char text[kTextMax];
            snprintf(text, sizeof text, "%s  \xC2\xB7  %s  \xC2\xB7  %s", v.version, size,
                     v.channel == crosspad::FwChannel::Beta ? "beta" : "stable");
            lv_obj_t * row = lv_button_create(s.olderBox);
            lv_obj_set_size(row, LV_PCT(100), kButtonH);
            lv_obj_t * l = lv_label_create(row);
            lv_label_set_text(l, text);
            lv_obj_center(l);
            lv_obj_add_event_cb(row, older_row_cb, LV_EVENT_CLICKED, reinterpret_cast<void *>(static_cast<uintptr_t>(older[i])));
            lv_group_add_obj(s.group, row);
        }
    }
    set_hidden(s.olderBox, false);
}

/* A new catalog: the count is worked out again, and rows built from the old
 * one (their indices name its versions) are dropped until asked for again. */
static void update_older(void) {
    const crosspad::FwStoreKit * k = store_kit(nullptr);
    uint8_t older[crosspad::kFwCatalogVersions];
    const size_t n = k ? crosspad::fwStoreOlder(*k, older, crosspad::kFwCatalogVersions) : 0;
    char text[kTextMax];
    snprintf(text, sizeof text, "Older versions (%u)", static_cast<unsigned>(n));
    set_text(lv_obj_get_child(s.olderBtn, 0), text);
    set_hidden(s.olderBox, true);
    lv_obj_clean(s.olderBox);
    set_hidden(s.olderBtn, n == 0);
}

static void back_clicked_cb(lv_event_t *) { lv_async_call([](void *) { fw_kit_card_close(); }, nullptr); }

static bool card_back_cb(void *) {
    fw_kit_card_close();
    return true;
}

static lv_obj_t * button(lv_obj_t * parent, const char * text) {
    lv_obj_t * b = lv_button_create(parent);
    lv_obj_set_height(b, kButtonH);
    lv_obj_t * l = lv_label_create(b);
    lv_label_set_text(l, text);
    lv_obj_center(l);
    lv_group_add_obj(s.group, b);
    return b;
}

static void build(void) {
    s.overlay = lv_obj_create(crosspad_gui::getOverlayParent());
    lv_obj_remove_style_all(s.overlay);
    lv_obj_set_size(s.overlay, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(s.overlay, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s.overlay, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(s.overlay, kPad, 0);
    lv_obj_set_style_pad_row(s.overlay, kPad, 0);
    lv_obj_set_flex_flow(s.overlay, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(s.overlay, LV_DIR_VER);
    lv_obj_add_flag(s.overlay, LV_OBJ_FLAG_CLICKABLE);   // touches stop here, not on the tab below

    lv_obj_t * back = button(s.overlay, LV_SYMBOL_LEFT "  Back");
    lv_obj_add_event_cb(back, back_clicked_cb, LV_EVENT_CLICKED, nullptr);

    lv_obj_t * head = lv_obj_create(s.overlay);
    lv_obj_remove_style_all(head);
    lv_obj_set_size(head, LV_PCT(100), kIconSide);
    lv_obj_set_flex_flow(head, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(head, kPad, 0);
    lv_obj_remove_flag(head, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t * iconBox = lv_obj_create(head);
    lv_obj_remove_style_all(iconBox);
    lv_obj_set_size(iconBox, kIconSide, kIconSide);
    lv_obj_remove_flag(iconBox, LV_OBJ_FLAG_SCROLLABLE);
    s.img = lv_image_create(iconBox);
    lv_obj_set_size(s.img, kIconSide, kIconSide);
    lv_obj_add_flag(s.img, LV_OBJ_FLAG_HIDDEN);
    s.letter = lv_label_create(iconBox);
    lv_obj_set_style_text_font(s.letter, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(s.letter, CROSSPAD_THEME_PRIMARY, 0);
    lv_obj_center(s.letter);
    lv_obj_t * titles = lv_obj_create(head);
    lv_obj_remove_style_all(titles);
    lv_obj_set_flex_grow(titles, 1);
    lv_obj_set_height(titles, kIconSide);
    lv_obj_set_flex_flow(titles, LV_FLEX_FLOW_COLUMN);
    lv_obj_remove_flag(titles, LV_OBJ_FLAG_SCROLLABLE);
    s.name = text_label(titles, false);
    lv_obj_set_style_text_font(s.name, &lv_font_montserrat_16, 0);
    s.version = text_label(titles, false);

    s.description = text_label(s.overlay, true);
    s.chips = lv_obj_create(s.overlay);
    lv_obj_remove_style_all(s.chips);
    lv_obj_set_size(s.chips, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(s.chips, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_style_pad_gap(s.chips, kPad / 2, 0);
    lv_obj_remove_flag(s.chips, LV_OBJ_FLAG_SCROLLABLE);
    s.meta = text_label(s.overlay, false);
    s.notesHead = text_label(s.overlay, false);
    lv_obj_set_style_text_color(s.notesHead, CROSSPAD_THEME_PRIMARY, 0);
    lv_label_set_text(s.notesHead, "What's new");
    s.notes = text_label(s.overlay, true);
    s.status = text_label(s.overlay, true);

    lv_obj_t * actions = lv_obj_create(s.overlay);
    lv_obj_remove_style_all(actions);
    lv_obj_set_size(actions, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(actions, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_style_pad_gap(actions, kPad, 0);
    lv_obj_remove_flag(actions, LV_OBJ_FLAG_SCROLLABLE);
    for (int i = 0; i < kActionCount; ++i) {
        s.buttons[i] = button(actions, fw_action_label(kActions[i]));
        lv_obj_add_event_cb(s.buttons[i], action_cb, LV_EVENT_CLICKED,
                            reinterpret_cast<void *>(static_cast<uintptr_t>(kActions[i])));
        lv_obj_add_flag(s.buttons[i], LV_OBJ_FLAG_HIDDEN);
    }

    s.olderBtn = button(s.overlay, "Older versions");
    lv_obj_set_width(s.olderBtn, LV_PCT(100));
    lv_obj_add_event_cb(s.olderBtn, older_toggle_cb, LV_EVENT_CLICKED, nullptr);
    lv_obj_add_flag(s.olderBtn, LV_OBJ_FLAG_HIDDEN);
    s.olderBox = lv_obj_create(s.overlay);
    lv_obj_remove_style_all(s.olderBox);
    lv_obj_set_size(s.olderBox, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(s.olderBox, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(s.olderBox, kPad / 2, 0);
    lv_obj_add_flag(s.olderBox, LV_OBJ_FLAG_HIDDEN);
}

static void open_async(void *) {
    if (!s_openPending) return;   // the host closed before this cycle came
    s_openPending = false;
    // Taken before a replaced card lets go of its own: a card whose tab closed
    // under it would otherwise free the views' data only to load it again.
    fw_views_acquire();          // the card keeps the data alive even if its tab closes under it
    fw_views_want_store(true);   // a Library card also needs the catalog: in it? older versions?
    if (s.overlay) fw_kit_card_close();
    s = Card{};
    s.target = s_opening;
    s.tab = s_openingTab;
    if (s.target.pkg[0]) crosspad_gui::fw_text::copy(s.pkg, s.target.pkg, sizeof s.pkg);
    else if (s.target.kit[0]) crosspad::fwPackageName(s.target.kit, s.target.version, s.pkg, sizeof s.pkg);
    lv_indev_t * nav = crosspad_gui::getGuiPlatform().getNavigationEncoder();
    s.prevGroup = nav ? lv_indev_get_group(nav) : nullptr;
    s.group = lv_group_create();
    build();
    char name[kNameMax];
    if (s.target.kit[0]) crosspad_gui::fw_text::copy(name, s.target.kit, sizeof name);
    else snprintf(name, sizeof name, "Slot %c", s.target.slot ? s.target.slot : '?');
    char letter[kLetterMax];
    fw_letter_of(name, letter, sizeof letter);
    lv_label_set_text(s.letter, letter);
    lv_label_set_text(s.name, name);
    if (nav) lv_indev_set_group(nav, s.group);
    s.backToken = crosspad_gui::ui_back_push(card_back_cb, nullptr);
    fwViewService().loadDetail(s.target.kit, s.target.version, s.pkg);
    s.detailGen = fwViewService().detailGen();
    s.storeGen = fwViewService().storeGen() - 1;   // older versions are worked out on the first refresh
    if (const lv_image_dsc_t * held = fw_icons_acquire(s.pkg, s.target.kit, s.target.version))
        icon_done(held, nullptr);   // the tile under the card shows it: no card read
    else
        s.iconTicket = fw_icons_request(s.pkg, s.target.kit, s.target.version, icon_done, nullptr);
    fwViewService().uiCard(s.target.kit, s.target.version);
    show_detail();
    fw_kit_card_refresh();
    fw_views_focus_first(s.overlay, s.group);
}

void fw_kit_card_open(const FwTarget & t, FwNetTab tab) {
    s_opening = t;
    s_openingTab = tab;
    s_openPending = true;
    lv_async_call(open_async, nullptr);
}

bool fw_kit_card_is_open(void) { return s.overlay != nullptr; }

void fw_kit_card_close(void) {
    s_openPending = false;
    if (!s.overlay) return;
    if (s.iconTicket) fw_icons_cancel(s.iconTicket);
    if (s.icon) {
        lv_image_set_src(s.img, nullptr);
        fw_icons_release(s.icon);
    }
    // The launcher decodes its icons again when it is drawn: the Firmware app
    // does not keep them meanwhile, nor does the radio's PSRAM gate count them.
    for (size_t i = 0; i < kChipIconsMax && s.chipImg[i]; ++i) {
        lv_image_set_src(s.chipImg[i], nullptr);
        lv_image_cache_drop(s.chipSrc[i]);
    }
    crosspad_gui::ui_back_remove(s.backToken);
    lv_indev_restore_group(crosspad_gui::getGuiPlatform().getNavigationEncoder(), s.prevGroup);
    lv_group_delete_deferred(s.group);   // closed from its own Back button: the encoder read may still hold it
    lv_obj_add_flag(s.overlay, LV_OBJ_FLAG_HIDDEN);
    lv_obj_delete_async(s.overlay);
    s = Card{};
    fwViewService().uiCard(nullptr, nullptr);
    fw_views_want_store(false);
    fw_views_release();
}

void fw_kit_card_refresh(void) {
    if (!s.overlay) return;
    if (fwViewService().detailGen() != s.detailGen) {
        s.detailGen = fwViewService().detailGen();
        show_detail();
    }
    if (fwViewService().storeGen() != s.storeGen) {
        s.storeGen = fwViewService().storeGen();
        update_older();
        show_detail();   // size and channel come from the catalog
    }
    update_actions();
}

#endif // USE_LVGL
