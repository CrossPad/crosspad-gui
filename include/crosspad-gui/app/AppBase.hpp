// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#ifdef USE_LVGL

#include "lvgl.h"
#include "crosspad-gui/app/ILvglApp.h"
#include "crosspad-gui/components/app_orchestrator.h"
#include "crosspad-gui/components/app_lifecycle.h"
#include "crosspad-gui/platform/IGuiPlatform.h"
#include "crosspad-gui/widgets/gui_helpers.h"
#include "crosspad/pad/PadManager.hpp"
#include "crosspad/transport/Transport.hpp"
#include <memory>

namespace crosspad_gui {

/**
 * @brief CRTP base class for LVGL applications.
 *
 * Provides the LVGL app lifecycle every platform's App class shares:
 * start() builds the app's screen with its create function, destroyApp()
 * tears it down with its destroy function, and pause()/resume() hide and
 * show it. With a pad logic name (REGISTER_APP_PL), start() gives the pads
 * to that logic and destroyApp() hands them back. Set-up belongs in the
 * create function, clean-up in the destroy function.
 *
 * @tparam Derived  The platform's concrete App class (CRTP pattern).
 *                  Needed so that function-pointer types use the right
 *                  App* type (e.g. `lv_obj_t* (*create)(lv_obj_t*, App*)`).
 *
 * crosspad-gui's own App (crosspad-gui/app/App.hpp) is the subclass every
 * platform uses:
 * @code
 * class App : public crosspad_gui::AppBase<App> {
 * public:
 *     using AppBase::AppBase;          // inherit constructors
 *     crosspad::PadManager& padManager();
 * };
 * @endcode
 */
template<typename Derived>
class AppBase : public ILvglApp {
public:
    using AppFlags = crosspad::AppFlags;
    /// Build the app's screen under @p parent and return its root. Returning
    /// nullptr leaves the app not started.
    using CreateFn  = lv_obj_t* (*)(lv_obj_t* parent, Derived* a);
    /// The app's teardown, called once for every start() that built a root.
    /// From destroyApp() it gets the root and deletes it
    /// (lv_obj_delete_async()). When someone else deleted the root -- a
    /// launcher rebuild's lv_obj_clean() -- it is called with nullptr from
    /// inside that deletion: release what the app owns (timers, event-bus
    /// subscriptions, its encoder group, pointers into the tree) and delete
    /// nothing, the tree is already going.
    using DestroyFn = void (*)(lv_obj_t* app);
    /// Show the app's root; the default clears LV_OBJ_FLAG_HIDDEN.
    using ShowFn    = void (*)(lv_obj_t* app);
    /// Hide the app's root; the default sets LV_OBJ_FLAG_HIDDEN.
    using HideFn    = void (*)(lv_obj_t* app);
    /// Return true to consume the back gesture; false lets the framework close
    /// the app and return to the launcher.
    using BackFn    = bool (*)(lv_obj_t* app);

    lv_obj_t* app   = nullptr;   ///< This app's root LVGL object
    lv_obj_t* app_c = nullptr;   ///< Parent container
    Derived* parent_app = nullptr; ///< Parent app (for sub-apps)

    // ── Constructors ────────────────────────────────────────────────────

    /// An app in container @p app_c, named @p name, with the icon at @p icon.
    /// The strings are kept by pointer and must outlive the app.
    AppBase(lv_obj_t* app_c, const char* name, const char* icon,
            CreateFn create, DestroyFn destroy = lv_Default_onDestroy)
        : name_(name), icon_(icon), create_(create), destroy_(destroy)
    {
        this->app_c = app_c;
    }

    /// A sub-app of @p parent, built inside the parent's root.
    AppBase(Derived* parent, const char* name, const char* icon,
            CreateFn create, DestroyFn destroy = lv_Default_onDestroy)
        : name_(name), icon_(icon), create_(create), destroy_(destroy)
    {
        this->parent_app = parent;
        this->app_c = parent->app;
    }

    virtual ~AppBase() {
        // The root outlives this object in one case only (the owner is
        // released while the tree is still up); leaving the hook behind would
        // let a later delete write through a freed AppBase.
        if (app) lv_obj_remove_event_cb_with_user_data(app, rootDeletedCb, this);
    }

    // ── IApp interface ──────────────────────────────────────────────────

    /// The name the app was registered with.
    const char* getName() const override { return name_; }

    // ── Flag management ─────────────────────────────────────────────────

    /// Add @p flag to the app's flags.
    void AddFlag(AppFlags flag) override    { type_ = type_ | flag; }
    /// True when every bit of @p flag is set.
    bool HasFlag(AppFlags flag) override   { return (type_ & flag) == flag; }

    // ── LVGL lifecycle ──────────────────────────────────────────────────

