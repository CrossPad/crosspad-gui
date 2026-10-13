// SPDX-License-Identifier: GPL-3.0-or-later
#ifdef USE_LVGL

#include "crosspad-gui/fw/fw_actions.h"

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "crosspad-gui/components/ui_modal.h"
#include "crosspad-gui/components/ui_toast.h"
#include "crosspad-gui/fw/fw_updates_view.h"
#include "crosspad-gui/fw/fw_views_common.h"
#include "crosspad-gui/platform/psram_placement.h"
#include "crosspad-gui/widgets/gui_helpers.h"
#include "crosspad/fw/FwStore.hpp"
#include "crosspad/fw/Semver.hpp"
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

static constexpr char kBusy[] = "busy";
static constexpr size_t kMessageMax = 320;
/* switch_async's argument: non-null moves a RolledBack slot back to Installed first. */
static constexpr uintptr_t kClearRollback = 1;
static constexpr const char* kCommunityText =
    "CrossPad CI built this firmware from community code. The CrossPad team has not tested it. Continue?";
static constexpr const char* kStmProtoOlderWarning =
    "Built for an older pad controller protocol - it may not work.";
// An image with no CPFI block predates fw_confirm_running(): the bootloader
// stages it PENDING_VERIFY like any other trial and nothing in it ever marks
// it valid, so it is rolled back after exactly one boot.
static constexpr const char* kNoDescWarning =
    "This firmware was built before firmware packages existed. It cannot confirm itself "
    "and will be rolled back after one boot.";
// A bootloader without rollback never undoes a firmware that does not start:
// said before every restart into the other slot, as the one-page app said it.
static constexpr const char* kNoRollbackWarning = "Rollback unavailable: old bootloader.";

/* What the user pressed and is still waiting for. Its outcome may ask
 * something; an automatic check never does. */
enum class Pending : uint8_t { None, Check, Download, Install };

/* The press a job belongs to: settled only by that job's end -- the views'
 * updater generation moved past `gen`, and a download is for this kit and
 * version -- never by another job that ended meanwhile. */
struct PendingJob {
    Pending kind;
    uint32_t gen;
    FwNetTab tab;
    char kit[crosspad::kFwKitMax];
    char version[32];
};

/* The questions before an install, in this order. */
enum Step : uintptr_t { kStepCommunity, kStepOlder, kStepFinal };

/* PSRAM: LVGL-thread text and state nothing needs fast; internal RAM gates the radio. */
CROSSPAD_GUI_PSRAM_BSS static PendingJob s_pending;
CROSSPAD_GUI_PSRAM_BSS static FwTarget s_target;   // what the open dialogs are about
CROSSPAD_GUI_PSRAM_BSS static char s_message[kMessageMax];
static crosspad_gui::FwCardAct s_act = crosspad_gui::kFwActDownload;
static FwNetTab s_tab = kFwNetTabStore;
static lv_group_t* s_group;      // nullptr: the host closed, nothing is asked any more
static lv_obj_t* s_dialog;       // our confirm on screen (one at a time: it holds the encoder and the touch)
static bool s_communityAccepted;   // "Continue" on the community warning, remembered once the install starts
static uintptr_t s_hostGen;        // bumped by fw_actions_forget()

/* A start queued for the next LVGL cycle carries the host generation it was
 * asked in, beside its step: a host closed since drops it, even when a host
 * opened again has taken s_group by then. */
static constexpr unsigned kStepBits = 2;
static constexpr uintptr_t kStepMask = (uintptr_t{1} << kStepBits) - 1;
static constexpr uintptr_t kGenMask = UINTPTR_MAX >> kStepBits;
static_assert(kStepFinal <= kStepMask, "every step fits beside the generation");

static void* queued(uintptr_t step) { return reinterpret_cast<void*>(((s_hostGen & kGenMask) << kStepBits) | step); }

static bool still_asked(void* user) {
    return s_group && (reinterpret_cast<uintptr_t>(user) >> kStepBits) == (s_hostGen & kGenMask);
}

