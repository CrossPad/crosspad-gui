// SPDX-License-Identifier: GPL-3.0-or-later
#include <catch2/catch_test_macros.hpp>

#include "crosspad-gui/app/App.hpp"
#include "crosspad-gui/components/app_orchestrator.h"
#include "crosspad-gui/components/mode_overlay.h"
#include "crosspad-gui/components/power_gesture.h"
#include "gui_test_platform.h"

#include "crosspad/pad/IPadLogicHandler.hpp"
#include "crosspad/settings/CrosspadSettings.hpp"
#include "crosspad/settings/ISettingsUI.hpp"

#include <catch2/generators/catch_generators.hpp>

#include <memory>

using namespace crosspad_gui;

namespace {

constexpr const char* kBase = "Base";
constexpr const char* kMapLogic = "ModeOverlay";
constexpr const char* kFirstLogic = "FirstLogic";
constexpr const char* kSecondLogic = "SecondLogic";

class NoopLogic : public crosspad::IPadLogicHandler {
public:
    void onActivate(crosspad::PadManager&) override {}
    void onDeactivate(crosspad::PadManager&) override {}
    void onPadPress(crosspad::PadManager&, uint8_t, uint8_t) override {}
    void onPadRelease(crosspad::PadManager&, uint8_t) override {}
    void onPadPressure(crosspad::PadManager&, uint8_t, uint8_t) override {}
};

lv_obj_t* create_root(lv_obj_t* parent, App*) { return lv_obj_create(parent); }
void destroy_root(lv_obj_t* root) { if (root) lv_obj_delete_async(root); }

/// What CDC APP_START and the simulator's app_start both do: hand the app to
/// the orchestrator, map or no map.
void start_programmatically(App& app, lv_obj_t* container)
{
    AppOrchestrator::onAppSelected(&app, container);
}

} // namespace

namespace {

/// Exit to home is the platform's LoadMainScreen(); counting the calls is
/// enough to see that a changed home style asks for the rebuild.
class CountingSettingsUI : public crosspad::ISettingsUI {
public:
    void saveSettings() override {}
    void exitToHome() override { ++exits; }
    int exits = 0;
};

void reset_orchestrator()
{
    AppOrchestrator::getInstance().setRunningApp(nullptr);
    AppOrchestrator::getInstance().setForegroundChangedHandler(nullptr);
    AppOrchestrator::getInstance().setBeforeLaunchHandler(nullptr);
}

/// What build_main_screen() does for the given home style, on a bare container.
lv_obj_t* make_home(guitest::GuiFixture& f, crosspad::LauncherStyle style)
{
    crosspad::CrosspadSettings::getInstance()->launcherStyle = style;
    lv_obj_t* container = lv_obj_create(f.screen);
    lv_obj_set_size(container, 320, 210);
    mode_overlay_init(container, nullptr, 0, style);
    if (style == crosspad::LauncherStyle::Pads) mode_overlay_create_home(container, f.group);
    f.advance(1);   // the home's own check
    return container;
}

void register_logics(guitest::GuiFixture& f)
{
    f.padManager.registerPadLogic(kBase, std::make_shared<NoopLogic>());
    f.padManager.registerPadLogic(kFirstLogic, std::make_shared<NoopLogic>());
    f.padManager.registerPadLogic(kSecondLogic, std::make_shared<NoopLogic>());
    f.padManager.setActivePadLogic(kBase);
}

} // namespace

TEST_CASE("MODE map: an app started from outside the map gets the pads", "[mode][app]")
{
    guitest::GuiFixture f;
    register_logics(f);
    lv_obj_t* container = make_home(f, crosspad::LauncherStyle::Pads);

    App first(container, "First", nullptr, create_root, destroy_root);
    first.setPadLogicName(kFirstLogic);
    App second(container, "Second", nullptr, create_root, destroy_root);
    second.setPadLogicName(kSecondLogic);

    // Home: the map with nothing over it has the pads.
    REQUIRE(mode_overlay_is_open());
    REQUIRE(f.padManager.getActivePadLogic() == kMapLogic);

    start_programmatically(first, container);
    f.advance(1);   // the map's own check after the start
    CHECK_FALSE(mode_overlay_is_open());
    CHECK(first.isStarted());
    CHECK(f.padManager.getActivePadLogic() == kFirstLogic);

    // MODE over a running app, then another app from outside the map.
    mode_overlay_toggle();
    REQUIRE(mode_overlay_is_open());
    REQUIRE(f.padManager.getActivePadLogic() == kMapLogic);

    start_programmatically(second, container);
    f.advance(1);
    CHECK_FALSE(mode_overlay_is_open());
    CHECK_FALSE(first.isStarted());
    CHECK(second.isStarted());
    CHECK(f.padManager.getActivePadLogic() == kSecondLogic);

    // Closing it lands on the home map, which takes the pads back.
    second.destroyApp();
    f.advance(1);
    CHECK(mode_overlay_is_open());
    CHECK(f.padManager.getActivePadLogic() == kMapLogic);

    // The home goes with the screen's rebuild, and hands the pads to the base.
    lv_obj_clean(container);
    CHECK_FALSE(mode_overlay_is_open());
    CHECK(f.padManager.getActivePadLogic() == kBase);

    reset_orchestrator();
}

namespace {

/// Counts what reaches it: the instrument or the app under the map.
class CountingLogic : public NoopLogic {
public:
    void onPadPress(crosspad::PadManager&, uint8_t, uint8_t) override { ++presses; }
    void onPadRelease(crosspad::PadManager&, uint8_t) override { ++releases; }
    int presses = 0, releases = 0;
};

} // namespace

