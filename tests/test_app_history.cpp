// SPDX-License-Identifier: GPL-3.0-or-later
#include <catch2/catch_test_macros.hpp>

#include "crosspad-gui/app/App.hpp"
#include "crosspad-gui/components/app_orchestrator.h"
#include "crosspad-gui/components/mode_overlay.h"
#include "gui_test_platform.h"

#include "crosspad/app/AppRegistry.hpp"
#include "crosspad/settings/CrosspadSettings.hpp"

#include <cstring>
#include <memory>

using namespace crosspad_gui;

namespace {

constexpr const char* kA = "HistoryA";
constexpr const char* kB = "HistoryB";
constexpr const char* kC = "HistoryC";

lv_obj_t* create_root(lv_obj_t* parent, App*) { return lv_obj_create(parent); }
void destroy_root(lv_obj_t* root) { if (root) lv_obj_delete_async(root); }

/// Three registered apps, once per test binary: the registry has no way to
/// take an entry back.
void register_once()
{
    static bool done = false;
    if (done) return;
    done = true;
    for (const char* name : {kA, kB, kC}) {
        crosspad::AppEntry e{};
        e.name = name;
        e.icon = nullptr;
        e.createLVGL = create_root;
        e.destroyLVGL = destroy_root;
        crosspad::AppRegistry::getInstance().registerApp(e);
    }
}

std::shared_ptr<ILvglApp> make_app(lv_obj_t* container, const char* name, const char* icon,
                                   lv_obj_t* (*create)(lv_obj_t*, App*), void (*destroy)(lv_obj_t*))
{
    if (std::strncmp(name, "History", 7) != 0) return nullptr;
    return defaultAppFactory<App>(container, name, icon, create, destroy);
}

std::shared_ptr<ILvglApp> no_app(lv_obj_t*, const char*, const char*,
                                 lv_obj_t* (*)(lv_obj_t*, App*), void (*)(lv_obj_t*))
{
    return nullptr;
}

/// The orchestrator populated with the three, and emptied again after.
struct Apps {
    explicit Apps(lv_obj_t* container)
    {
        register_once();
        auto& o = AppOrchestrator::getInstance();
        OrchestratorConfig cfg;
        cfg.app_factory = make_app;
        o.init(cfg);
        o.populateApps(container);
        o.clearHistory();
    }

    ~Apps()
    {
        auto& o = AppOrchestrator::getInstance();
        for (const auto& app : o.getApps()) {
            if (app && app->isStarted()) app->destroyApp();
        }
        OrchestratorConfig cfg;
        cfg.app_factory = no_app;
        o.init(cfg);
        o.populateApps(nullptr);
        o.clearHistory();
        o.setRunningApp(nullptr);
        o.setForegroundChangedHandler(nullptr);
        o.setBeforeLaunchHandler(nullptr);
    }

    ILvglApp* get(const char* name) const
    {
        for (const auto& app : AppOrchestrator::getInstance().getApps()) {
            if (app && std::strcmp(app->getName(), name) == 0) return app.get();
        }
        return nullptr;
    }
};

void open(guitest::GuiFixture& f, ILvglApp* app, lv_obj_t* container)
{
    AppOrchestrator::onAppSelected(app, container);
    f.advance(1);
}

/// The pad-map home over @p container, its pads 1-3 the three apps.
void make_pad_home(guitest::GuiFixture& f, lv_obj_t* container)
{
    static const ModeEntry kMap[] = {
        {1, "A", "", 0xFFFFFF, ModeKind::App, kA, nullptr, nullptr},
        {2, "B", "", 0xFFFFFF, ModeKind::App, kB, nullptr, nullptr},
        {3, "C", "", 0xFFFFFF, ModeKind::App, kC, nullptr, nullptr},
    };
    crosspad::CrosspadSettings::getInstance()->launcherStyle = crosspad::LauncherStyle::Pads;
    mode_overlay_init(container, kMap, sizeof(kMap) / sizeof(kMap[0]), crosspad::LauncherStyle::Pads);
    mode_overlay_create_home(container, f.group);
    f.advance(1);
}

void pick_from_home(guitest::GuiFixture& f, uint8_t pad)
{
    REQUIRE(mode_overlay_is_open());
    mode_overlay_select_pad(static_cast<uint8_t>(pad - 1));
    f.advance(1);
}

} // namespace

TEST_CASE("app history: back is the app this one was opened from", "[history]")
{
    guitest::GuiFixture f;
    lv_obj_t* container = lv_obj_create(f.screen);
    Apps apps(container);
    auto& o = AppOrchestrator::getInstance();
    ILvglApp* a = apps.get(kA);
    ILvglApp* b = apps.get(kB);
    ILvglApp* c = apps.get(kC);
    REQUIRE((a && b && c));

    open(f, a, container);
    open(f, b, container);
    open(f, c, container);
    CHECK(o.historyBack(c) == b);
    open(f, b, container);           // what the power click opens
    CHECK(o.historyBack(b) == a);
    open(f, a, container);
    CHECK(o.historyBack(a) == nullptr);   // home next
}

TEST_CASE("app history: opening one already in it drops what came after it", "[history]")
{
    guitest::GuiFixture f;
    lv_obj_t* container = lv_obj_create(f.screen);
    Apps apps(container);
    auto& o = AppOrchestrator::getInstance();

    open(f, apps.get(kA), container);
    open(f, apps.get(kB), container);
    open(f, apps.get(kC), container);
    open(f, apps.get(kA), container);   // handed back, not a step forward
    CHECK(o.historyBack(apps.get(kA)) == nullptr);
}

TEST_CASE("app history: cleared, or not the top, there is no step back", "[history]")
{
    guitest::GuiFixture f;
    lv_obj_t* container = lv_obj_create(f.screen);
    Apps apps(container);
    auto& o = AppOrchestrator::getInstance();

    open(f, apps.get(kA), container);
    open(f, apps.get(kB), container);
    CHECK(o.historyBack(apps.get(kA)) == nullptr);   // not the one on top: left alone
    CHECK(o.historyBack(apps.get(kB)) == apps.get(kA));

    open(f, apps.get(kB), container);
    o.clearHistory();
    CHECK(o.historyBack(apps.get(kB)) == nullptr);
}

TEST_CASE("app history: going home starts it afresh", "[history][mode]")
{
    guitest::GuiFixture f;
    lv_obj_t* container = lv_obj_create(f.screen);
    lv_obj_set_size(container, 320, 210);
    Apps apps(container);
    make_pad_home(f, container);
    auto& o = AppOrchestrator::getInstance();

    open(f, apps.get(kA), container);
    open(f, apps.get(kB), container);
    mode_overlay_go_home();
    f.advance(1);
    pick_from_home(f, 2);
    REQUIRE(apps.get(kB)->isStarted());
    CHECK(o.historyBack(apps.get(kB)) == nullptr);

    lv_obj_clean(container);
}

TEST_CASE("app history: an app picked on the home map starts it afresh", "[history][mode]")
{
    guitest::GuiFixture f;
    lv_obj_t* container = lv_obj_create(f.screen);
    lv_obj_set_size(container, 320, 210);
    Apps apps(container);
    make_pad_home(f, container);
    auto& o = AppOrchestrator::getInstance();

    open(f, apps.get(kA), container);
    open(f, apps.get(kB), container);
    apps.get(kB)->destroyApp();   // closed some other way than home (APP_STOP)
    f.advance(1);
    pick_from_home(f, 2);
    REQUIRE(apps.get(kB)->isStarted());
    CHECK(o.historyBack(apps.get(kB)) == nullptr);

    lv_obj_clean(container);
}
