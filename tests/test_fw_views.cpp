// SPDX-License-Identifier: GPL-3.0-or-later
#include <catch2/catch_test_macros.hpp>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "crosspad-gui/app/App.hpp"
#include "crosspad-gui/fw/fw_actions.h"
#include "crosspad-gui/fw/fw_app.h"
#include "crosspad-gui/fw/fw_kit_card.h"
#include "crosspad-gui/fw/fw_library_view.h"
#include "crosspad-gui/fw/fw_store_view.h"
#include "crosspad-gui/fw/fw_updates_view.h"
#include "crosspad-gui/fw/fw_views_common.h"
#include "crosspad-gui/platform/IGuiPlatform.h"
#include "crosspad-gui/styles/styles.h"
#include "crosspad-gui/theme/crosspad_theme.h"
#include "fw_fake_service.h"
#include "gui_test_platform.h"
#include "lvgl_headless.h"

using crosspad::FwChannel;
using crosspad::FwSlotState;
using crosspad::FwTier;

namespace {

constexpr int32_t kPageW = 312;
constexpr int32_t kPageH = 206;   // the app's page under its tab bar
constexpr uint32_t kTickMs = 250;

/// A Firmware view on a page of the size the app gives it, the fake service registered.
struct FwFixture : guitest::GuiFixture {
    guitest::FakeFwService svc;
    lv_obj_t* page = nullptr;

    FwFixture() {
        initStyles();
        crosspad_gui::setFwViewService(&svc);
        page = lv_obj_create(screen);
        lv_obj_remove_style_all(page);
        lv_obj_set_size(page, kPageW, kPageH);
        lv_obj_set_flex_flow(page, LV_FLEX_FLOW_COLUMN);
    }
    ~FwFixture() {
        fw_actions_forget();
        fw_kit_card_close();
        lv_timer_handler();
        crosspad_gui::setOverlayParent(nullptr);
        crosspad_gui::setFwViewService(nullptr);
    }
    /// One refresh of `refresh` on a new LVGL tick (the views poll once a tick).
    void tick(void (*refresh)()) {
        advance(kTickMs);
        refresh();
    }
    void snapshot(const char* name) {
        const char* dir = std::getenv("FW_VIEWS_SNAPSHOT_DIR");
        if (!dir) return;
        char path[512];
        std::snprintf(path, sizeof path, "%s/%s.png", dir, name);
        CHECK(lvgl_headless_write_png(path));
    }
};

lv_obj_t* find_label(lv_obj_t* root, const char* text, bool prefix = false) {
    if (lv_obj_check_type(root, &lv_label_class)) {
        const char* t = lv_label_get_text(root);
        if (prefix ? std::strncmp(t, text, std::strlen(text)) == 0 : std::strcmp(t, text) == 0) return root;
    }
    for (uint32_t i = 0; i < lv_obj_get_child_count(root); ++i)
        if (lv_obj_t* hit = find_label(lv_obj_get_child(root, static_cast<int32_t>(i)), text, prefix)) return hit;
    return nullptr;
}

/// Visible on screen: neither it nor an ancestor is hidden.
bool shown(lv_obj_t* obj) {
    if (!obj) return false;
    for (; obj; obj = lv_obj_get_parent(obj))
        if (lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN)) return false;
    return true;
}

/// The kit card's button labelled `text` (the label's parent), or nullptr.
lv_obj_t* card_button(const char* text) {
    lv_obj_t* l = find_label(crosspad_gui::getOverlayParent(), text);
    return l ? lv_obj_get_parent(l) : nullptr;
}

lv_obj_t* tile_grid(lv_obj_t* page) {
    lv_obj_t* root = lv_obj_get_child(page, 0);
    return root ? lv_obj_get_child(root, 1) : nullptr;   // status, grid, (footer)
}

} // namespace

