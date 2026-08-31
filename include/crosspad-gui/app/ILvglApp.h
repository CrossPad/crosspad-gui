#pragma once

/**
 * @file ILvglApp.h
 * @brief Non-template interface for LVGL applications.
 *
 * Extends crosspad::IApp with the LVGL-specific lifecycle (start, pause,
 * resume, destroy) and flag management.  AppBase<Derived> implements this
 * interface, giving the shared AppOrchestrator a concrete, non-template
 * base pointer it can work with across all platforms.
 */

#ifdef USE_LVGL

#include "lvgl.h"
#include "crosspad/app/IApp.hpp"
#include "crosspad/app/AppFlags.hpp"

#include <memory>

namespace crosspad_gui {

struct ILvglApp : public crosspad::IApp {
    // ── Metadata ─────────────────────────────────────────────────────────
    virtual const char* getIcon() = 0;

    // ── LVGL lifecycle ───────────────────────────────────────────────────
    virtual bool isStarted() = 0;
    virtual void start(lv_obj_t* parent) = 0;
    virtual void pause() = 0;
    virtual void resume() = 0;
    virtual void destroyApp() = 0;

    /// Close this app from inside itself — the path an in-app Back button
    /// takes. Deferred to the next LVGL iteration, so it is safe to call from
    /// an event callback of a widget the app is about to delete, and it clears
    /// the orchestrator's running-app pointer. Apps should prefer this over
    /// calling destroyApp() directly.
    virtual void destroyAppAsync() { destroyApp(); }

    /// Set by the app factory so a deferred self-close can hold the app alive
    /// until the callback runs. Default no-op for hand-built implementations.
    virtual void setSelfRef(std::weak_ptr<ILvglApp> self) { (void)self; }

    /// Contextual "back" gesture (the power button's short click) offered to
    /// the app before the framework closes it. Return true if the app consumed
    /// it — e.g. it stepped out of its own sub-view. Default false: the
    /// framework then returns to the launcher.
    virtual bool handleBack() { return false; }

    virtual lv_obj_t* getAppContainer() = 0;

    /// Declarative pad logic: name auto-activated on start() and restored on
    /// destroyApp(). Default no-op so non-AppBase implementers are unaffected.
    virtual void setPadLogicName(const char* name) { (void)name; }

    // ── Flag management ──────────────────────────────────────────────────
    virtual void AddFlag(crosspad::AppFlags flag) = 0;
    virtual bool HasFlag(crosspad::AppFlags flag) = 0;
};

} // namespace crosspad_gui

#endif // USE_LVGL
