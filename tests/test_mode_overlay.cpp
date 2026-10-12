// SPDX-License-Identifier: GPL-3.0-or-later
#include <catch2/catch_test_macros.hpp>

#include "crosspad-gui/app/App.hpp"
#include "crosspad-gui/components/app_orchestrator.h"
#include "crosspad-gui/components/mode_overlay.h"
#include "gui_test_platform.h"

#include "crosspad/pad/IPadLogicHandler.hpp"

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

TEST_CASE("MODE map: an app started from outside the map gets the pads", "[mode][app]")
{
    guitest::GuiFixture f;
    f.padManager.registerPadLogic(kBase, std::make_shared<NoopLogic>());
    f.padManager.registerPadLogic(kFirstLogic, std::make_shared<NoopLogic>());
    f.padManager.registerPadLogic(kSecondLogic, std::make_shared<NoopLogic>());
    f.padManager.setActivePadLogic(kBase);

    lv_obj_t* container = lv_obj_create(f.screen);
    mode_overlay_init(container, nullptr, 0);

    App first(container, "First", nullptr, create_root, destroy_root);
    first.setPadLogicName(kFirstLogic);
    App second(container, "Second", nullptr, create_root, destroy_root);
    second.setPadLogicName(kSecondLogic);

    // Home: the map with nothing under it.
    mode_overlay_go_home();
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

    // Closing it leaves the base under it, not the map nor the first app.
    second.destroyApp();
    f.advance(1);   // nothing left on screen: the map comes up as home
    REQUIRE(mode_overlay_is_open());
    mode_overlay_close();
    CHECK(f.padManager.getActivePadLogic() == kBase);

    AppOrchestrator::getInstance().setRunningApp(nullptr);
    AppOrchestrator::getInstance().setForegroundChangedHandler(nullptr);
    AppOrchestrator::getInstance().setBeforeLaunchHandler(nullptr);
}