TEST_CASE("fw library: packages and slot-only images are tiles, the running image first", "[fw_views]") {
    FwFixture f;
    f.svc.addPackage("alpha", "1.0.0", "Alpha");
    f.svc.addPackage("beta", "2.0.0", "Beta");
    f.svc.addPackage("sampler", "1.1.0", "Sampler");
    f.svc.setSlot(0, FwSlotState::Confirmed, true, "sampler-1.1.0", "sampler", "v1.1.0");
    f.svc.setSlot(1, FwSlotState::Installed, false, "", "daw", "v0.9.0");   // flashed over USB: no package

    fw_library_view_build(f.page, f.group);
    CHECK(f.svc.uiTabV == "library");
    CHECK(find_label(f.page, "Scanning the card...") != nullptr);
    f.tick(fw_library_view_refresh);

    CHECK(f.svc.uiItemsV == 4);
    CHECK(f.svc.uiSelV == 0);
    CHECK(f.svc.uiSelKey == "sampler@1.1.0");   // the running image leads
    CHECK(find_label(f.page, "daw") != nullptr);  // then the other slot's image, by its kit
    CHECK(find_label(f.page, "1.1.0 " LV_SYMBOL_PLAY "A") != nullptr);
    CHECK(find_label(f.page, "SD: 6/512 MB", true) != nullptr);
    f.snapshot("fw_library");

    fw_library_view_close();
    CHECK(f.svc.uiItemsV == 0);
}

TEST_CASE("fw library: an empty card says so, a card that cannot be read is reported", "[fw_views]") {
    {
        FwFixture f;
        fw_library_view_build(f.page, f.group);
        f.tick(fw_library_view_refresh);
        CHECK(f.svc.uiItemsV == 0);
        lv_obj_t* empty = find_label(f.page, "No firmware on the card yet");
        REQUIRE(empty != nullptr);
        CHECK(shown(empty));
        fw_library_view_close();
    }
    {
        FwFixture f;
        f.svc.scanFails = true;
        fw_library_view_build(f.page, f.group);
        for (int i = 0; i < 40; ++i) f.tick(fw_library_view_refresh);   // 10 s: every retry has run
        CHECK(find_label(f.page, "Could not read the SD card") != nullptr);
        fw_library_view_close();
    }
}

TEST_CASE("fw store: Official then Community, each tile at its channel's version", "[fw_views]") {
    FwFixture f;
    f.svc.storeData.fetched = 1791763200;   // 2026-10-12
    auto& sampler = f.svc.addStoreKit("sampler", "Sampler", FwTier::Official);
    f.svc.addStoreVersion(sampler, "1.0.0", FwChannel::Stable, FwTier::Official);
    f.svc.addStoreVersion(sampler, "1.1.0", FwChannel::Beta, FwTier::Official);
    auto& drums = f.svc.addStoreKit("drums", "Drums", FwTier::Official);
    f.svc.addStoreVersion(drums, "2.0.0", FwChannel::Stable, FwTier::Official);
    auto& lofi = f.svc.addStoreKit("lofi", "Lo-Fi", FwTier::Community);
    f.svc.addStoreVersion(lofi, "0.3.0", FwChannel::Stable, FwTier::Community);
    f.svc.addPackage("sampler", "1.0.0", "Sampler");

    fw_store_view_build(f.page, f.group);
    fw_store_view_shown();
    CHECK(f.svc.uiTabV == "store");
    CHECK(f.svc.leases == 1);   // a shown Store holds the views' lease
    CHECK(f.svc.checks == 1);   // and checks the catalog
    f.tick(fw_store_view_refresh);
    f.tick(fw_store_view_refresh);

    CHECK(f.svc.uiItemsV == 3);
    CHECK(f.svc.uiStoreV == "ok");
    CHECK(f.svc.uiSelKey == "drums@2.0.0");   // official kits by name: Drums, Sampler
    CHECK(find_label(f.page, "Official") != nullptr);
    CHECK(find_label(f.page, "Community") != nullptr);
    CHECK(find_label(f.page, "Catalog of 2026-10-12") != nullptr);
    CHECK(find_label(f.page, "1.0.0 " LV_SYMBOL_OK) != nullptr);        // stable, and on the card
    CHECK(find_label(f.page, "0.3.0 " LV_SYMBOL_WARNING) != nullptr);   // a community build
    f.snapshot("fw_store");

    fw_store_view_close();
    CHECK(f.svc.leases == 0);   // the lease goes with the tab
}

