#pragma once

#ifdef USE_LVGL

#include "lvgl.h"
#include "crosspad-gui/app/ILvglApp.h"
#include "crosspad-gui/components/app_lifecycle.h"
#include "crosspad-gui/platform/IGuiPlatform.h"
#include "crosspad/pad/PadManager.hpp"
#include <memory>

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
 * };
 * // Use crosspad::getPadManager() etc. directly from PlatformServices.
 * @endcode
 */
template<typename Derived>
class AppBase : public ILvglApp {
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

    void AddFlag(AppFlags flag) override    { type_ = type_ | flag; }
    void RemoveFlag(AppFlags flag)         { type_ = type_ & ~flag; }
    bool HasFlag(AppFlags flag) override   { return (type_ & flag) == flag; }

    // ── LVGL lifecycle ──────────────────────────────────────────────────

    void start(lv_obj_t* parent = lv_screen_active()) override {
        if (started_) {
            LV_LOG_WARN("App already started");
            return;
        }
        if (!create_) {
            LV_LOG_WARN("App creation failed, no create function");
            return;
        }

        // Remember whoever owns encoder navigation before the app takes it
        // over. Apps routinely create their own lv_group and delete it on
        // teardown; without a framework-owned restore, the indev is left
        // pointing at a deleted group and the launcher loses the encoder.
        // Same ownership model as pushPadLogic()/popPadLogic() below.
        lv_indev_t* enc = getGuiPlatform().getNavigationEncoder();
        priorEncoderGroup_ = enc ? lv_indev_get_group(enc) : nullptr;

        app = create_(parent, static_cast<Derived*>(this));
        this->app_c = parent;
        LV_LOG_TRACE("App created");

        started_ = true;
        if (show_) {
            show_(app);
            visible_ = true;
        }

        // Guard on `app`: a failed create_() (returns nullptr) must not push —
        // destroyApp()'s `if (app == nullptr) return;` guard would make the
        // matching pop unreachable, leaking a stack frame forever.
        if (app && padLogicName_ && padLogicName_[0] != '\0') {
            crosspad::getPadManager().pushPadLogic(padLogicName_);
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

    void pause() override {
        if (app != nullptr && started_) {
            hide_(app);
            visible_ = false;
            LV_LOG_INFO("App paused");
        }
    }

    void resume() override {
        if (app == nullptr) return;
        if (started_ && !visible_ && show_) {
            show_(app);
            visible_ = true;
            LV_LOG_INFO("App resumed");
        }
    }

    void destroyApp() override {
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

        if (padLogicName_ && padLogicName_[0] != '\0') {
            crosspad::getPadManager().popPadLogic();
        }

        if (destroy_) {
            destroy_(app_to_destroy);
        } else {
            LV_LOG_WARN("App with no destroy function, marked as destroyed");
            restoreEncoderGroup();
            return;
        }
        // After destroy_, because that is where the app deletes its own group.
        restoreEncoderGroup();
        LV_LOG_INFO("App destroyed");
    }

    // ── Callback setters ────────────────────────────────────────────────

    void setOnShow(ShowFn show)       { show_ = show; }
    void setOnHide(HideFn hide)       { hide_ = hide; }
    void setOnDestroy(DestroyFn dest)  { destroy_ = dest; }
    void setPadLogicName(const char* name) override { padLogicName_ = name; }

    // ── Getters ─────────────────────────────────────────────────────────

    lv_obj_t* getAppContainer() override { return app; }
    const char* getIcon() override      { return icon_; }
    bool isVisible()                    { return visible_; }
    bool isStarted() override           { return started_; }
    bool isPaused()                     { return !visible_; }

private:
    /// Hand encoder navigation back to whoever held it when this app started.
    /// No-op when the app never took it over, so apps that manage the encoder
    /// themselves (modal overlays restoring their own prior group) are not
    /// second-guessed.
    void restoreEncoderGroup() {
        lv_indev_t* enc = getGuiPlatform().getNavigationEncoder();
        if (!enc || !priorEncoderGroup_) return;
        if (lv_indev_get_group(enc) == priorEncoderGroup_) return;
        lv_indev_set_group(enc, priorEncoderGroup_);
        priorEncoderGroup_ = nullptr;
    }

    const char* name_;
    const char* icon_;
    bool visible_ = false;
    bool started_ = false;
    AppFlags type_ = crosspad::APP_FLAG_NONE;
    const char* padLogicName_ = nullptr;
    lv_group_t* priorEncoderGroup_ = nullptr;

    CreateFn  create_   = nullptr;
    DestroyFn destroy_  = lv_Default_onDestroy;
    ShowFn    show_     = lv_Default_onShow;
    HideFn    hide_     = lv_Default_onHide;
    LogicFn   appLogic_ = nullptr;
};

/// Default app factory — eliminates per-platform boilerplate.
/// Usage: `config.app_factory = crosspad_gui::defaultAppFactory<App>;`
///
/// Returns a std::shared_ptr so that AppType (which derives from
/// std::enable_shared_from_this) can call shared_from_this() at runtime
/// (required by destroyAppAsync()). Constructing via raw `new` would leave
/// the internal weak_ptr unset and break shared_from_this().
template<typename AppType>
std::shared_ptr<ILvglApp> defaultAppFactory(
    lv_obj_t* container, const char* name, const char* icon,
    lv_obj_t* (*create)(lv_obj_t*, AppType*),
    void (*destroy)(lv_obj_t*)) {
    return std::make_shared<AppType>(container, name, icon, create, destroy);
}

} // namespace crosspad_gui

#endif // USE_LVGL
