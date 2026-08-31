#pragma once

/**
 * @file power_gesture.h
 * @brief Portable resolution of the hardware power button's two GUI gestures.
 *
 * The button itself is platform-owned (an STM32 register poll on ESP-IDF, a
 * key or a control-port verb on the PC simulator); everything it *means* for
 * the UI lives here, so both platforms resolve the same contexts in the same
 * order.
 *
 * Both entry points must run on the LVGL thread (ESP-IDF: lv_async_call).
 */

#ifdef USE_LVGL

#include "lvgl.h"
#include <cstdint>

namespace crosspad_gui {

/// What powerBack() actually did — for logging and for HIL assertions.
enum class BackResult : uint8_t {
    None = 0,   ///< nothing was open; the launcher stays as it is
    Dropdown,   ///< an open dropdown/picker was closed
    Overlay,    ///< a modal on the overlay layer was cancelled
    Editing,    ///< the encoder group left edit mode
    Drawer,     ///< the quick-settings drawer was closed
    App,        ///< the running app handled it, or was closed
};

const char* backResultName(BackResult r);

// ── Back handlers (overlay layer) ────────────────────────────────────────────

/// Return true if the handler consumed the gesture (and tore its overlay down).
using BackHandlerFn = bool (*)(void* user_data);

/// Register a dismissable overlay. Returns a token, or 0 when the stack is
/// full. The most recently pushed handler is asked first.
uint32_t ui_back_push(BackHandlerFn fn, void* user_data);

/// Drop a registration. Safe to call with 0 or an already-removed token.
void ui_back_remove(uint32_t token);

/// True while at least one overlay is registered.
bool ui_back_has_overlay();

// ── Gestures ─────────────────────────────────────────────────────────────────

/**
 * Contextual "back" — the short click of the power button.
 *
 * Resolved most-local-first:
 *   1. an open dropdown / picker on the focused object      → close it
 *   2. a registered overlay (modal, confirm box)            → cancel it
 *   3. the encoder group in edit mode                       → leave edit mode
 *   4. the quick-settings drawer                            → close it
 *   5. a running app                                        → its onBack hook,
 *      or close the app and return to the launcher
 *   6. the bare launcher                                    → nothing
 */
BackResult powerBack();

/// The hold threshold both platforms use, and the STM32's own
/// LONG_PRESS_SLEEP_MS: under it a press is a click, at it the hold fires.
constexpr uint32_t kPowerHoldMs = 500;

/// Press / release of an *emulated* power button (the simulator's on-screen
/// button, a keyboard key, a control-port verb). Does the same click-vs-hold
/// split the firmware does from its register poll, on an LVGL timer.
void powerButtonPress();
void powerButtonRelease();

/// Toggle the status-bar quick-settings drawer — the 0.5 s hold. No-op when
/// the drawer is disabled in settings (no arrow button exists).
bool powerToggleQuickSettings();

} // namespace crosspad_gui

#endif // USE_LVGL