    /// Build the app under @p parent with its create function, show it, and
    /// give the pads to its pad logic (when it has one). Does nothing when the
    /// app is already started. Remembers which group the encoder is on, so
    /// destroyApp() can give the encoder back to it.
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

        // A transport an app left stopped goes home first; an app with one of
        // its own claims it in its create or show, after this.
        crosspad::transportGoHome();

        app = create_(parent, static_cast<Derived*>(this));
        if (!app) {
            // Nothing to show, hook or tear down: the app stays not started,
            // no pad logic is pushed, and whatever group the create put on
            // the encoder goes back to its owner.
            LV_LOG_ERROR("App %s: create returned no root, not started", name_ ? name_ : "?");
            restoreEncoderGroup();
            return;
        }
        this->app_c = parent;
        LV_LOG_TRACE("App created");

        // The root can be deleted by someone other than destroyApp():
        // build_main_screen() calls lv_obj_clean() on the whole screen, which
        // takes with it any app the orchestrator is not tracking as running --
        // the kit-selector gate overlay above all, since it is started from
        // the pre-launch hook and never becomes runningApp_. Without this hook
        // started_ outlives the tree it describes, and every later start() is
        // refused with "App already started" against a dangling root.
        lv_obj_add_event_cb(app, rootDeletedCb, LV_EVENT_DELETE, this);

        started_ = true;
        /* Whatever started this app — the launcher, or a pre-launch gate that
         * hand-builds it — it now owns the screen, and the back gesture has to
         * be able to find it. */
        AppOrchestrator::getInstance().noteAppStarted(this);
        if (show_) {
            show_(app);
            visible_ = true;
        }

        if (padLogicName_ && padLogicName_[0] != '\0') {
            crosspad::getPadManager().pushPadLogic(padLogicName_);
        }

        LV_LOG_INFO("App started");
    }

    /// Hide the app's screen with its hide handler, keeping it built;
    /// resume() shows it again. The framework itself does not pause apps.
    void pause() override {
        if (app != nullptr && started_) {
            hide_(app);
            visible_ = false;
            LV_LOG_INFO("App paused");
        }
    }

    /// Show the app's screen again after pause(). The framework calls it
    /// right after start(); it has an effect only while the app is hidden.
    void resume() override {
        if (app == nullptr) return;
        if (started_ && !visible_ && show_) {
            show_(app);
            visible_ = true;
            LV_LOG_INFO("App resumed");
        }
    }

    /// Set by defaultAppFactory(); see ILvglApp::setSelfRef().
    void setSelfRef(std::weak_ptr<ILvglApp> self) override { selfRef_ = self; }

    /// Close this app from inside itself -- the call for an in-app Back
    /// button. The close runs on the next LVGL iteration, after the event
    /// callback that asked for it has returned, so a widget's own event
    /// handler may call it.
    void destroyAppAsync() override {
        if (auto self = selfRef_.lock()) {
            lv_async_call([](void* user_data) {
                auto* keep = static_cast<std::shared_ptr<ILvglApp>*>(user_data);
                (*keep)->destroyApp();
                delete keep;
            }, new std::shared_ptr<ILvglApp>(self));
            return;
        }
        // No shared_ptr owner registered (hand-built app, test harness): the
        // deferral still beats destroying mid-event, there is just nothing
        // holding the object alive but the caller.
        lv_async_call([](void* user_data) {
            static_cast<ILvglApp*>(user_data)->destroyApp();
        }, this);
    }

    /// Close the app: hand the pads back, call its destroy function with the
    /// root, and give encoder navigation back to whoever had it. From inside
    /// the app's own event callbacks, use destroyAppAsync() instead.
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

        visible_ = false;
        started_ = false;
        AppOrchestrator::getInstance().noteAppStopped(this);

        // An app can be closed from the launcher or from inside itself (a Back
        // button). Only the launcher path used to clear this, so a self-close
        // left the orchestrator pointing at a torn-down app — which is what
        // APP_LIST then reported as still running.
        auto& orch = AppOrchestrator::getInstance();
        if (orch.getRunningApp() == this) {
            orch.setRunningApp(nullptr);
        }

        // Cache and clear before destroying
        lv_obj_t* app_to_destroy = app;
        app = nullptr;

        // destroy_ deletes the root asynchronously. If this app is restarted
        // before that delete runs, the old root's DELETE event would roll the
        // fresh instance back through onRootDeleted() (and pop its pad logic).
        // This teardown is ours; take the hook off first.
        lv_obj_remove_event_cb_with_user_data(app_to_destroy, rootDeletedCb, this);

        if (padLogicName_ && padLogicName_[0] != '\0') {
            crosspad::getPadManager().popPadLogic();
        }

        // Back to the home screen's transport, unless this app's still plays.
        crosspad::transportGoHome();

