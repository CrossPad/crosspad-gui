// SPDX-License-Identifier: GPL-3.0-or-later
#include <catch2/catch_test_macros.hpp>
#include <string>
#include <vector>

#include "crosspad-gui/components/mode_more.h"
#include "lvgl_headless.h"

using namespace crosspad_gui;

namespace {

const ModeEntry kMap[] = {
    {  1, "Sampler", "play", 0, ModeKind::Core, "Sampler",  nullptr, nullptr },
    {  8, "More",    "apps", 0, ModeKind::More, nullptr,    nullptr, nullptr },
    { 16, "Setup",   "",     0, ModeKind::Rare, "Settings", nullptr, nullptr },
};

std::vector<std::string> strings(const std::vector<const char*>& v)
{
    return {v.begin(), v.end()};
}

const char* g_picked = nullptr;
void pick(const char* name) { g_picked = name; }

lv_obj_t* fresh_screen()
{
    lvgl_headless_init();
    lv_obj_t* scr = lv_obj_create(nullptr);
    lv_screen_load(scr);
    return scr;
}

} // namespace

TEST_CASE("More lists the apps no pad names, in registry order", "[mode_more]")
{
    const std::vector<const char*> reg{"Settings", "HelloScreen", "Sampler", "PadsLeds"};
    REQUIRE(strings(mode_more_apps(kMap, 3, reg)) ==
            std::vector<std::string>{"HelloScreen", "PadsLeds"});
}

TEST_CASE("More matches names the way the map does: case-insensitive", "[mode_more]")
{
    const std::vector<const char*> reg{"sampler", "SETTINGS", "Motion"};
    REQUIRE(strings(mode_more_apps(kMap, 3, reg)) == std::vector<std::string>{"Motion"});
}

TEST_CASE("More skips null names and is empty when every app has a pad", "[mode_more]")
{
    const std::vector<const char*> reg{nullptr, "Sampler", "Settings"};
    REQUIRE(mode_more_apps(kMap, 3, reg).empty());
}

TEST_CASE("The list has one row per app and a click picks that app", "[mode_more]")
{
    lv_obj_t* scr = fresh_screen();
    lv_group_t* group = lv_group_create();
    const std::vector<const char*> names{"HelloScreen", "PadsLeds"};
    g_picked = nullptr;

    lv_obj_t* list = mode_more_build(scr, names, pick, group);

    REQUIRE(lv_obj_get_child_count(list) == 2);
    REQUIRE(lv_group_get_obj_count(group) == 2);
    REQUIRE(lv_group_get_focused(group) == lv_obj_get_child(list, 0));
    lv_obj_send_event(lv_obj_get_child(list, 1), LV_EVENT_CLICKED, nullptr);
    REQUIRE(g_picked != nullptr);
    REQUIRE(std::string(g_picked) == "PadsLeds");

    lv_group_delete(group);
    lv_obj_delete(scr);
}

TEST_CASE("An empty list says so and picks nothing", "[mode_more]")
{
    lv_obj_t* scr = fresh_screen();
    g_picked = nullptr;

    lv_obj_t* list = mode_more_build(scr, {}, pick, nullptr);

    REQUIRE(lv_obj_get_child_count(list) == 1);
    lv_obj_t* row = lv_obj_get_child(list, 0);
    REQUIRE(std::string(lv_label_get_text(row)) == "No other apps");
    lv_obj_send_event(row, LV_EVENT_CLICKED, nullptr);
    REQUIRE(g_picked == nullptr);

    lv_obj_delete(scr);
}
