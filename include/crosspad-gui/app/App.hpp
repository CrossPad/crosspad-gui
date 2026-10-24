// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>  // For std::enable_shared_from_this

#include "crosspad/app/AppFlags.hpp"

#include "lvgl.h"
#include "crosspad-gui/app/AppBase.hpp"

namespace crosspad { class PadManager; class PadLedController; class CrosspadSettings; class CrosspadStatus; }
using crosspad::CrosspadSettings;
using crosspad::CrosspadStatus;

/**
 * @brief The App class every platform builds its apps from.
 *
 * Inherits the LVGL lifecycle from crosspad_gui::AppBase<App> (CRTP) and adds
 * convenience accessors for crosspad-core's singletons. A platform supplies nothing here: the accessors read
 * PlatformServices, which crosspad_platform_init() filled.
 */
class App : public crosspad_gui::AppBase<App>,
            public std::enable_shared_from_this<App> {
public:
    using AppBase::AppBase;  // inherit all constructors

    virtual ~App() = default;

    /// The platform's PadManager.
    crosspad::PadManager& padManager();
    /// The platform's PadLedController.
    crosspad::PadLedController& ledController();
    /// The device status (battery, connections, ...).
    CrosspadStatus& status();
    /// The device settings.
    CrosspadSettings& settings();
};
