// SPDX-License-Identifier: GPL-3.0-or-later
#include <catch2/catch_test_macros.hpp>

#include "crosspad-gui/app/App.hpp"
#include "crosspad-gui/components/app_orchestrator.h"
#include "gui_test_platform.h"

#include "crosspad/pad/IPadLogicHandler.hpp"

#include <memory>

using namespace crosspad_gui;

namespace {

constexpr const char* kBase = "Base";
constexpr const char* kAppLogic = "TestAppLogic";

class NoopLogic : public crosspad::IPadLogicHandler {
public:
    void onActivate(crosspad::PadManager&) override {}
    void onDeactivate(crosspad::PadManager&) override {}
    void onPadPress(crosspad::PadManager&, uint8_t, uint8_t) override {}
    void onPadRelease(crosspad::PadManager&, uint8_t) override {}
    void onPadPressure(crosspad::PadManager&, uint8_t, uint8_t) override {}
};

struct Counts {
    int created = 0;
    int destroyed = 0;
    lv_group_t* appGroup = nullptr;
};
Counts g_counts;

/// An app that, like most, takes the encoder over with a group of its own.
lv_obj_t* create_app(lv_obj_t* parent, App* a)
{
    g_counts.created++;
    lv_obj_t* root = lv_obj_create(parent);
    g_counts.appGroup = lv_group_create();
    lv_indev_set_group(getGuiPlatform().getNavigationEncoder(), g_counts.appGroup);
    a->setPadLogicName(kAppLogic);
    return root;
}

void destroy_app(lv_obj_t* root)
{
    g_counts.destroyed++;
    if (g_counts.appGroup) { lv_group_delete(g_counts.appGroup); g_counts.appGroup = nullptr; }
    lv_obj_delete_async(root);
}

lv_obj_t* create_nothing(lv_obj_t*, App*) { return nullptr; }

struct AppFixture : guitest::GuiFixture {
    AppFixture()
    {
        g_counts = Counts{};
        padManager.registerPadLogic(kBase, std::make_shared<NoopLogic>());
        padManager.registerPadLogic(kAppLogic, std::make_shared<NoopLogic>());
        padManager.setActivePadLogic(kBase);
    }
};

} // namespace

TEST_CASE("App: start pushes its pad logic, destroyApp pops it", "[app]")
{
    AppFixture f;
    App app(f.screen, "Test", nullptr, create_app, destroy_app);

    app.start(f.screen);
    CHECK(app.isStarted());
    CHECK(g_counts.created == 1);
    CHECK(f.padManager.getActivePadLogic() == kAppLogic);
    CHECK(AppOrchestrator::getInstance().getForegroundApp() == &app);

    app.destroyApp();
    CHECK_FALSE(app.isStarted());
    CHECK(g_counts.destroyed == 1);
    CHECK(f.padManager.getActivePadLogic() == kBase);
    CHECK(AppOrchestrator::getInstance().getForegroundApp() == nullptr);
    CHECK(lv_indev_get_group(f.gui.encoder) == f.group);   // the launcher's group is back

    app.destroyApp();   // a second destroy changes nothing
    CHECK(g_counts.destroyed == 1);
    CHECK(f.padManager.getActivePadLogic() == kBase);
}

TEST_CASE("App: start twice is refused, the stack stays balanced", "[app]")
{
    AppFixture f;
    App app(f.screen, "Test", nullptr, create_app, destroy_app);
    app.start(f.screen);
    app.start(f.screen);
    CHECK(g_counts.created == 1);
    app.destroyApp();
    CHECK(f.padManager.getActivePadLogic() == kBase);
}

TEST_CASE("App: a root deleted behind its back rolls the app back", "[app]")
{
    AppFixture f;
    App app(f.screen, "Test", nullptr, create_app, destroy_app);
    app.start(f.screen);
    REQUIRE(f.padManager.getActivePadLogic() == kAppLogic);

    lv_obj_clean(f.screen);   // a launcher rebuild takes the app's tree with it
    lv_group_delete(g_counts.appGroup);   // and installs a group of its own
    g_counts.appGroup = nullptr;
    lv_indev_set_group(f.gui.encoder, f.group);
    CHECK_FALSE(app.isStarted());
    CHECK(g_counts.destroyed == 0);
    CHECK(f.padManager.getActivePadLogic() == kBase);
    CHECK(AppOrchestrator::getInstance().getForegroundApp() == nullptr);

    app.start(f.screen);   // and it can start again
    CHECK(app.isStarted());
    CHECK(g_counts.created == 2);
    app.destroyApp();
    CHECK(f.padManager.getActivePadLogic() == kBase);
}

TEST_CASE("App: a restart before the old root's async delete keeps the new instance", "[app]")
{
    AppFixture f;
    App app(f.screen, "Test", nullptr, create_app, destroy_app);
    app.start(f.screen);
    app.destroyApp();          // queues the root's delete
    app.start(f.screen);       // before it ran
    f.advance(1);              // the old root goes now

    CHECK(app.isStarted());
    CHECK(f.padManager.getActivePadLogic() == kAppLogic);
    app.destroyApp();
    CHECK(f.padManager.getActivePadLogic() == kBase);
}

TEST_CASE("App: a create that fails pushes nothing", "[app]")
{
    AppFixture f;
    App app(f.screen, "Test", nullptr, create_nothing, destroy_app);
    app.setPadLogicName(kAppLogic);
    app.setOnShow(nullptr);   // the default show asserts on the missing root
    app.start(f.screen);
    CHECK(f.padManager.getActivePadLogic() == kBase);
    app.destroyApp();
    CHECK(f.padManager.getActivePadLogic() == kBase);
    AppOrchestrator::getInstance().noteAppStopped(&app);
}

TEST_CASE("App: the accessors are the platform's objects", "[app]")
{
    AppFixture f;
    App app(f.screen, "Test", nullptr, create_app, destroy_app);
    CHECK(&app.padManager() == &f.padManager);
    CHECK(&app.ledController() == &f.padLedController);
    CHECK(&app.status() == &f.status);
    CHECK(&app.settings() == crosspad::CrosspadSettings::getInstance());
}
