#ifdef USE_LVGL

#include "crosspad-gui/components/app_orchestrator.h"
#include "crosspad/app/AppRegistry.hpp"
#include <cstring>

namespace crosspad_gui {

AppOrchestrator& AppOrchestrator::getInstance() {
    static AppOrchestrator instance;
    return instance;
}

void AppOrchestrator::init(const OrchestratorConfig& config) {
    config_ = config;
}

void AppOrchestrator::populateApps(lv_obj_t* container) {
    // Clean up previously created apps to avoid memory leaks
    for (auto* app : apps_) {
        if (!app) continue;
        if (app == runningApp_) {
            if (app->isStarted()) app->destroyApp();
            runningApp_ = nullptr;
        }
        delete app;
    }
    apps_.clear();
    infos_.clear();

    if (!config_.app_factory) {
        LV_LOG_ERROR("AppOrchestrator: no app_factory set — call init() first");
        return;
    }

    auto& registry = crosspad::AppRegistry::getInstance();
    const auto* entries = registry.getApps();
    size_t count = registry.getAppCount();

    for (size_t i = 0; i < count; i++) {
        const auto& entry = entries[i];
        if (entry.createLVGL == nullptr) continue;

        // Resolve icon path (platform may prepend asset prefix)
        const char* icon = entry.icon;
        if (config_.icon_resolver) {
            icon = config_.icon_resolver(apps_.size(), entry.icon);
        }

        ILvglApp* app = config_.app_factory(
            container, entry.name, icon,
            entry.createLVGL, entry.destroyLVGL);
        if (!app) continue;

        // Flag Power OFF as message box
        if (strcmp(entry.name, "Power OFF") == 0) {
            app->AddFlag(crosspad::APP_FLAG_MSGBOX);
        }

        apps_.push_back(app);
        infos_.push_back({app->getName(), app->getIcon(), app});
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