TEST_CASE("MODE map: the pads are buttons, every other note plays under the map", "[mode][pads]")
{
    guitest::GuiFixture f;
    auto base = std::make_shared<CountingLogic>();
    auto app = std::make_shared<CountingLogic>();
    f.padManager.registerPadLogic(kBase, base);
    f.padManager.registerPadLogic(kFirstLogic, app);
    f.padManager.setActivePadLogic(kBase);
    lv_obj_t* container = make_home(f, crosspad::LauncherStyle::Pads);
    REQUIRE(f.padManager.getActivePadLogic() == kMapLogic);
    REQUIRE(f.padManager.getPadLogic(kMapLogic)->buttonsOnly());

    // At home: a finger chooses and plays nothing; a pattern and a host play
    // the instrument under the map.
    f.padManager.handlePadPress(15, 100);
    f.padManager.handlePadRelease(15);
    CHECK(base->presses == 0);
    f.padManager.handlePadPress(3, 100, crosspad::EventSource::SequencerCallback);
    f.padManager.handlePadRelease(3, crosspad::EventSource::SequencerCallback);
    f.padManager.handlePadPress(4, 100, crosspad::EventSource::MidiInput);
    f.padManager.handlePadRelease(4, crosspad::EventSource::MidiInput);
    CHECK(base->presses == 2);
    CHECK(base->releases == 2);

    // MODE over an app: the app keeps its notes under the map.
    App first(container, "First", nullptr, create_root, destroy_root);
    first.setPadLogicName(kFirstLogic);
    start_programmatically(first, container);
    f.advance(1);
    mode_overlay_toggle();
    REQUIRE(f.padManager.getActivePadLogic() == kMapLogic);
    f.padManager.handlePadPress(5, 100, crosspad::EventSource::SequencerCallback);
    CHECK(app->presses == 1);
    CHECK(base->presses == 2);

    mode_overlay_toggle();   // MODE again: back to the app
    f.advance(1);
    first.destroyApp();
    f.advance(1);
    lv_obj_clean(container);
    reset_orchestrator();
}

TEST_CASE("MODE map: MODE and the power click over an app", "[mode][app]")
{
    guitest::GuiFixture f;
    register_logics(f);
    lv_obj_t* container = make_home(f, crosspad::LauncherStyle::Pads);
    App first(container, "First", nullptr, create_root, destroy_root);
    first.setPadLogicName(kFirstLogic);
    start_programmatically(first, container);
    f.advance(1);

    mode_overlay_toggle();
    REQUIRE(mode_overlay_is_open());
    mode_overlay_toggle();   // MODE again: back to the app
    f.advance(1);
    CHECK_FALSE(mode_overlay_is_open());
    CHECK(first.isStarted());
    CHECK(f.padManager.getActivePadLogic() == kFirstLogic);

    mode_overlay_toggle();
    REQUIRE(mode_overlay_is_open());
    CHECK(powerBack() == BackResult::Overlay);   // the click closes the map, not the app
    f.advance(1);
    CHECK_FALSE(mode_overlay_is_open());
    CHECK(first.isStarted());
    CHECK(f.padManager.getActivePadLogic() == kFirstLogic);

    first.destroyApp();
    f.advance(1);
    lv_obj_clean(container);
    reset_orchestrator();
}

TEST_CASE("MODE map: with the grid or the list as home, MODE goes home", "[mode][app]")
{
    guitest::GuiFixture f;
    register_logics(f);
    const auto style = GENERATE(crosspad::LauncherStyle::Grid, crosspad::LauncherStyle::List);
    lv_obj_t* container = make_home(f, style);

    // No map at home: the pads stay the base's.
    CHECK_FALSE(mode_overlay_is_open());
    CHECK(f.padManager.getActivePadLogic() == kBase);

    App first(container, "First", nullptr, create_root, destroy_root);
    first.setPadLogicName(kFirstLogic);
    start_programmatically(first, container);
    f.advance(1);
    REQUIRE(first.isStarted());

    mode_overlay_toggle();
    f.advance(1);
    CHECK_FALSE(first.isStarted());
    CHECK_FALSE(mode_overlay_is_open());
    CHECK(f.padManager.getActivePadLogic() == kBase);

    lv_obj_clean(container);
    reset_orchestrator();
}

TEST_CASE("MODE map: a home style changed while an app was open rebuilds on the way back",
          "[mode][app][settings]")
{
    guitest::GuiFixture f;
    register_logics(f);
    CountingSettingsUI ui;
    crosspad::setSettingsUI(&ui);
    lv_obj_t* container = make_home(f, crosspad::LauncherStyle::Pads);
    REQUIRE(ui.exits == 0);

    App first(container, "First", nullptr, create_root, destroy_root);
    first.setPadLogicName(kFirstLogic);
    start_programmatically(first, container);
    f.advance(1);
    crosspad::CrosspadSettings::getInstance()->launcherStyle = crosspad::LauncherStyle::List;
    f.advance(1);
    CHECK(ui.exits == 0);   // not while the app is up

    first.destroyApp();
    f.advance(1);
    CHECK(ui.exits == 1);

    crosspad::setSettingsUI(nullptr);
    crosspad::CrosspadSettings::getInstance()->launcherStyle = crosspad::LauncherStyle::Pads;
    lv_obj_clean(container);
    reset_orchestrator();
}
