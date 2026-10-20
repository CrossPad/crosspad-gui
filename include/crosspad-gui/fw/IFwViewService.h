// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

/**
 * @file IFwViewService.h
 * @brief What the Firmware views (Library, Store, Updates, the kit card and
 *        its actions) need from a platform: the firmware library on the card,
 *        the cached catalog, the two slots, the updater's jobs, the WiFi lease
 *        and the network-mode fallback, and the FW_UI state a test reads.
 *
 * The views run on the LVGL thread and call nothing else of the platform's.
 * Everything that touches storage happens behind requests (scanLibrary(),
 * loadStore(), loadDetail(), requestIcon(), the card writes) whose answers move
 * in when dataPoll() runs from the views' timer; what a getter returns stays
 * valid until the next dataPoll(). Getters work between dataOpen() and
 * dataClose() only. The board's implementation does that work on one worker
 * task of its own; the simulator's and the tests' answer from memory.
 */

#include <cstddef>
#include <cstdint>

#include "lvgl.h"
#include "crosspad/fw/FwLibraryGc.hpp"
#include "crosspad/fw/FwPackage.hpp"
#include "crosspad/fw/FwStore.hpp"
#include "crosspad/net/WifiTypes.hpp"

namespace crosspad_gui {

constexpr size_t kFwLibMax = crosspad::kFwGcMaxPackages;       ///< what the space cleanup handles
constexpr size_t kFwCommunityOkMax = crosspad::kFwBetaListMax;   ///< space-separated kits: the beta list's helpers serve it
/// The updater's text fields, bytes incl. NUL.
constexpr size_t kFwuVersionMax = 24;
constexpr size_t kFwuKitMax = 16;
constexpr size_t kFwuErrorMax = 48;

enum class FwIconSrc : uint8_t { None, Package, Cache };

/** A package on the card, or an image in a slot whose package is not on the
 *  card (a board flashed over USB): name "", onCard false, kit and version
 *  from the slot. Slot-only items live in the Library view, never in a
 *  snapshot. slot, running and slotState are set by the Library view from
 *  readSlots(): a platform's storage worker never reads the slots. */
struct FwLibItem {
    char name[crosspad::kFwPackageNameMax];              ///< package directory, "" for a slot-only image
    char kit[crosspad::kFwKitMax];                   ///< "" for an image without CPFI; the manifest's without an image
    char version[crosspad::kFwPkgVersionMax];        ///< the image's (else the manifest's) version, no leading 'v'
    char displayName[crosspad::kFwDisplayNameMax];   ///< the manifest's, else the kit, else the name
    crosspad::FwPkgVerdict verdict;
    bool complete;                                   ///< not a download in flight
    bool pinned;
    bool community;
    bool stmProtoOlder;                              ///< built for an older pad-controller protocol
    bool onCard;
    char slot;                                       ///< 'A' / 'B' when a slot holds this image, else 0
    bool running;                                    ///< ... and it is the running one
    crosspad::FwSlotState slotState;                 ///< that slot's state (RolledBack: it did not start)
    FwIconSrc icon;
    uint64_t bytes;
};

struct FwLibrarySnap {
    size_t count;
    uint64_t bytes;                                  ///< the whole library
    char communityOk[kFwCommunityOkMax];             ///< kits whose community warning was accepted
    FwLibItem items[kFwLibMax];
};

/// The updater's job, in the order the board's updater numbers it.
enum class FwUpdaterState : uint8_t {
    Idle, Checking, UpToDate, Available, Downloading, Installing, ReadyToRestart, Failed
};

/// Where a refusal for memory offers network mode: that screen's tabs, in its order.
enum FwNetTab : uint8_t { kFwNetTabWifi, kFwNetTabStore, kFwNetTabUpdates, kFwNetTabCount };

class IFwViewService {
public:
    virtual ~IFwViewService() = default;

    // ── The card: requests, answers moved in by dataPoll() ──────────────
    /// A view starts / stops using the data; the last close frees everything.
    virtual void dataOpen() = 0;
    virtual void dataClose() = 0;
    /// Requests queued or running, or answers not moved in yet.
    virtual bool dataBusy() = 0;
    /// Moves finished answers in; an icon's goes to fw_icons_deliver().
    virtual void dataPoll() = 0;

    virtual void scanLibrary() = 0;
    /// nullptr until the first scan landed; the Library view writes slot/running into it.
    virtual FwLibrarySnap* library() = 0;
    virtual uint32_t libraryGen() = 0;   ///< changes with every scan that landed

    virtual void loadStore() = 0;
    virtual void dropStore() = 0;                         ///< no view needs the catalog any more
    virtual const crosspad::FwStore* store() = 0;         ///< nullptr unless the last load was Ok
    virtual crosspad::FwStoreLoad storeState() = 0;
    virtual bool storeLoaded() = 0;                       ///< a load finished, whatever its result
    virtual uint32_t storeGen() = 0;