TEST_CASE("fw store: offline it keeps the cached catalog and says why", "[fw_views]") {
    FwFixture f;
    f.svc.storeData.fetched = 1791763200;
    auto& k = f.svc.addStoreKit("sampler", "Sampler", FwTier::Official);
    f.svc.addStoreVersion(k, "1.0.0", FwChannel::Stable, FwTier::Official);
    f.svc.wifiAllowed = false;
    f.svc.wifiFailReason = crosspad::WifiFail::Disabled;

    fw_store_view_build(f.page, f.group);
    fw_store_view_shown();
    f.tick(fw_store_view_refresh);
    f.tick(fw_store_view_refresh);
    CHECK(f.svc.checks == 0);
    CHECK(f.svc.uiItemsV == 1);
    CHECK(find_label(f.page, "Catalog of 2026-10-12\nWiFi is off - see Settings > WiFi") != nullptr);
    CHECK(f.svc.offers.empty());   // only no_mem offers network mode
    fw_store_view_close();

    f.svc.wifiFailReason = crosspad::WifiFail::NoMem;
    fw_store_view_build(f.page, f.group);
    fw_store_view_shown();
    REQUIRE(f.svc.offers.size() == 1);
    CHECK(f.svc.offers[0] == crosspad_gui::kFwNetTabStore);
    fw_store_view_close();
}

TEST_CASE("fw store: a catalog never fetched, or of another board", "[fw_views]") {
    FwFixture f;
    f.svc.storeLoad = crosspad::FwStoreLoad::Missing;
    fw_store_view_build(f.page, f.group);
    fw_store_view_shown();
    f.tick(fw_store_view_refresh);
    f.tick(fw_store_view_refresh);
    CHECK(f.svc.uiItemsV == 0);
    CHECK(f.svc.uiStoreV == "missing");
    CHECK(find_label(f.page, "Never fetched") != nullptr);
    CHECK(find_label(f.page, "No firmware in the store") != nullptr);
    fw_store_view_close();

    f.svc.storeLoad = crosspad::FwStoreLoad::OtherBoard;
    fw_store_view_build(f.page, f.group);
    fw_store_view_shown();
    f.tick(fw_store_view_refresh);
    f.tick(fw_store_view_refresh);
    CHECK(find_label(f.page, "Store unavailable - catalog of another board") != nullptr);
    fw_store_view_close();
}

TEST_CASE("fw updates: a row per kit with a newer version, Update installs it", "[fw_views]") {
    FwFixture f;
    auto& k = f.svc.addStoreKit("sampler", "Sampler", FwTier::Official);
    f.svc.addStoreVersion(k, "1.0.0", FwChannel::Stable, FwTier::Official);
    f.svc.addStoreVersion(k, "1.1.0", FwChannel::Stable, FwTier::Official);
    f.svc.addPackage("sampler", "1.0.0", "Sampler");

    fw_updates_view_build(f.page, f.group);
    fw_updates_view_shown();
    f.tick(fw_updates_view_refresh);
    f.tick(fw_updates_view_refresh);
    CHECK(f.svc.uiTabV == "updates");
    CHECK(f.svc.uiItemsV == 1);
    CHECK(find_label(f.page, "Running v1.2.0, checked 2026-10-12") != nullptr);
    CHECK(find_label(f.page, "Channel, check at startup") != nullptr);   // the Settings shortcut, not in network mode
    lv_obj_t* row = find_label(f.page, "Sampler 1.0.0 -> 1.1.0");
    REQUIRE(row != nullptr);
    lv_obj_t* update = find_label(lv_obj_get_parent(row), "Update");
    REQUIRE(update != nullptr);
    f.snapshot("fw_updates");

    lv_obj_send_event(lv_obj_get_parent(update), LV_EVENT_CLICKED, nullptr);
    f.advance(1);   // the question chain runs a cycle later; an empty other slot asks nothing
    REQUIRE(f.svc.downloads.size() == 1);
    CHECK(f.svc.downloads[0] == "sampler@1.1.0+install");
    fw_updates_view_close();
}