static uintptr_t queued_step(void* user) { return reinterpret_cast<uintptr_t>(user) & kStepMask; }

const char* fw_action_label(crosspad_gui::FwCardAct act) {
    switch (act) {
        case crosspad_gui::kFwActDownload:   return "Download";
        case crosspad_gui::kFwActResume:     return "Resume download";
        case crosspad_gui::kFwActInstallRun: return "Install and run";
        case crosspad_gui::kFwActInstall:    return "Install";
        case crosspad_gui::kFwActRun:        return "Run";
        case crosspad_gui::kFwActRunNew:     return "Run the new version";
        case crosspad_gui::kFwActPin:        return "Pin";
        case crosspad_gui::kFwActUnpin:      return "Unpin";
        case crosspad_gui::kFwActDelete:     return "Delete from card";
    }
    return "?";
}

/* A kit card's group goes with the card; a dialog asked after that gives the
 * encoder back to whatever holds it then (nullptr). */
static lv_group_t* live(lv_group_t* group) { return lv_group_is_alive(group) ? group : nullptr; }

/* Every confirm callback clears s_dialog first: the dialog is gone by then. */
static void ask(const char* title, const char* message, const char* yes, const char* no, modal_confirm_cb_t cb,
                void* user, lv_group_t* group) {
    s_dialog = ui_modal_confirm(title, message, yes, no, cb, user, group);
}

static void track(Pending kind, FwNetTab tab) {
    s_pending.kind = kind;
    s_pending.gen = fw_views_updater_gen();
    s_pending.tab = tab;
    crosspad_gui::fw_text::copy(s_pending.kit, kind == Pending::Check ? "" : s_target.kit, sizeof s_pending.kit);
    crosspad_gui::fw_text::copy(s_pending.version, kind == Pending::Check ? "" : s_target.version, sizeof s_pending.version);
    fw_views_job_started();
}

/* The updater's running (or last) download is for this kit and version. */
static bool updater_job_is(const char* kit, const char* version) {
    if (!kit[0]) return false;
    char tk[kFwuKitMax];
    char tv[kFwuVersionMax];
    fwViewService().updaterTargetKit(tk, sizeof tk);
    fwViewService().updaterTarget(tv, sizeof tv);
    return strcmp(tk, kit) == 0 && crosspad::compareVersionText(tv, version) == 0;
}

static const crosspad::FwSlotInfo& other_slot(void) {
    const crosspad::FwSlotInfo* slots = fw_views_slots();
    return slots[slots[0].running ? 1 : 0];
}

/* "\n\n<note>" after the first `n` bytes of s_message, when it fits; the new length. */
static int append_note(int n, const char* note) {
    if (n <= 0 || n >= static_cast<int>(sizeof s_message)) return n;
    return n + snprintf(s_message + n, sizeof s_message - static_cast<size_t>(n), "\n\n%s", note);
}

static int append_rollback_note(int n) { return fwViewService().rollbackSupported() ? n : append_note(n, kNoRollbackWarning); }

/* The community warning was accepted for this install, and the install has started. */
static void remember_community(void) {
    if (s_communityAccepted) fwViewService().ackCommunity(s_target.kit);
    s_communityAccepted = false;
}

void fw_action_refused(const char* why, FwNetTab tab, lv_group_t* group) {
    if (strcmp(why, kBusy) == 0) ui_toast_show("Busy - try again in a moment", kFwToastMs, UI_TOAST_INFO);
    else if (fw_updates_is_no_mem(why) && !fwViewService().networkMode()) fwViewService().offerNetworkMode(tab, live(group));
    else ui_toast_show(fw_updates_error_text(why), kFwToastMs, UI_TOAST_WARNING);
}

void fw_actions_track_check(FwNetTab tab) { track(Pending::Check, tab); }

