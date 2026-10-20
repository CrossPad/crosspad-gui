// SPDX-License-Identifier: GPL-3.0-or-later
#include <catch2/catch_test_macros.hpp>

#include "crosspad-gui/components/file_explorer.h"
#include "crosspad-gui/styles/styles.h"
#include "crosspad/platform/PlatformServices.hpp"
#include "crosspad/platform/PosixFileSystem.hpp"
#include "gui_test_platform.h"

#include <string>
#include <vector>

namespace {

/// A card with one kits folder: a kit folder, a hidden one and a loose sample.
class CardFs final : public crosspad::PosixFileSystem {
public:
    std::vector<std::string> listed;

    bool list(const char* dir, EntryFn each, void* ctx) override {
        listed.emplace_back(dir);
        if (std::string(dir) != "/card/crosspad/kits") return true;
        each(ctx, "A", true);
        each(ctx, ".fx", true);
        each(ctx, "kick.wav", false);
        return true;
    }
};

void on_load(lv_event_t*) {}

} // namespace

TEST_CASE("file explorer: lists the platform's storage path, rows keep the card path", "[file_explorer]") {
    guitest::GuiFixture f;
    initStyles();
    CardFs card;
    crosspad::getPlatformServices().setFileSystem(&card);
    f.gui.storageRoot = "/card";

    lv_obj_t* explorer = lv_CreateFileExplorer(f.screen, FILTER_AUDIO, "/crosspad/kits", on_load);
    REQUIRE(explorer);
    f.advance(1000);

    REQUIRE_FALSE(card.listed.empty());
    CHECK(card.listed.front() == "/card/crosspad/kits");
    CHECK(GetCurrentFolder().path == "/crosspad/kits");

    /* A folder highlighted on the left is listed on the right: by the row's
     * path, which is the card path the explorer was opened on plus the name. */
    lv_group_t* group = lv_indev_get_group(f.gui.encoder);
    for (int i = 0; i < 4 && card.listed.size() < 2; ++i) {
        lv_group_focus_next(group);
        f.advance(300);
    }
    REQUIRE(card.listed.size() == 2);
    CHECK(card.listed[1] == "/card/crosspad/kits/A");
    for (const std::string& dir : card.listed) CHECK(dir.find(".fx") == std::string::npos);

    lv_obj_delete(explorer);
    f.advance(10);
}