TEST_CASE("fw updates: up to date, no catalog, and network mode", "[fw_views]") {
    FwFixture f;
    auto& k = f.svc.addStoreKit("sampler", "Sampler", FwTier::Official);
    f.svc.addStoreVersion(k, "1.0.0", FwChannel::Stable, FwTier::Official);
    f.svc.addPackage("sampler", "1.0.0", "Sampler");
    fw_updates_view_build(f.page, f.group);
    f.tick(fw_updates_view_refresh);
    f.tick(fw_updates_view_refresh);
    CHECK(find_label(f.page, "Everything on the card is up to date.") != nullptr);
    fw_updates_view_close();

    f.svc.storeLoad = crosspad::FwStoreLoad::Missing;
    f.svc.netMode = true;
    fw_updates_view_build(f.page, f.group);
    f.tick(fw_updates_view_refresh);
    f.tick(fw_updates_view_refresh);
    CHECK(find_label(f.page, "No catalog on the card yet.") != nullptr);
    CHECK(find_label(f.page, "Channel, check at startup") == nullptr);
    fw_updates_view_close();
}

TEST_CASE("fw kit card: the buttons are fwCardPlan's", "[fw_views]") {
    FwFixture f;
    f.svc.detailData.appCount = 0;
    std::snprintf(f.svc.detailData.description, sizeof f.svc.detailData.description, "Drums and samples.");
    auto& k = f.svc.addStoreKit("sampler", "Sampler", FwTier::Official);
    f.svc.addStoreVersion(k, "1.0.0", FwChannel::Stable, FwTier::Official);
    f.svc.addStoreVersion(k, "1.1.0", FwChannel::Stable, FwTier::Official);
    fw_library_view_build(f.page, f.group);   // a host keeps the views' data open
    f.tick(fw_library_view_refresh);

    SECTION("a catalog version not on the card: Download and Install") {
        FwTarget t{};
        std::snprintf(t.kit, sizeof t.kit, "sampler");
        std::snprintf(t.version, sizeof t.version, "1.1.0");
        crosspad_gui::setOverlayParent(f.screen);   // on the screen, so a snapshot shows it
        fw_kit_card_open(t, crosspad_gui::kFwNetTabStore);
        f.tick(fw_library_view_refresh);
        f.tick(fw_library_view_refresh);
        REQUIRE(fw_kit_card_is_open());
        CHECK(f.svc.uiCardV == "sampler@1.1.0");
        CHECK(shown(card_button("Download")));
        CHECK(shown(card_button("Install")));
        CHECK_FALSE(shown(card_button("Delete from card")));
        CHECK_FALSE(shown(card_button("Install and run")));
        CHECK(find_label(f.screen, "Drums and samples.") != nullptr);
        CHECK(find_label(f.screen, "Older versions (1)") != nullptr);
        f.snapshot("fw_kit_card");
        fw_kit_card_close();
        CHECK(f.svc.uiCardV == "-");
    }
    SECTION("an image the other slot rolled back: try the slot again, or reinstall") {
        f.svc.addPackage("sampler", "1.0.0", "Sampler");
        f.svc.setSlot(1, FwSlotState::RolledBack, false, "sampler-1.0.0", "sampler", "v1.0.0");
        fw_views_slots_invalidate();   // the slots are read again after a flow or an install, as here
        f.svc.scanLibrary();
        f.tick(fw_library_view_refresh);
        FwTarget t{};
        std::snprintf(t.kit, sizeof t.kit, "sampler");
        std::snprintf(t.version, sizeof t.version, "1.0.0");
        std::snprintf(t.pkg, sizeof t.pkg, "sampler-1.0.0");
        fw_kit_card_open(t, crosspad_gui::kFwNetTabStore);
        f.tick(fw_library_view_refresh);
        REQUIRE(fw_kit_card_is_open());
        CHECK(shown(card_button("Try this slot again")));
        CHECK(shown(card_button("Reinstall")));
        CHECK(shown(card_button("Pin")));
        lv_obj_t* del = card_button("Delete from card");
        CHECK(shown(del));
        CHECK(lv_obj_has_state(del, LV_STATE_DISABLED));   // still in a slot: never deleted
        CHECK(find_label(crosspad_gui::getOverlayParent(), "In slot B - it did not start last time") != nullptr);
    }
    SECTION("a pinned package is not offered Delete; Unpin writes the card") {
        f.svc.addPackage("sampler", "1.0.0", "Sampler").pinned = true;
        f.svc.scanLibrary();
        f.tick(fw_library_view_refresh);
        FwTarget t{};
        std::snprintf(t.kit, sizeof t.kit, "sampler");
        std::snprintf(t.version, sizeof t.version, "1.0.0");
        std::snprintf(t.pkg, sizeof t.pkg, "sampler-1.0.0");
        fw_kit_card_open(t, crosspad_gui::kFwNetTabStore);
        f.tick(fw_library_view_refresh);
        REQUIRE(fw_kit_card_is_open());
        CHECK_FALSE(shown(card_button("Delete from card")));
        lv_obj_t* unpin = card_button("Unpin");
        REQUIRE(shown(unpin));
        lv_obj_send_event(unpin, LV_EVENT_CLICKED, nullptr);
        REQUIRE(f.svc.pins.size() == 1);
        CHECK(f.svc.pins[0] == "sampler-1.0.0=0");
        f.tick(fw_library_view_refresh);
        CHECK(shown(card_button("Delete from card")));
    }
    fw_library_view_close();
}

