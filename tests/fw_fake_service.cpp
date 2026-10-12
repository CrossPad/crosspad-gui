// SPDX-License-Identifier: GPL-3.0-or-later
#include "fw_fake_service.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>

#include "crosspad-gui/fw/fw_icons.h"

namespace guitest {

namespace {
constexpr uint8_t kLvImageMagic = 0x19;
constexpr uint8_t kLvCfRgb565A8 = 0x14;
} // namespace

FakeFwService::FakeFwService() : lib(new crosspad_gui::FwLibrarySnap{}) {
    slots[0].label = 'A';
    slots[1].label = 'B';
    slots[0].running = true;
}

crosspad_gui::FwLibItem& FakeFwService::addPackage(const char* kit, const char* version, const char* displayName) {
    crosspad_gui::FwLibItem& it = lib->items[lib->count++];
    it = crosspad_gui::FwLibItem{};
    crosspad::fwPackageName(kit, version, it.name, sizeof it.name);
    std::snprintf(it.kit, sizeof it.kit, "%s", kit);
    std::snprintf(it.version, sizeof it.version, "%s", version);
    std::snprintf(it.displayName, sizeof it.displayName, "%s", displayName);
    it.verdict = crosspad::FwPkgVerdict::Ok;
    it.complete = true;
    it.onCard = true;
    it.icon = crosspad_gui::FwIconSrc::Package;
    it.bytes = 2u * 1024u * 1024u;
    lib->bytes += it.bytes;
    return it;
}

crosspad::FwStoreKit& FakeFwService::addStoreKit(const char* kit, const char* displayName, crosspad::FwTier tier) {
    crosspad::FwStoreKit& k = storeData.kits[storeData.kitCount++];
    k = crosspad::FwStoreKit{};
    std::snprintf(k.kit, sizeof k.kit, "%s", kit);
    std::snprintf(k.displayName, sizeof k.displayName, "%s", displayName);
    k.tier = tier;
    return k;
}

void FakeFwService::addStoreVersion(crosspad::FwStoreKit& k, const char* version, crosspad::FwChannel channel,
                                    crosspad::FwTier tier, bool hasIcon) {
    crosspad::FwStoreVersion& v = k.versions[k.count++];
    std::snprintf(v.version, sizeof v.version, "%s", version);
    v.channel = channel;
    v.tier = tier;
    v.hasIcon = hasIcon;
    v.bytes = 3u * 1024u * 1024u;
}

void FakeFwService::setSlot(int i, crosspad::FwSlotState st, bool running, const char* pkg, const char* kit,
                            const char* version) {
    crosspad::FwSlotInfo& s = slots[i];
    s.state = st;
    s.running = running;
    std::snprintf(s.package, sizeof s.package, "%s", pkg);
    std::snprintf(s.kit, sizeof s.kit, "%s", kit);
    std::snprintf(s.version, sizeof s.version, "%s", version);
    s.hasDesc = true;
}

void FakeFwService::dataClose() {
    ++closes;
    open_ = false;
    libReady_ = storeLoaded_ = detailReady_ = false;
    scanWanted_ = storeWanted_ = detailWanted_ = false;
    for (uint32_t t : iconWanted_) fw_icons_deliver(t, nullptr);   // a closed view's icon: nobody is waiting
    iconWanted_.clear();
}

void FakeFwService::dataPoll() {
    if (scanWanted_) {
        scanWanted_ = false;
        if (!scanFails) {
            libReady_ = true;
            ++libGen_;
        }
    }
    if (storeWanted_) {
        storeWanted_ = false;
        storeLoaded_ = true;
        ++storeGen_;
    }
    if (detailWanted_) {
        detailWanted_ = false;
        detailReady_ = true;
        ++detailGen_;
    }
    const std::vector<uint32_t> tickets = std::move(iconWanted_);
    iconWanted_.clear();
    for (uint32_t t : tickets) {
        uint8_t* buf = nullptr;
        if (iconsValid) {
            buf = static_cast<uint8_t*>(std::calloc(1, crosspad::kFwIconBytes));
            buf[0] = kLvImageMagic;
            buf[1] = kLvCfRgb565A8;
            buf[4] = static_cast<uint8_t>(crosspad::kFwIconSide);
            buf[6] = static_cast<uint8_t>(crosspad::kFwIconSide);
            buf[8] = static_cast<uint8_t>(crosspad::kFwIconSide * 2);
            buf[crosspad::kFwIconHeaderBytes] = static_cast<uint8_t>(t);   // a distinct buffer per ticket
            ++iconsHeld;
        }
        fw_icons_deliver(t, buf);
    }
}

void FakeFwService::setPinned(const char* pkg, bool pinned) {
    pins.push_back(std::string(pkg) + (pinned ? "=1" : "=0"));
    for (size_t i = 0; i < lib->count; ++i)
        if (std::strcmp(lib->items[i].name, pkg) == 0) lib->items[i].pinned = pinned;
    scanWanted_ = true;
}

void FakeFwService::remove(const char* pkg, const char* slotA, const char* slotB) {
    if (std::strcmp(pkg, slotA) == 0 || std::strcmp(pkg, slotB) == 0) return;
    for (size_t i = 0; i < lib->count; ++i) {
        if (std::strcmp(lib->items[i].name, pkg) != 0) continue;
        std::copy(lib->items + i + 1, lib->items + lib->count, lib->items + i);
        --lib->count;
        break;
    }
    scanWanted_ = true;
}

void FakeFwService::ackCommunity(const char* kit) { communityOk_ += std::string(" ") + kit + " "; }

bool FakeFwService::communityOk(const char* kit) {
    return communityOk_.find(std::string(" ") + kit + " ") != std::string::npos;
}

bool FakeFwService::requestIcon(uint32_t ticket, const char*, const char*, const char*) {
    if (!open_) return false;
    iconWanted_.push_back(ticket);
    return true;
}

void FakeFwService::cancelIcon(uint32_t ticket) {
    iconWanted_.erase(std::remove(iconWanted_.begin(), iconWanted_.end(), ticket), iconWanted_.end());
}

void FakeFwService::freeIcon(uint8_t* buf) {
    if (buf) --iconsHeld;
    std::free(buf);
}

bool FakeFwService::checkStart(char* refusal, size_t n) {
    if (busy) {
        std::snprintf(refusal, n, "busy");
        return false;
    }
    ++checks;
    return true;
}

bool FakeFwService::downloadStart(const char* kit, const char* version, bool install, char* refusal, size_t n) {
    if (busy) {
        std::snprintf(refusal, n, "busy");
        return false;
    }
    downloads.push_back(std::string(kit) + "@" + version + (install ? "+install" : ""));
    return true;
}

bool FakeFwService::wifiAcquire(const char*) {
    if (!wifiAllowed) return false;
    ++leases;
    return true;
}

void FakeFwService::uiSel(int32_t index, const char* kit, const char* version) {
    uiSelV = index < 0 ? -1 : index;
    uiSelKey = index < 0 || !kit || !kit[0] ? "-" : std::string(kit) + "@" + text(version);
}

void FakeFwService::uiCard(const char* kit, const char* version) {
    uiCardV = !kit || !kit[0] ? "-" : std::string(kit) + "@" + text(version);
}

void FakeFwService::uiReset() {
    uiHostV = uiTabV = uiCardV = uiStoreV = uiSelKey = "-";
    uiItemsV = 0;
    uiSelV = -1;
}

void FakeFwService::copy(const std::string& s, char* out, size_t n) { std::snprintf(out, n, "%s", s.c_str()); }

} // namespace guitest
