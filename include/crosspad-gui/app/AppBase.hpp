#pragma once

#ifdef USE_LVGL

#include "lvgl.h"
#include "crosspad/app/IApp.hpp"
#include "crosspad/app/AppFlags.hpp"
#include "crosspad-gui/components/app_lifecycle.h"

namespace crosspad_gui {

/**
 * @brief CRTP base class for LVGL applications.
 *
 * Provides the full LVGL lifecycle (start / pause / resume / destroyApp),
 * flag management, and getter/setter boilerplate that would otherwise be
 * duplicated across every platform's App class.
 *
 * @tparam Derived  The platform's concrete App class (CRTP pattern).
 *                  Needed so that function-pointer types use the right
 *                  App* type (e.g. `lv_obj_t* (*create)(lv_obj_t*, App*)`).
 *
 * Platform subclass example:
 * @code
 * class App : public crosspad_gui::AppBase<App> {
 * public:
 *     using AppBase::AppBase;          // inherit constructors
 *     Sequencer& sequencer();          // platform-specific accessor
 *     crosspad::PadManager& padManager();
 * };
 * @endcode
 */
template<typename Derived>
class AppBase : public crosspad::IApp {
public:
    using AppFlags = crosspad::AppFlags;
    using CreateFn  = lv_obj_t* (*)(lv_obj_t* parent, Derived* a);
    using DestroyFn = void (*)(lv_obj_t* app);
    using ShowFn    = void (*)(lv_obj_t* app);
    using HideFn    = void (*)(lv_obj_t* app);
    using LogicFn   = void (*)(Derived* self);

    lv_obj_t* app   = nullptr;   ///< This app's root LVGL object
    lv_obj_t* app_c = nullptr;   ///< Parent container
    Derived* parent_app = nullptr; ///< Parent app (for sub-apps)

    // ── Constructors ────────────────────────────────────────────────────

    AppBase(lv_obj_t* app_c, const char* name, const char* icon,
            CreateFn create, DestroyFn destroy = lv_Default_onDestroy)
        : name_(name), icon_(icon), create_(create), destroy_(destroy)
    {
        this->app_c = app_c;
    }

    AppBase(Derived* parent, const char* name, const char* icon,
            CreateFn create, DestroyFn destroy = lv_Default_onDestroy)
        : name_(name), icon_(icon), create_(create), destroy_(destroy)
    {
        this->parent_app = parent;
        this->app_c = parent->app;
    }

    /// Enhanced constructor for auto-registered apps with business logic.
    AppBase(lv_obj_t* app_c, const char* name, const char* icon,
            CreateFn create, DestroyFn destroy, LogicFn appLogic)
        : name_(name), icon_(icon), create_(create), destroy_(destroy), appLogic_(appLogic)
    {
        this->app_c = app_c;
        type_ = crosspad::APP_FLAG_AUTO_REGISTERED;
    }

    virtual ~AppBase() = default;

    // ── IApp interface ──────────────────────────────────────────────────

    const char* getName() const override { return name_; }

    // ── Flag management ─────────────────────────────────────────────────

    void AddFlag(AppFlags flag)    { type_ = type_ | flag; }
    void RemoveFlag(AppFlags flag) { type_ = type_ & ~flag; }
    bool HasFlag(AppFlags flag)    { return (type_ & flag) == flag; }

    // ── LVGL lifecycle ──────────────────────────────────────────────────

    void start(lv_obj_t* parent = lv_screen_active()) {
        if (started_) {
            LV_LOG_WARN("App already started");
            return;
        }
        if (!create_) {
            LV_LOG_WARN("App creation failed, no create function");
            return;
        }

        app = create_(parent, static_cast<Derived*>(this));
        this->app_c = parent;
        LV_LOG_TRACE("App created");

        started_ = true;
        if (show_) {
            show_(app);
            visible_ = true;
        }

        // Call app initialization for auto-registered apps
        if (HasFlag(crosspad::APP_FLAG_AUTO_REGISTERED)) {
            this->onAppInit();
            if (appLogic_) {
                appLogic_(static_cast<Derived*>(this));
            }
        }

        LV_LOG_INFO("App started");
    }

    void pause() {
        if (app != nullptr && started_) {
            hide_(app);
            visible_ = false;
            LV_LOG_INFO("App paused");
        }
    }

    void resume() {
        if (app == nullptr) return;
        if (started_ && !visible_ && show_) {
            show_(app);
            visible_ = true;
            LV_LOG_INFO("App resumed");
        }
    }

    void destroyApp() {
        if (app == nullptr) {
            LV_LOG_WARN("App doesn't exist");
            return;
        }

        // Prevent double destruction
        if (!started_ && !visible_) {
            LV_LOG_WARN("App already being destroyed or destroyed");
            return;
        }

        // Call app deinitialization for auto-registered apps
        if (HasFlag(crosspad::APP_FLAG_AUTO_REGISTERED)) {
            this->onAppDeinit();
        }

        visible_ = false;
        started_ = false;

        // Cache and clear before destroying
        lv_obj_t* app_to_destroy = app;
        app = nullptr;

        if (destroy_) {
            destroy_(app_to_destroy);
        } else {
            LV_LOG_WARN("App with no destroy function, marked as destroyed");
            return;
        }
        LV_LOG_INFO("App destroyed");
    }

    // ── Callback setters ────────────────────────────────────────────────

    void setOnShow(ShowFn show)       { show_ = show; }
    void setOnHide(HideFn hide)       { hide_ = hide; }
    void setOnDestroy(DestroyFn dest)  { destroy_ = dest; }

    // ── Getters ─────────────────────────────────────────────────────────

    lv_obj_t* getAppContainer() { return app; }
    const char* getIcon()       { return icon_; }
    bool isVisible()            { return visible_; }
    bool isStarted()            { return started_; }
    bool isPaused()             { return !visible_; }

private:
    const char* name_;
    const char* icon_;
    bool visible_ = false;
    bool started_ = false;
    AppFlags type_ = crosspad::APP_FLAG_NONE;

    CreateFn  create_   = nullptr;
    DestroyFn destroy_  = lv_Default_onDestroy;
    ShowFn    show_     = lv_Default_onShow;
    HideFn    hide_     = lv_Default_onHide;
    LogicFn   appLogic_ = nullptr;
};

} // namespace crosspad_gui

#endif // USE_LVGL
