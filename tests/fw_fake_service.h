// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

/// An IFwViewService that answers from memory: a test fills the card, the
/// catalog, the slots and the updater's state, and the views see them after
/// the next dataPoll(), as they would after the board's worker answered.

#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "crosspad-gui/fw/IFwViewService.h"
#include "crosspad/fw/FwCatalog.hpp"

namespace guitest {

class FakeFwService : public crosspad_gui::IFwViewService {
public:
    FakeFwService();

    // ── What the test sets up ────────────────────────────────────────────
    crosspad_gui::FwLibItem& addPackage(const char* kit, const char* version, const char* displayName);
    crosspad::FwStoreKit& addStoreKit(const char* kit, const char* displayName, crosspad::FwTier tier);
    void addStoreVersion(crosspad::FwStoreKit& k, const char* version, crosspad::FwChannel channel,
                         crosspad::FwTier tier, bool hasIcon = false);
    void setSlot(int i, crosspad::FwSlotState state, bool running, const char* pkg, const char* kit,
                 const char* version);

    std::unique_ptr<crosspad_gui::FwLibrarySnap> lib;
    crosspad::FwStore storeData{};
    crosspad::FwStoreLoad storeLoad = crosspad::FwStoreLoad::Ok;
    crosspad::FwKitDetail detailData{};
    crosspad::FwSlotInfo slots[2]{};
    bool scanFails = false;      ///< a scan lands nothing (no card)
    bool iconsValid = false;     ///< every icon request answers with a valid icon

    crosspad_gui::FwUpdaterState state = crosspad_gui::FwUpdaterState::Idle;
    bool busy = false;
    uint8_t progress = 0;
    std::string error, target, targetKit, readyVersion;
    bool checked = true;
    int64_t lastCheck = 1791763200;   // 2026-10-12
    bool wifiAllowed = true;
    crosspad::WifiFail wifiFailReason = crosspad::WifiFail::None;
    bool netMode = false;

    // ── What the views did ───────────────────────────────────────────────
    int opens = 0;
    int checks = 0;
    int leases = 0;
    std::vector<std::string> downloads;   ///< "kit@version[+install]"
    std::vector<std::string> pins;        ///< "pkg=1/0"
    std::vector<crosspad_gui::FwNetTab> offers;
    std::string uiHostV = "-", uiTabV = "-", uiCardV = "-", uiStoreV = "-", uiSelKey = "-";
    uint32_t uiItemsV = 0;
    int32_t uiSelV = -1;
    int iconsHeld = 0;

    // ── IFwViewService ───────────────────────────────────────────────────
    void dataOpen() override { ++opens; open_ = true; }
    void dataClose() override;
    bool dataBusy() override { return scanWanted_ || storeWanted_ || detailWanted_ || !iconWanted_.empty(); }
    void dataPoll() override;
    void scanLibrary() override { if (open_) scanWanted_ = true; }
    crosspad_gui::FwLibrarySnap* library() override { return open_ && libReady_ ? lib.get() : nullptr; }
    uint32_t libraryGen() override { return open_ ? libGen_ : 0; }
    void loadStore() override { if (open_) storeWanted_ = true; }
    void dropStore() override { storeLoaded_ = false; ++storeGen_; }
    const crosspad::FwStore* store() override {
        return open_ && storeLoaded_ && storeLoad == crosspad::FwStoreLoad::Ok ? &storeData : nullptr;
    }
    crosspad::FwStoreLoad storeState() override { return storeLoad; }
    bool storeLoaded() override { return open_ && storeLoaded_; }
    uint32_t storeGen() override { return open_ ? storeGen_ : 0; }
    void loadDetail(const char*, const char*, const char*) override { detailReady_ = false; detailWanted_ = true; }
    const crosspad::FwKitDetail* detail() override { return detailReady_ ? &detailData : nullptr; }
    uint32_t detailGen() override { return detailGen_; }
    void setPinned(const char* pkg, bool pinned) override;
    void remove(const char* pkg, const char* slotA, const char* slotB) override;
    void ackCommunity(const char* kit) override;
    bool communityOk(const char* kit) override;
    bool requestIcon(uint32_t ticket, const char*, const char*, const char*) override;
    void cancelIcon(uint32_t ticket) override;
    void freeIcon(uint8_t* buf) override;
    uint32_t libraryLimitMb() override { return 512; }

    void readSlots(crosspad::FwSlotInfo out[2]) override { out[0] = slots[0]; out[1] = slots[1]; }
    bool runningOnTrial() override { return false; }
    bool rollbackSupported() override { return true; }
    bool installWriting() override { return false; }
    bool flowActive() override { return false; }
    bool flowInstallAndRestart(const char*, bool) override { return true; }
    bool flowSwitchAndRestart(bool) override { return true; }
    const char* runningVersion() override { return "v1.2.0"; }

    crosspad_gui::FwUpdaterState updaterState() override { return state; }
    bool updaterBusy() override { return busy; }
    uint8_t updaterProgress() override { return progress; }
    void updaterError(char* out, size_t n) override { copy(error, out, n); }
    void updaterTarget(char* out, size_t n) override { copy(target, out, n); }
    void updaterTargetKit(char* out, size_t n) override { copy(targetKit, out, n); }
    void updaterReadyVersion(char* out, size_t n) override { copy(readyVersion, out, n); }
    bool updaterChecked() override { return checked; }
    int64_t updaterLastCheck() override { return lastCheck; }
    bool channelBeta(const char*) override { return false; }
    bool checkStart(char* refusal, size_t n) override;
    bool downloadStart(const char* kit, const char* version, bool install, char* refusal, size_t n) override;

    bool wifiAcquire(const char*) override;
    void wifiRelease() override { --leases; }
    crosspad::WifiLinkState wifiState() override { return crosspad::WifiLinkState::Up; }
    crosspad::WifiFail wifiFail() override { return wifiFailReason; }
    bool networkMode() override { return netMode; }
    void offerNetworkMode(crosspad_gui::FwNetTab tab, lv_group_t*) override { offers.push_back(tab); }
    int64_t nowUs() override { return nowUs_ += 1000; }

    void uiHost(const char* host) override { uiHostV = text(host); }
    void uiTab(const char* tab) override { uiTabV = text(tab); }
    bool uiTabIs(const char* tab) override { return uiTabV == tab; }
    void uiItems(uint32_t n) override { uiItemsV = n; }
    void uiSel(int32_t index, const char* kit, const char* version) override;
    void uiCard(const char* kit, const char* version) override;
    void uiStore(const char* state) override { uiStoreV = text(state); }
    void uiReset() override;

private:
    static void copy(const std::string& s, char* out, size_t n);
    static std::string text(const char* s) { return s && s[0] ? s : "-"; }

    bool open_ = false;
    bool scanWanted_ = false, libReady_ = false;
    uint32_t libGen_ = 0;
    bool storeWanted_ = false, storeLoaded_ = false;
    uint32_t storeGen_ = 0;
    bool detailWanted_ = false, detailReady_ = false;
    uint32_t detailGen_ = 0;
    std::vector<uint32_t> iconWanted_;
    std::string communityOk_;
    int64_t nowUs_ = 0;
};

} // namespace guitest