void fw_actions_forget(void) {
    s_pending.kind = Pending::None;
    s_group = nullptr;
    ++s_hostGen;
    s_communityAccepted = false;
    // Answered "no" for the user: a confirm left over a closed app would act on it.
    if (s_dialog) ui_modal_confirm_dismiss(s_dialog, false);
}

// ── Switching: the existing overlay, one LVGL cycle after the dialog ─────

/* ui_modal_confirm() gives the encoder group back after its callback returns,
 * which would undo the empty group a flow overlay sets -- so every flow and
 * every follow-up dialog starts one LVGL cycle later. */
static void switch_async(void* clearRollback) {
    if (!fwViewService().flowSwitchAndRestart(clearRollback != nullptr))
        ui_toast_show("Could not start the switch", kFwToastMs, UI_TOAST_WARNING);
}

static void run_new_cb(bool yes, void*) {
    s_dialog = nullptr;
    if (yes) lv_async_call(switch_async, nullptr);
}

static void ask_run_new(lv_group_t* group) {
    char ver[kFwuVersionMax];
    fwViewService().updaterReadyVersion(ver, sizeof ver);
    append_rollback_note(
        snprintf(s_message, sizeof s_message, "%s is installed in the other slot. Run the new version now?", ver));
    ask("Run the new version now?", s_message, "Run", "Later", run_new_cb, nullptr, group);
}

static void run_cb(bool yes, void* clearRollback) {
    s_dialog = nullptr;
    if (yes) lv_async_call(switch_async, clearRollback);
}

static void ask_run(void) {
    const crosspad::FwSlotInfo& other = other_slot();
    // A rolled-back slot is offered again rather than locked out: what failed
    // (a missing card, a flat battery) may since have been fixed.
    const bool retry = other.state == crosspad::FwSlotState::RolledBack;
    int n;
    if (retry) {
        n = snprintf(s_message, sizeof s_message, "Slot %c did not start last time. Try it once more?", other.label);
    } else {
        n = snprintf(s_message, sizeof s_message,
                     "The CrossPad restarts into %s. This firmware's settings are saved to its package.",
                     other.package[0] ? other.package : "the other slot");
        if (!other.hasDesc) n = append_note(n, kNoDescWarning);
    }
    append_rollback_note(n);
    ask(retry ? "Try this slot again" : "Switch firmware", s_message, retry ? "Try again" : "Restart", "Cancel",
        run_cb, retry ? reinterpret_cast<void*>(kClearRollback) : nullptr, live(s_group));
}

// ── Installing ───────────────────────────────────────────────────────────

static void install_from_card_async(void*) {
    if (fwViewService().flowInstallAndRestart(s_target.pkg, s_target.unknownBuild)) remember_community();
    else ui_toast_show("Could not start the install", kFwToastMs, UI_TOAST_WARNING);
}

static void install_from_card_cb(bool yes, void*) {
    s_dialog = nullptr;
    if (yes) lv_async_call(install_from_card_async, nullptr);
}

static void download_and_install(void) {
    char why[kFwuErrorMax];
    if (fwViewService().downloadStart(s_target.kit, s_target.version, true, why, sizeof why)) {
        remember_community();
        track(Pending::Install, s_tab);
    } else {
        fw_action_refused(why, s_tab, s_group);
    }
}

/* From the next LVGL cycle, not the Replace dialog's callback: a refusal may
 * offer network mode, which must take the encoder after that dialog gave it back. */
static void download_and_install_async(void* user) {
    if (still_asked(user)) download_and_install();
}

static void replace_cb(bool yes, void*) {
    s_dialog = nullptr;
    if (yes) lv_async_call(download_and_install_async, queued(0));
}

/* The newest version of `kit` in the user's channel for it (the Store tile's),
 * or on the card; "" when none. */
