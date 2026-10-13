// SPDX-License-Identifier: GPL-3.0-or-later
#ifdef USE_LVGL
#include "crosspad-gui/fw/fw_icons.h"

#include <cstring>

#include "crosspad-gui/fw/IFwViewService.h"
#include "crosspad/fw/FwCatalog.hpp"
#include "src/misc/cache/instance/lv_image_cache.h"   // lv_image_cache_drop(): lvgl.h stopped exporting it in 9.4
#include "crosspad-gui/platform/psram_placement.h"
#include "fw_text.h"

/* The budget's bound: 6 tiles in view, a card's header and one slack. */
static constexpr size_t kIconsMax = 8;
/* Requests in flight: the built tiles (4 rows of 3) and a card's header, with room to spare. */
static constexpr size_t kPendingMax = 16;
static constexpr uint32_t kFnvOffset = 2166136261u;
static constexpr uint32_t kFnvPrime = 16777619u;

static_assert(sizeof(lv_image_header_t) == crosspad::kFwIconHeaderBytes, "an icon file starts with LVGL's own header");

namespace {

/* The files an icon was read for. */
struct Key {
    char pkg[crosspad::kFwPackageNameMax];
    char kit[crosspad::kFwKitMax];
    char version[crosspad::kFwPkgVersionMax];
};

struct Entry {
    lv_image_dsc_t dsc;
    uint8_t* buf;
    uint32_t hash;
    uint16_t refs;   // 0 with a buffer: unheld, freed by the next trim
    Key key;
};

struct Pending {
    uint32_t ticket;
    FwIconDone done;
    void* user;
    Key key;
};

}  // namespace

/* PSRAM: internal RAM is what WiFi and the audio tasks need. */
CROSSPAD_GUI_PSRAM_BSS static Entry s_icons[kIconsMax];
CROSSPAD_GUI_PSRAM_BSS static Pending s_pending[kPendingMax];
static uint32_t s_nextTicket = 1;

static uint32_t fnv1a(const uint8_t* p, size_t n) {
    uint32_t h = kFnvOffset;
    for (size_t i = 0; i < n; ++i) {
        h ^= p[i];
        h *= kFnvPrime;
    }
    return h;
}

static void key_set(Key& k, const char* pkg, const char* kit, const char* version) {
    crosspad_gui::fw_text::copy(k.pkg, pkg ? pkg : "", sizeof k.pkg);
    crosspad_gui::fw_text::copy(k.kit, kit ? kit : "", sizeof k.kit);
    crosspad_gui::fw_text::copy(k.version, version ? version : "", sizeof k.version);
}

static bool key_eq(const Key& a, const Key& b) {
    return strcmp(a.pkg, b.pkg) == 0 && strcmp(a.kit, b.kit) == 0 && strcmp(a.version, b.version) == 0;
}

static void entry_free(Entry& e) {
    lv_image_cache_drop(&e.dsc);   // never cached (a variable source drawn in place): belt and braces
    crosspad_gui::fwViewService().freeIcon(e.buf);
    e = Entry{};
}

static Pending* find_pending(uint32_t ticket) {
    if (!ticket) return nullptr;
    for (Pending& p : s_pending)
        if (p.ticket == ticket) return &p;
    return nullptr;
}

const lv_image_dsc_t* fw_icons_acquire(const char* pkg, const char* kit, const char* version) {
    Key k;
    key_set(k, pkg, kit, version);
    for (Entry& e : s_icons) {
        if (!e.buf || !key_eq(e.key, k)) continue;
        ++e.refs;
        return &e.dsc;
    }
    return nullptr;
}

uint32_t fw_icons_request(const char* pkg, const char* kit, const char* version, FwIconDone done, void* user) {
    Pending* slot = nullptr;
    for (Pending& p : s_pending) {
        if (!p.ticket) {
            slot = &p;
            break;
        }
    }
    if (!slot || !done) return 0;
    uint32_t ticket = s_nextTicket++;
    if (!ticket) ticket = s_nextTicket++;   // 0 means "none"
    if (!crosspad_gui::fwViewService().requestIcon(ticket, pkg, kit, version)) return 0;
    slot->ticket = ticket;   // answers arrive only through the poll, after this returns
    slot->done = done;
    slot->user = user;
    key_set(slot->key, pkg, kit, version);
    return ticket;
}

void fw_icons_cancel(uint32_t ticket) {
    Pending* p = find_pending(ticket);
    if (!p) return;
    crosspad_gui::fwViewService().cancelIcon(ticket);   // a dead request costs neither a card read nor a queue slot
    p->ticket = 0;
}

void fw_icons_release(const lv_image_dsc_t* icon) {
    for (Entry& e : s_icons) {
        if (e.buf && &e.dsc == icon) {
            if (e.refs) --e.refs;
            return;
        }
    }
}

void fw_icons_trim(void) {
    for (Entry& e : s_icons)
        if (e.buf && e.refs == 0) entry_free(e);
}

void fw_icons_drop_all(void) {
    memset(s_pending, 0, sizeof s_pending);
    fw_icons_trim();
}

void fw_icons_deliver(uint32_t ticket, uint8_t* buf) {
    Pending* p = find_pending(ticket);
    if (!p) {
        crosspad_gui::fwViewService().freeIcon(buf);   // cancelled: the tile is gone or shows something else
        return;
    }
    const FwIconDone done = p->done;
    void* user = p->user;
    const Key key = p->key;
    p->ticket = 0;
    if (!buf) {
        done(nullptr, user);
        return;
    }
    const uint32_t hash = fnv1a(buf, crosspad::kFwIconBytes);
    for (Entry& e : s_icons) {
        if (e.buf && e.hash == hash && memcmp(e.buf, buf, crosspad::kFwIconBytes) == 0) {
            crosspad_gui::fwViewService().freeIcon(buf);
            ++e.refs;
            done(&e.dsc, user);
            return;
        }
    }
    Entry* slot = nullptr;
    for (Entry& e : s_icons) {
        if (!e.buf) {
            slot = &e;
            break;
        }
    }
    for (Entry& e : s_icons) {
        if (slot) break;
        if (e.refs == 0) {   // unheld: its place goes to an icon someone shows
            entry_free(e);
            slot = &e;
        }
    }
    if (!slot) {
        crosspad_gui::fwViewService().freeIcon(buf);   // every buffer held: this tile keeps its letter
        done(nullptr, user);
        return;
    }
    slot->buf = buf;
    slot->hash = hash;
    slot->refs = 1;
    slot->key = key;
    slot->dsc = lv_image_dsc_t{};
    memcpy(&slot->dsc.header, buf, sizeof slot->dsc.header);
    slot->dsc.data = buf + crosspad::kFwIconHeaderBytes;
    slot->dsc.data_size = crosspad::kFwIconBytes - crosspad::kFwIconHeaderBytes;
    done(&slot->dsc, user);
}

#endif // USE_LVGL