    /// A kit card's text: the package's manifest (pkg may be ""), filled in from
    /// the catalog's kit text. Only the latest request's answer is kept.
    virtual void loadDetail(const char* kit, const char* version, const char* pkg) = 0;
    virtual const crosspad::FwKitDetail* detail() = 0;    ///< nullptr while it loads
    virtual uint32_t detailGen() = 0;

    /// Card writes; each ends with a library scan. A remove of a package named
    /// by a slot is refused by the platform too.
    virtual void setPinned(const char* pkg, bool pinned) = 0;
    virtual void remove(const char* pkg, const char* slotA, const char* slotB) = 0;
    virtual void ackCommunity(const char* kit) = 0;
    virtual bool communityOk(const char* kit) = 0;

    /// <pkg>/icon.bin, else the catalog's icon of <kit>-<version>: a valid
    /// RGB565A8 48x48 LVGL image file, handed to fw_icons_deliver(ticket, buf)
    /// from dataPoll(). False when it could not be queued.
    virtual bool requestIcon(uint32_t ticket, const char* pkg, const char* kit, const char* version) = 0;
    /// A request still queued for `ticket` is dropped unread.
    virtual void cancelIcon(uint32_t ticket) = 0;
    /// A buffer the platform handed to fw_icons_deliver() goes back.
    virtual void freeIcon(uint8_t* buf) = 0;

    virtual uint32_t libraryLimitMb() = 0;

    // ── Slots, installs and switches ─────────────────────────────────────
    /// out[0] = slot A, out[1] = slot B. May read flash: never from a worker.
    virtual void readSlots(crosspad::FwSlotInfo out[2]) = 0;
    /// This boot is a trial: a crash in it rolls the slot back.
    virtual bool runningOnTrial() = 0;
    virtual bool rollbackSupported() = 0;
    /// An install is verifying or writing the other slot.
    virtual bool installWriting() = 0;
    /// The full-screen install/switch overlay is up.
    virtual bool flowActive() = 0;
    virtual bool flowInstallAndRestart(const char* pkg, bool allowUnknownBuild) = 0;
    virtual bool flowSwitchAndRestart(bool clearRollback) = 0;
    virtual const char* runningVersion() = 0;

    // ── The updater (catalog checks, downloads, installs over the network) ──
    virtual FwUpdaterState updaterState() = 0;
    virtual bool updaterBusy() = 0;
    virtual uint8_t updaterProgress() = 0;
    virtual void updaterError(char* out, size_t n) = 0;          ///< "" or one token: "catalog:network", "card_full", ...
    virtual void updaterTarget(char* out, size_t n) = 0;         ///< version of the running (or last) download/install
    virtual void updaterTargetKit(char* out, size_t n) = 0;      ///< its kit
    virtual void updaterReadyVersion(char* out, size_t n) = 0;   ///< installed this boot, waiting for a restart; "" otherwise
    virtual bool updaterChecked() = 0;                           ///< a catalog check has succeeded, dated or not
    virtual int64_t updaterLastCheck() = 0;                      ///< server time of that check, 0 without a usable Date
    virtual bool channelBeta(const char* kit) = 0;
    /// On false `refusal` is "busy" or "no_mem".
    virtual bool checkStart(char* refusal, size_t n) = 0;
    /// On false `refusal` is "bad_args", "blocked", "busy" or "no_mem".
    virtual bool downloadStart(const char* kit, const char* version, bool install, char* refusal, size_t n) = 0;

    // ── WiFi and network mode ───────────────────────────────────────────
    virtual bool wifiAcquire(const char* reason) = 0;   ///< false: refused now, wifiFail() says why
    virtual void wifiRelease() = 0;
    virtual crosspad::WifiLinkState wifiState() = 0;
    virtual crosspad::WifiFail wifiFail() = 0;
    /// This boot is network mode: nothing is offered, the Settings shortcut is absent.
    virtual bool networkMode() = 0;
    /// A confirm offering a restart into network mode on `tab`; `group` gets the encoder back.
    virtual void offerNetworkMode(FwNetTab tab, lv_group_t* group) = 0;
    virtual int64_t nowUs() = 0;

    // ── FW_UI: what the views show, for a test that drives them blind ───
    virtual void uiHost(const char* host) = 0;
    virtual void uiTab(const char* tab) = 0;
    virtual bool uiTabIs(const char* tab) = 0;
    virtual void uiItems(uint32_t n) = 0;
    virtual void uiSel(int32_t index, const char* kit, const char* version) = 0;   ///< index < 0: none
    virtual void uiCard(const char* kit, const char* version) = 0;                 ///< kit nullptr: closed
    virtual void uiStore(const char* state) = 0;                                   ///< fwStoreLoadName(), or nullptr
    virtual void uiReset() = 0;
};

/// The platform registers its service before a Firmware view is built.
void setFwViewService(IFwViewService* service);
IFwViewService& fwViewService();

} // namespace crosspad_gui