static void newest_known(const char* kit, char* out, size_t n) {
    out[0] = '\0';
    if (const crosspad::FwStore* store = fwViewService().store()) {
        if (const crosspad::FwStoreKit* k = crosspad::fwStoreFind(*store, kit)) {
            const auto channel = fwViewService().channelBeta(kit) ? crosspad::FwChannel::Beta : crosspad::FwChannel::Stable;
            const int v = crosspad::fwShownVersion(*k, channel);
            if (v >= 0) crosspad_gui::fw_text::copy(out, k->versions[v].version, n);
        }
    }
    if (const FwLibrarySnap* lib = fwViewService().library()) {
        for (size_t i = 0; i < lib->count; ++i) {
            const FwLibItem& it = lib->items[i];
            if (strcmp(it.kit, kit) == 0 && crosspad::compareVersionText(it.version, out) > 0)
                crosspad_gui::fw_text::copy(out, it.version, n);
        }
    }
}

static void final_step(void) {
    if (s_act == crosspad_gui::kFwActInstallRun) {
        const crosspad::FwSlotInfo& other = other_slot();
        int n;
        if (s_target.unknownBuild) {
            n = snprintf(s_message, sizeof s_message,
                         "\"%s\" could not be verified against this board. Installing it to slot %c "
                         "anyway may leave that slot unbootable.", s_target.pkg, other.label);
        } else {
            n = snprintf(s_message, sizeof s_message, "Install \"%s\" (%s, %s) to slot %c and restart into it.",
                         s_target.pkg, s_target.kit[0] ? s_target.kit : "-", s_target.version, other.label);
        }
        if (s_target.stmProtoOlder) n = append_note(n, kStmProtoOlderWarning);
        append_rollback_note(n);
        ask("Install firmware", s_message, "Install", "Cancel", install_from_card_cb, nullptr, live(s_group));
        return;
    }
    // Install: the slot not running may hold another kit (the DAW firmware,
    // say) -- that is the user's to give up, so it is asked. An empty or
    // rolled-back slot, or one with this kit, is replaced without a question.
    const crosspad::FwSlotInfo& other = other_slot();
    const bool anotherKit = other.state != crosspad::FwSlotState::Empty && other.state != crosspad::FwSlotState::RolledBack &&
                            strcmp(other.kit, s_target.kit) != 0;
    if (!anotherKit) {
        download_and_install();
        return;
    }
    snprintf(s_message, sizeof s_message, "The other slot holds %s %s. Installing %s %s replaces it.",
             other.kit[0] ? other.kit : "another firmware", other.version, s_target.kit, s_target.version);
    ask("Replace the other firmware?", s_message, "Replace", "Cancel", replace_cb, nullptr, live(s_group));
}

static void next_step(Step from);

static void next_step_async(void* user) {
    if (still_asked(user)) next_step(static_cast<Step>(queued_step(user)));
}

static void step_cb(bool yes, void* step) {
    s_dialog = nullptr;
    if (!yes) return;   // declined: nothing happens (spec §5)
    const auto done = static_cast<Step>(reinterpret_cast<uintptr_t>(step));
    if (done == kStepCommunity) s_communityAccepted = true;
    lv_async_call(next_step_async, queued(static_cast<uintptr_t>(done) + 1));
}

static void next_step(Step from) {
    if (from <= kStepCommunity && s_target.community && !fwViewService().communityOk(s_target.kit)) {
        ask("Community firmware", kCommunityText, "Continue", "Cancel", step_cb,
            reinterpret_cast<void*>(kStepCommunity), live(s_group));
        return;
    }
    if (from <= kStepOlder) {
        char newest[kFwuVersionMax];
        newest_known(s_target.kit, newest, sizeof newest);
        if (newest[0] && crosspad::compareVersionText(s_target.version, newest) < 0) {
            snprintf(s_message, sizeof s_message, "%s is older than %s. Install it anyway?", s_target.version, newest);
            ask("Older version", s_message, "Install", "Cancel", step_cb, reinterpret_cast<void*>(kStepOlder),
                live(s_group));
            return;
        }
    }
    final_step();
}

/* Dialogs from here open on their own, over whatever is in front: they give
 * the encoder back to the group current now (nullptr), which is right even
 * when another modal is up. */