TEST_CASE("fw library: icons only for the tiles in view, given back when the tab closes", "[fw_views]") {
    FwFixture f;
    f.svc.iconsValid = true;
    char kit[8];
    for (int i = 0; i < 12; ++i) {
        std::snprintf(kit, sizeof kit, "kit%02d", i);
        f.svc.addPackage(kit, "1.0.0", kit);
    }
    fw_library_view_build(f.page, f.group);
    f.tick(fw_library_view_refresh);   // the scan lands, the visible tiles ask
    f.tick(fw_library_view_refresh);   // the icons land
    CHECK(f.svc.uiItemsV == 12);
    CHECK(f.svc.iconsHeld == 6);   // two rows of three
    fw_library_view_close();
    CHECK(f.svc.iconsHeld == 0);
}

TEST_CASE("fw app: a tab button's click builds that page and closes the one left", "[fw_views]") {
    FwFixture f;
    static lv_group_t* s_home;
    static int s_tails;
    s_home = f.group;
    s_tails = 0;
    static const crosspad_gui::FwAppHooks hooks = {
        [](lv_obj_t* page, lv_group_t* group, lv_obj_t** tail, size_t cap) -> size_t {
            ++s_tails;
            lv_obj_t* close = lv_button_create(page);
            lv_group_add_obj(group, close);
            if (cap) tail[0] = close;
            return cap ? 1 : 0;
        },
        nullptr, nullptr, [] { return s_home; }};
    App app(f.screen, "Firmware", nullptr,
            [](lv_obj_t* parent, App* a) { return crosspad_gui::fw_app_create(parent, a, hooks); },
            crosspad_gui::fw_app_destroy);
    app.start(f.screen);
    CHECK(f.svc.uiHostV == "app");
    CHECK(f.svc.uiTabV == "library");
    CHECK(crosspad_gui::fw_app_container() == f.screen);

    lv_obj_t* tabs = lv_obj_get_child(app.getAppContainer(), 0);
    lv_obj_t* bar = lv_tabview_get_tab_bar(tabs);
    lv_obj_t* content = lv_tabview_get_content(tabs);
    lv_obj_send_event(lv_obj_get_child(bar, 1), LV_EVENT_CLICKED, nullptr);
    f.advance(kTickMs);
    CHECK(f.svc.uiTabV == "store");
    CHECK(lv_obj_get_child_count(lv_obj_get_child(content, 0)) == 0);   // the Library page went
    CHECK(f.svc.leases == 1);

    lv_obj_send_event(lv_obj_get_child(bar, 2), LV_EVENT_CLICKED, nullptr);
    f.advance(kTickMs);
    CHECK(f.svc.uiTabV == "updates");
    CHECK(s_tails == 1);   // the platform's block ends the Updates tab
    CHECK(f.svc.leases == 1);   // the Store let go, the Updates tab holds it

    app.destroyApp();
    f.advance(kTickMs);
    CHECK(f.svc.leases == 0);
    CHECK(f.svc.uiTabV == "-");
    CHECK(crosspad_gui::fw_app_container() == nullptr);
    CHECK(lv_indev_get_group(f.gui.encoder) == f.group);
}
