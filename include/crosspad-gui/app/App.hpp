#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>  // For std::enable_shared_from_this

#include "crosspad/app/AppFlags.hpp"
// Forward declarations for crosspad-core types used in convenience accessors
namespace crosspad { class PadManager; class PadLedController; }

#ifdef USE_LVGL
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

    /**
     * @brief Safely destroy app asynchronously (LVGL callback-safe)
     *
     * Uses shared_from_this() to keep App alive until callback executes.
     * Prevents use-after-free if App is deleted before lv_async_call runs.
     *
     * @note Requires App to be managed by std::shared_ptr, not raw pointer!
     */
    void destroyAppAsync() {
        try {
            auto self = shared_from_this();
            lv_async_call([](void* user_data) {
                auto* app_ptr = static_cast<std::shared_ptr<App>*>(user_data);
                (*app_ptr)->destroyApp();
                delete app_ptr;
            }, new std::shared_ptr<App>(self));
        } catch (const std::bad_weak_ptr& e) {
            LV_LOG_ERROR("destroyAppAsync failed: App not managed by shared_ptr!");
            destroyApp();
        }
    }

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


#else
// Simple GUI version - minimal app wrapper for when LVGL is not used
#include "crosspad/app/IApp.hpp"

namespace crosspad { class CrosspadSettings; class CrosspadStatus; }
using crosspad::CrosspadSettings;
using crosspad::CrosspadStatus;

class App : public crosspad::IApp {
public:
    App(const char* name) : name_(name), visible_(true), appLogic_(nullptr) {
        initializeResources();
    }

    App(const char* name, void (*logic)(App* self)) : name_(name), visible_(true), appLogic_(logic) {
        initializeResources();
    }

    virtual ~App() = default;

    // IApp interface
    const char* getName() const override { return name_; }

    // Business logic callbacks
    virtual void onNoteOn(uint8_t channel, uint8_t note, uint8_t velocity) {}
    virtual void onNoteOff(uint8_t channel, uint8_t note) {}
    virtual void onPadPressed(uint8_t padIdx, bool isOn, uint8_t velocity) {}
    virtual void onPadReleased(uint8_t padIdx) {}
    virtual void onAppInit() {}
    virtual void onAppDeinit() {}
    virtual void onAppUpdate() {
        if (appLogic_) {
            appLogic_(this);
        }
    }
    virtual void onAppActivated() {}
    virtual void registerCLICommands() {}

    // Platform-specific resource accessors
    class Sequencer& sequencer();
    crosspad::PadManager& padManager();
    crosspad::PadLedController& ledController();
    CrosspadStatus& status();
    CrosspadSettings& settings();

    void addCLICommand(const char* name, void (*handler)(char* argv[], int argc));
    void sendEvent(int eventType, const void* eventData = nullptr);

    bool isVisible() const { return visible_; }

private:
    const char* name_;
    bool visible_;
    void (*appLogic_)(App* self);
    void initializeResources();

    static struct console_entry_s* commands[64];
    static size_t commandCount;
};
#endif // USE_LVGL
