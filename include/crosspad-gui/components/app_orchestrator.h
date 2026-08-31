#pragma once

/**
 * @file app_orchestrator.h
 * @brief Shared app lifecycle orchestration for LVGL platforms.
 *
 * Eliminates duplicated on_app_selected / on_popup_close / AddApps /
 * LoadMainScreen logic that was previously copy-pasted across every
 * platform's gui.cpp.
 *
 * Platforms provide hooks for platform-specific behavior (icon path
 * resolution, pre-launch interception like kit-selector overlays).
 */

#ifdef USE_LVGL

#include "crosspad-gui/app/ILvglApp.h"
#include "crosspad-gui/components/main_screen.h"
#include "crosspad/app/AppEntry.hpp"
#include <memory>
#include <vector>

namespace crosspad_gui {

// ── Hook types ───────────────────────────────────────────────────────────────

/// Called before an app is launched.  Return true to proceed normally,
/// false to cancel (e.g. platform intercepted with a kit-selector overlay).
using PreLaunchHook = bool (*)(ILvglApp* app, lv_obj_t* app_container);

/// Transform an icon path before App creation (e.g. prepend asset prefix).
/// Return the resolved path.  May return the original pointer unchanged.
using IconResolverFn = const char* (*)(size_t index, const char* icon);

/// Factory that creates a platform-specific App (which inherits ILvglApp)
/// from an AppEntry.  Called once per registered LVGL app during populateApps().
///
/// Must return a std::shared_ptr so that App (which uses
/// std::enable_shared_from_this) can call shared_from_this() at runtime —
/// required by App::destroyAppAsync(). Use crosspad_gui::defaultAppFactory<App>.
using AppFactoryFn = std::shared_ptr<ILvglApp> (*)(lv_obj_t* container,
                                    const char* name,
                                    const char* icon,
                                    lv_obj_t* (*createLVGL)(lv_obj_t*, App*),
                                    void (*destroyLVGL)(lv_obj_t*));

// ── Configuration ────────────────────────────────────────────────────────────

struct OrchestratorConfig {
    PreLaunchHook  pre_launch    = nullptr;  ///< Optional pre-launch hook
    IconResolverFn icon_resolver = nullptr;  ///< Optional icon path transformer
    AppFactoryFn   app_factory   = nullptr;  ///< Required: creates platform App
};

// ── AppOrchestrator ──────────────────────────────────────────────────────────

class AppOrchestrator {
public:
    static AppOrchestrator& getInstance();

    /// Configure the orchestrator.  Call once before populateApps().
    void init(const OrchestratorConfig& config);

    /// Iterate AppRegistry, create App objects via factory, populate launcher
    /// info list.  Replaces the platform's AddApps() function.
    void populateApps(lv_obj_t* container);

    /// Standard on_app_selected callback for MainScreenConfig / LauncherConfig.
    /// Casts void* to ILvglApp*, handles destroy-previous + start + resume.
    static void onAppSelected(void* app_ptr, lv_obj_t* app_container);

    /// Standard on_popup_close callback.  Destroys the app.
    static void onPopupClose(void* app_ptr);

    /// Build the main screen using the shared launcher.
    /// Destroys the currently running app first.
    /// Replaces the platform's LoadMainScreen() function.
    MainScreenResult loadMainScreen(lv_obj_t* parent,
                                    const MainScreenConfig& config);

    // ── Accessors ────────────────────────────────────────────────────────

    ILvglApp* getRunningApp() const { return runningApp_; }
    void setRunningApp(ILvglApp* app) { runningApp_ = app; }

    /// The app that actually owns the screen right now — the most recently
    /// started one that is still up, which is not always runningApp_: an app
    /// launched from the pre-launch hook (the sampler's kit-selector gate) is
    /// hand-built and never handed to setRunningApp(). Falls back to
    /// runningApp_, and returns nullptr in the bare launcher.
    ILvglApp* getForegroundApp() const;

    /// Called by AppBase::start() / destroyApp(); platforms do not call these.
    void noteAppStarted(ILvglApp* app);
    void noteAppStopped(ILvglApp* app);

    const std::vector<std::shared_ptr<ILvglApp>>& getApps() const { return apps_; }
    const std::vector<LauncherAppInfo>& getAppInfos() const { return infos_; }

private:
    AppOrchestrator() = default;

    OrchestratorConfig config_{};
    ILvglApp* runningApp_ = nullptr;

    std::vector<ILvglApp*> started_;                ///< Start order, newest last
    std::vector<std::shared_ptr<ILvglApp>> apps_;   ///< Owned App objects
    std::vector<LauncherAppInfo>           infos_;  ///< Parallel launcher descriptors
};

} // namespace crosspad_gui

#endif // USE_LVGL