void fw_actions_tick(void) {
    if (s_pending.kind == Pending::None || fwViewService().updaterBusy()) return;
    if (s_dialog) return;   // asked once the user has answered ours
    if (fw_views_updater_gen() == s_pending.gen) return;   // its job's end not seen yet
    const PendingJob job = s_pending;
    s_pending.kind = Pending::None;
    if (job.kind != Pending::Check && !updater_job_is(job.kit, job.version)) return;   // another job ended
    char err[kFwuErrorMax];
    fwViewService().updaterError(err, sizeof err);
    const FwUpdaterState st = fwViewService().updaterState();
    if (st == FwUpdaterState::Failed && fw_updates_is_no_mem(err)) {   // the job's lease, or the radio's own gate after it
        if (!fwViewService().networkMode()) fwViewService().offerNetworkMode(job.tab, nullptr);
        return;
    }
    if (job.kind == Pending::Install && st == FwUpdaterState::ReadyToRestart) ask_run_new(nullptr);
}

// ── Pin, delete, download ────────────────────────────────────────────────

static void start_download(void) {
    char why[kFwuErrorMax];
    if (fwViewService().downloadStart(s_target.kit, s_target.version, false, why, sizeof why))
        track(Pending::Download, s_tab);
    else
        fw_action_refused(why, s_tab, s_group);
}

/* The package as the card holds it now; nullptr when it is gone. */
static const FwLibItem* card_item(const char* pkg) {
    const FwLibrarySnap* lib = fwViewService().library();
    if (!lib) return nullptr;
    for (size_t i = 0; i < lib->count; ++i)
        if (strcmp(lib->items[i].name, pkg) == 0) return &lib->items[i];
    return nullptr;
}

/* Deletable now, not only when the dialog opened: a pin, a slot, an install
 * or a download (FW_INSTALL / FW_DOWNLOAD over CDC) may have come since. */
static bool deletable(const char* pkg) {
    fw_views_slots_invalidate();   // read the slots as they are now, not as the last flow left them
    if (fwViewService().flowActive() || fwViewService().installWriting()) return false;
    const FwLibItem* it = card_item(pkg);
    if (!it || it->pinned || fw_views_slot_of(pkg, nullptr)) return false;
    return !(fwViewService().updaterBusy() && updater_job_is(it->kit, it->version));
}

static void delete_cb(bool yes, void*) {
    s_dialog = nullptr;
    if (!yes) return;
    if (!deletable(s_target.pkg)) {
        ui_toast_show("Not deleted - it is pinned, in a slot, installing or in use", kFwToastMs, UI_TOAST_WARNING);
        return;
    }
    const crosspad::FwSlotInfo* slots = fw_views_slots();
    fwViewService().remove(s_target.pkg, slots[0].package, slots[1].package);
}

void fw_action_start(crosspad_gui::FwCardAct act, const FwTarget& t, FwNetTab tab, lv_group_t* group) {
    s_target = t;
    s_act = act;
    s_tab = tab;
    s_group = group;
    s_communityAccepted = false;
    switch (act) {
        case crosspad_gui::kFwActDownload:
        case crosspad_gui::kFwActResume:
            start_download();
            break;
        case crosspad_gui::kFwActInstallRun:
        case crosspad_gui::kFwActInstall:
            next_step(kStepCommunity);
            break;
        case crosspad_gui::kFwActRun:
            ask_run();
            break;
        case crosspad_gui::kFwActRunNew:
            ask_run_new(live(group));
            break;
        case crosspad_gui::kFwActPin:
            fwViewService().setPinned(t.pkg, true);
            break;
        case crosspad_gui::kFwActUnpin:
            fwViewService().setPinned(t.pkg, false);
            break;
        case crosspad_gui::kFwActDelete:
            snprintf(s_message, sizeof s_message, "Delete %s from the SD card?", t.pkg);
            ask("Delete firmware", s_message, "Delete", "Cancel", delete_cb, nullptr, live(group));
            break;
    }
}

#endif // USE_LVGL
