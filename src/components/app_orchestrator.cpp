#ifdef USE_LVGL

#include "crosspad-gui/components/app_orchestrator.h"
#include "crosspad/app/AppRegistry.hpp"
#include <algorithm>
#include <cstring>
#include <vector>

namespace crosspad_gui {

AppOrchestrator& AppOrchestrator::getInstance() {
    static AppOrchestrator instance;
    return instance;
}

void AppOrchestrator::init(const OrchestratorConfig& config) {
    config_ = config;
}

void AppOrchestrator::populateApps(lv_obj_t* container) {
    // Tear down currently running app before releasing owners
    for (auto& app : apps_) {
        if (!app) continue;
        if (app.get() == runningApp_) {
            if (app->isStarted()) app->destroyApp();
            runningApp_ = nullptr;
        }
    }
    apps_.clear();   // shared_ptr destructors release memory
    infos_.clear();

    if (!config_.app_factory) {
        LV_LOG_ERROR("AppOrchestrator: no app_factory set — call init() first");
        return;
    }

    auto& registry = crosspad::AppRegistry::getInstance();
    const auto* entries = registry.getApps();
    size_t count = registry.getAppCount();

    // Stable ordering lets ad-hoc app registrations choose their launcher
    // position with AppEntry::priority while preserving registration order for
    // entries with the same priority.
    std::vector<const crosspad::AppEntry*> sortedEntries;
    sortedEntries.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        if (entries[i].createLVGL != nullptr) {
            sortedEntries.push_back(&entries[i]);
        }
    }
    std::stable_sort(sortedEntries.begin(), sortedEntries.end(),
        [](const crosspad::AppEntry* lhs, const crosspad::AppEntry* rhs) {
            return lhs->priority > rhs->priority;
        });

    for (const auto* entryPtr : sortedEntries) {
        const auto& entry = *entryPtr;

        // Resolve icon path (platform may prepend asset prefix)
        const char* icon = entry.icon;
        if (config_.icon_resolver) {
            icon = config_.icon_resolver(apps_.size(), entry.icon);
        }

        std::shared_ptr<ILvglApp> app = config_.app_factory(
            container, entry.name, icon,
            entry.createLVGL, entry.destroyLVGL);
        if (!app) continue;

        app->setPadLogicName(entry.padLogic);

        // Flag Power OFF as message box
        if (strcmp(entry.name, "Power OFF") == 0) {
            app->AddFlag(crosspad::APP_FLAG_MSGBOX);
        }

        ILvglApp* raw = app.get();
        apps_.push_back(std::move(app));
        infos_.push_back({raw->getName(), raw->getIcon(), raw});
    }

    LV_LOG_INFO("AppOrchestrator: populated %d apps", (int)apps_.size());
}

void AppOrchestrator::onAppSelected(void* app_ptr, lv_obj_t* app_container) {
    auto* app = static_cast<ILvglApp*>(app_ptr);
    if (!app) return;

    auto& self = getInstance();

    // Destroy previous app if running
    auto* prev = self.runningApp_;
    if (prev && prev != app && prev->isStarted()) {
        prev->destroyApp();
        self.runningApp_ = nullptr;
    }

    // Pre-launch hook (e.g. kit selector interception)
    if (self.config_.pre_launch) {
        if (!self.config_.pre_launch(app, app_container)) {
            return; // platform intercepted — don't launch normally
        }
    }

    // Standard launch
    app->start(app_container);
    app->resume();
    self.runningApp_ = app;
}

void AppOrchestrator::onPopupClose(void* app_ptr) {
    auto* app = static_cast<ILvglApp*>(app_ptr);
    if (app) app->destroyApp();
}

void AppOrchestrator::noteAppStarted(ILvglApp* app) {
    if (!app) return;
    noteAppStopped(app);          // one entry per app, newest position
    started_.push_back(app);
}

void AppOrchestrator::noteAppStopped(ILvglApp* app) {
    for (size_t i = 0; i < started_.size(); i++) {
        if (started_[i] != app) continue;
        started_.erase(started_.begin() + (long)i);
        return;
    }
}

ILvglApp* AppOrchestrator::getForegroundApp() const {
    for (size_t i = started_.size(); i > 0; i--) {
        ILvglApp* app = started_[i - 1];
        if (app && app->isStarted()) return app;
    }
    return (runningApp_ && runningApp_->isStarted()) ? runningApp_ : nullptr;
}

MainScreenResult AppOrchestrator::loadMainScreen(
    lv_obj_t* parent, const MainScreenConfig& config)
{
    // Destroy currently running app
    if (runningApp_ && runningApp_->isStarted()) {
        runningApp_->destroyApp();
        runningApp_ = nullptr;
    }

    return build_main_screen(parent, infos_.data(), infos_.size(), config);
}

} // namespace crosspad_gui

#endif // USE_LVGL
