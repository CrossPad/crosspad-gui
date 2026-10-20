// SPDX-License-Identifier: GPL-3.0-or-later
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
    /// The app's icon path.
    virtual const char* getIcon() = 0;

    // ── LVGL lifecycle ───────────────────────────────────────────────────
    /// True from start() until the app is destroyed.
    virtual bool isStarted() = 0;
    /// Build the app's screen under @p parent and show it.
    virtual void start(lv_obj_t* parent) = 0;
    /// Hide the app's screen, keeping it built.
    virtual void pause() = 0;
    /// Show the app's screen again.
    virtual void resume() = 0;
    /// Tear the app down at once. From the app's own event callbacks, use
    /// destroyAppAsync().
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

    /// The app's root object; null while it is not started.
    virtual lv_obj_t* getAppContainer() = 0;

    /// Declarative pad logic: name auto-activated on start() and restored on
    /// destroyApp(). Default no-op so non-AppBase implementers are unaffected.
    virtual void setPadLogicName(const char* name) { (void)name; }

    // ── Flag management ──────────────────────────────────────────────────
    /// Add @p flag to the app's flags.
    virtual void AddFlag(crosspad::AppFlags flag) = 0;
    /// True when every bit of @p flag is set.
    virtual bool HasFlag(crosspad::AppFlags flag) = 0;
};

} // namespace crosspad_gui

#endif // USE_LVGL
