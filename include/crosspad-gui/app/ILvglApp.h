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