        if (destroy_) {
            destroy_(app_to_destroy);
        } else {
            /* No teardown callback means nobody else will delete the root, so
             * the framework has to — an app registered with destroy = nullptr
             * (the launcher's own Power OFF dialog) used to be "destroyed"
             * with its widgets still on screen and no way back to the
             * launcher. There is no double-delete risk here: without destroy_
             * nothing else queued one. */
            LV_LOG_WARN("App with no destroy function; deleting its root");
            lv_obj_delete_async(app_to_destroy);
            restoreEncoderGroup();
            return;
        }
        // After destroy_, because that is where the app deletes its own group.
        restoreEncoderGroup();
        LV_LOG_INFO("App destroyed");
    }

    // ── Callback setters ────────────────────────────────────────────────

    /// Set the show handler (start() and resume() call it).
    void setOnShow(ShowFn show)       { show_ = show; }
    /// Optional in-app handler for the power button's short click. Without one
    /// the gesture closes the app (destroyAppAsync).
    void setOnBack(BackFn back)       { back_ = back; }
    /// Set the hide handler (pause() calls it).
    void setOnHide(HideFn hide)       { hide_ = hide; }
    /// Set the destroy function (see DestroyFn).
    void setOnDestroy(DestroyFn dest)  { destroy_ = dest; }
    /// The pad logic start() activates and destroyApp() hands back; set from
    /// REGISTER_APP_PL. The string is kept by pointer.
    void setPadLogicName(const char* name) override { padLogicName_ = name; }

    // ── Getters ─────────────────────────────────────────────────────────

    /// The app's root object; null while the app is not started.
    lv_obj_t* getAppContainer() override { return app; }
    /// The icon path the app was registered with.
    const char* getIcon() override      { return icon_; }
    /// True while the app is started and not paused.
    bool isVisible()                    { return visible_; }
    /// True from start() until the app is destroyed.
    bool isStarted() override           { return started_; }

    /// Offer the back gesture to the app's back handler (setOnBack()); false
    /// when it has none.
    bool handleBack() override { return (back_ && app) ? back_(app) : false; }

private:
    /// The root was deleted by someone other than destroyApp(). Roll the app
    /// back to the not-started state so the next start() rebuilds it, and let
    /// the app release what it owns: destroy_(nullptr), since LVGL is deleting
    /// the root already.
    void onRootDeleted() {
        if (!started_ && !visible_) return;   // destroyApp() is doing this

        visible_ = false;
        started_ = false;
        app = nullptr;

        auto& orch = AppOrchestrator::getInstance();
        orch.noteAppStopped(this);
        if (orch.getRunningApp() == this) {
            orch.setRunningApp(nullptr);
        }
        if (padLogicName_ && padLogicName_[0] != '\0') {
            crosspad::getPadManager().popPadLogic();
        }
        // Deliberately no restoreEncoderGroup(): whoever wiped the tree is
        // rebuilding the screen and installs its own group, and the group we
        // remembered is usually part of that same teardown.
        priorEncoderGroup_ = nullptr;

        if (destroy_) destroy_(nullptr);
    }

    static void rootDeletedCb(lv_event_t* e) {
        static_cast<AppBase*>(lv_event_get_user_data(e))->onRootDeleted();
    }

    /// Hand encoder navigation back to whoever held it when this app started.
    /// No-op when the app never took it over, so apps that manage the encoder
    /// themselves (modal overlays restoring their own prior group) are not
    /// second-guessed.
    void restoreEncoderGroup() {
        lv_indev_t* enc = getGuiPlatform().getNavigationEncoder();
        if (!enc || !priorEncoderGroup_) return;
        if (lv_indev_get_group(enc) == priorEncoderGroup_) return;
        // Guarded: a launcher rebuild deletes the group this app started over.
        lv_indev_restore_group(enc, priorEncoderGroup_);
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
    BackFn    back_     = nullptr;

    /// Set by defaultAppFactory; empty for hand-built apps.
    std::weak_ptr<ILvglApp> selfRef_;
};

/// Default app factory for the orchestrator.
/// Usage: `config.app_factory = crosspad_gui::defaultAppFactory<App>;`
///
/// Returns a std::shared_ptr and hands the app a weak reference to it
/// (setSelfRef()), which destroyAppAsync() uses to keep the app alive until
/// the deferred close has run.
template<typename AppType>
std::shared_ptr<ILvglApp> defaultAppFactory(
    lv_obj_t* container, const char* name, const char* icon,
    lv_obj_t* (*create)(lv_obj_t*, AppType*),
    void (*destroy)(lv_obj_t*)) {
    auto app = std::make_shared<AppType>(container, name, icon, create, destroy);
    // Hand the app a weak reference to its own owner so a deferred self-close
    // can keep it alive across the async hop.
    app->setSelfRef(std::weak_ptr<ILvglApp>(app));
    return app;
}

} // namespace crosspad_gui

#endif // USE_LVGL
