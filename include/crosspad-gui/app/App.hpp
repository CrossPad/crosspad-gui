#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>  // For std::enable_shared_from_this

#include "crosspad/app/AppFlags.hpp"
// Forward declarations for crosspad-core types used in convenience accessors
namespace crosspad { class PadManager; class PadLedController; }

#include "lvgl.h"
#include "crosspad-gui/app/AppBase.hpp"

// Forward declarations for enhanced app system
struct console_entry_s;
namespace crosspad { class CrosspadSettings; class CrosspadStatus; }
using crosspad::CrosspadSettings;
using crosspad::CrosspadStatus;

/**
 * @brief Platform App class for P4 / 2playerCrosspad.
 *
 * Inherits LVGL lifecycle from crosspad_gui::AppBase<App> (CRTP) and adds:
 * - std::enable_shared_from_this for safe async destruction
 * - Platform-specific resource accessors
 */
class App : public crosspad_gui::AppBase<App>,
            public std::enable_shared_from_this<App> {
public:
    using AppBase::AppBase;  // inherit all constructors

    virtual ~App() = default;

    // destroyAppAsync() now lives in AppBase, where every platform gets the
    // same deferred, orchestrator-aware close path.

    // ── Platform-specific resource accessors ─────────────────────────────
    class Sequencer& sequencer();
    crosspad::PadManager& padManager();
    crosspad::PadLedController& ledController();
    CrosspadStatus& status();
    CrosspadSettings& settings();

    void addCLICommand(const char* name, void (*handler)(char* argv[], int argc));
    void sendEvent(int eventType, const void* eventData = nullptr);
    void postGuiUpdate() { sendEvent(8); }

    virtual void registerCLICommands() {}

private:
    void initializeResources();

    static struct console_entry_s* commands[64];
    static size_t commandCount;
};
